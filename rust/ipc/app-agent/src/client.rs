//! The client side: connect-or-spawn, the hello handshake, and call/subscribe.

use std::collections::HashMap;
use std::io;
use std::marker::PhantomData;
use std::pin::Pin;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll};
use std::time::{Duration, Instant};

use futures_core::Stream;
use serde::Serialize;
use serde::de::DeserializeOwned;
use tokio::sync::mpsc;

use crate::agent::EXIT_ALREADY_RUNNING;
use crate::app::App;
use crate::error::{Error, Result};
use crate::locations::{Locations, ensure_private_dir};
use crate::lock;
use crate::protocol::{AgentMsg, ClientMsg, ErrorCode, Hello, PROTOCOL_VERSION};
use crate::spawn::{Child, Output, reap, spawn_detached};
use crate::transport;

/// The agent runs a different app version or protocol than this client.
/// The crate only reports it; the app decides whether to [`Client::restart`].
#[derive(Clone, Debug, PartialEq, Eq)]
#[non_exhaustive]
pub struct Mismatch {
    pub client_version: String,
    pub agent_version: String,
    pub client_protocol: u32,
    pub agent_protocol: u32,
}

type Pending = Mutex<HashMap<u64, mpsc::UnboundedSender<AgentMsg>>>;

struct Conn {
    tx: mpsc::UnboundedSender<ClientMsg>,
    pending: Arc<Pending>,
    next_id: AtomicU64,
    reader: tokio::task::AbortHandle,
}

impl Drop for Conn {
    fn drop(&mut self) {
        // Nobody can wait on replies any more; stop reading so the connection closes.
        self.reader.abort();
    }
}

impl Conn {
    fn start(
        &self,
        make: impl FnOnce(u64) -> ClientMsg,
    ) -> Result<(u64, mpsc::UnboundedReceiver<AgentMsg>)> {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        let (tx, rx) = mpsc::unbounded_channel();
        self.pending
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .insert(id, tx);
        if self.tx.send(make(id)).is_err() {
            self.forget(id);
            return Err(Error::Disconnected);
        }
        Ok((id, rx))
    }

    fn forget(&self, id: u64) {
        self.pending
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .remove(&id);
    }

    fn cancel(&self, id: u64) {
        self.forget(id);
        let _ = self.tx.send(ClientMsg::Cancel { id });
    }
}

/// A connection to a running agent. Calls on one client may run concurrently.
pub struct Client {
    conn: Arc<Conn>,
    hello: Hello,
    app: App,
    closed: tokio::sync::watch::Receiver<bool>,
}

impl std::fmt::Debug for Client {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Client")
            .field("app", &self.app.name())
            .field("hello", &self.hello)
            .finish_non_exhaustive()
    }
}

/// Wait for the agent's empty acknowledgement of a built-in `what` message.
async fn expect_ack(rx: &mut mpsc::UnboundedReceiver<AgentMsg>, what: &str) -> Result<()> {
    match rx.recv().await {
        Some(AgentMsg::Response { .. }) => Ok(()),
        Some(AgentMsg::Error { code, message, .. }) => Err(remote_error(code, message)),
        Some(_) => Err(Error::Protocol(format!("unexpected reply to {what}"))),
        None => Err(Error::Disconnected),
    }
}

fn remote_error(code: ErrorCode, message: String) -> Error {
    match code {
        ErrorCode::Busy => Error::Busy(message),
        ErrorCode::Handler => Error::Remote(message),
        ErrorCode::BadRequest => Error::Protocol(format!("agent rejected request: {message}")),
    }
}

impl Client {
    /// What the agent said in `hello`.
    pub fn hello(&self) -> &Hello {
        &self.hello
    }

    /// `Some` if the agent's app version or protocol differs from this client's.
    pub fn mismatch(&self) -> Option<Mismatch> {
        let m = Mismatch {
            client_version: self.app.version().to_string(),
            agent_version: self.hello.app_version.clone(),
            client_protocol: PROTOCOL_VERSION,
            agent_protocol: self.hello.protocol,
        };
        (m.client_version != m.agent_version || m.client_protocol != m.agent_protocol).then_some(m)
    }

    /// Send one request and wait for its single response.
    pub async fn call<Req, Resp>(&self, request: &Req) -> Result<Resp>
    where
        Req: Serialize + ?Sized,
        Resp: DeserializeOwned,
    {
        let body = serde_json::to_value(request)?;
        let (id, mut rx) = self.conn.start(|id| ClientMsg::Request { id, body })?;
        match rx.recv().await {
            Some(AgentMsg::Response { body, .. }) => Ok(serde_json::from_value(body)?),
            Some(AgentMsg::Error { code, message, .. }) => Err(remote_error(code, message)),
            Some(AgentMsg::Event { .. } | AgentMsg::End { .. }) => {
                self.conn.cancel(id);
                Err(Error::Protocol(
                    "the agent answered with an event stream; use subscribe".into(),
                ))
            }
            None => Err(Error::Disconnected),
        }
    }

