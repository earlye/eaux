use std::ffi::{OsStr, OsString};
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::time::Duration;

use crate::agent::{self, EXIT_ALREADY_RUNNING, Handler, Stopped};
use crate::client::{self, Client};
use crate::error::{Error, Result};
use crate::kill;
use crate::locations::{self, Locations};

/// One app's agent: who it is, how to start it, and how it behaves.
///
/// Build the same `App` in the CLI and in the agent process (usually one shared function), then
/// call [`App::connect_or_spawn`] from the CLI and [`App::run`] (or [`App::serve`]) in the agent.
#[derive(Clone, Debug)]
pub struct App {
    name: String,
    version: String,
    program: Option<PathBuf>,
    args: Vec<OsString>,
    endpoint: Option<OsString>,
    idle_timeout: Option<Duration>,
    watch_interval: Option<Duration>,
    spawn_timeout: Duration,
    hello_timeout: Duration,
}

impl App {
    /// `name` keys every location (`[A-Za-z0-9._-]`); `version` is the app version reported in
    /// `hello` and compared by [`Client::mismatch`].
    pub fn new(name: impl Into<String>, version: impl Into<String>) -> Self {
        App {
            name: name.into(),
            version: version.into(),
            program: None,
            args: Vec::new(),
            endpoint: None,
            idle_timeout: None,
            watch_interval: None,
            spawn_timeout: Duration::from_secs(5),
            hello_timeout: Duration::from_secs(5),
        }
    }

    /// The executable to spawn as the agent. Defaults to the current executable.
    pub fn agent_program(mut self, program: impl Into<PathBuf>) -> Self {
        self.program = Some(program.into());
        self
    }

    /// Arguments that make the program run as the agent, e.g. `["agent"]`.
    pub fn agent_args<I, S>(mut self, args: I) -> Self
    where
        I: IntoIterator<Item = S>,
        S: Into<OsString>,
    {
        self.args = args.into_iter().map(Into::into).collect();
        self
    }

    /// Use this endpoint instead of the per-user default, as the `<APP>_AGENT_ENDPOINT`
    /// environment variable would (this wins over the variable). A spawned agent inherits it.
    /// On Windows a bare name is taken as `\\.\pipe\<name>`.
    pub fn endpoint(mut self, endpoint: impl Into<OsString>) -> Self {
        self.endpoint = Some(endpoint.into());
        self
    }

    /// Agent: exit after this long with no requests in flight while the handler is not busy.
    /// `None` (the default) means never.
    pub fn idle_timeout(mut self, timeout: Option<Duration>) -> Self {
        self.idle_timeout = timeout;
        self
    }

    /// Agent: poll the installed executable at this interval and, once the handler is not busy,
    /// restart from it when it changes. Off by default.
    pub fn watch_binary(mut self, every: Duration) -> Self {
        self.watch_interval = Some(every);
        self
    }

    /// Client: how long [`App::connect_or_spawn`] keeps retrying after spawning, and how long
    /// [`App::kill`] waits for the lock to be released and, on Windows, for the process to exit
    /// (default 5s).
    pub fn spawn_timeout(mut self, timeout: Duration) -> Self {
        self.spawn_timeout = timeout;
        self
    }

    /// Client: how long to wait for `hello` before reporting [`Error::Hung`] (default 5s).
    pub fn hello_timeout(mut self, timeout: Duration) -> Self {
        self.hello_timeout = timeout;
        self
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn version(&self) -> &str {
        &self.version
    }

    /// Resolve the endpoint, lock and log paths for this user.
    pub fn locations(&self) -> Result<Locations> {
        locations::resolve(&self.name, self.endpoint.as_deref(), &|k| {
            std::env::var_os(k)
        })
    }

    /// Connect to a running agent; [`Error::NotRunning`] if there is none.
    pub async fn connect(&self) -> Result<Client> {
        client::connect(self, false).await
    }

    /// Connect to the agent, starting it detached first if it is not running.
    pub async fn connect_or_spawn(&self) -> Result<Client> {
        client::connect(self, true).await
    }

    /// Kill the agent (SIGKILL, or `TerminateProcess` on Windows) and return its pid. Works on a
    /// hung agent: it needs no `hello`. The crate never kills on its own; this is the only way.
    ///
    /// The pid is the one the OS reports for the endpoint's server, and the process is pinned
    /// before the kill (a pidfd on Linux, a process handle on Windows) and the connection
    /// re-checked, so a recycled pid is not killed; macOS has no pidfd and keeps a small race
    /// between that check and the kill. Waits up to the spawn timeout for the lock to be released
    /// and, on Windows, for the process to exit (its pipe can outlive the lock file briefly), so
    /// [`App::connect_or_spawn`] can follow directly.
    ///
    /// Fails with [`Error::NotRunning`] if nothing answers on the endpoint and nothing holds the
    /// lock, [`Error::Unverified`] if the lock is held but nothing confirmable answers (nothing is
    /// killed), and [`Error::LockNotReleased`] if the agent is not gone by the spawn timeout.
    pub async fn kill(&self) -> Result<u32> {
        kill::kill(self).await
    }

    /// Be the agent: take the lock, bind, and serve `handler` until stopped.
    /// Returns [`Error::AlreadyRunning`] if another agent holds the lock.
    ///
    /// The lock is released before this returns, so the process should exit soon after;
    /// requests already in flight keep running until the runtime shuts down.
    pub async fn serve<H: Handler>(&self, handler: H) -> Result<Stopped> {
        agent::serve(self, handler).await
    }

    /// [`App::serve`] for `main`: maps "already running" to [`EXIT_ALREADY_RUNNING`], other
    /// errors to 1 (after printing them to stderr), and every normal stop to success.
    pub async fn run<H: Handler>(&self, handler: H) -> ExitCode {
        match self.serve(handler).await {
            Ok(_) => ExitCode::SUCCESS,
            Err(Error::AlreadyRunning { .. }) => ExitCode::from(EXIT_ALREADY_RUNNING),
            Err(e) => {
                eprintln!("app-agent: {}: {e}", self.name);
                ExitCode::FAILURE
            }
        }
    }

    pub(crate) fn program(&self) -> Option<&Path> {
        self.program.as_deref()
    }
    pub(crate) fn args(&self) -> &[OsString] {
        &self.args
    }
    pub(crate) fn explicit_endpoint(&self) -> Option<&OsStr> {
        self.endpoint.as_deref()
    }
    pub(crate) fn endpoint_env(&self) -> String {
        locations::endpoint_env_var(&self.name)
    }
    #[cfg(windows)]
    pub(crate) fn installed_exe_env(&self) -> String {
        self.endpoint_env()
            .replace("_AGENT_ENDPOINT", "_AGENT_INSTALLED_EXE")
    }
    pub(crate) fn idle_timeout_value(&self) -> Option<Duration> {
        self.idle_timeout
    }
    pub(crate) fn watch_interval(&self) -> Option<Duration> {
        self.watch_interval
    }
    pub(crate) fn spawn_timeout_value(&self) -> Duration {
        self.spawn_timeout
    }
    pub(crate) fn hello_timeout_value(&self) -> Duration {
        self.hello_timeout
    }
}
