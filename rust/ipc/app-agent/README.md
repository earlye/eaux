# app-agent

A per-user, single-instance resident agent, like `ssh-agent` or `gpg-agent`.
A CLI starts it on demand and talks to it over a private local socket (Unix)
or named pipe (Windows).

The crate handles the parts every such agent needs: where the endpoint lives,
which process gets to serve, starting the agent detached, keeping other
users out, replacing the binary while the agent runs, and a small
request/response/stream protocol. The app supplies one handler.

The crate is pre-release. It is published as `0.0.1-alpha.n` while the design
settles; `0.1.0` will be the first stable public release. Cargo treats every
bump in the meantime as breaking.

```toml
[dependencies]
app-agent = "=0.0.1-alpha.3"
```

## Usage

Build the same `App` in the CLI and in the agent process, then connect from
the CLI and serve in the agent:

```rust
use app_agent::{App, Handler, Reply};
use serde::{Deserialize, Serialize};

fn app() -> App {
    App::new("demo", env!("CARGO_PKG_VERSION"))
        .agent_args(["agent"]) // the agent is `demo agent`; the program defaults to current_exe()
        .idle_timeout(None) // never idle out (the default)
}

#[derive(Serialize, Deserialize)]
enum Request { Status, Follow { job: u64 } }

struct Agent;

impl Handler for Agent {
    type Request = Request;
    type Response = String;
    type Event = u32;

    async fn handle(&self, request: Request) -> Result<Reply<String, u32>, String> {
        match request {
            Request::Status => Ok(Reply::Response("ok".into())),
            Request::Follow { .. } => Ok(Reply::events(some_progress_stream())),
        }
    }

    fn busy(&self) -> Option<String> {
        None // Some("2 jobs running") refuses shutdown; postpones idle exit, restarts, drain
    }

    fn draining(&self) {
        // A client asked the agent to drain. The crate still dispatches requests to `handle`;
        // refuse new jobs from here on.
    }
}

#[tokio::main(flavor = "current_thread")]
async fn main() -> std::process::ExitCode {
    if std::env::args().nth(1).as_deref() == Some("agent") {
        return app().run(Agent).await;
    }
    let client = app().connect_or_spawn().await.expect("agent");
    if let Some(m) = client.mismatch() {
        eprintln!("agent is {}, cli is {}", m.agent_version, m.client_version);
    }
    let status: String = client.call(&Request::Status).await.expect("status");
    println!("{status}");
    std::process::ExitCode::SUCCESS
}
```

`some_progress_stream()` stands for any `Stream<Item = Result<u32, String>>`.
On the client, `client.subscribe::<_, u32>(&Request::Follow { job: 1 })`
returns a `Stream` of those events. Dropping it early tells the agent to stop
producing them.

## API

| Item | Purpose |
| --- | --- |
| `App` | Builder: name, version, `agent_program`, `agent_args`, `endpoint`, `idle_timeout`, `watch_binary`, `spawn_timeout`, `hello_timeout`. |
| `App::connect` / `App::connect_or_spawn` | Get a `Client`. The first fails with `Error::NotRunning`; the second starts the agent if needed. |
| `App::run` / `App::serve` | Become the agent. `run` maps the outcome to an `ExitCode` for `main`; `serve` returns `Stopped` or `Error`. |
| `App::locations` | The resolved `Locations` (endpoint, lock, log). |
| `Client::call` / `Client::subscribe` | One response, or a `Subscription` stream of events. |
| `Client::hello` / `Client::mismatch` | What the agent reported, and whether its version or protocol differs. |
| `Client::shutdown` / `Client::restart` | Graceful stop (refused with `Error::Busy` while busy), and stop-then-respawn. |
| `Client::drain` | Stop once idle (not busy, no request in flight), waiting up to a timeout (`Error::StillDraining` if it runs out; the drain stays in effect). |
| `Handler` | The app's one trait: `handle` returns `Reply::Response` or `Reply::Events`; `busy` says "won't stop"; `draining` says a drain began. |
| `Error`, `Stopped`, `Hello`, `Mismatch`, `EXIT_ALREADY_RUNNING`, `PROTOCOL_VERSION`, `endpoint_env_var` | Supporting types and constants. |