    /// Send one request and follow the events it produces. Dropping the stream early tells the
    /// agent to stop producing them.
    pub async fn subscribe<Req, Event>(&self, request: &Req) -> Result<Subscription<Event>>
    where
        Req: Serialize + ?Sized,
        Event: DeserializeOwned,
    {
        let body = serde_json::to_value(request)?;
        let (id, rx) = self.conn.start(|id| ClientMsg::Request { id, body })?;
        Ok(Subscription {
            conn: self.conn.clone(),
            id,
            rx,
            done: false,
            _event: PhantomData,
        })
    }

    /// Ask the agent to stop. Fails with [`Error::Busy`] if the app says it is busy.
    /// On success, waits (briefly) for the agent to close the connection.
    pub async fn shutdown(&self) -> Result<()> {
        let (_, mut rx) = self.conn.start(|id| ClientMsg::Shutdown { id })?;
        expect_ack(&mut rx, "shutdown").await?;
        let mut closed = self.closed.clone();
        let _ = tokio::time::timeout(
            self.app.spawn_timeout_value(),
            closed.wait_for(|closed| *closed),
        )
        .await;
        Ok(())
    }

    /// Ask the agent to stop once it is idle (not busy, no request in flight), and wait up to
    /// `timeout`, covering both the acknowledgement and the agent closing the connection. On
    /// timeout returns [`Error::StillDraining`]; the drain stays in effect. The agent keeps
    /// serving requests while draining unless the handler refuses them.
    ///
    /// [`Error::Disconnected`] does not mean the drain was ignored: the agent may have applied it
    /// and failed only to deliver the acknowledgement.
    pub async fn drain(&self, timeout: Duration) -> Result<()> {
        let (_, mut rx) = self.conn.start(|id| ClientMsg::Drain { id })?;
        let mut closed = self.closed.clone();
        let drained = async {
            expect_ack(&mut rx, "drain").await?;
            let _ = closed.wait_for(|closed| *closed).await;
            Ok(())
        };
        tokio::time::timeout(timeout, drained)
            .await
            .unwrap_or(Err(Error::StillDraining {
                pid: self.hello.pid,
            }))
    }

    /// Shut the agent down (unless busy) and connect to a freshly spawned one, e.g. after
    /// [`Client::mismatch`] reports a version change.
    pub async fn restart(self) -> Result<Client> {
        self.shutdown().await?;
        let app = self.app.clone();
        drop(self);
        connect(&app, true).await
    }
}

/// The events produced by [`Client::subscribe`]. Ends after the agent's last event; an agent-side
/// error arrives as one final `Err`.
pub struct Subscription<Event> {
    conn: Arc<Conn>,
    id: u64,
    rx: mpsc::UnboundedReceiver<AgentMsg>,
    done: bool,
    _event: PhantomData<fn() -> Event>,
}

impl<Event> std::fmt::Debug for Subscription<Event> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Subscription")
            .field("id", &self.id)
            .field("done", &self.done)
            .finish()
    }
}

impl<Event: DeserializeOwned> Stream for Subscription<Event> {
    type Item = Result<Event>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.done {
            return Poll::Ready(None);
        }
        let msg = match self.rx.poll_recv(cx) {
            Poll::Pending => return Poll::Pending,
            Poll::Ready(msg) => msg,
        };
        let item = match msg {
            Some(AgentMsg::Event { body, .. }) => {
                return Poll::Ready(Some(serde_json::from_value(body).map_err(Error::from)));
            }
            Some(AgentMsg::End { .. }) => None,
            Some(AgentMsg::Error { code, message, .. }) => Some(Err(remote_error(code, message))),
            Some(AgentMsg::Response { .. }) => Some(Err(Error::Protocol(
                "the agent answered with a single response; use call".into(),
            ))),
            None => Some(Err(Error::Disconnected)),
        };
        self.done = true;
        Poll::Ready(item)
    }
}

impl<Event> Drop for Subscription<Event> {
    fn drop(&mut self) {
        if !self.done {
            self.conn.cancel(self.id);
        }
    }
}

fn agent_absent(e: &io::Error) -> bool {
    matches!(
        e.kind(),
        io::ErrorKind::NotFound | io::ErrorKind::ConnectionRefused
    )
}

