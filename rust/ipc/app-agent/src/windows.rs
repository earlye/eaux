//! Windows-only pieces: user SIDs, the pipe DACL, the pipe-owner check and the shadow copy.

use std::ffi::OsString;
use std::io;
use std::path::{Path, PathBuf};

use interprocess::os::windows::security_descriptor::SecurityDescriptor;
use widestring::U16CString;
use windows_sys::Win32::Foundation::{
    CloseHandle, ERROR_INSUFFICIENT_BUFFER, HANDLE, HLOCAL, LocalFree,
};
use windows_sys::Win32::Security::Authorization::ConvertSidToStringSidW;
use windows_sys::Win32::Security::{
    GetLengthSid, GetTokenInformation, TOKEN_QUERY, TOKEN_USER, TokenUser,
};
use windows_sys::Win32::System::Threading::{
    GetCurrentProcess, OpenProcess, OpenProcessToken, PROCESS_QUERY_LIMITED_INFORMATION,
};

use crate::error::{Error, Result};
use crate::spawn::{Output, spawn_detached};

struct OwnedHandle(HANDLE);

impl Drop for OwnedHandle {
    fn drop(&mut self) {
        // SAFETY: we own the handle and close it exactly once.
        unsafe { CloseHandle(self.0) };
    }
}

/// A SID copied out of a token, as raw bytes (byte equality is SID equality).
struct Sid(Vec<u8>);

fn token_user_sid(process: HANDLE) -> io::Result<Sid> {
    let mut token: HANDLE = std::ptr::null_mut();
    // SAFETY: `process` is a valid process handle; `token` receives a new handle we own.
    if unsafe { OpenProcessToken(process, TOKEN_QUERY, &mut token) } == 0 {
        return Err(io::Error::last_os_error());
    }
    let token = OwnedHandle(token);
    let mut len = 0u32;
    // SAFETY: size query with a null buffer; Windows reports the needed length in `len`.
    unsafe { GetTokenInformation(token.0, TokenUser, std::ptr::null_mut(), 0, &mut len) };
    let err = io::Error::last_os_error();
    if err.raw_os_error() != Some(ERROR_INSUFFICIENT_BUFFER as i32) {
        return Err(err);
    }
    // u64 storage keeps TOKEN_USER (pointer-aligned) correctly aligned.
    let mut buf = vec![0u64; (len as usize).div_ceil(8)];
    // SAFETY: `buf` holds at least `len` bytes.
    if unsafe { GetTokenInformation(token.0, TokenUser, buf.as_mut_ptr().cast(), len, &mut len) }
        == 0
    {
        return Err(io::Error::last_os_error());
    }
    // SAFETY: on success the buffer starts with a TOKEN_USER whose SID points into the buffer.
    let sid = unsafe { (*(buf.as_ptr() as *const TOKEN_USER)).User.Sid };
    // SAFETY: `sid` is a valid SID inside `buf`, which is alive here.
    let sid_len = unsafe { GetLengthSid(sid) } as usize;
    // SAFETY: GetLengthSid reported the SID's size in bytes.
    let bytes = unsafe { std::slice::from_raw_parts(sid as *const u8, sid_len) }.to_vec();
    Ok(Sid(bytes))
}

fn current_user_sid() -> io::Result<Sid> {
    // SAFETY: the current-process pseudo handle needs no closing.
    token_user_sid(unsafe { GetCurrentProcess() })
}

/// The current user's SID in `S-1-5-...` form.
pub(crate) fn current_user_sid_string() -> io::Result<String> {
    let sid = current_user_sid()?;
    let mut out: *mut u16 = std::ptr::null_mut();
    // SAFETY: `sid.0` is a valid SID; on success `out` is a LocalAlloc'ed NUL-terminated string.
    if unsafe { ConvertSidToStringSidW(sid.0.as_ptr() as *mut _, &mut out) } == 0 {
        return Err(io::Error::last_os_error());
    }
    // SAFETY: `out` is NUL-terminated per the API contract.
    let s = unsafe { widestring::U16CStr::from_ptr_str(out) }.to_string_lossy();
    // SAFETY: `out` was allocated by LocalAlloc inside ConvertSidToStringSidW.
    unsafe { LocalFree(out as HLOCAL) };
    Ok(s)
}

