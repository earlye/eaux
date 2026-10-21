//! End-to-end tests against a real, detached dummy agent.
//!
//! This binary is its own dummy agent: launched as `agent --dummy-agent <app> <version>
//! <idle-ms> <watch-ms|off>` it serves; otherwise it runs the tests below, each in its own thread
//! with its own app name and endpoint so they can run in parallel. Optional positional arguments
//! filter tests by substring, like libtest.

use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

use app_agent::{App, Client, Error, Handler, Reply};
use futures_util::StreamExt;
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};

// ---------------------------------------------------------------------------------------------
// The dummy agent

#[derive(Serialize, Deserialize)]
#[serde(tag = "op", rename_all = "snake_case")]
enum Req {
    Echo { text: String },
    Count { n: u32, delay_ms: u64 },
    SetBusy { busy: bool },
    Exe,
    Fail,
}

struct Dummy {
    busy: AtomicBool,
}

impl Handler for Dummy {
    type Request = Req;
    type Response = Value;
    type Event = u32;

    async fn handle(&self, request: Req) -> Result<Reply<Value, u32>, String> {
        Ok(match request {
            Req::Echo { text } => Reply::Response(json!(text)),
            Req::Count { n, delay_ms } => {
                Reply::events(futures_util::stream::unfold(0, move |i| async move {
                    if i >= n {
                        return None;
                    }
                    tokio::time::sleep(Duration::from_millis(delay_ms)).await;
                    Some((Ok(i), i + 1))
                }))
            }
            Req::SetBusy { busy } => {
                self.busy.store(busy, Ordering::SeqCst);
                Reply::Response(Value::Null)
            }
            Req::Exe => Reply::Response(json!(std::env::current_exe().unwrap())),
            Req::Fail => return Err("boom".into()),
        })
    }

    fn busy(&self) -> Option<String> {
        self.busy
            .load(Ordering::SeqCst)
            .then(|| "dummy job running".to_string())
    }
}

fn dummy_main(args: &[String]) -> ExitCode {
    let [name, version, idle, watch] = args else {
        eprintln!("usage: --dummy-agent <app> <version> <idle-ms> <watch-ms|off>");
        return ExitCode::FAILURE;
    };
    let mut app =
        App::new(name, version).idle_timeout(Some(Duration::from_millis(idle.parse().unwrap())));
    if watch != "off" {
        app = app.watch_binary(Duration::from_millis(watch.parse().unwrap()));
    }
    tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .unwrap()
        .block_on(app.run(Dummy {
            busy: AtomicBool::new(false),
        }))
}

// ---------------------------------------------------------------------------------------------
// Fixtures

/// Every agent idles out after a minute, so a failed test cannot leave one behind for long.
const IDLE_MS: &str = "60000";

struct Fixture {
    name: String,
    endpoint: Option<String>,
    program: Option<PathBuf>,
    idle_ms: String,
    watch: String,
}

impl Fixture {
    /// An isolated app: a unique name and, unless `default_locations`, a private endpoint.
    fn new(test: &str, default_locations: bool) -> Self {
        let name = format!("aat-{test}-{}", std::process::id());
        let endpoint = (!default_locations).then(|| {
            if cfg!(windows) {
                name.clone()
            } else {
                std::env::temp_dir()
                    .join(format!("{name}.sock"))
                    .to_string_lossy()
                    .into_owned()
            }
        });
        Fixture {
            name,
            endpoint,
            program: None,
            idle_ms: IDLE_MS.into(),
            watch: "off".into(),
        }
    }

    fn app(&self, version: &str) -> App {
        let mut app = App::new(&self.name, version).agent_args([
            "--dummy-agent",
            &self.name,
            version,
            &self.idle_ms,
            &self.watch,
        ]);
        if let Some(endpoint) = &self.endpoint {
            app = app.endpoint(endpoint);
        }
        if let Some(program) = &self.program {
            app = app.agent_program(program);
        }
        app
    }

    fn log(&self) -> String {
        let log = self.app("1.0.0").locations().unwrap().log;
        std::fs::read_to_string(log).unwrap_or_default()
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        if std::thread::panicking() {
            eprintln!("--- agent log for {} ---\n{}", self.name, self.log());
        }
        // Best-effort: stop whatever agent is left, then remove its files.
        let app = self.app("1.0.0");
        block_on(async {
            if let Ok(client) = app.connect().await {
                let _ = client.call::<_, Value>(&Req::SetBusy { busy: false }).await;
                let _ = client.shutdown().await;
            }
        });
        if let Ok(l) = app.locations() {
            let _ = std::fs::remove_file(&l.lock);
            let _ = std::fs::remove_file(&l.log);
            if !cfg!(windows) {
                let _ = std::fs::remove_file(&l.endpoint);
                if self.endpoint.is_none() {
                    let _ = std::fs::remove_dir(l.endpoint.parent().unwrap());
                }
            }
        }
        if let Some(local) = std::env::var_os("LOCALAPPDATA") {
            let _ = std::fs::remove_dir_all(Path::new(&local).join(&self.name));
        }
    }
}

