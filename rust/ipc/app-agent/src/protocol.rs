//! The wire envelope. One JSON object per line in each direction; `id` ties replies to requests.

use std::time::SystemTime;

use serde::{Deserialize, Serialize};
use serde_json::Value;

/// Bumped whenever the envelope changes incompatibly. `hello` must stay readable across versions.
/// A new message kind that older agents reject is announced by a [`Hello`] field instead, so
/// clients and agents that differ only by it are not reported as a mismatch.
pub const PROTOCOL_VERSION: u32 = 1;

/// Client to agent.
#[derive(Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub(crate) enum ClientMsg {
    Hello {
        id: u64,
    },
    Shutdown {
        id: u64,
    },
    /// Stop once idle: the handler not busy and no request in flight.
    Drain {
        id: u64,
    },
    Request {
        id: u64,
        body: Value,
    },
    /// The client stopped listening to a subscription.
    Cancel {
        id: u64,
    },
}

/// Agent to client.
#[derive(Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub(crate) enum AgentMsg {
    Response {
        id: u64,
        body: Value,
    },
    Event {
        id: u64,
        body: Value,
    },
    End {
        id: u64,
    },
    Error {
        id: u64,
        code: ErrorCode,
        message: String,
    },
}

impl AgentMsg {
    pub(crate) fn id(&self) -> u64 {
        match self {
            AgentMsg::Response { id, .. }
            | AgentMsg::Event { id, .. }
            | AgentMsg::End { id }
            | AgentMsg::Error { id, .. } => *id,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub(crate) enum ErrorCode {
    /// The app says it is busy and will not stop.
    Busy,
    /// The app's handler failed.
    Handler,
    /// The request could not be decoded.
    BadRequest,
}

/// The agent's answer to `hello`.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[non_exhaustive]
pub struct Hello {
    /// The agent's [`PROTOCOL_VERSION`].
    pub protocol: u32,
    /// The agent's process id.
    pub pid: u32,
    /// When the agent started serving.
    pub started_at: SystemTime,
    /// The app version the agent was built with.
    pub app_version: String,
    /// Whether the agent understands `drain`. Agents before 0.0.1-alpha.3 omit it, which reads
    /// as `false`.
    #[serde(default)]
    pub drain: bool,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn envelope_shape() {
        let msg = AgentMsg::Error {
            id: 3,
            code: ErrorCode::Busy,
            message: "2 jobs".into(),
        };
        assert_eq!(
            serde_json::to_string(&msg).unwrap(),
            r#"{"kind":"error","id":3,"code":"busy","message":"2 jobs"}"#
        );
        let msg: ClientMsg =
            serde_json::from_str(r#"{"kind":"request","id":1,"body":{"x":1}}"#).unwrap();
        assert!(matches!(msg, ClientMsg::Request { id: 1, .. }));
    }

    #[test]
    fn hello_without_drain_is_old_agent() {
        let hello: Hello = serde_json::from_str(
            r#"{"protocol":1,"pid":7,"started_at":{"secs_since_epoch":0,"nanos_since_epoch":0},"app_version":"1"}"#,
        )
        .unwrap();
        assert!(!hello.drain);
    }
}
