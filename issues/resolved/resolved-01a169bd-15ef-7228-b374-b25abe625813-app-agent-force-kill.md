# app-agent: caller-requested force kill (target 0.0.1-alpha.4)

## Context

Requested by Early via the firkin session `earlye/a0c542bdb2e7/agent-stop-while-busy`.

firkin is designing `firkin agent stop --force`. Early decided the force-kill belongs in
app-agent, not firkin. It is the companion to drain-then-stop (shipped in 0.0.1-alpha.3,
`issues/resolved/resolved-01a1693e-db0d-7025-b8c1-9b8cc9d85f0e-app-agent-drain-then-stop.md`),
which leaves killing out of scope.

## Stance change

app-agent currently says it never kills: `Error::Hung`'s doc reads "The crate never kills it",
its `Display` says "it was not killed", and `README.md:155-156` says "It never kills the agent."
The stance becomes: the crate never kills **on its own**; it kills only when the caller calls
`App::kill()`. Update the docs and messages to match.

## Design (settled in the 2026-10-23 grill)

- **API:** `App::kill(&self) -> Result<u32>`, returning the killed pid. No `Client::kill`: the
  connect-time `Hung` case has no `Client`, and `kill` doesn't need `hello`.
- **Pid source: the endpoint's kernel-reported peer pid**, not the lock file and not `hello`.
  `SO_PEERCRED` (Linux), `LOCAL_PEERPID` (macOS), `GetNamedPipeServerProcessId` (Windows, already
  wrapped by `transport::peer_pid`). The agent takes the lock before binding, so the listener is
  the lock holder, and a hung agent still completes connects via the listen backlog.
- **Pin before killing:**
  - Linux: `pidfd_open` on the peer pid, confirm the connection is still open, kill through the
    pidfd (`pidfd_send_signal`, SIGKILL).
  - Windows: `OpenProcess(PROCESS_TERMINATE)`, re-check the pipe server pid, `TerminateProcess`.
  - macOS: no pidfd; `kill(pid, SIGKILL)` with a small residual race between check and kill.
- **Nothing answers on the endpoint:** if the lock isn't held → `Error::NotRunning`. If the lock
  is held, Linux may fall back to `/proc/locks` (the agent's flock shows its own pid); otherwise
  `Error::Unverified`, and nothing is killed. **Never kill an unconfirmed pid.**
- **Wait for release:** after the kill, wait (bounded by `spawn_timeout`) until the lock is free,
  so `kill()` followed by `connect_or_spawn()` works directly.
- **No escalation:** a drain timeout only reports failure (`Hung` / `StillDraining`). Killing
  requires the user to actively choose `--force`; the caller maps that to `App::kill()`. No
  `drain_or_kill` helper.

Superseded: the original plan resolved the pid from `Hello::pid`, falling back to the lock-file
pid in `Error::Hung { pid }` after confirming it held the lock. Dropped because the endpoint peer
pid is authoritative in every case and avoids the lock-then-write-pid window in `try_acquire`.

## Tests

- CI matrix (`.github/workflows/rust.yml:23`) covers ubuntu, macos, windows.
- Peer-pid test: a child binds the endpoint and never accepts; assert the peer pid equals the
  child's pid; then fill the backlog and record behaviour per OS. If macOS can't report a peer
  pid with a full backlog, `kill` must return `Unverified` there, not kill.
- `kill` of a hung agent: returns its pid, the lock is free on return, `connect_or_spawn` succeeds.
- `kill` with no agent → `NotRunning`.

## Relevant files

- `rust/ipc/app-agent/src/error.rs:15-21`: `Error::Hung { pid }` and the "never kills" doc.
- `rust/ipc/app-agent/src/error.rs:58-61`: `Hung` `Display` text ("it was not killed").
- `rust/ipc/app-agent/src/app.rs`: `App`; add `kill` beside `connect` (:111).
- `rust/ipc/app-agent/src/lock.rs:20-44`: `try_acquire` (lock, then write pid), `read_pid`.
- `rust/ipc/app-agent/src/transport.rs:34-37`: Windows `peer_pid`.
- `rust/ipc/app-agent/src/windows.rs`: process APIs; `TerminateProcess` is in the already-enabled
  `Win32_System_Threading` feature.
- `rust/ipc/app-agent/README.md:155-156`: "never kills" stance.

## Grill Log

### 2026-10-23

- Q: How do we confirm the pid before killing (and guard against pid reuse)? — A: Use the
  kernel-reported peer pid of the agent's endpoint, not the lock file: `SO_PEERCRED` (Linux),
  `LOCAL_PEERPID` (macOS), `GetNamedPipeServerProcessId` (Windows, already used by
  `transport::peer_pid`). The agent takes the lock before binding, so the listener is the lock
  holder, and the kernel's answer can't be stale. A hung agent still completes connects via the
  listen backlog. Pin the process before killing: Linux pidfd opened on the peer pid, then confirm
  the connection is still open, then kill through the pidfd; Windows `OpenProcess(PROCESS_TERMINATE)`
  then re-check the pipe server pid; macOS has no pidfd, leaving a small residual race. If nothing
  answers on the endpoint but the lock is held, Linux may fall back to `/proc/locks`; otherwise
  refuse with an error, never kill. Verify with a CI test on all three OSes (the matrix in
  `.github/workflows/rust.yml` already covers them): a child binds and never accepts; assert peer
  pid equals the child pid, and record behaviour once the backlog is full.
- Q: API shape? — A: One entry point, `App::kill(&self) -> Result<u32>` (returns the killed pid);
  no `Client::kill`. It works without `hello` (the connect-time `Hung` case has no `Client`), so
  the pid always comes from the endpoint peer, and the `hello.pid` fallback is dropped. It waits,
  bounded by `spawn_timeout`, for the lock to be released before returning, so
  `kill()` + `connect_or_spawn()` works directly. New errors: `NotRunning` (lock not held) and
  `Unverified` (lock held but holder can't be confirmed; nothing killed). Open to iterating if it
  proves awkward for firkin.
- Q: Can a drain timeout escalate to a kill inside the crate? — A: No. A drain timeout only
  reports failure (`Hung` / `StillDraining`); killing requires the user to actively choose
  `--force`, which the caller maps to `App::kill()`. No `drain_or_kill` helper.
