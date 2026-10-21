use std::ffi::{OsStr, OsString};
use std::io;
use std::path::{Path, PathBuf};

use crate::error::{Error, Result};

/// Where an app's agent lives. Resolve with [`App::locations`](crate::App::locations).
#[derive(Clone, Debug, PartialEq, Eq)]
#[non_exhaustive]
pub struct Locations {
    /// Unix socket path, or on Windows the named pipe path (`\\.\pipe\...`).
    pub endpoint: PathBuf,
    /// The single-instance lock file. It holds the agent's pid while it runs.
    pub lock: PathBuf,
    /// Where a spawned agent's stdout and stderr go.
    pub log: PathBuf,
    /// The per-user directory that holds the endpoint; created 0700 and owner-checked.
    /// `None` when the endpoint was overridden.
    pub(crate) private_dir: Option<PathBuf>,
    /// The per-user app data directory (Windows shadow copies live under it).
    #[cfg_attr(not(windows), allow(dead_code))]
    pub(crate) data_dir: Option<PathBuf>,
}

/// The name of the endpoint override variable for `app`: `<APP>_AGENT_ENDPOINT`, with the
/// app name upper-cased and every character outside `[A-Z0-9]` replaced by `_`.
pub fn endpoint_env_var(app: &str) -> String {
    let mut name: String = app
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() {
                c.to_ascii_uppercase()
            } else {
                '_'
            }
        })
        .collect();
    name.push_str("_AGENT_ENDPOINT");
    name
}

pub(crate) fn validate_app_name(app: &str) -> Result<()> {
    let ok = !app.is_empty()
        && app != "."
        && app != ".."
        && app
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_' || c == '.');
    if ok {
        Ok(())
    } else {
        Err(Error::Io(io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("invalid app name {app:?}: use ASCII letters, digits, '-', '_' or '.'"),
        )))
    }
}

