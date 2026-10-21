//! The single-instance lock. The lock decides who serves, never the endpoint.
//!
//! The holder writes its pid at the start of the file. On Unix this is an advisory `flock` over
//! the whole file. On Windows `LockFileEx` locks are mandatory, so the lock covers one byte far
//! past the pid; other processes can still read the pid while the agent runs.

use std::fs::{File, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::Path;

use crate::locations::ensure_parent;

/// Held for the agent's whole life. Dropping it (or the process dying) releases the lock.
#[derive(Debug)]
pub(crate) struct LockGuard {
    _file: File,
}

/// Take the lock without blocking. `Ok(None)` means someone else holds it.
pub(crate) fn try_acquire(path: &Path) -> io::Result<Option<LockGuard>> {
    ensure_parent(path)?;
    // No truncate here: the current holder's pid must survive a failed attempt.
    let mut file = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(path)?;
    if !platform::try_lock(&file)? {
        return Ok(None);
    }
    file.set_len(0)?;
    file.seek(SeekFrom::Start(0))?;
    write!(file, "{}", std::process::id())?;
    file.flush()?;
    Ok(Some(LockGuard { _file: file }))
}

/// The pid recorded in the lock file, if any. It may be stale if no agent holds the lock.
pub(crate) fn read_pid(path: &Path) -> Option<u32> {
    let mut text = String::new();
    File::open(path).ok()?.read_to_string(&mut text).ok()?;
    text.trim().parse().ok()
}

#[cfg(unix)]
mod platform {
    use super::*;

    pub(super) fn try_lock(file: &File) -> io::Result<bool> {
        match file.try_lock() {
            Ok(()) => Ok(true),
            Err(std::fs::TryLockError::WouldBlock) => Ok(false),
            Err(std::fs::TryLockError::Error(e)) => Err(e),
        }
    }
}

#[cfg(windows)]
mod platform {
    use super::*;
    use std::os::windows::io::AsRawHandle;
    use windows_sys::Win32::Foundation::{ERROR_IO_PENDING, ERROR_LOCK_VIOLATION, HANDLE};
    use windows_sys::Win32::Storage::FileSystem::{
        LOCKFILE_EXCLUSIVE_LOCK, LOCKFILE_FAIL_IMMEDIATELY, LockFileEx,
    };
    use windows_sys::Win32::System::IO::OVERLAPPED;

    pub(super) fn try_lock(file: &File) -> io::Result<bool> {
        // SAFETY: OVERLAPPED is plain data; all-zero is a valid value.
        let mut overlapped: OVERLAPPED = unsafe { std::mem::zeroed() };
        overlapped.Anonymous.Anonymous.Offset = u32::MAX;
        overlapped.Anonymous.Anonymous.OffsetHigh = i32::MAX as u32;
        // SAFETY: the handle is valid for the call and `overlapped` outlives it; the file is not
        // opened for overlapped I/O, so the call completes synchronously.
        let ok = unsafe {
            LockFileEx(
                file.as_raw_handle() as HANDLE,
                LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                0,
                1,
                0,
                &mut overlapped,
            )
        };
        if ok != 0 {
            return Ok(true);
        }
        let err = io::Error::last_os_error();
        match err.raw_os_error() {
            Some(code)
                if code == ERROR_LOCK_VIOLATION as i32 || code == ERROR_IO_PENDING as i32 =>
            {
                Ok(false)
            }
            _ => Err(err),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn second_acquire_fails_and_pid_is_readable() {
        let path =
            std::env::temp_dir().join(format!("app-agent-lock-test-{}.lock", std::process::id()));
        let _ = std::fs::remove_file(&path);
        let guard = try_acquire(&path).unwrap().expect("first acquire");
        assert!(try_acquire(&path).unwrap().is_none());
        assert_eq!(read_pid(&path), Some(std::process::id()));
        drop(guard);
        let again = try_acquire(&path).unwrap();
        assert!(again.is_some());
        drop(again);
        std::fs::remove_file(&path).unwrap();
    }
}
