//! Windows-only pieces: user SIDs, the pipe DACL, the pipe-owner check, spawning without leaking
//! handles, and the shadow copy.

use std::ffi::{OsStr, OsString};
use std::fs::OpenOptions;
use std::io;
use std::os::windows::ffi::OsStrExt;
use std::os::windows::io::{AsRawHandle, FromRawHandle, OwnedHandle, RawHandle};
use std::os::windows::process::ExitStatusExt;
use std::path::{Path, PathBuf};
use std::process::ExitStatus;

use interprocess::os::windows::security_descriptor::SecurityDescriptor;
use widestring::U16CString;
use windows_sys::Win32::Foundation::{
    DUPLICATE_SAME_ACCESS, DuplicateHandle, ERROR_INSUFFICIENT_BUFFER, HANDLE, HLOCAL,
    INVALID_HANDLE_VALUE, LocalFree, WAIT_OBJECT_0, WAIT_TIMEOUT,
};
use windows_sys::Win32::Security::Authorization::ConvertSidToStringSidW;
use windows_sys::Win32::Security::{
    GetLengthSid, GetTokenInformation, TOKEN_QUERY, TOKEN_USER, TokenUser,
};
use windows_sys::Win32::Storage::FileSystem::{
    BY_HANDLE_FILE_INFORMATION, FILE_BASIC_INFO, FILE_READ_ATTRIBUTES, FileBasicInfo,
    GetFileInformationByHandle, GetFileInformationByHandleEx,
};
use windows_sys::Win32::System::Threading::{
    CREATE_NEW_PROCESS_GROUP, CREATE_UNICODE_ENVIRONMENT, CreateProcessW, DETACHED_PROCESS,
    DeleteProcThreadAttributeList, EXTENDED_STARTUPINFO_PRESENT, GetCurrentProcess,
    GetExitCodeProcess, INFINITE, InitializeProcThreadAttributeList, OpenProcess, OpenProcessToken,
    PROC_THREAD_ATTRIBUTE_HANDLE_LIST, PROCESS_INFORMATION, PROCESS_QUERY_LIMITED_INFORMATION,
    STARTF_USESTDHANDLES, STARTUPINFOEXW, UpdateProcThreadAttribute, WaitForSingleObject,
};

use crate::error::{Error, Result};
use crate::locations::ensure_parent;
use crate::spawn::Output;

/// Take ownership of a handle a Win32 call just returned.
fn owned(handle: HANDLE) -> OwnedHandle {
    // SAFETY: callers pass a valid handle that nothing else owns or closes.
    unsafe { OwnedHandle::from_raw_handle(handle) }
}

/// A SID copied out of a token, as raw bytes (byte equality is SID equality).
struct Sid(Vec<u8>);

fn token_user_sid(process: HANDLE) -> io::Result<Sid> {
    let mut token: HANDLE = std::ptr::null_mut();
    // SAFETY: `process` is a valid process handle; `token` receives a new handle we own.
    if unsafe { OpenProcessToken(process, TOKEN_QUERY, &mut token) } == 0 {
        return Err(io::Error::last_os_error());
    }
    let token = owned(token);
    let mut len = 0u32;
    // SAFETY: size query with a null buffer; Windows reports the needed length in `len`.
    unsafe {
        GetTokenInformation(
            token.as_raw_handle(),
            TokenUser,
            std::ptr::null_mut(),
            0,
            &mut len,
        )
    };
    let err = io::Error::last_os_error();
    if err.raw_os_error() != Some(ERROR_INSUFFICIENT_BUFFER as i32) {
        return Err(err);
    }
    // u64 storage keeps TOKEN_USER (pointer-aligned) correctly aligned.
    let mut buf = vec![0u64; (len as usize).div_ceil(8)];
    // SAFETY: `buf` holds at least `len` bytes.
    if unsafe {
        GetTokenInformation(
            token.as_raw_handle(),
            TokenUser,
            buf.as_mut_ptr().cast(),
            len,
            &mut len,
        )
    } == 0
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
    let process = owned(process);
    let theirs =
        token_user_sid(process.as_raw_handle()).map_err(|_| Error::NotOwner { pid: server_pid })?;
    if theirs.0 == current_user_sid()?.0 {
        Ok(())
    } else {
        Err(Error::NotOwner { pid: server_pid })
    }
}

