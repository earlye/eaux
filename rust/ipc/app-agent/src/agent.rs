//! The agent side: lock, bind, serve, and the reasons to stop.

use std::collections::HashMap;
use std::future::Future;
use std::path::Path;
use std::pin::Pin;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime};

use futures_core::Stream;
use serde::Serialize;
use serde::de::DeserializeOwned;
use tokio::sync::{mpsc, oneshot};
use tokio::task::AbortHandle;

use crate::app::App;
use crate::error::{Error, Result};
#[cfg(unix)]
use crate::locations::ensure_parent;
use crate::locations::ensure_private_dir;
use crate::lock;
use crate::protocol::{AgentMsg, ClientMsg, ErrorCode, Hello, PROTOCOL_VERSION};
use crate::spawn::{Output, spawn_detached};
use crate::transport;
use interprocess::local_socket::traits::tokio::Listener as _;

/// A boxed stream of events for [`Reply::Events`]. An `Err` item ends the stream and reaches the
/// client as [`Error::Remote`].
pub type EventStream<E> = Pin<Box<dyn Stream<Item = Result<E, String>> + Send>>;

/// What a [`Handler`] answers with.
pub enum Reply<R, E> {
    /// A single response, for [`Client::call`](crate::Client::call).
    Response(R),
    /// A stream of events, for [`Client::subscribe`](crate::Client::subscribe).
    Events(EventStream<E>),
}

impl<R, E> Reply<R, E> {
    /// Wrap any stream as [`Reply::Events`].
    pub fn events(stream: impl Stream<Item = Result<E, String>> + Send + 'static) -> Self {
        Reply::Events(Box::pin(stream))
    }
}

/// The app's side of the agent: one handler for every app-defined request.
///
/// `hello` and `shutdown` are built in and never reach the handler.
pub trait Handler: Send + Sync + 'static {
    /// The app's request type, as sent by `call` and `subscribe`.
    type Request: DeserializeOwned + Send + 'static;
    /// The single-response type.
    type Response: Serialize + Send + 'static;
    /// The event type for streamed replies.
    type Event: Serialize + Send + 'static;

    /// Answer one request with a response or a stream of events. `Err` reaches the client as
    /// [`Error::Remote`]. Requests run concurrently.
    fn handle(
        &self,
        request: Self::Request,
    ) -> impl Future<Output = Result<Reply<Self::Response, Self::Event>, String>> + Send;

    /// `Some(reason)` while the agent must not stop: `shutdown` is refused with
    /// [`Error::Busy`], idle exit waits, and a binary-watch restart waits.
    fn busy(&self) -> Option<String> {
        None
    }
}

/// Why [`App::serve`] returned.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[non_exhaustive]
pub enum Stopped {
    /// A client sent `shutdown`.
    Shutdown,
    /// Nothing happened for the idle timeout.
    IdleTimeout,
    /// The watched binary changed; a new agent was started from it.
    Restarted,
    /// Windows: this process was the installed exe and started its shadow copy, which serves.
    HandedOff,
}

/// The exit code for "another agent already holds the lock". [`App::run`] uses it, and the
/// spawning client treats it as a lost race rather than a failure.
pub const EXIT_ALREADY_RUNNING: u8 = 75;

struct Shared<H> {
    handler: H,
    hello: Hello,
    in_flight: AtomicUsize,
    last_activity: Mutex<Instant>,
    stop: mpsc::UnboundedSender<Stopped>,
}

impl<H> Shared<H> {
    fn touch(&self) {
        *self.last_activity.lock().unwrap_or_else(|e| e.into_inner()) = Instant::now();
    }
    fn idle_for(&self) -> Duration {
        self.last_activity
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .elapsed()
    }
}

/// Counts a request as in flight until dropped, including when it is aborted.
struct InFlight<H>(Arc<Shared<H>>);

impl<H> InFlight<H> {
    fn new(shared: &Arc<Shared<H>>) -> Self {
        shared.in_flight.fetch_add(1, Ordering::SeqCst);
        shared.touch();
        InFlight(shared.clone())
    }
}

impl<H> Drop for InFlight<H> {
    fn drop(&mut self) {
        self.0.touch();
        self.0.in_flight.fetch_sub(1, Ordering::SeqCst);
    }
}

