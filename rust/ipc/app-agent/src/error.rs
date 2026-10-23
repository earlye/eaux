use std::fmt;
use std::io;
use std::path::PathBuf;
use std::process::ExitStatus;

/// Everything that can go wrong on either side of an agent connection.
#[derive(Debug)]
#[non_exhaustive]
pub enum Error {
    /// The agent lock is held by another process, so this process must not serve.
    /// `pid` is read from the lock file and may be absent if it was not written yet.
    AlreadyRunning { pid: Option<u32> },
    /// No agent is listening on the endpoint (returned by [`App::connect`](crate::App::connect)).
    NotRunning,
    /// The agent accepted the connection but did not answer `hello` in time.
    /// The crate never kills it; `pid` comes from the lock file.
    Hung { pid: Option<u32> },
    /// The spawned agent exited with an unexpected status before it started listening.
    AgentFailed { status: ExitStatus, log: PathBuf },
    /// The agent never became reachable within the spawn timeout.
    SpawnTimedOut { log: PathBuf },
    /// The agent refused to stop because the app says it is busy.
    Busy(String),
    /// The agent had not stopped when [`Client::drain`](crate::Client::drain) gave up waiting.
    /// The drain stays in effect: the agent stops once it is idle (the handler not busy and no
    /// request in flight).
    StillDraining { pid: u32 },
    /// The app's handler returned an error.
    Remote(String),
    /// Windows only: the named pipe is served by a process owned by another user.
    NotOwner { pid: u32 },
    /// The per-user runtime directory exists but is unsafe (wrong owner or mode).
    InsecureDir { path: PathBuf, reason: String },
    /// The agent sent something this client could not understand, or the reply
    /// shape did not match the call (e.g. an event stream answered `call`).
    Protocol(String),
    /// The connection to the agent closed.
    Disconnected,
    /// An underlying I/O error.
    Io(io::Error),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::AlreadyRunning { pid: Some(pid) } => {
                write!(f, "agent already running (pid {pid})")
            }
            Error::AlreadyRunning { pid: None } => write!(f, "agent already running"),
            Error::NotRunning => write!(f, "agent is not running"),
            Error::Hung { pid: Some(pid) } => {
                write!(f, "agent (pid {pid}) is not answering; it was not killed")
            }
            Error::Hung { pid: None } => write!(f, "agent is not answering; it was not killed"),
            Error::AgentFailed { status, log } => {
                write!(f, "agent exited with {status}; see {}", log.display())
            }
            Error::SpawnTimedOut { log } => {
                write!(f, "agent did not start in time; see {}", log.display())
            }
            Error::Busy(reason) => write!(f, "agent is busy: {reason}"),
            Error::StillDraining { pid } => write!(
                f,
                "agent (pid {pid}) is still draining; it will stop once idle"
            ),
            Error::Remote(message) => write!(f, "agent error: {message}"),
            Error::NotOwner { pid } => write!(
                f,
                "agent endpoint is served by pid {pid}, which belongs to another user"
            ),
            Error::InsecureDir { path, reason } => {
                write!(f, "refusing to use {}: {reason}", path.display())
            }
            Error::Protocol(message) => write!(f, "agent protocol error: {message}"),
            Error::Disconnected => write!(f, "connection to agent closed"),
            Error::Io(e) => write!(f, "{e}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Error::Io(e) => Some(e),
            _ => None,
        }
    }
}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Error::Io(e)
    }
}

impl From<serde_json::Error> for Error {
    fn from(e: serde_json::Error) -> Self {
        Error::Protocol(e.to_string())
    }
}

pub type Result<T, E = Error> = std::result::Result<T, E>;