/// Which file `path` is and when it last changed, beyond length and mtime: `CopyFile` (and so
/// `std::fs::copy`) gives the copy its source's last-write time, so a replacement can match the
/// old file in both. A rename-replace gives a new volume/file index; an in-place overwrite bumps
/// the change time, which `CopyFile` does not carry over.
pub(crate) fn file_identity(path: &Path) -> io::Result<(u32, u64, i64)> {
    use std::os::windows::fs::OpenOptionsExt;
    let file = OpenOptions::new()
        .access_mode(FILE_READ_ATTRIBUTES)
        .open(path)?;
    let handle = file.as_raw_handle() as HANDLE;
    // SAFETY: plain-data out struct, fully written on success.
    let mut info: BY_HANDLE_FILE_INFORMATION = unsafe { std::mem::zeroed() };
    // SAFETY: `handle` is open for the duration of the call; `info` is a valid out pointer.
    if unsafe { GetFileInformationByHandle(handle, &mut info) } == 0 {
        return Err(io::Error::last_os_error());
    }
    // SAFETY: plain-data out struct, fully written on success.
    let mut basic: FILE_BASIC_INFO = unsafe { std::mem::zeroed() };
    // SAFETY: `basic` is a FILE_BASIC_INFO of exactly the size passed, as FileBasicInfo requires.
    if unsafe {
        GetFileInformationByHandleEx(
            handle,
            FileBasicInfo,
            (&mut basic as *mut FILE_BASIC_INFO).cast(),
            std::mem::size_of::<FILE_BASIC_INFO>() as u32,
        )
    } == 0
    {
        return Err(io::Error::last_os_error());
    }
    let index = (u64::from(info.nFileIndexHigh) << 32) | u64::from(info.nFileIndexLow);
    Ok((info.dwVolumeSerialNumber, index, basic.ChangeTime))
}

/// A detached process started by [`spawn_detached`].
pub(crate) struct Child(OwnedHandle);

impl Child {
    pub(crate) fn try_wait(&mut self) -> io::Result<Option<ExitStatus>> {
        self.wait_for(0)
    }

    pub(crate) fn wait(&mut self) -> io::Result<ExitStatus> {
        self.wait_for(INFINITE)
            .map(|status| status.expect("an infinite wait returns only once the process exits"))
    }

    fn wait_for(&self, millis: u32) -> io::Result<Option<ExitStatus>> {
        let process = self.0.as_raw_handle();
        // SAFETY: `process` is a process handle we own.
        match unsafe { WaitForSingleObject(process, millis) } {
            WAIT_OBJECT_0 => {}
            WAIT_TIMEOUT => return Ok(None),
            _ => return Err(io::Error::last_os_error()),
        }
        let mut code = 0u32;
        // SAFETY: `process` is a process handle we own; `code` is a valid out pointer.
        if unsafe { GetExitCodeProcess(process, &mut code) } == 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(Some(ExitStatus::from_raw(code)))
    }
}

/// An inheritable duplicate of `handle`, for one child's handle list.
fn inheritable(handle: RawHandle) -> io::Result<OwnedHandle> {
    let mut dup: HANDLE = std::ptr::null_mut();
    // SAFETY: `handle` is open for the duration of the call; `dup` receives a new handle we own.
    if unsafe {
        DuplicateHandle(
            GetCurrentProcess(),
            handle,
            GetCurrentProcess(),
            &mut dup,
            0,
            1,
            DUPLICATE_SAME_ACCESS,
        )
    } == 0
    {
        return Err(io::Error::last_os_error());
    }
    Ok(owned(dup))
}

