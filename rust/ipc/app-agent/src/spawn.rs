//! Launching a detached agent process.

use std::ffi::OsString;
use std::fs::OpenOptions;
use std::io;
use std::path::Path;
use std::process::{Child, Command, Stdio};

use crate::locations::ensure_parent;

/// Where the detached process's stdout/stderr go.
pub(crate) enum Output<'a> {
    /// Append to this log file.
    Log(&'a Path),
    /// Keep the current process's stdout/stderr (already the log when we are an agent).
    Inherit,
}

/// Start `program` in its own session (Unix) or detached process group (Windows), so it survives
/// the terminal and the process that started it.
pub(crate) fn spawn_detached(
    program: &Path,
    args: &[OsString],
    env: &[(String, OsString)],
    output: Output<'_>,
) -> io::Result<Child> {
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
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        // SAFETY: setsid is async-signal-safe and touches no memory of the parent.
        unsafe {
            cmd.pre_exec(|| {
                if libc::setsid() == -1 {
                    return Err(io::Error::last_os_error());
                }
                Ok(())
            });
        }
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        use windows_sys::Win32::System::Threading::{CREATE_NEW_PROCESS_GROUP, DETACHED_PROCESS};
        cmd.creation_flags(DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP);
    }
    cmd.spawn()
}

/// Reap `child` in the background so it never lingers as a zombie while we keep running.
pub(crate) fn reap(child: Option<Child>) {
    if let Some(mut child) = child {
        std::thread::spawn(move || {
            let _ = child.wait();
        });
    }
}
