# app-agent: version bump and publish to crates.io

## Context

Early asked to track bumping app-agent's version and publishing it.

State as of 2026-10-24:

- `rust/ipc/app-agent/Cargo.toml` is already at `0.0.1-alpha.4`, and the README pins
  `app-agent = "=0.0.1-alpha.4"`. PR #3 (`361f4ff`, drain-then-stop) bumped it to alpha.3, and
  PR #4 (`a0d5c29`, force kill) bumped it to alpha.4.
- crates.io only has `0.0.1-alpha.1` (2026-10-21) and `0.0.1-alpha.2` (2026-10-21).
  **alpha.3 was never published.**
- There are no git tags for Rust crates. The Go modules are tagged `go/<path>/vX.Y.Z`.

## Blocked by

- `issues/issue-01a16f17-67ba-701a-a655-afde79369232-windows-kill-hung-agent-ci.md`: CI is red on
  `main` (Windows `kill_hung_agent`).

## Blocker: CI is red on `main`

`rust/README.md` says to publish only after the `rust` workflow passes on every OS and the MSRV
job, from the exact commit CI passed on, with a clean tree.

The `rust` run on `a0d5c29` (run 40118843935) failed on `windows-latest`. ubuntu, macos and MSRV
1.89 passed. The failing test is `kill_hung_agent`:

```
thread '<unnamed>' panicked at tests\agent.rs:1046:51:
called `Result::unwrap()` on an `Err` value: Disconnected
--- agent log for aat-kill-hung-8348 ---
app-agent: aat-kill-hung-8348 1.0.0 agent pid 3140 serving \\.\pipe\aat-kill-hung-8348
```

Line 1046 is the `connect_or_spawn()` after `app.kill()` succeeded and the lock was confirmed free.
So the kill worked, and the reconnect failed.

Hypothesis (unverified, medium confidence): on Windows, a pipe instance from the killed agent
lingers briefly after `TerminateProcess`, while the lock is already free. The client connects to
that dying instance and gets `Disconnected` instead of `NotRunning`, so it doesn't spawn. If
that's right, the fix belongs in the crate (kill waits for the process handle to signal, or
connect-or-spawn treats an immediate disconnect as not running), not only in the test. This
would be wrong if the Windows failure doesn't reproduce on a re-run, which would make it a flake.
In that case it still needs a fix before publishing, but a different one.

## Next steps

1. Fix the Windows `kill_hung_agent` failure and get the `rust` workflow green on `main`.
2. From that green commit, with a clean tree: `make package` (`cargo publish --dry-run`), then
   `cargo publish`.
3. Decide whether to publish under `0.0.1-alpha.4` (the version in source now) or bump again.
   alpha.3 is skipped on crates.io either way, unless it is published from `361f4ff`, which passed
   CI on every OS. That commit lacks force kill.
4. Optionally add a git tag (e.g. `rust/ipc/app-agent/v0.0.1-alpha.4`, matching the Go tag style).
   Nothing has been decided about tags.

## Relevant files

- `rust/README.md:7-15`: release rules (CI green on every OS, publish from that commit,
  `0.0.1-alpha.n` scheme, exact pins).
- `rust/ipc/app-agent/Cargo.toml:3`: `version = "0.0.1-alpha.4"`.
- `rust/ipc/app-agent/README.md:12-18`: pre-release note and the `=0.0.1-alpha.4` pin.
- `rust/ipc/app-agent/Makefile`: `make package` runs `cargo publish --dry-run`.
- `rust/ipc/app-agent/tests/agent.rs:1030-1050`: `kill_hung_agent`; line 1046 fails on Windows.
- `rust/ipc/app-agent/src/kill.rs`: the force-kill implementation from PR #4.
- `.github/workflows/rust.yml`: the CI matrix.
- `issues/resolved/resolved-01a169bd-15ef-7228-b374-b25abe625813-app-agent-force-kill.md`: the
  force-kill issue that introduced the failing test.
