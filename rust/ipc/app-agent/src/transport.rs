//! JSON-lines over `interprocess` local sockets (Unix domain sockets, Windows named pipes).
//! Kept private so it can become HTTP-over-socket later without touching the public API.

use std::io;
use std::path::Path;

use interprocess::local_socket::tokio::prelude::*;
use interprocess::local_socket::tokio::{RecvHalf, SendHalf};
use interprocess::local_socket::{GenericFilePath, ListenerOptions};
use serde::Serialize;
use serde::de::DeserializeOwned;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};

pub(crate) use interprocess::local_socket::tokio::{Listener, Stream};

pub(crate) async fn connect(endpoint: &Path) -> io::Result<Stream> {
    let name = endpoint.to_fs_name::<GenericFilePath>()?;
    Stream::connect(name).await
}

/// Bind the endpoint. On Unix the caller must already hold the lock and have removed any stale
/// socket file.
pub(crate) fn bind(endpoint: &Path) -> io::Result<Listener> {
    let name = endpoint.to_fs_name::<GenericFilePath>()?;
    let options = ListenerOptions::new().name(name);
    #[cfg(windows)]
    let options = {
        use interprocess::os::windows::local_socket::ListenerOptionsExt;
        options.security_descriptor(crate::windows::pipe_security_descriptor()?)
    };
    options.create_tokio()
}

/// The pid of the named pipe server on the other end of the connection.
#[cfg(windows)]
pub(crate) fn peer_pid(stream: &Stream) -> io::Result<Option<u32>> {
    Ok(stream.peer_creds()?.pid())
}

pub(crate) struct Reader {
    inner: BufReader<RecvHalf>,
    line: String,
}

pub(crate) struct Writer {
    inner: SendHalf,
    buf: Vec<u8>,
}

pub(crate) fn split(stream: Stream) -> (Reader, Writer) {
    let (recv, send) = stream.split();
    (
        Reader {
            inner: BufReader::new(recv),
            line: String::new(),
        },
        Writer {
            inner: send,
            buf: Vec::new(),
        },
    )
}

impl Reader {
    /// The next message, or `None` at end of stream.
    pub(crate) async fn read<T: DeserializeOwned>(&mut self) -> io::Result<Option<T>> {
        loop {
            self.line.clear();
            if self.inner.read_line(&mut self.line).await? == 0 {
                return Ok(None);
            }
            if self.line.trim().is_empty() {
                continue;
            }
            return serde_json::from_str(&self.line)
                .map(Some)
                .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e));
        }
    }
}

impl Writer {
    pub(crate) async fn write<T: Serialize>(&mut self, msg: &T) -> io::Result<()> {
        self.buf.clear();
        serde_json::to_writer(&mut self.buf, msg)
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
        self.buf.push(b'\n');
        self.inner.write_all(&self.buf).await?;
        self.inner.flush().await
    }
}