/// A DACL that grants full access to the current user and SYSTEM, and nobody else.
pub(crate) fn pipe_security_descriptor() -> io::Result<SecurityDescriptor> {
    let sddl = format!("D:P(A;;GA;;;SY)(A;;GA;;;{})", current_user_sid_string()?);
    let sddl =
        U16CString::from_str(sddl).map_err(|e| io::Error::new(io::ErrorKind::InvalidInput, e))?;
    SecurityDescriptor::deserialize(&sddl)
}

/// Refuse a pipe server owned by another user: a squatter can deny service but cannot pose as
/// the agent.
pub(crate) fn verify_server_owner(server_pid: u32) -> Result<()> {
    // SAFETY: plain call; a null result is handled.
    let process = unsafe { OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, server_pid) };
    if process.is_null() {
        // A process we may not even query is not ours.
        return Err(Error::NotOwner { pid: server_pid });
    }
    let process = OwnedHandle(process);
    let theirs = token_user_sid(process.0).map_err(|_| Error::NotOwner { pid: server_pid })?;
    if theirs.0 == current_user_sid()?.0 {
        Ok(())
    } else {
        Err(Error::NotOwner { pid: server_pid })
    }
}

/// Whether this process should keep starting up or has handed off to its shadow copy.
pub(crate) enum Shadow {
    /// We are the shadow copy; `installed` is the original exe to watch and relaunch.
    Running { installed: PathBuf },
    /// We were the installed exe and started the shadow copy; exit now.
    HandedOff,
}

fn same_file_contents(a: &Path, b: &Path) -> bool {
    match (std::fs::metadata(a), std::fs::metadata(b)) {
        (Ok(a), Ok(b)) => a.len() == b.len() && a.modified().ok() == b.modified().ok(),
        _ => false,
    }
}

fn eq_ignore_case(a: &Path, b: &Path) -> bool {
    a.as_os_str()
        .to_string_lossy()
        .eq_ignore_ascii_case(&b.as_os_str().to_string_lossy())
}

/// Run the agent from `<data>\bin\<exe>-<version>.exe` so the installed exe is never locked and
/// `cargo install` can replace it while the agent runs.
pub(crate) fn shadow(data_dir: &Path, version: &str, installed_env: &str) -> io::Result<Shadow> {
    let exe = std::env::current_exe()?;
    let bin = data_dir.join("bin");
    if exe.parent().is_some_and(|p| eq_ignore_case(p, &bin)) {
        // Delete copies left by earlier versions. Running ones are locked and survive until a
        // later start.
        if let Ok(entries) = std::fs::read_dir(&bin) {
            for entry in entries.flatten() {
                let path = entry.path();
                if !eq_ignore_case(&path, &exe) {
                    let _ = std::fs::remove_file(path);
                }
            }
        }
        let installed = std::env::var_os(installed_env)
            .map(PathBuf::from)
            .unwrap_or(exe);
        return Ok(Shadow::Running { installed });
    }

    let stem = exe
        .file_stem()
        .map(|s| s.to_string_lossy().into_owned())
        .unwrap_or_else(|| "agent".into());
    let target = bin.join(format!("{stem}-{version}.exe"));
    if !same_file_contents(&exe, &target) {
        std::fs::create_dir_all(&bin)?;
        let tmp = bin.join(format!("{stem}-{version}.{}.tmp", std::process::id()));
        std::fs::copy(&exe, &tmp)?;
        // The old copy may still be running (e.g. it is the agent restarting itself after a
        // binary change) and so locked; give it a few seconds to exit.
        let mut attempts = 0;
        while let Err(e) = std::fs::rename(&tmp, &target) {
            attempts += 1;
            if attempts >= 50 {
                let _ = std::fs::remove_file(&tmp);
                if target.exists() {
                    // Fall back to the copy we have rather than not starting at all.
                    break;
                }
                return Err(e);
            }
            std::thread::sleep(std::time::Duration::from_millis(100));
        }
    }
    let args: Vec<OsString> = std::env::args_os().skip(1).collect();
    let env = [(installed_env.to_string(), exe.into_os_string())];
    let child = spawn_detached(&target, &args, &env, Output::Inherit)?;
    drop(child);
    Ok(Shadow::HandedOff)
}