/// Append `arg` to a command line, quoted so `CommandLineToArgvW` and the MSVC runtime (and so
/// Rust's `args_os`) read it back unchanged.
fn push_arg(line: &mut Vec<u16>, arg: &OsStr, force_quotes: bool) -> io::Result<()> {
    let arg: Vec<u16> = arg.encode_wide().collect();
    if arg.contains(&0) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "argument contains a NUL character",
        ));
    }
    let quote = force_quotes
        || arg.is_empty()
        || arg
            .iter()
            .any(|&c| matches!(c, 0x20 | 0x09 | 0x0a | 0x0b | 0x22));
    if quote {
        line.push(u16::from(b'"'));
    }
    let mut backslashes = 0;
    for &c in &arg {
        if c == u16::from(b'\\') {
            backslashes += 1;
        } else {
            if c == u16::from(b'"') {
                // Double the backslashes before a quote, then escape the quote itself.
                line.extend(std::iter::repeat_n(u16::from(b'\\'), backslashes + 1));
            }
            backslashes = 0;
        }
        line.push(c);
    }
    if quote {
        line.extend(std::iter::repeat_n(u16::from(b'\\'), backslashes));
        line.push(u16::from(b'"'));
    }
    Ok(())
}

/// This process's environment with `env` applied, as a `CREATE_UNICODE_ENVIRONMENT` block.
fn env_block(env: &[(String, OsString)]) -> io::Result<Vec<u16>> {
    let mut vars: Vec<(OsString, OsString)> = std::env::vars_os()
        .filter(|(k, _)| {
            !env.iter()
                .any(|(o, _)| k.to_string_lossy().eq_ignore_ascii_case(o))
        })
        .chain(env.iter().map(|(k, v)| (k.into(), v.clone())))
        .collect();
    vars.sort_by_cached_key(|(k, _)| k.to_string_lossy().to_uppercase());
    let mut block = Vec::new();
    for (k, v) in vars {
        let start = block.len();
        block.extend(k.encode_wide());
        block.push(u16::from(b'='));
        block.extend(v.encode_wide());
        if block[start..].contains(&0) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "environment variable contains a NUL character",
            ));
        }
        block.push(0);
    }
    if block.is_empty() {
        block.push(0);
    }
    block.push(0);
    Ok(block)
}

