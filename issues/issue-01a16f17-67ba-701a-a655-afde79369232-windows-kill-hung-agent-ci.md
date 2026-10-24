# CI red on main: `kill_hung_agent` fails on Windows

## Context

The `rust` workflow fails on `main` at `a0d5c29` (PR #4, "app-agent: caller-requested force kill
(0.0.1-alpha.4)"):
https://github.com/earlye/eaux/actions/runs/40118843935/job/120945782154

| job | result |
|---|---|
| app-agent (windows-latest) | failure |
| app-agent (ubuntu-latest) | success |
| app-agent (macos-latest) | success |
| app-agent (MSRV 1.89) | success |

The previous run on `main` (`361f4ff`, PR #3) passed on every job. Windows log:

```
thread '<unnamed>' (1864) panicked at tests\agent.rs:1046:51:
called `Result::unwrap()` on an `Err` value: Disconnected
--- agent log for aat-kill-hung-8348 ---
app-agent: aat-kill-hung-8348 1.0.0 agent pid 3140 serving \\.\pipe\aat-kill-hung-8348
test kill_hung_agent ... FAILED
test result: FAILED. 20 passed; 1 failed
```

Line 1046 is the `connect_or_spawn()` that runs **after** `app.kill()` returned the right pid
and `lock_free(&app)` passed. The kill itself worked; the reconnect failed. On Windows the test
doesn't `SIGSTOP` the agent first, so the agent being killed is a healthy one.

`Disconnected` here most likely comes from `src/client.rs:426`: the pipe opened, `hello` was sent,
and the stream hit EOF before any reply. If the pipe had been absent, the client would have
returned `NotRunning` and spawned a new agent.

## Root cause hypothesis

Unverified, medium confidence.

`TerminateProcess` returns before the process has fully gone. `kill()` (`src/kill.rs:29-37`)
waits only for the **lock** to be released, not for the process to exit. The lock-file handle
can close before the named-pipe server handle does. In that window,
`\\.\pipe\aat-kill-hung-<pid>` still exists, the client connects to the dying instance, and then
gets EOF. Unix doesn't show this because SIGKILL followed by the lock release means the socket
is already dead, and connecting to a stale socket path fails with "refused", which maps to
`NotRunning`.

This would be falsified if a re-run of the Windows job passes repeatedly (pointing to a different
flake), or if the agent log showed a successor agent started and then exited.

## Candidate fixes (not decided)

- In `kill_pipe_server` (`src/windows.rs:147-216`), open the process with `SYNCHRONIZE` as well,
  and `WaitForSingleObject` it after `TerminateProcess`, before returning. Then `kill()` returns
  only once every handle of the old agent, including the pipe, is closed. This fixes the crate,
  not just the test, so it also helps firkin's `--force` followed by a restart.
- Alternatively, or as well, have connect-or-spawn treat EOF before `hello` as "not running" and
  retry or spawn. That's a broader behaviour change with its own risks: it can't tell a dying
  agent from a crashing one.

## Blocking

- `issues/issue-01a16f16-2b6b-77f5-9c54-807521e7332b-app-agent-version-bump-publish.md`:
  `rust/README.md` forbids publishing until CI passes on every OS.

## Relevant files

- `rust/ipc/app-agent/tests/agent.rs:1030-1050`: `kill_hung_agent`; line 1046 panics.
- `rust/ipc/app-agent/tests/agent.rs:344-352`: `lock_free`, which passed before the failing line.
- `rust/ipc/app-agent/src/kill.rs:21-38`: `kill()`; it waits on the lock, not the process.
- `rust/ipc/app-agent/src/windows.rs:147-216`: `kill_pipe_server`; `TerminateProcess` at 213,
  with no wait afterwards.
- `rust/ipc/app-agent/src/windows.rs:266-270`: an existing `WaitForSingleObject` helper on spawned
  children, which could be reused.
- `rust/ipc/app-agent/src/client.rs:420-427`: the `hello` read; `Ok(None)`, which means EOF, maps
  to `Disconnected`.
- `.github/workflows/rust.yml`: the CI matrix.