/// Stable 64-bit FNV-1a, used to key override lock/log names on Windows. It must not change
/// between builds, so `std`'s randomized hasher is not an option.
#[cfg_attr(not(windows), allow(dead_code))]
pub(crate) fn fnv1a(bytes: &[u8]) -> u64 {
    let mut hash: u64 = 0xcbf2_9ce4_8422_2325;
    for b in bytes {
        hash ^= u64::from(*b);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    hash
}

fn non_empty(v: Option<OsString>) -> Option<OsString> {
    v.filter(|v| !v.is_empty())
}

/// Resolve locations. `explicit` is the builder override; `env` looks variables up so tests can
/// inject an environment.
pub(crate) fn resolve(
    app: &str,
    explicit: Option<&OsStr>,
    env: &dyn Fn(&str) -> Option<OsString>,
) -> Result<Locations> {
    validate_app_name(app)?;
    let overridden = explicit
        .map(OsStr::to_os_string)
        .or_else(|| non_empty(env(&endpoint_env_var(app))));
    platform::resolve(app, overridden, &|k| non_empty(env(k)))
}

fn missing(var: &str) -> Error {
    Error::Io(io::Error::new(
        io::ErrorKind::NotFound,
        format!("{var} is not set; cannot locate the agent's files"),
    ))
}

#[cfg(unix)]
mod platform {
    use super::*;

    fn with_suffix(path: &Path, suffix: &str) -> PathBuf {
        let mut s = path.as_os_str().to_os_string();
        s.push(suffix);
        PathBuf::from(s)
    }

    pub(super) fn resolve(
        app: &str,
        overridden: Option<OsString>,
        env: &dyn Fn(&str) -> Option<OsString>,
    ) -> Result<Locations> {
        if let Some(endpoint) = overridden {
            let endpoint = PathBuf::from(endpoint);
            return Ok(Locations {
                lock: with_suffix(&endpoint, ".lock"),
                log: with_suffix(&endpoint, ".log"),
                endpoint,
                private_dir: None,
                data_dir: None,
            });
        }
        let dir = runtime_dir(app, env);
        Ok(Locations {
            endpoint: dir.join("agent.sock"),
            lock: dir.join("agent.lock"),
            log: log_dir(env)?.join(app).join("agent.log"),
            private_dir: Some(dir),
            data_dir: None,
        })
    }

    fn fallback_dir(app: &str) -> PathBuf {
        // SAFETY: geteuid has no preconditions and cannot fail.
        let uid = unsafe { libc::geteuid() };
        PathBuf::from(format!("/tmp/{app}-{uid}"))
    }

    #[cfg(target_os = "macos")]
    fn runtime_dir(app: &str, env: &dyn Fn(&str) -> Option<OsString>) -> PathBuf {
        match env("TMPDIR") {
            Some(tmp) => PathBuf::from(tmp).join(app),
            None => fallback_dir(app),
        }
    }

    #[cfg(not(target_os = "macos"))]
    fn runtime_dir(app: &str, env: &dyn Fn(&str) -> Option<OsString>) -> PathBuf {
        match env("XDG_RUNTIME_DIR") {
            Some(run) => PathBuf::from(run).join(app),
            None => fallback_dir(app),
        }
    }

    #[cfg(target_os = "macos")]
    fn log_dir(env: &dyn Fn(&str) -> Option<OsString>) -> Result<PathBuf> {
        let home = env("HOME").ok_or_else(|| missing("HOME"))?;
        Ok(PathBuf::from(home).join("Library").join("Logs"))
    }

    #[cfg(not(target_os = "macos"))]
    fn log_dir(env: &dyn Fn(&str) -> Option<OsString>) -> Result<PathBuf> {
        if let Some(state) = env("XDG_STATE_HOME") {
            return Ok(PathBuf::from(state));
        }
        let home = env("HOME").ok_or_else(|| missing("HOME"))?;
        Ok(PathBuf::from(home).join(".local").join("state"))
    }
}

#[cfg(windows)]
mod platform {
    use super::*;

    pub(super) const PIPE_PREFIX: &str = r"\\.\pipe\";

    pub(super) fn resolve(
        app: &str,
        overridden: Option<OsString>,
        env: &dyn Fn(&str) -> Option<OsString>,
    ) -> Result<Locations> {
        let data =
            PathBuf::from(env("LOCALAPPDATA").ok_or_else(|| missing("LOCALAPPDATA"))?).join(app);
        if let Some(endpoint) = overridden {
            let endpoint = pipe_path(&endpoint);
            let key = format!("{:016x}", fnv1a(endpoint.as_os_str().as_encoded_bytes()));
            return Ok(Locations {
                lock: data.join(format!("agent-{key}.lock")),
                log: data.join(format!("agent-{key}.log")),
                endpoint,
                private_dir: None,
                data_dir: Some(data),
            });
        }
        let sid = crate::windows::current_user_sid_string()?;
        Ok(Locations {
            endpoint: PathBuf::from(format!("{PIPE_PREFIX}{app}-agent-{sid}")),
            lock: data.join("agent.lock"),
            log: data.join("agent.log"),
            private_dir: None,
            data_dir: Some(data),
        })
    }

    /// Accept either a full `\\.\pipe\name` path or a bare pipe name.
    fn pipe_path(endpoint: &OsStr) -> PathBuf {
        let s = endpoint.to_string_lossy();
        if s.starts_with(r"\\") {
            PathBuf::from(endpoint)
        } else {
            PathBuf::from(format!("{PIPE_PREFIX}{s}"))
        }
    }
}

/// Create the private endpoint directory (0700) if needed and refuse it unless this user owns it
/// and no one else can reach into it. A no-op when the endpoint was overridden or on Windows.
pub(crate) fn ensure_private_dir(locations: &Locations, create: bool) -> Result<()> {
    let Some(dir) = &locations.private_dir else {
        return Ok(());
    };
    #[cfg(unix)]
    {
        use std::os::unix::fs::{DirBuilderExt, MetadataExt};
        if create {
            if let Some(parent) = dir.parent() {
                std::fs::create_dir_all(parent)?;
            }
            match std::fs::DirBuilder::new().mode(0o700).create(dir) {
                Ok(()) => {}
                Err(e) if e.kind() == io::ErrorKind::AlreadyExists => {}
                Err(e) => return Err(e.into()),
            }
        }
        let meta = match std::fs::symlink_metadata(dir) {
            Ok(meta) => meta,
            Err(e) if !create && e.kind() == io::ErrorKind::NotFound => return Ok(()),
            Err(e) => return Err(e.into()),
        };
        let insecure = |reason: String| Error::InsecureDir {
            path: dir.clone(),
            reason,
        };
        if !meta.file_type().is_dir() {
            return Err(insecure("not a directory".into()));
        }
        // SAFETY: geteuid has no preconditions and cannot fail.
        let uid = unsafe { libc::geteuid() };
        if meta.uid() != uid {
            return Err(insecure(format!(
                "owned by uid {}, expected {uid}",
                meta.uid()
            )));
        }
        if meta.mode() & 0o077 != 0 {
            return Err(insecure(format!(
                "mode {:o} grants group/other access, expected 700",
                meta.mode() & 0o777
            )));
        }
    }
    #[cfg(not(unix))]
    let _ = (dir, create);
    Ok(())
}

/// Create the parent directory of `path` if it is missing.
pub(crate) fn ensure_parent(path: &Path) -> io::Result<()> {
    match path.parent() {
        Some(parent) if !parent.as_os_str().is_empty() => std::fs::create_dir_all(parent),
        _ => Ok(()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    fn env_of(pairs: &[(&str, &str)]) -> impl Fn(&str) -> Option<OsString> {
        let map: HashMap<String, OsString> = pairs
            .iter()
            .map(|(k, v)| ((*k).to_string(), OsString::from(v)))
            .collect();
        move |k| map.get(k).cloned()
    }

    #[test]
    fn env_var_name() {
        assert_eq!(endpoint_env_var("firkin"), "FIRKIN_AGENT_ENDPOINT");
        assert_eq!(endpoint_env_var("my-app.x"), "MY_APP_X_AGENT_ENDPOINT");
    }

    #[test]
    fn rejects_bad_names() {
        assert!(validate_app_name("").is_err());
        assert!(validate_app_name("a/b").is_err());
        assert!(validate_app_name("..").is_err());
        assert!(validate_app_name("ok-name_1.2").is_ok());
    }

    #[test]
    fn fnv_is_stable() {
        assert_eq!(fnv1a(b""), 0xcbf2_9ce4_8422_2325);
        assert_eq!(fnv1a(b"a"), 0xaf63_dc4c_8601_ec8c);
    }

    #[cfg(unix)]
    #[test]
    fn override_from_env() {
        let env = env_of(&[("DEMO_AGENT_ENDPOINT", "/x/y.sock"), ("HOME", "/h")]);
        let l = resolve("demo", None, &env).unwrap();
        assert_eq!(l.endpoint, PathBuf::from("/x/y.sock"));
        assert_eq!(l.lock, PathBuf::from("/x/y.sock.lock"));
        assert_eq!(l.log, PathBuf::from("/x/y.sock.log"));
        assert_eq!(l.private_dir, None);
    }

    #[cfg(unix)]
    #[test]
    fn explicit_override_beats_env() {
        let env = env_of(&[("DEMO_AGENT_ENDPOINT", "/x/y.sock")]);
        let l = resolve("demo", Some(OsStr::new("/z.sock")), &env).unwrap();
        assert_eq!(l.endpoint, PathBuf::from("/z.sock"));
    }

    #[cfg(all(unix, not(target_os = "macos")))]
    #[test]
    fn linux_defaults() {
        let env = env_of(&[
            ("XDG_RUNTIME_DIR", "/run/user/7"),
            ("XDG_STATE_HOME", "/s"),
            ("HOME", "/h"),
        ]);
        let l = resolve("demo", None, &env).unwrap();
        assert_eq!(l.endpoint, PathBuf::from("/run/user/7/demo/agent.sock"));
        assert_eq!(l.lock, PathBuf::from("/run/user/7/demo/agent.lock"));
        assert_eq!(l.log, PathBuf::from("/s/demo/agent.log"));

        let l = resolve("demo", None, &env_of(&[("HOME", "/h")])).unwrap();
        let uid = unsafe { libc::geteuid() };
        assert_eq!(
            l.endpoint,
            PathBuf::from(format!("/tmp/demo-{uid}/agent.sock"))
        );
        assert_eq!(l.log, PathBuf::from("/h/.local/state/demo/agent.log"));
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn macos_defaults() {
        let env = env_of(&[("TMPDIR", "/var/folders/t/"), ("HOME", "/h")]);
        let l = resolve("demo", None, &env).unwrap();
        assert_eq!(l.endpoint, PathBuf::from("/var/folders/t/demo/agent.sock"));
        assert_eq!(l.log, PathBuf::from("/h/Library/Logs/demo/agent.log"));
    }

    #[cfg(windows)]
    #[test]
    fn windows_defaults_and_override() {
        let env = env_of(&[(r"LOCALAPPDATA", r"C:\Users\u\AppData\Local")]);
        let l = resolve("demo", None, &env).unwrap();
        let ep = l.endpoint.to_string_lossy().into_owned();
        assert!(ep.starts_with(r"\\.\pipe\demo-agent-S-1-"), "{ep}");
        assert_eq!(
            l.lock,
            PathBuf::from(r"C:\Users\u\AppData\Local\demo\agent.lock")
        );

        let env = env_of(&[
            (r"LOCALAPPDATA", r"C:\L"),
            ("DEMO_AGENT_ENDPOINT", "custom"),
        ]);
        let l = resolve("demo", None, &env).unwrap();
        assert_eq!(l.endpoint, PathBuf::from(r"\\.\pipe\custom"));
        let lock = l.lock.to_string_lossy().into_owned();
        assert!(lock.starts_with(r"C:\L\demo\agent-") && lock.ends_with(".lock"));
    }

    #[cfg(unix)]
    #[test]
    fn private_dir_checks() {
        use std::os::unix::fs::PermissionsExt;
        let base = std::env::temp_dir().join(format!("aa-unit-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&base);
        let env_base = base.clone();
        let env = move |k: &str| {
            (k == "XDG_RUNTIME_DIR" || k == "TMPDIR").then(|| env_base.clone().into_os_string())
        };
        let l = resolve("demo", None, &|k| {
            if k == "HOME" {
                Some("/h".into())
            } else {
                env(k)
            }
        })
        .unwrap();
        // Missing directory is fine for a client; the agent creates it.
        ensure_private_dir(&l, false).unwrap();
        ensure_private_dir(&l, true).unwrap();
        let dir = l.private_dir.clone().unwrap();
        assert_eq!(
            std::fs::metadata(&dir).unwrap().permissions().mode() & 0o777,
            0o700
        );
        std::fs::set_permissions(&dir, std::fs::Permissions::from_mode(0o755)).unwrap();
        assert!(matches!(
            ensure_private_dir(&l, false),
            Err(Error::InsecureDir { .. })
        ));
        std::fs::remove_dir_all(&base).unwrap();
    }
}