fn block_on<F: std::future::Future>(f: F) -> F::Output {
    tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .unwrap()
        .block_on(f)
}

async fn echo(client: &Client, text: &str) -> String {
    client.call(&Req::Echo { text: text.into() }).await.unwrap()
}

async fn set_busy(client: &Client, busy: bool) {
    let _: Value = client.call(&Req::SetBusy { busy }).await.unwrap();
}

/// Poll until `app.connect()` says nothing is listening.
async fn wait_until_gone(app: &App, within: Duration) {
    let deadline = Instant::now() + within;
    loop {
        match app.connect().await {
            Err(Error::NotRunning) => return,
            Ok(_) | Err(_) if Instant::now() < deadline => {
                tokio::time::sleep(Duration::from_millis(50)).await
            }
            other => panic!("agent still there after {within:?}: {other:?}"),
        }
    }
}

fn kill_hard(pid: u32) {
    let status = if cfg!(windows) {
        Command::new("taskkill")
            .args(["/F", "/PID", &pid.to_string()])
            .status()
    } else {
        Command::new("kill").args(["-9", &pid.to_string()]).status()
    };
    assert!(status.unwrap().success(), "failed to kill {pid}");
}

/// Copy this test binary somewhere else, as if it were an installed app.
fn install_copy(fx: &Fixture, dir_name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("{}-{dir_name}", fx.name));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = dir.join(format!("aat-installed{}", std::env::consts::EXE_SUFFIX));
    std::fs::copy(std::env::current_exe().unwrap(), &exe).unwrap();
    exe
}

/// Replace `exe` with a fresh copy of this binary via rename, the way installers do.
fn reinstall(exe: &Path) {
    let tmp = exe.with_extension("new");
    std::fs::copy(std::env::current_exe().unwrap(), &tmp).unwrap();
    std::fs::rename(&tmp, exe).unwrap();
}

// ---------------------------------------------------------------------------------------------
// Tests

fn cold_start() {
    // Uses the real per-user locations, not an override.
    let fx = Fixture::new("cold", true);
    let app = fx.app("1.0.0");
    block_on(async {
        assert!(matches!(app.connect().await, Err(Error::NotRunning)));
        let client = app.connect_or_spawn().await.unwrap();
        let hello = client.hello().clone();
        assert_ne!(hello.pid, std::process::id());
        assert_eq!(hello.app_version, "1.0.0");
        assert_eq!(hello.protocol, app_agent::PROTOCOL_VERSION);
        assert!(client.mismatch().is_none());
        assert_eq!(echo(&client, "hi").await, "hi");

        // A second client reaches the same agent without spawning.
        let again = app.connect().await.unwrap();
        assert_eq!(again.hello().pid, hello.pid);
        let lock = std::fs::read_to_string(app.locations().unwrap().lock).unwrap();
        assert_eq!(lock.trim(), hello.pid.to_string());
    });
}

fn call_and_subscribe() {
    let fx = Fixture::new("calls", false);
    let app = fx.app("1.0.0");
    block_on(async {
        let client = app.connect_or_spawn().await.unwrap();

        let events: Vec<u32> = client
            .subscribe::<_, u32>(&Req::Count { n: 5, delay_ms: 0 })
            .await
            .unwrap()
            .map(Result::unwrap)
            .collect()
            .await;
        assert_eq!(events, vec![0, 1, 2, 3, 4]);

        let err = client.call::<_, Value>(&Req::Fail).await.unwrap_err();
        assert!(
            matches!(err, Error::Remote(ref m) if m == "boom"),
            "{err:?}"
        );

        let err = client
            .call::<_, Value>(&Req::Count { n: 1, delay_ms: 0 })
            .await
            .unwrap_err();
        assert!(matches!(err, Error::Protocol(_)), "{err:?}");

        let mut sub = client
            .subscribe::<_, u32>(&Req::Echo { text: "x".into() })
            .await
            .unwrap();
        assert!(matches!(sub.next().await, Some(Err(Error::Protocol(_)))));
        assert!(sub.next().await.is_none());

        // Abandon a long subscription; the connection stays usable.
        let mut long = client
            .subscribe::<_, u32>(&Req::Count {
                n: 1000,
                delay_ms: 5,
            })
            .await
            .unwrap();
        assert_eq!(long.next().await.unwrap().unwrap(), 0);
        drop(long);

        // Concurrent calls on one client.
        let (a, b) = tokio::join!(echo(&client, "a"), echo(&client, "b"));
        assert_eq!((a.as_str(), b.as_str()), ("a", "b"));
    });
}

