# app-agent: drain-then-stop request (target 0.0.1-alpha.3)

## Context

Requested by Early via the firkin session `earlye/a0c542bdb2e7/agent-stop-while-busy`.

firkin is designing `firkin agent stop --wait PT60S` (the default) and `--force` (kill -9).

In 0.0.1-alpha.2, `ClientMsg::Shutdown` fails straight away with `ErrorCode::Busy` whenever
`handler.busy()` returns `Some`. A client can only poll, and while it polls, new jobs keep the
agent busy, so the stop may never succeed. Early's position: it isn't fair to ask a server to
shut down without telling it to drain.

## Requested feature

- **New client message**, e.g. `ClientMsg::Drain { id }` or `Shutdown { id, when_idle: true }`
  (the name and shape are open). It sets a draining flag, and the agent stops once `busy()`
  returns `None`. This fits the existing tick loop, which already waits on `busy().is_none()`
  for idle exit and for restarting when the binary changes.
- **Handler sees the draining state**, either passed to the `Handler` or queried by it, so the
  app can refuse new work while draining.
- **The client API waits for the agent to stop**, with a timeout the caller supplies. If the
  timeout expires, the drain stays in effect: the agent still stops once it is idle.

## Out of scope

- Killing the agent. Tracked separately in
  `issues/issue-01a169bd-15ef-7228-b374-b25abe625813-app-agent-force-kill.md`.

## Open questions

- Should a busy plain `Shutdown` keep failing with `Busy`? The requester assumes yes, for
  compatibility.
- How should `restart()` relate to drain? For example, should restart drain first, and should
  a drain cancel or win over a pending binary-change restart?

## Relevant files

- `rust/ipc/app-agent/src/agent.rs:299-310`: `ClientMsg::Shutdown` handling; returns
  `ErrorCode::Busy` when `handler.busy()` is `Some`.
- `rust/ipc/app-agent/src/agent.rs:237-254`: tick loop; idle-timeout and binary-change restart
  already wait for `busy().is_none()`. Drain-stop would be a third condition here.
- `rust/ipc/app-agent/src/agent.rs:50-67`: `Handler` trait and its `busy()` default (the
  request called it `AgentHandler`; the trait is `Handler`).
- `rust/ipc/app-agent/src/client.rs:167`: `Client::shutdown`.
- `rust/ipc/app-agent/src/client.rs:186`: `Client::restart`.
- `rust/ipc/app-agent/src/protocol.rs:18`: `ClientMsg::Shutdown` definition.
- `rust/ipc/app-agent/src/error.rs:16`: `Error::Hung` doc ("The crate never kills it").
- `rust/ipc/app-agent/Cargo.toml:3`: currently `0.0.1-alpha.2`; bump to `0.0.1-alpha.3`.
