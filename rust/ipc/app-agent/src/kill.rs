//! Caller-requested force kill ([`App::kill`](crate::App::kill)).
//!
//! The pid comes from the kernel's record of who serves the endpoint, never from the lock file:
//! the agent binds only while holding the lock, so the listener is the lock holder, and a hung
//! agent still completes connects through the listen backlog (or its idle pipe instance). The
//! process is pinned before the kill and the connection re-checked, so a pid recycled after the
//! agent died is never signalled. A pid that cannot be confirmed this way is never killed.
//!
//! After the kill it waits for the agent to be gone: its lock released and, on Windows, its
//! process exited. `TerminateProcess` returns while the process is still closing its handles, and
//! the lock file can close before the pipe does; a client connecting in between reaches the dying
//! agent and reads EOF instead of finding nothing there and spawning a successor.

use std::io;
use std::time::{Duration, Instant};

use crate::app::App;
use crate::error::{Error, Result};
use crate::locations::ensure_private_dir;
use crate::lock;

#[cfg(windows)]
use crate::windows::kill_pipe_server as kill_listener;
#[cfg(unix)]
use unix::kill_listener;

pub(crate) async fn kill(app: &App) -> Result<u32> {
    let locations = app.locations()?;
    ensure_private_dir(&locations, false)?;
    let killed = match kill_listener(&locations.endpoint)? {
        Some(killed) => killed,
        None if lock::is_held(&locations.lock)? => return Err(Error::Unverified),
        None => return Err(Error::NotRunning),
    };
    let pid = killed.pid;
    // Wait for the lock to be released and, on Windows, for the process to exit, so a successor
    // can start.
    let deadline = Instant::now() + app.spawn_timeout_value();
    let exited = || {
        killed.exited().map_err(|e| {
            io::Error::new(
                e.kind(),
                format!("killed agent (pid {pid}), but waiting for it to exit failed: {e}"),
            )
        })
    };
    while lock::is_held(&locations.lock)? || !exited()? {
        if Instant::now() >= deadline {
            return Err(Error::LockNotReleased { pid });
        }
        tokio::time::sleep(Duration::from_millis(10)).await;
    }
    Ok(pid)
}

#[cfg(unix)]
mod unix {
    use std::io;
    use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
    use std::os::unix::ffi::OsStrExt;
    use std::path::Path;

    use crate::error::Result;

    /// A process [`kill_listener`] sent SIGKILL.
    pub(super) struct Killed {
        pub(super) pid: u32,
    }

    impl Killed {
        /// Always true: on Unix a connect to a dead agent's socket is refused (reported as
        /// `NotRunning`) rather than reaching it, so waiting on the lock is enough.
        pub(super) fn exited(&self) -> io::Result<bool> {
            Ok(true)
        }
    }

    /// Kill the process listening on `endpoint`. `None` if nothing accepted the connection or
    /// the listener could not be confirmed; nothing was killed then.
    pub(super) fn kill_listener(endpoint: &Path) -> Result<Option<Killed>> {
        let Some(sock) = connect(endpoint)? else {
            return Ok(None);
        };
        let Some(pid) = peer_pid(&sock)? else {
            return Ok(None);
        };
        let Some(target) = pin(pid)? else {
            return Ok(None);
        };
        // While the connection is open the listener is alive, so `pid` was still the agent's
        // when it was pinned.
        if !connected(&sock)? {
            return Ok(None);
        }
        target.kill()?;
        Ok(Some(Killed { pid }))
    }