/// Identity of a file, to notice when it is replaced.
#[derive(Clone, Debug, PartialEq, Eq)]
struct FileId {
    len: u64,
    modified: Option<SystemTime>,
    #[cfg(unix)]
    ino: (u64, u64),
    /// Volume, file index and change time; see [`crate::windows::file_identity`].
    #[cfg(windows)]
    id: (u32, u64, i64),
}

fn file_id(path: &Path) -> Option<FileId> {
    let meta = std::fs::metadata(path).ok()?;
    Some(FileId {
        len: meta.len(),
        modified: meta.modified().ok(),
        #[cfg(unix)]
        ino: {
            use std::os::unix::fs::MetadataExt;
            (meta.dev(), meta.ino())
        },
        #[cfg(windows)]
        id: crate::windows::file_identity(path).ok()?,
    })
}

pub(crate) async fn serve<H: Handler>(app: &App, handler: H) -> Result<Stopped> {
    let locations = app.locations()?;

    #[cfg(windows)]
    let installed = {
        let data = locations
            .data_dir
            .as_deref()
            .expect("data_dir is always set on Windows");
        match crate::windows::shadow(data, app.version(), &app.installed_exe_env())? {
            crate::windows::Shadow::HandedOff => return Ok(Stopped::HandedOff),
            crate::windows::Shadow::Running { installed } => installed,
        }
    };
    #[cfg(not(windows))]
    let installed = std::env::current_exe()?;

    // Lock first: the lock decides who serves.
    ensure_private_dir(&locations, true)?;
    let guard = lock::try_acquire(&locations.lock)?.ok_or_else(|| Error::AlreadyRunning {
        pid: lock::read_pid(&locations.lock),
    })?;

    // Holding the lock, any socket file left behind is stale by definition.
    #[cfg(unix)]
    {
        ensure_parent(&locations.endpoint)?;
        match std::fs::remove_file(&locations.endpoint) {
            Ok(()) => {}
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
            Err(e) => return Err(e.into()),
        }
    }
    let listener = transport::bind(&locations.endpoint)?;
    eprintln!(
        "app-agent: {} {} agent pid {} serving {}",
        app.name(),
        app.version(),
        std::process::id(),
        locations.endpoint.display()
    );

    let (stop_tx, mut stop_rx) = mpsc::unbounded_channel();
    let shared = Arc::new(Shared {
        handler,
        hello: Hello {
            protocol: PROTOCOL_VERSION,
            pid: std::process::id(),
            started_at: SystemTime::now(),
            app_version: app.version().to_string(),
        },
        in_flight: AtomicUsize::new(0),
        last_activity: Mutex::new(Instant::now()),
        stop: stop_tx,
    });

    let idle_timeout = app.idle_timeout_value();
    let watch = app
        .watch_interval()
        .map(|every| (every, file_id(&installed)));
    let tick = [
        idle_timeout.map(|t| (t / 4).max(Duration::from_millis(10))),
        watch.as_ref().map(|(every, _)| *every),
        Some(Duration::from_secs(1)),
    ]
    .into_iter()
    .flatten()
    .min()
    .unwrap_or(Duration::from_secs(1));
    let mut ticker = tokio::time::interval(tick);
    ticker.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
    let mut last_watch = Instant::now();

    let stopped = loop {
        tokio::select! {
            accepted = listener.accept() => match accepted {
                Ok(stream) => {
                    tokio::spawn(connection(stream, shared.clone()));
                }
                Err(e) => eprintln!("app-agent: accept failed: {e}"),
            },
            Some(reason) = stop_rx.recv() => break reason,
            _ = ticker.tick() => {
                let idle = shared.in_flight.load(Ordering::SeqCst) == 0;
                if let Some(timeout) = idle_timeout
                    && idle
                    && shared.idle_for() >= timeout
                    && shared.handler.busy().is_none()
                {
                    break Stopped::IdleTimeout;
                }
                if let Some((every, original)) = &watch
                    && last_watch.elapsed() >= *every
                {
                    last_watch = Instant::now();
                    let now = file_id(&installed);
                    if now.is_some() && now != *original && shared.handler.busy().is_none() {
                        break Stopped::Restarted;
                    }
                }
            }
        }
    };

    // Stop listening (removes the socket on Unix), then release the lock, so a successor can
    // take over cleanly.
    drop(listener);
    drop(guard);
    eprintln!(
        "app-agent: agent pid {} stopping: {stopped:?}",
        std::process::id()
    );
    if stopped == Stopped::Restarted {
        let args: Vec<_> = std::env::args_os().skip(1).collect();
        let child = spawn_detached(&installed, &args, &[], Output::Inherit)?;
        drop(child);
    }
    Ok(stopped)
}

