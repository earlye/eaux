//! A per-user, single-instance resident agent (in the style of `ssh-agent` or `gpg-agent`) that
//! a CLI starts on demand and talks to over a private local socket or named pipe.
//!
//! - [`App`] names the agent and how to start it; the CLI and the agent build the same one.
//! - [`App::connect_or_spawn`] returns a [`Client`] with [`Client::call`] and
//!   [`Client::subscribe`].
//! - [`App::run`] / [`App::serve`] make the current process the agent, serving a [`Handler`].
//!
//! See the README for locations, the lock-first startup order and the Windows specifics.

mod agent;
mod app;
mod client;
mod error;
mod locations;
mod lock;
mod protocol;
mod spawn;
mod transport;
#[cfg(windows)]
mod windows;

pub use agent::{EXIT_ALREADY_RUNNING, EventStream, Handler, Reply, Stopped};
pub use app::App;
pub use client::{Client, Mismatch, Subscription};
pub use error::{Error, Result};
pub use locations::{Locations, endpoint_env_var};
pub use protocol::{Hello, PROTOCOL_VERSION};