    /// Connect without blocking. `None` when nothing accepts: no socket, nobody listening, or
    /// (Linux) a full backlog. A blocking connect would wait forever on a full backlog.
    fn connect(endpoint: &Path) -> io::Result<Option<OwnedFd>> {
        let path = endpoint.as_os_str().as_bytes();
        // SAFETY: sockaddr_un is plain data; all-zero is a valid value.
        let mut addr: libc::sockaddr_un = unsafe { std::mem::zeroed() };
        addr.sun_family = libc::AF_UNIX as libc::sa_family_t;
        if path.len() >= addr.sun_path.len() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "endpoint path too long for a Unix socket",
            ));
        }
        for (dst, src) in addr.sun_path.iter_mut().zip(path) {
            *dst = *src as libc::c_char;
        }
        // SAFETY: plain call; the result is checked before use.
        let fd = unsafe { libc::socket(libc::AF_UNIX, libc::SOCK_STREAM, 0) };
        if fd == -1 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: `fd` is a fresh descriptor nothing else owns.
        let sock = unsafe { OwnedFd::from_raw_fd(fd) };
        // SAFETY: fcntl on a descriptor we own.
        unsafe {
            if libc::fcntl(fd, libc::F_SETFD, libc::FD_CLOEXEC) == -1
                || libc::fcntl(fd, libc::F_SETFL, libc::O_NONBLOCK) == -1
            {
                return Err(io::Error::last_os_error());
            }
        }
        let len = std::mem::size_of::<libc::sockaddr_un>() - addr.sun_path.len() + path.len() + 1;
        // SAFETY: `addr` is a valid sockaddr_un and `len` covers the NUL-terminated path in it.
        let rc = unsafe {
            libc::connect(
                fd,
                (&addr as *const libc::sockaddr_un).cast(),
                len as libc::socklen_t,
            )
        };
        if rc == 0 {
            return Ok(Some(sock));
        }
        let err = io::Error::last_os_error();
        match err.raw_os_error() {
            Some(libc::ENOENT | libc::ECONNREFUSED | libc::EAGAIN | libc::EINPROGRESS) => Ok(None),
            _ => Err(err),
        }
    }

    /// Whether the peer still holds its end: a peek finds no data yet (or some), not EOF or an
    /// error.
    fn connected(sock: &OwnedFd) -> io::Result<bool> {
        let mut byte = 0u8;
        // SAFETY: `byte` is a valid one-byte buffer; MSG_PEEK leaves the socket's data alone.
        let n = unsafe {
            libc::recv(
                sock.as_raw_fd(),
                (&mut byte as *mut u8).cast(),
                1,
                libc::MSG_PEEK | libc::MSG_DONTWAIT,
            )
        };
        if n > 0 {
            return Ok(true);
        }
        if n == 0 {
            return Ok(false);
        }
        let err = io::Error::last_os_error();
        match err.kind() {
            io::ErrorKind::WouldBlock => Ok(true),
            io::ErrorKind::ConnectionReset | io::ErrorKind::NotConnected => Ok(false),
            _ => Err(err),
        }
    }

    /// The pid of the process that listens on the endpoint (`SO_PEERCRED`), recorded by the
    /// kernel when it called `listen`. `None` if the connection is already gone.
    #[cfg(target_os = "linux")]
    fn peer_pid(sock: &OwnedFd) -> io::Result<Option<u32>> {
        // SAFETY: ucred is plain data; all-zero is a valid value.
        let mut cred: libc::ucred = unsafe { std::mem::zeroed() };
        let mut len = std::mem::size_of::<libc::ucred>() as libc::socklen_t;
        // SAFETY: `cred` and `len` are valid out pointers of the size passed.
        if unsafe {
            libc::getsockopt(
                sock.as_raw_fd(),
                libc::SOL_SOCKET,
                libc::SO_PEERCRED,
                (&mut cred as *mut libc::ucred).cast(),
                &mut len,
            )
        } == -1
        {
            return Err(io::Error::last_os_error());
        }
        Ok(u32::try_from(cred.pid).ok().filter(|&pid| pid != 0))
    }

    /// The pid of the process on the other end (`LOCAL_PEERPID`).
    #[cfg(target_os = "macos")]
    fn peer_pid(sock: &OwnedFd) -> io::Result<Option<u32>> {
        let mut pid: libc::pid_t = 0;
        let mut len = std::mem::size_of::<libc::pid_t>() as libc::socklen_t;
        // SAFETY: `pid` and `len` are valid out pointers of the size passed.
        if unsafe {
            libc::getsockopt(
                sock.as_raw_fd(),
                libc::SOL_LOCAL,
                libc::LOCAL_PEERPID,
                (&mut pid as *mut libc::pid_t).cast(),
                &mut len,
            )
        } == -1
        {
            let err = io::Error::last_os_error();
            return match err.kind() {
                io::ErrorKind::NotConnected => Ok(None),
                _ => Err(err),
            };
        }
        Ok(u32::try_from(pid).ok().filter(|&pid| pid != 0))
    }

    /// No peer pid on this platform, so the agent can never be confirmed.
    #[cfg(not(any(target_os = "linux", target_os = "macos")))]
    fn peer_pid(_sock: &OwnedFd) -> io::Result<Option<u32>> {
        Ok(None)
    }

    /// A process to kill. On Linux a pidfd refers to that process for good, so a recycled pid
    /// cannot redirect the signal; elsewhere it is the bare pid, with a small race between the
    /// connection check and the kill.
    enum Target {
        #[cfg(target_os = "linux")]
        PidFd(OwnedFd),
        Pid(libc::pid_t),
    }

    /// `None` if the process is already gone.
    fn pin(pid: u32) -> io::Result<Option<Target>> {
        let raw = libc::pid_t::try_from(pid)
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
        #[cfg(target_os = "linux")]
        {
            // SAFETY: plain syscall; the result is checked before use.
            let fd = unsafe { libc::syscall(libc::SYS_pidfd_open, raw, 0) };
            if fd >= 0 {
                // SAFETY: pidfd_open returned a fresh descriptor nothing else owns.
                return Ok(Some(Target::PidFd(unsafe {
                    OwnedFd::from_raw_fd(fd as libc::c_int)
                })));
            }
            let err = io::Error::last_os_error();
            match err.raw_os_error() {
                Some(libc::ESRCH) => return Ok(None),
                // Before Linux 5.3: fall back to the bare pid, as on macOS.
                Some(libc::ENOSYS) => {}
                _ => return Err(err),
            }
        }
        Ok(Some(Target::Pid(raw)))
    }

    impl Target {
        /// SIGKILL. A target that exited in the meantime counts as killed.
        fn kill(&self) -> io::Result<()> {
            let rc = match self {
                #[cfg(target_os = "linux")]
                // SAFETY: plain syscall on a pidfd we own.
                Target::PidFd(fd) => unsafe {
                    libc::syscall(
                        libc::SYS_pidfd_send_signal,
                        fd.as_raw_fd(),
                        libc::SIGKILL,
                        std::ptr::null::<libc::siginfo_t>(),
                        0,
                    ) as libc::c_int
                },
                // SAFETY: plain call.
                Target::Pid(pid) => unsafe { libc::kill(*pid, libc::SIGKILL) },
            };
            if rc == -1 {
                let err = io::Error::last_os_error();
                if err.raw_os_error() != Some(libc::ESRCH) {
                    return Err(err);
                }
            }
            Ok(())
        }
    }
}