The transport is private. The wire format is JSON lines over
[`interprocess`](https://crates.io/crates/interprocess) local sockets, so it
can change (to HTTP over the same socket, say) without an API change.

## Locations

| | Linux | macOS | Windows |
| --- | --- | --- | --- |
| Endpoint | `$XDG_RUNTIME_DIR/<app>/agent.sock`, else `/tmp/<app>-$UID/agent.sock` | `$TMPDIR/<app>/agent.sock` (same `/tmp` fallback) | `\\.\pipe\<app>-agent-<user SID>` |
| Lock | `agent.lock` beside the socket | `agent.lock` beside the socket | `%LOCALAPPDATA%\<app>\agent.lock` |
| Log | `$XDG_STATE_HOME/<app>/agent.log`, else `~/.local/state/<app>/agent.log` | `~/Library/Logs/<app>/agent.log` | `%LOCALAPPDATA%\<app>\agent.log` |

On Unix the endpoint directory is created with mode 0700. Agent and client
both refuse it if another user owns it or it grants group or other access.

`<APP>_AGENT_ENDPOINT` (for example `DEMO_AGENT_ENDPOINT`) overrides the
endpoint. It is meant for tests and debugging, so parallel tests can run
isolated agents. `App::endpoint` does the same in code. A spawned agent
inherits the override. With an override:

- On Unix the lock is `<endpoint>.lock` and the log is `<endpoint>.log`, and
  the directory checks are skipped.
- On Windows the lock is `%LOCALAPPDATA%\<app>\agent-<hash>.lock` and the log is
  `agent-<hash>.log`. A bare name means `\\.\pipe\<name>`.

## How it works

**Lock first.** The lock decides which process serves; the socket does not.
On start the agent:

1. takes an OS file lock without blocking (`flock`, or `LockFileEx` on Windows),
   held for its whole life;
2. if the lock is taken, exits with `EXIT_ALREADY_RUNNING` (75);
3. writes its pid into the lock file;
4. deletes any leftover socket, which is stale because the lock is held;
5. binds and serves.

The OS releases the lock when the process dies. On Windows the lock covers one
byte far past the pid, so other processes can still read the pid.

**Connect or spawn.** The client connects and sends `hello`. If nothing is
listening, it spawns the agent detached (Unix: `setsid`; Windows:
`DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP`, with stdout and stderr going to
the log) and retries with backoff for up to `spawn_timeout` (5s).

The agent inherits nothing from the CLI but null stdin and the log. A CLI run
with piped output, e.g. under `Command::output()`, would otherwise hand its
pipes to the long-lived agent, and its caller would never see EOF. On Unix,
every descriptor above stderr is marked close-on-exec in the child. On Windows
the agent is started with `CreateProcessW` and a
`PROC_THREAD_ATTRIBUTE_HANDLE_LIST` naming only its three standard handles;
`std::process::Command` would pass on every inheritable handle.

When clients race, each may spawn an agent, but only one wins the lock. The
losers exit with code 75, and every client connects to the winner. If an
agent exits with any other code before it listens, the client reports
`Error::AgentFailed` with the log path. If `hello` gets no answer within
`hello_timeout`, the client reports `Error::Hung` with the pid from the lock
file. It never kills the agent.

**Built-in messages.**

- `hello` returns the protocol version, pid, start time and app version.
  `Client::mismatch` compares them with the client's; the app decides what to
  do about a mismatch.
- `shutdown` stops the agent gracefully. While the handler's `busy()` returns
  `Some`, the agent refuses with `Error::Busy`, so an app can restart on
  mismatch but not while it has jobs running.
- `drain` asks the agent to stop once it is idle: `busy()` returns `None` and
  no request is in flight (open subscriptions count). The agent acknowledges
  at once, calls the handler's `draining()` (once), and stops as soon as it is
  idle, which may be immediately. The crate keeps dispatching requests while
  draining; refusing new work is the handler's job, typically from
  `draining()` on. A drain wins over a pending binary-watch restart: the agent
  stops and starts nothing. `Client::drain(timeout)` waits, for the
  acknowledgement and then for the agent to close the connection, up to
  `timeout` in all; if it runs out it returns `Error::StillDraining`, and the
  agent still stops once it is idle.

**Idle exit.** With `idle_timeout(Some(d))`, the agent exits once no request
has been in flight for `d` and the handler is not busy. `hello` does not count
as activity. The default, `None`, never idles out.

**Binary watch** (opt-in, `watch_binary(interval)`). The agent records the
identity of its installed executable: size, mtime, and on Unix device and
inode; on Windows volume serial, file index and change time. On Windows that
is the installed path, not the shadow copy. When the
file changes and the handler is not busy, the agent stops listening, releases
the lock, starts the new binary with the same arguments, and exits.

**Windows security.** The pipe's DACL grants access only to the current user
and SYSTEM, and the pipe rejects remote clients. Before trusting the pipe, the
client checks who serves it: it gets the server pid, opens the process, reads
the user SID from its token, and compares it with its own. A squatter from
another account can deny service but cannot impersonate the agent
(`Error::NotOwner`).

**Windows shadow copy.** A spawned agent first copies itself to
`%LOCALAPPDATA%\<app>\bin\<exe>-<version>.exe` and relaunches from that copy.
The installed exe is therefore never locked, and `cargo install` can replace it
while the agent runs. The copy is made only when it is missing or its bytes
differ from the installed exe, so a rebuilt binary with the same version
number is not masked by an old copy. Each start
from the shadow directory deletes the other, stale copies there.

**Envelope.** Each line is one JSON object. The client sends
`{"kind":"hello"|"shutdown"|"drain"|"request"|"cancel","id":N,...}`, with the app's
request under `body`. The agent answers with `kind` set to `response`,
`event`, `end` or `error`, carrying the same `id`. App types are any serde
types. Requests on one connection run concurrently.

## Why async (tokio)

The crate is async on tokio, and the choice was deliberate:

- `subscribe` returns a `Stream`, and the handler produces one. Following a
  job's progress is naturally async. A sync API would need a thread per
  subscription, or a hand-rolled iterator over a blocking socket that cannot
  be cancelled cleanly.
- One connection multiplexes concurrent calls and subscriptions by request id.
  That takes a reader task, a writer task and per-request channels, which
  tokio provides directly.
- The agent serves many clients at once, with timers for idle exit and the
  binary watch. All of that is one `select!` loop.
- `interprocess` has first-class tokio support for both Unix sockets and
  Windows named pipes.

The crate enables only tokio's `rt`, `io-util`, `sync`, `time` and `macros`
features. A CLI that is otherwise sync can call it from
`#[tokio::main(flavor = "current_thread")]` or a small `block_on`.

## Development

```sh
make            # fmt check, clippy -D warnings, tests
make test
make check-windows   # clippy for x86_64-pc-windows-gnu from a non-Windows host
make package    # cargo publish --dry-run
```

The integration tests (`tests/agent.rs`) start real detached agents. The test
binary is its own dummy agent: given `--dummy-agent` it serves instead of
running tests. The tests cover cold start, concurrent clients ending with
one agent, recovery from `kill -9` with a stale endpoint, `shutdown` with busy
refusal, `drain` (idle, waiting out busy and requests in flight, timing out,
and winning over a binary-watch restart), idle exit, version mismatch with restart, the Windows
shadow copy, binary-watch restart, a piped CLI whose `output()` returns after
it cold-starts the agent, and (Unix) hung-agent reporting. CI runs them on Linux,
macOS and Windows.

## License

MIT. See [LICENSE.MIT](LICENSE.MIT).