type Outgoing = (AgentMsg, Option<oneshot::Sender<()>>);

async fn connection<H: Handler>(stream: transport::Stream, shared: Arc<Shared<H>>) {
    let (mut reader, mut writer) = transport::split(stream);
    let (tx, mut rx) = mpsc::unbounded_channel::<Outgoing>();
    let writer_task = tokio::spawn(async move {
        while let Some((msg, flushed)) = rx.recv().await {
            if writer.write(&msg).await.is_err() {
                break;
            }
            if let Some(flushed) = flushed {
                let _ = flushed.send(());
            }
        }
    });
    let tasks: Arc<Mutex<HashMap<u64, AbortHandle>>> = Arc::default();

    while let Ok(Some(msg)) = reader.read::<ClientMsg>().await {
        match msg {
            // `hello` is a liveness probe; it deliberately does not count as activity.
            ClientMsg::Hello { id } => {
                let body = serde_json::to_value(&shared.hello).unwrap_or_default();
                let _ = tx.send((AgentMsg::Response { id, body }, None));
            }
            ClientMsg::Shutdown { id } => {
                shared.touch();
                if let Some(reason) = shared.handler.busy() {
                    let _ = tx.send((
                        AgentMsg::Error {
                            id,
                            code: ErrorCode::Busy,
                            message: reason,
                        },
                        None,
                    ));
                    continue;
                }
                let (flushed_tx, flushed_rx) = oneshot::channel();
                let body = serde_json::Value::Null;
                let _ = tx.send((AgentMsg::Response { id, body }, Some(flushed_tx)));
                let _ = flushed_rx.await;
                let _ = shared.stop.send(Stopped::Shutdown);
            }
            ClientMsg::Cancel { id } => {
                if let Some(task) = tasks.lock().unwrap_or_else(|e| e.into_inner()).remove(&id) {
                    task.abort();
                }
            }
            ClientMsg::Request { id, body } => {
                let guard = InFlight::new(&shared);
                let shared = shared.clone();
                let tx = tx.clone();
                let tasks_for_task = tasks.clone();
                let mut registry = tasks.lock().unwrap_or_else(|e| e.into_inner());
                let handle = tokio::spawn(async move {
                    let _guard = guard;
                    run_request(&shared.handler, id, body, &tx).await;
                    tasks_for_task
                        .lock()
                        .unwrap_or_else(|e| e.into_inner())
                        .remove(&id);
                });
                registry.insert(id, handle.abort_handle());
            }
        }
    }

    for (_, task) in tasks.lock().unwrap_or_else(|e| e.into_inner()).drain() {
        task.abort();
    }
    drop(tx);
    let _ = writer_task.await;
}

async fn run_request<H: Handler>(
    handler: &H,
    id: u64,
    body: serde_json::Value,
    tx: &mpsc::UnboundedSender<Outgoing>,
) {
    let send = |msg: AgentMsg| tx.send((msg, None)).is_ok();
    let error = |code, message: String| AgentMsg::Error { id, code, message };
    let request = match serde_json::from_value::<H::Request>(body) {
        Ok(request) => request,
        Err(e) => {
            send(error(ErrorCode::BadRequest, e.to_string()));
            return;
        }
    };
    match handler.handle(request).await {
        Err(message) => {
            send(error(ErrorCode::Handler, message));
        }
        Ok(Reply::Response(response)) => {
            send(match serde_json::to_value(response) {
                Ok(body) => AgentMsg::Response { id, body },
                Err(e) => error(ErrorCode::Handler, e.to_string()),
            });
        }
        Ok(Reply::Events(mut events)) => loop {
            let item = std::future::poll_fn(|cx| events.as_mut().poll_next(cx)).await;
            let msg = match item {
                None => AgentMsg::End { id },
                Some(Err(message)) => error(ErrorCode::Handler, message),
                Some(Ok(event)) => match serde_json::to_value(event) {
                    Ok(body) => AgentMsg::Event { id, body },
                    Err(e) => error(ErrorCode::Handler, e.to_string()),
                },
            };
            let last = !matches!(msg, AgentMsg::Event { .. });
            if !send(msg) || last {
                break;
            }
        },
    }
}
