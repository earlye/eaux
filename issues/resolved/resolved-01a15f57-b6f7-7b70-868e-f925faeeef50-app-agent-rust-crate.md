# New Rust crate: `app-agent` (per-user resident agent pattern)

## Context

Requested on 2026-10-21 by the firkin session (`de3fbd7b44c1-agent-cli-first-slice-b4`)
after a grill of firkin-media/firkin's first slice. Full reasoning lives in that repo:
`issues/issue-01a15626-41e8-7a62-8096-de3fbd7b44c1-agent-cli-first-slice.md`, section
"Grill Log", 2026-10-20 onward, on branch `de3fbd7b44c1-agent-cli-first-slice`.

The firkin side of its first slice is blocked on `app-agent` 0.0.1 being on crates.io.
The firkin session wants a reply with this issue's path, then the published version and
API entry points.

The firkin session says the user approved publishing to crates.io immediately. That
approval was relayed by a peer session, not given in this session, so it has to be
confirmed before the first `cargo publish`.

## Spec (verbatim from the request)

WHAT: a reusable crate for the "app-agent" pattern. That is a per-user, single-instance resident process (like gpg-agent or ssh-agent) that a CLI starts on demand and talks to over a private local IPC channel. firkin is the first consumer, but the crate must know nothing about firkin; imagine a pure-Rust pass/gpg/ssh agent as the second consumer. We checked crates.io and no crate covers this whole pattern; only pieces exist (`interprocess` for transport, `fs4`/`fd-lock` for locks). The user wants it built straight in eaux, skipping eaux-candidates.

LOCATION AND VERSIONING:
- Put it in earlye/eaux following the `{language}/{topic}/{library}` layout, e.g. `rust/ipc/app-agent/`. eaux has no `rust/` tree yet; extend the root Makefile accordingly. Check the crates.io name `app-agent` is free first.
- Publish to crates.io immediately as 0.0.n while proving the design. Each 0.0.n bump counts as breaking under Cargo semver, which is intended. 0.1.0 will be the first public release, later.
- MIT license, README and Makefile, per eaux convention. Add GitHub Actions CI with an ubuntu/macos/windows matrix.

THE CRATE OWNS:
1. Locations keyed by an app name (e.g. "firkin"):
   - Endpoint, Linux: `$XDG_RUNTIME_DIR/<app>/agent.sock`. If `XDG_RUNTIME_DIR` is unset, use `/tmp/<app>-$UID/agent.sock`; create that dir 0700, and refuse it if another user owns it or its mode is wrong.
   - Endpoint, macOS: `$TMPDIR/<app>/agent.sock`.
   - Endpoint, Windows: named pipe `\\.\pipe\<app>-agent-<user SID>`.
   - Override: an env var `<APP>_AGENT_ENDPOINT` (a test/debug knob), so parallel tests get isolated agents.
   - Agent log for the detached agent's stdout/stderr: `$XDG_STATE_HOME/<app>/agent.log` or the platform equivalent.
2. Lock-first single instance; the lock decides, not the socket:
   - An OS file lock (flock/LockFileEx, e.g. via fs4), taken non-blocking at start and held for the agent's life. The OS releases it on death. Write the pid into the lock file.
   - Lock location: Unix `agent.lock` beside the socket (`<endpoint>.lock` under the override). Windows `%LOCALAPPDATA%\<app>\agent.lock` (`agent-<endpoint-hash>.lock` under the override).
   - Agent start order: take the lock (on failure, exit with a distinct "already running" code), then delete any leftover socket (it's stale by definition), then bind and serve. interprocess's `try_overwrite` deletes unconditionally, so it is only safe after taking the lock.
3. Client connect-or-spawn:
   - Connect and send `hello`. If the endpoint is missing or refused, spawn the agent binary detached (Unix: setsid via pre_exec; Windows: DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP) and retry with backoff for about 5s.
   - Two racing clients must end with exactly one agent: the agent that loses the lock exits, and both clients connect to the winner.
   - If `hello` times out, report a hung agent with the pid from the lock file. Do not kill it automatically.
4. Windows security: the pipe DACL allows only the current user plus SYSTEM. The client verifies the pipe server's owner: `server_process_id()`, then OpenProcess, then the token SID, compared with its own SID. A squatter can then deny service but can't impersonate the agent.
5. Windows shadow copy: launch the agent from a copy, `%LOCALAPPDATA%\<app>\bin\<agent>-<version>.exe` (copy only if missing; delete stale copies on the next start). The installed exe is then never locked, so `cargo install` can replace it while the agent runs.
6. Built-in messages:
   - `hello`: protocol version, pid, start time, app version.
   - Graceful `shutdown`.
   - On a version mismatch, the crate reports it; the app decides the policy. firkin will restart the agent, but refuse while jobs are running. So the agent needs a way to say "busy, won't stop".
7. Opt-in binary-watch: the agent records its `current_exe()` identity (for the shadow copy on Windows, the original installed path), and when that file changes, it restarts itself once the app says it is idle or has no jobs.
8. Idle-exit hook: a per-app timeout including "never". firkin uses never; a pass/gpg-style agent would use minutes.
9. Envelope and SDK:
   - Transport: JSON-lines over the `interprocess` crate (Unix sockets, Windows named pipes). It stays private so it can later become HTTP-over-socket without changing the API.
   - Envelope: request id plus kind (response, event, end, error) wrapping app-defined serde types.
   - Client: `call<Req, Resp>()` and `subscribe<Req, Event>()`, which returns a stream (for progress following).
   - Agent side: one handler trait that returns either a single response or a stream of events.
   - Keep it small; firkin is the only consumer so far.

TESTS (the crate's own, using a dummy agent binary inside the crate):
- cold start
- two concurrent clients leave exactly one agent
- stale endpoint after `kill -9`, recovered
- `shutdown`
- idle exit with a short timeout
- version mismatch reported
- shadow copy on Windows
All of these run on all 3 OSes in CI.

## Relevant files

- `Makefile` — root Makefile currently forwards every target to `go/`; must be extended for a `rust/` tree.
- `go/` — existing eaux layout and conventions (README, Makefile, MIT) to mirror.

## Blocking

- firkin-media/firkin `issue-01a15626-41e8-7a62-8096-de3fbd7b44c1-agent-cli-first-slice.md`
