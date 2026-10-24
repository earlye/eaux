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
    /// From [`App::kill`](crate::App::kill): nothing answered on the endpoint and nothing holds
    /// the lock.
    NotRunning,
    /// The agent accepted the connection but did not answer `hello` in time, or did not
    /// acknowledge [`Client::drain`](crate::Client::drain) before its timeout. The drain was
    /// sent, so whether the agent received it is unknown; if it did, the drain is in effect once
    /// the agent resumes.
    /// The crate never kills it on its own; [`App::kill`](crate::App::kill) kills it on request.
    /// `pid` comes from the lock file when connecting, or from `hello` for `drain`.
    Hung { pid: Option<u32> },
    /// From [`App::kill`](crate::App::kill): something holds the agent lock, but no process could
    /// be confirmed as the agent through its endpoint (nothing accepted the connection, e.g. its
    /// listen backlog is full), so nothing was killed.
    Unverified,
    /// From [`App::kill`](crate::App::kill): the agent was killed, but it was not gone when the
    /// spawn timeout ran out: its lock was still held or, on Windows, its process had not exited.
    /// The name predates the Windows process check.
    LockNotReleased { pid: u32 },
    /// The spawned agent exited with an unexpected status before it started listening.
    AgentFailed { status: ExitStatus, log: PathBuf },
    /// The agent never became reachable within the spawn timeout.
    SpawnTimedOut { log: PathBuf },
    /// The agent refused to stop because the app says it is busy, or refused a drain because
    /// it is already restarting into a new binary.
    Busy(String),
    /// The agent had not stopped when [`Client::drain`](crate::Client::drain) gave up waiting.
    /// The drain stays in effect: the agent stops once it is idle (the handler not busy and no
    /// request in flight).
    StillDraining { pid: u32 },
    /// The agent is too old to understand the named request.
    Unsupported(&'static str),
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
            Error::Unverified => write!(
                f,
                "agent lock is held, but its holder could not be confirmed; nothing was killed"
            ),
            Error::LockNotReleased { pid } => write!(
                f,
                "killed agent (pid {pid}), but it did not exit and release its lock in time"
            ),
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
            Error::Unsupported(what) => write!(f, "agent does not support {what}"),
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