fn two_concurrent_clients_one_agent() {
    let fx = Fixture::new("race", false);
    let app = fx.app("1.0.0");
    block_on(async {
        let tasks: Vec<_> = (0..4)
            .map(|_| {
                let app = app.clone();
                tokio::spawn(async move { app.connect_or_spawn().await.unwrap() })
            })
            .collect();
        let mut pids = Vec::new();
        for task in tasks {
            pids.push(task.await.unwrap().hello().pid);
        }
        pids.dedup();
        assert_eq!(pids.len(), 1, "clients reached different agents: {pids:?}");
        // Only one process ever served; the others lost the lock and exited.
        tokio::time::sleep(Duration::from_millis(300)).await;
        let serving = fx.log().matches(" serving ").count();
        assert_eq!(serving, 1, "log:\n{}", fx.log());
    });
}

fn stale_endpoint_after_kill() {
    let fx = Fixture::new("kill", false);
    let app = fx.app("1.0.0");
    block_on(async {
        let first = app.connect_or_spawn().await.unwrap().hello().pid;
        kill_hard(first);
        wait_until_gone(&app, Duration::from_secs(5)).await;
        if cfg!(unix) {
            // The dead agent's socket file is still there; the next agent must replace it.
            assert!(app.locations().unwrap().endpoint.exists());
        }
        let client = app.connect_or_spawn().await.unwrap();
        assert_ne!(client.hello().pid, first);
        assert_eq!(echo(&client, "back").await, "back");
    });
}

fn shutdown() {
    let fx = Fixture::new("shutdown", false);
    let app = fx.app("1.0.0");
    block_on(async {
        let client = app.connect_or_spawn().await.unwrap();
        set_busy(&client, true).await;
        let err = client.shutdown().await.unwrap_err();
        assert!(
            matches!(err, Error::Busy(ref r) if r == "dummy job running"),
            "{err:?}"
        );
        set_busy(&client, false).await;
        client.shutdown().await.unwrap();
        wait_until_gone(&app, Duration::from_secs(5)).await;
    });
}

fn idle_exit() {
    let mut fx = Fixture::new("idle", false);
    fx.idle_ms = "300".into();
    let app = fx.app("1.0.0");
    block_on(async {
        let started = Instant::now();
        let client = app.connect_or_spawn().await.unwrap();
        assert_eq!(echo(&client, "x").await, "x");

        // Busy holds off idle exit.
        set_busy(&client, true).await;
        tokio::time::sleep(Duration::from_millis(900)).await;
        assert_eq!(echo(&client, "still here").await, "still here");
        set_busy(&client, false).await;

        wait_until_gone(&app, Duration::from_secs(5)).await;
        assert!(started.elapsed() >= Duration::from_millis(1200));
    });
}

fn version_mismatch_reported() {
    let fx = Fixture::new("version", false);
    let old = fx.app("1.0.0");
    let new = fx.app("2.0.0");
    block_on(async {
        let first = old.connect_or_spawn().await.unwrap();
        assert!(first.mismatch().is_none());

        let client = new.connect_or_spawn().await.unwrap();
        assert_eq!(client.hello().pid, first.hello().pid);
        let m = client.mismatch().expect("mismatch reported");
        assert_eq!(m.client_version, "2.0.0");
        assert_eq!(m.agent_version, "1.0.0");
        assert_eq!(m.client_protocol, m.agent_protocol);

        // The app's policy: restart, but not while busy.
        set_busy(&client, true).await;
        let err = client.restart().await.unwrap_err();
        assert!(matches!(err, Error::Busy(_)), "{err:?}");

        set_busy(&first, false).await;
        let client = new.connect().await.unwrap().restart().await.unwrap();
        assert_ne!(client.hello().pid, first.hello().pid);
        assert_eq!(client.hello().app_version, "2.0.0");
        assert!(client.mismatch().is_none());
    });
}

fn shadow_copy() {
    let mut fx = Fixture::new("shadow", false);
    let installed = install_copy(&fx, "install");
    fx.program = Some(installed.clone());
    let app = fx.app("1.2.3");
    block_on(async {
        let client = app.connect_or_spawn().await.unwrap();
        let exe: PathBuf = client.call(&Req::Exe).await.unwrap();
        if cfg!(windows) {
            let local = PathBuf::from(std::env::var_os("LOCALAPPDATA").unwrap());
            let expected = local
                .join(&fx.name)
                .join("bin")
                .join("aat-installed-1.2.3.exe");
            assert_eq!(
                exe.to_string_lossy().to_lowercase(),
                expected.to_string_lossy().to_lowercase()
            );
        } else {
            assert_eq!(
                exe.canonicalize().unwrap(),
                installed.canonicalize().unwrap()
            );
        }
        // The installed exe is not locked by the running agent, so it can be replaced.
        reinstall(&installed);
        assert_eq!(echo(&client, "alive").await, "alive");
    });
    drop(fx);
    let _ = std::fs::remove_dir_all(installed.parent().unwrap());
}