/// Connect to the agent, spawning it if `spawn` and it is not running.
pub(crate) async fn connect(app: &App, spawn: bool) -> Result<Client> {
    let locations = app.locations()?;
    ensure_private_dir(&locations, false)?;
    let deadline = Instant::now() + app.spawn_timeout_value();
    let mut child: Option<Child> = None;
    let mut handed_off = false;
    let mut last_spawn: Option<Instant> = None;
    let mut delay = Duration::from_millis(10);

    let stream = loop {
        let err = match transport::connect(&locations.endpoint).await {
            Ok(stream) => break stream,
            Err(e) if agent_absent(&e) => e,
            Err(e) => {
                reap(child);
                return Err(e.into());
            }
        };
        if !spawn {
            let _ = err;
            return Err(Error::NotRunning);
        }
        if let Some(running) = child.as_mut()
            && let Some(status) = running.try_wait()?
        {
            child = None;
            match status.code() {
                // Windows: the installed exe started its shadow copy and left.
                Some(0) => handed_off = true,
                // Lost the lock race; whoever won may still be binding.
                Some(code) if code == i32::from(EXIT_ALREADY_RUNNING) => {}
                _ => {
                    return Err(Error::AgentFailed {
                        status,
                        log: locations.log.clone(),
                    });
                }
            }
        }
        let now = Instant::now();
        if now >= deadline {
            reap(child);
            return Err(Error::SpawnTimedOut {
                log: locations.log.clone(),
            });
        }
        if child.is_none()
            && !handed_off
            && last_spawn.is_none_or(|t| now - t >= Duration::from_millis(100))
        {
            child = Some(spawn_agent(app, &locations)?);
            last_spawn = Some(now);
        }
        tokio::time::sleep(delay.min(deadline - now)).await;
        delay = (delay * 2).min(Duration::from_millis(200));
    };
    reap(child);
    handshake(app, &locations, stream).await
}

fn spawn_agent(app: &App, locations: &Locations) -> Result<Child> {
    let program = match app.program() {
        Some(program) => program.to_path_buf(),
        None => std::env::current_exe()?,
    };
    let mut env = Vec::new();
    if let Some(endpoint) = app.explicit_endpoint() {
        env.push((app.endpoint_env(), endpoint.to_os_string()));
    }
    Ok(spawn_detached(
        &program,
        app.args(),
        &env,
        Output::Log(&locations.log),
    )?)
}

async fn handshake(app: &App, locations: &Locations, stream: transport::Stream) -> Result<Client> {
    #[cfg(windows)]
    {
        let pid = transport::peer_pid(&stream)?.ok_or(Error::NotOwner { pid: 0 })?;
        crate::windows::verify_server_owner(pid)?;
    }

    let (mut reader, mut writer) = transport::split(stream);
    let pending: Arc<Pending> = Arc::default();
    let (closed_tx, closed_rx) = tokio::sync::watch::channel(false);

    let routes = pending.clone();
    let reader_task = tokio::spawn(async move {
        while let Ok(Some(msg)) = reader.read::<AgentMsg>().await {
            let id = msg.id();
            let last = !matches!(msg, AgentMsg::Event { .. });
            let mut routes = routes.lock().unwrap_or_else(|e| e.into_inner());
            if let Some(route) = routes.get(&id) {
                let _ = route.send(msg);
            }
            if last {
                routes.remove(&id);
            }
        }
        // Dropping every route wakes all waiters with "disconnected".
        routes.lock().unwrap_or_else(|e| e.into_inner()).clear();
        let _ = closed_tx.send(true);
    });

    let (tx, mut rx) = mpsc::unbounded_channel::<ClientMsg>();
    tokio::spawn(async move {
        while let Some(msg) = rx.recv().await {
            if writer.write(&msg).await.is_err() {
                break;
            }
        }
    });

    let conn = Arc::new(Conn {
        tx,
        pending,
        next_id: AtomicU64::new(1),
        reader: reader_task.abort_handle(),
    });
    let (_, mut hello_rx) = conn.start(|id| ClientMsg::Hello { id })?;
    let hung = || Error::Hung {
        pid: lock::read_pid(&locations.lock),
    };
    let hello = match tokio::time::timeout(app.hello_timeout_value(), hello_rx.recv()).await {
        Err(_) => return Err(hung()),
        Ok(Some(AgentMsg::Response { body, .. })) => serde_json::from_value::<Hello>(body)?,
        Ok(Some(AgentMsg::Error { code, message, .. })) => return Err(remote_error(code, message)),
        Ok(Some(_)) => return Err(Error::Protocol("unexpected reply to hello".into())),
        Ok(None) => return Err(Error::Disconnected),
    };
    Ok(Client {
        conn,
        hello,
        app: app.clone(),
        closed: closed_rx,
    })
}
