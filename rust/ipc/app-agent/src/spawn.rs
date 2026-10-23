//! Launching a detached agent process.
//!
//! The agent outlives the CLI that starts it, so it must not keep any of the CLI's handles open:
//! a caller reading the CLI's stdout to EOF (e.g. `Command::output()`) would wait for the agent to
//! exit. The child gets exactly null stdin and its stdout/stderr, nothing else.

use std::path::Path;

#[cfg(unix)]
pub(crate) use std::process::Child;

#[cfg(windows)]
pub(crate) use crate::windows::{Child, spawn_detached};

/// Where the detached process's stdout/stderr go.
pub(crate) enum Output<'a> {
    /// Append to this log file.
    Log(&'a Path),
    /// Keep the current process's stdout/stderr (already the log when we are an agent).
    Inherit,
}

/// Start `program` in its own session, so it survives the terminal and the process that started
/// it.
#[cfg(unix)]
pub(crate) fn spawn_detached(
    program: &Path,
    args: &[std::ffi::OsString],
    env: &[(String, std::ffi::OsString)],
    output: Output<'_>,
) -> std::io::Result<Child> {
    use std::fs::OpenOptions;
    use std::io;
    use std::os::unix::process::CommandExt;
    use std::process::{Command, Stdio};

    use crate::locations::ensure_parent;

    let mut cmd = Command::new(program);
    cmd.args(args).stdin(Stdio::null());
    for (k, v) in env {
        cmd.env(k, v);
    }
    if let Output::Log(log) = output {
        ensure_parent(log)?;
        let file = OpenOptions::new().create(true).append(true).open(log)?;
        cmd.stdout(file.try_clone()?).stderr(file);
    }
    // SAFETY: plain call, read before forking.
    let open_max = unsafe { libc::sysconf(libc::_SC_OPEN_MAX) };
    let open_max = libc::c_int::try_from(open_max.clamp(256, 1 << 20)).unwrap_or(1 << 20);
    // SAFETY: setsid, fcntl and close_range are async-signal-safe and touch no memory of the
    // parent.
    unsafe {
        cmd.pre_exec(move || {
            if libc::setsid() == -1 {
                return Err(io::Error::last_os_error());
            }
            cloexec_from_3(open_max);
            Ok(())
        });
    }
    cmd.spawn()
}

/// Mark every descriptor above stdio close-on-exec, so descriptors this process inherited without
/// `FD_CLOEXEC` (a make jobserver, a shell's `3>&1`) do not reach the agent. Marking rather than
/// closing keeps `Command`'s own exec-error pipe working.
#[cfg(unix)]
unsafe fn cloexec_from_3(open_max: libc::c_int) {
    #[cfg(target_os = "linux")]
    // SAFETY: close_range only changes descriptor flags; failure (pre-5.11 kernel) falls through.
    if unsafe {
        libc::syscall(
            libc::SYS_close_range,
            3u32,
            u32::MAX,
            libc::CLOSE_RANGE_CLOEXEC,
        )
    } == 0
    {
        return;
    }
    for fd in 3..open_max {
        // SAFETY: fcntl on a descriptor that may not exist just fails with EBADF.
        unsafe {
            let flags = libc::fcntl(fd, libc::F_GETFD);
            if flags >= 0 && flags & libc::FD_CLOEXEC == 0 {
                libc::fcntl(fd, libc::F_SETFD, flags | libc::FD_CLOEXEC);
            }
        }
    }
}

/// Reap `child` in the background so it never lingers as a zombie while we keep running.
pub(crate) fn reap(child: Option<Child>) {
    if let Some(mut child) = child {
        std::thread::spawn(move || {
            let _ = child.wait();
        });
    }
}