fn binary_watch_restarts() {
    let mut fx = Fixture::new("watch", false);
    let installed = install_copy(&fx, "install");
    fx.program = Some(installed.clone());
    fx.watch = "100".into();
    let app = fx.app("1.0.0");
    block_on(async {
        let client = app.connect_or_spawn().await.unwrap();
        let first = client.hello().pid;
        set_busy(&client, true).await;
        // Let the watcher record the original identity, then replace the binary. Different
        // mtimes are what mark the change, so wait past coarse filesystem timestamps.
        tokio::time::sleep(Duration::from_millis(1100)).await;
        reinstall(&installed);

        // Busy: no restart yet.
        tokio::time::sleep(Duration::from_millis(600)).await;
        assert_eq!(app.connect().await.unwrap().hello().pid, first);
        set_busy(&client, false).await;

        let deadline = Instant::now() + Duration::from_secs(15);
        loop {
            if let Ok(c) = app.connect().await
                && c.hello().pid != first
            {
                break;
            }
            assert!(Instant::now() < deadline, "agent did not restart");
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
    });
    drop(fx);
    let _ = std::fs::remove_dir_all(installed.parent().unwrap());
}

#[cfg(unix)]
fn hung_agent_reported() {
    let fx = Fixture::new("hung", false);
    let app = fx.app("1.0.0").hello_timeout(Duration::from_millis(500));
    block_on(async {
        let pid = app.connect_or_spawn().await.unwrap().hello().pid;
        assert!(
            Command::new("kill")
                .args(["-STOP", &pid.to_string()])
                .status()
                .unwrap()
                .success()
        );
        let err = app.connect().await.unwrap_err();
        assert!(
            Command::new("kill")
                .args(["-CONT", &pid.to_string()])
                .status()
                .unwrap()
                .success()
        );
        assert!(
            matches!(err, Error::Hung { pid: Some(p) } if p == pid),
            "{err:?}"
        );
    });
}

// ---------------------------------------------------------------------------------------------
// Harness

type Test = (&'static str, fn());

fn tests() -> Vec<Test> {
    let tests: Vec<Test> = vec![
        ("cold_start", cold_start),
        ("call_and_subscribe", call_and_subscribe),
        (
            "two_concurrent_clients_one_agent",
            two_concurrent_clients_one_agent,
        ),
        ("stale_endpoint_after_kill", stale_endpoint_after_kill),
        ("shutdown", shutdown),
        ("idle_exit", idle_exit),
        ("version_mismatch_reported", version_mismatch_reported),
        ("shadow_copy", shadow_copy),
        ("binary_watch_restarts", binary_watch_restarts),
    ];
    #[cfg(unix)]
    let tests = {
        let mut tests = tests;
        tests.push(("hung_agent_reported", hung_agent_reported));
        tests
    };
    tests
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().collect();
    if args.get(1).map(String::as_str) == Some("--dummy-agent") {
        return dummy_main(&args[2..]);
    }
    let filters: Vec<&str> = args[1..]
        .iter()
        .filter(|a| !a.starts_with('-'))
        .map(String::as_str)
        .collect();
    let selected: Vec<Test> = tests()
        .into_iter()
        .filter(|(name, _)| filters.is_empty() || filters.iter().any(|f| name.contains(f)))
        .collect();

    println!("\nrunning {} tests", selected.len());
    let results: Vec<(&str, bool)> = std::thread::scope(|s| {
        let handles: Vec<_> = selected
            .iter()
            .map(|(name, test)| (*name, s.spawn(*test)))
            .collect();
        handles
            .into_iter()
            .map(|(name, handle)| {
                let ok = handle.join().is_ok();
                println!("test {name} ... {}", if ok { "ok" } else { "FAILED" });
                (name, ok)
            })
            .collect()
    });
    let failed: Vec<&str> = results.iter().filter(|r| !r.1).map(|r| r.0).collect();
    println!(
        "\ntest result: {}. {} passed; {} failed",
        if failed.is_empty() { "ok" } else { "FAILED" },
        results.len() - failed.len(),
        failed.len()
    );
    if failed.is_empty() {
        ExitCode::SUCCESS
    } else {
        println!("failures: {failed:?}");
        ExitCode::FAILURE
    }
}
