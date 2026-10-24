# app-agent: caller-requested force kill (target 0.0.1-alpha.3)

## Context

Requested by Early via the firkin session `earlye/a0c542bdb2e7/agent-stop-while-busy`.

firkin is designing `firkin agent stop --force`. Early decided the force-kill belongs in
app-agent, not firkin. It is the companion to the drain-then-stop request
(`issues/issue-01a1693e-db0d-7025-b8c1-9b8cc9d85f0e-app-agent-drain-then-stop.md`), which leaves
killing out of scope. It is tracked separately because it changes a crate-wide stance and has
its own safety concerns.

## Stance change

app-agent currently says it never kills: `Error::Hung`'s doc reads "The crate never kills it",
and its `Display` says "it was not killed". The stance changes to: the crate never kills **on its
own**, but kills when the caller explicitly asks. The docs and messages need updating to match.

## What Early agreed

- **Resolve the pid** from `hello` (`Hello::pid`) when the agent answers. If it doesn't, fall back
  to the pid in `Error::Hung { pid }`, which is read from the lock file.
- **Never kill a stale pid.** Kill a lock-file pid only after confirming that process still holds
  the lock, because the recorded pid can be stale and belong to an unrelated process.
- **The kill is `kill -9`** (SIGKILL) on Unix and `TerminateProcess` on Windows.

## Open questions (not discussed with Early)

- **How to confirm the pid holds the lock.** `try_lock` failing shows that *someone* holds the
  lock, not that the recorded pid does. `try_acquire` takes the lock first and only then truncates
  and writes its pid, so for a short window a live holder sits next to the previous holder's pid.
  Possible approaches: Linux `/proc/locks`, a pidfd (Linux) or process handle (Windows) opened
  before the check and killed through, so the pid can't be reused between the check and the kill.
- **Pid reuse between check and kill** applies even to a pid from `hello`, if the agent exits
  between `hello` and the kill.
- **API shape**, e.g. `App::kill()` vs `Client::kill()`, and whether it waits for the lock to be
  released before returning.
- **Interaction with drain**: whether a drain timeout can escalate to a kill inside the crate, or
  whether escalation stays with the caller (firkin).

## Relevant files

- `rust/ipc/app-agent/src/error.rs:14-17`: `Error::Hung { pid }` and the "never kills" doc.
- `rust/ipc/app-agent/src/error.rs:47-50`: `Hung` `Display` text ("it was not killed").
- `rust/ipc/app-agent/src/client.rs:378-380`: `Hung` pid comes from `lock::read_pid`.
- `rust/ipc/app-agent/src/lock.rs:19-43`: `try_acquire` (lock, then write pid) and `read_pid`
  (documented as possibly stale).
- `rust/ipc/app-agent/src/protocol.rs:82`: `Hello::pid`.
- `rust/ipc/app-agent/src/windows.rs`: existing Windows process code, likely home for
  `TerminateProcess`.

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