/// Start `program` as a detached process (`DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP`), so it
/// survives the console and the process that started it.
///
/// `std::process::Command` cannot be used: it passes `bInheritHandles=TRUE` with no handle list,
/// so the child inherits every inheritable handle in this process. A CLI run with piped stdio
/// holds its caller's pipes as inheritable handles, and an agent holding them keeps the caller
/// from ever seeing EOF. Here the child inherits only the three standard handles, named in
/// `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`. They must be inheritable to be listed, so they are
/// inheritable duplicates that live only for this call; a `Command` spawned concurrently on
/// another thread may also inherit them, which leaks only the log or `NUL`.
pub(crate) fn spawn_detached(
    program: &Path,
    args: &[OsString],
    env: &[(String, OsString)],
    output: Output<'_>,
) -> io::Result<Child> {
    let null = OpenOptions::new().read(true).write(true).open("NUL")?;
    let stdin = inheritable(null.as_raw_handle())?;
    let (stdout, stderr) = match output {
        Output::Log(log) => {
            ensure_parent(log)?;
            let file = OpenOptions::new().create(true).append(true).open(log)?;
            (
                inheritable(file.as_raw_handle())?,
                inheritable(file.as_raw_handle())?,
            )
        }
        Output::Inherit => {
            // No standard handle (or one we cannot duplicate) becomes `NUL`.
            let mine = |handle: RawHandle| {
                if handle.is_null() || handle == INVALID_HANDLE_VALUE {
                    inheritable(null.as_raw_handle())
                } else {
                    inheritable(handle).or_else(|_| inheritable(null.as_raw_handle()))
                }
            };
            (
                mine(io::stdout().as_raw_handle())?,
                mine(io::stderr().as_raw_handle())?,
            )
        }
    };
    let handles = [
        stdin.as_raw_handle(),
        stdout.as_raw_handle(),
        stderr.as_raw_handle(),
    ];

    let mut line = Vec::new();
    push_arg(&mut line, program.as_os_str(), true)?;
    for arg in args {
        line.push(u16::from(b' '));
        push_arg(&mut line, arg, false)?;
    }
    line.push(0);
    // A bare name is searched for the way `CreateProcessW` does when given no application name;
    // a path is used as is.
    let application = if program.parent().is_some_and(|p| !p.as_os_str().is_empty()) {
        let mut name: Vec<u16> = program.as_os_str().encode_wide().collect();
        name.push(0);
        Some(name)
    } else {
        None
    };
    let environment = if env.is_empty() {
        None
    } else {
        Some(env_block(env)?)
    };

    let mut size = 0usize;
    // SAFETY: size query with a null list; Windows reports the needed size in `size`.
    unsafe { InitializeProcThreadAttributeList(std::ptr::null_mut(), 1, 0, &mut size) };
    // u64 storage keeps the opaque list pointer-aligned.
    let mut attributes = vec![0u64; size.div_ceil(8)];
    let list = attributes.as_mut_ptr().cast();
    // SAFETY: `list` points to at least `size` writable bytes.
    if unsafe { InitializeProcThreadAttributeList(list, 1, 0, &mut size) } == 0 {
        return Err(io::Error::last_os_error());
    }

    // SAFETY: plain-data struct; zero is a valid "not set" for every field.
    let mut startup: STARTUPINFOEXW = unsafe { std::mem::zeroed() };
    startup.StartupInfo.cb = std::mem::size_of::<STARTUPINFOEXW>() as u32;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = handles[0];
    startup.StartupInfo.hStdOutput = handles[1];
    startup.StartupInfo.hStdError = handles[2];
    startup.lpAttributeList = list;
    // SAFETY: plain-data out struct, fully written on success.
    let mut info: PROCESS_INFORMATION = unsafe { std::mem::zeroed() };
    // DETACHED_PROCESS already means no console, so CREATE_NO_WINDOW would be ignored.
    let flags = DETACHED_PROCESS
        | CREATE_NEW_PROCESS_GROUP
        | CREATE_UNICODE_ENVIRONMENT
        | EXTENDED_STARTUPINFO_PRESENT;
    // SAFETY: `list` is initialized; `handles` outlives the list, which reads it only during
    // CreateProcessW. Every string is NUL-terminated (the environment block double-NUL) and
    // `line` is writable, as CreateProcessW requires.
    let created = unsafe {
        UpdateProcThreadAttribute(
            list,
            0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST as usize,
            handles.as_ptr().cast(),
            std::mem::size_of_val(&handles),
            std::ptr::null_mut(),
            std::ptr::null(),
        ) != 0
            && CreateProcessW(
                application
                    .as_ref()
                    .map_or(std::ptr::null(), |name| name.as_ptr()),
                line.as_mut_ptr(),
                std::ptr::null(),
                std::ptr::null(),
                1,
                flags,
                environment
                    .as_ref()
                    .map_or(std::ptr::null(), |block| block.as_ptr().cast()),
                std::ptr::null(),
                &startup.StartupInfo,
                &mut info,
            ) != 0
    };
    let err = io::Error::last_os_error();
    // SAFETY: `list` was initialized above and is deleted exactly once.
    unsafe { DeleteProcThreadAttributeList(list) };
    if !created {
        return Err(err);
    }
    drop(owned(info.hThread));
    Ok(Child(owned(info.hProcess)))
}

/// Whether this process should keep starting up or has handed off to its shadow copy.
pub(crate) enum Shadow {
    /// We are the shadow copy; `installed` is the original exe to watch and relaunch.
    Running { installed: PathBuf },
    /// We were the installed exe and started the shadow copy; exit now.
    HandedOff,
}

/// Byte equality. Length and mtime are not enough: a rebuild can keep both, and `CopyFile`
/// carries the source's mtime over. Agent binaries are small and this runs only at startup.
fn same_file_contents(a: &Path, b: &Path) -> bool {
    match (std::fs::metadata(a), std::fs::metadata(b)) {
        (Ok(ma), Ok(mb)) if ma.len() == mb.len() => {
            matches!((std::fs::read(a), std::fs::read(b)), (Ok(x), Ok(y)) if x == y)
        }
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
