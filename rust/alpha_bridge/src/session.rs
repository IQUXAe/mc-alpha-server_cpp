//! Native sessions: TCP transport plus the login state machine (mirrors
//! the `NetworkListenThread`/`NetworkManager` framing and
//! `NetLoginHandler`). Play dispatch arrives next; the server tick owns
//! the session set, broadcast, and config.
//!
//! Wire protocol bytes mirror `RustPackets.h` field-for-field; inbound
//! packets reuse the owned `PacketData` enum from `network.rs`.

use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::mpsc::{self, Receiver, Sender};
use std::thread;
use std::time::Duration;

use crate::network::{PacketData, put_i32, put_i64, put_i8, put_str, put_u8, read_packet_payload};

// ---- outbound builders (Alpha protocol, mirrors RustPackets.h) ----

/// Handshake response (id 2, server id or "-").
pub fn pkt_handshake(server_id: &str) -> Vec<u8> {
    let mut b = Vec::with_capacity(4 + server_id.len());
    put_u8(&mut b, 2);
    put_str(&mut b, server_id);
    b
}

/// Kick / disconnect (id 255).
pub fn pkt_kick(reason: &str) -> Vec<u8> {
    let mut b = Vec::with_capacity(4 + reason.len());
    put_u8(&mut b, 255);
    put_str(&mut b, reason);
    b
}

/// Login response (id 1): entity id in the protocol-version slot, empty
/// names, world seed, dimension — exactly like C++ `doLogin`.
pub fn pkt_login_response(entity_id: i32, seed: i64, dimension: i8) -> Vec<u8> {
    let mut b = Vec::with_capacity(16);
    put_u8(&mut b, 1);
    put_i32(&mut b, entity_id);
    put_str(&mut b, "");
    put_str(&mut b, "");
    put_i64(&mut b, seed);
    put_i8(&mut b, dimension);
    b
}

/// Chat message (id 3).
pub fn pkt_chat(msg: &str) -> Vec<u8> {
    let mut b = Vec::with_capacity(4 + msg.len());
    put_u8(&mut b, 3);
    put_str(&mut b, msg);
    b
}

/// Time update (id 4).
pub fn pkt_time(time: i64) -> Vec<u8> {
    let mut b = Vec::with_capacity(9);
    put_u8(&mut b, 4);
    put_i64(&mut b, time);
    b
}

/// Spawn position (id 6).
pub fn pkt_spawn_pos(x: i32, y: i32, z: i32) -> Vec<u8> {
    let mut b = Vec::with_capacity(13);
    put_u8(&mut b, 6);
    put_i32(&mut b, x);
    put_i32(&mut b, y);
    put_i32(&mut b, z);
    b
}

/// Health update (id 8, i8 payload like the C++ struct).
pub fn pkt_health(health: i8) -> Vec<u8> {
    let mut b = Vec::with_capacity(2);
    put_u8(&mut b, 8);
    put_i8(&mut b, health);
    b
}

// ---- connection pump (one read + one write thread per socket) ----

/// Connection event: a decoded packet or a dead socket.
pub enum ConnEvent {
    Packet(PacketData),
    Dropped,
}

/// Owned TCP connection: a read thread decodes packets into `inbound`, a
/// write thread ships `outbound` byte blobs. Either thread exiting means
/// the socket is gone (mirrors the C++ read/write thread pair).
pub struct Conn {
    inbound: Receiver<ConnEvent>,
    outbound: Sender<Vec<u8>>,
    pub remote: String,
}

impl Conn {
    pub fn new(stream: TcpStream) -> std::io::Result<Self> {
        let remote = stream.peer_addr().map(|a| a.to_string()).unwrap_or_default();
        stream.set_read_timeout(Some(Duration::from_secs(30)))?;
        let mut reader = stream.try_clone()?;
        let mut writer = stream;
        let (tx_in, inbound) = mpsc::channel();
        let (outbound, rx_out): (Sender<Vec<u8>>, Receiver<Vec<u8>>) = mpsc::channel();

        thread::Builder::new()
            .name(format!("conn-read-{remote}"))
            .spawn(move || {
                loop {
                    let mut id = [0u8; 1];
                    if reader.read_exact(&mut id).is_err() {
                        break;
                    }
                    match read_packet_payload(&mut reader, id[0]) {
                        Ok(PacketData::KickDisconnect { .. }) => break,
                        Ok(pkt) => {
                            if tx_in.send(ConnEvent::Packet(pkt)).is_err() {
                                break;
                            }
                        }
                        Err(_) => break,
                    }
                }
                let _ = tx_in.send(ConnEvent::Dropped);
            })
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e))?;

        thread::Builder::new()
            .name(format!("conn-write-{remote}"))
            .spawn(move || {
                while let Ok(msg) = rx_out.recv() {
                    if writer.write_all(&msg).is_err() || writer.flush().is_err() {
                        break;
                    }
                }
            })
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e))?;

        Ok(Self { inbound, outbound, remote })
    }

    /// Non-blocking drain of queued events.
    pub fn drain(&self) -> Vec<ConnEvent> {
        let mut out = Vec::new();
        while let Ok(ev) = self.inbound.try_recv() {
            out.push(ev);
        }
        out
    }

    /// Queue bytes for the socket (drops silently once dead).
    pub fn send(&self, bytes: Vec<u8>) {
        let _ = self.outbound.send(bytes);
    }
}

/// Bind a listener for the accept loop (the server slice drives it).
pub fn bind_listener(addr: &str) -> std::io::Result<TcpListener> {
    TcpListener::bind(addr)
}

// ---- login state machine (mirrors NetLoginHandler) ----

/// Session verification: the default hits the Mojang check endpoint
/// (blocking HTTP, run on a worker thread); tests inject a stub.
pub type VerifyFn = Box<dyn Fn(&str, &str) -> Result<String, String> + Send>;

/// Default verifier (mirrors `performSessionCheck`).
pub fn default_verify(username: &str, server_id: &str) -> Result<String, String> {
    use std::ffi::{CStr, CString};
    let u = CString::new(username).map_err(|e| e.to_string())?;
    let s = CString::new(server_id).map_err(|e| e.to_string())?;
    let mut buf = [0 as std::ffi::c_char; 128];
    let ok = unsafe { crate::rust_session_check(u.as_ptr(), s.as_ptr(), buf.as_mut_ptr(), buf.len()) };
    if !ok {
        return Err("Session verification failed".to_string());
    }
    unsafe { CStr::from_ptr(buf.as_ptr()) }
        .to_str()
        .map(|s| s.to_string())
        .map_err(|e| e.to_string())
}

/// Terminal login outcome for the server tick.
pub enum LoginEvent {
    /// Hand the username to player creation (packets already queued).
    Accepted { username: String },
    /// Kicked or gone; kick bytes (if any) are already queued.
    Done,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum LoginState {
    WaitHandshake,
    WaitLogin,
    Verifying,
    Finished,
}

/// Login session: feeds inbound packets, queues outbound bytes, and
/// reports its outcome through `poll` (mirrors `tryLogin`).
pub struct LoginSession {
    online_mode: bool,
    server_id: String,
    username: String,
    ticks: u32,
    state: LoginState,
    verifying: bool,
    verify_rx: Option<Receiver<Result<String, String>>>,
    verify: Option<VerifyFn>,
    finished: bool,
    done_sent: bool,
    /// Bytes for the socket, in order.
    pub outbox: Vec<Vec<u8>>,
}

impl LoginSession {
    pub fn new(online_mode: bool) -> Self {
        Self {
            online_mode,
            server_id: String::new(),
            username: String::new(),
            ticks: 0,
            state: LoginState::WaitHandshake,
            verifying: false,
            verify_rx: None,
            verify: Some(Box::new(default_verify)),
            finished: false,
            done_sent: false,
            outbox: Vec::new(),
        }
    }

    /// Test hook: replace the HTTP verifier.
    pub fn with_verify(mut self, f: VerifyFn) -> Self {
        self.verify = Some(f);
        self
    }

    fn kick(&mut self, reason: &str) {
        if self.finished {
            return;
        }
        self.outbox.push(pkt_kick(reason));
        self.finished = true;
        self.state = LoginState::Finished;
    }

    /// Feed one inbound packet (unexpected packets are ignored like the
    /// C++ handler set, which only overrides handshake/login/error).
    pub fn on_packet(&mut self, pkt: PacketData) {
        if self.finished {
            return;
        }
        match pkt {
            PacketData::Handshake { username } => {
                let _ = username;
                if self.online_mode {
                    // Nonce like C++ (hex of a uniform i64).
                    let rand_val = nonce_i64();
                    self.server_id = format!("{:x}", rand_val as u64);
                    self.outbox.push(pkt_handshake(&self.server_id.clone()));
                } else {
                    self.outbox.push(pkt_handshake("-"));
                }
                if self.state == LoginState::WaitHandshake {
                    self.state = LoginState::WaitLogin;
                }
            }
            PacketData::Login { protocol_version, mut username, .. } => {
                while username.ends_with(['\0', '\r', '\n', ' ']) {
                    username.pop();
                }
                self.username = username.clone();
                if protocol_version != 6 {
                    if protocol_version > 6 {
                        self.kick("Outdated server!");
                    } else {
                        self.kick("Outdated client!");
                    }
                    return;
                }
                if !self.online_mode {
                    self.finished = true;
                    return;
                }
                if self.verifying {
                    self.kick("Duplicate login packet");
                    return;
                }
                self.verifying = true;
                self.state = LoginState::Verifying;
                let (tx, rx) = mpsc::channel();
                self.verify_rx = Some(rx);
                let verify = self.verify.take();
                let sid = self.server_id.clone();
                thread::Builder::new()
                    .name(format!("login-verify-{username}"))
                    .spawn(move || {
                        let res = match verify {
                            Some(f) => f(&username, &sid),
                            None => Err("no verifier".to_string()),
                        };
                        let _ = tx.send(res);
                    })
                    .ok();
            }
            _ => {}
        }
    }

    /// Remote hung up (mirrors `handleErrorMessage`).
    pub fn on_drop(&mut self) {
        self.finished = true;
        self.state = LoginState::Finished;
    }

    /// Tick the timeout and the verifier result (mirrors `tryLogin`).
    pub fn poll(&mut self) -> Option<LoginEvent> {
        if self.done_sent {
            return None;
        }
        if self.state == LoginState::Verifying {
            if let Some(rx) = &self.verify_rx {
                match rx.try_recv() {
                    Ok(Ok(reply)) => {
                        if reply == "YES" {
                            self.finished = true;
                            self.done_sent = true;
                            return Some(LoginEvent::Accepted {
                                username: std::mem::take(&mut self.username),
                            });
                        }
                        self.kick("Failed to verify username!");
                        self.done_sent = true;
                        return Some(LoginEvent::Done);
                    }
                    Ok(Err(_)) => {
                        self.kick("Failed to verify username!");
                        self.done_sent = true;
                        return Some(LoginEvent::Done);
                    }
                    Err(_) => {}
                }
            }
        }
        // Offline accept lands here (finished, still WaitLogin); kicks
        // and drops land in Finished.
        if self.finished {
            self.done_sent = true;
            if self.state == LoginState::Finished {
                return Some(LoginEvent::Done);
            }
            return Some(LoginEvent::Accepted { username: std::mem::take(&mut self.username) });
        }
        self.ticks += 1;
        if self.ticks >= 600 {
            self.kick("Took too long to log in");
            self.done_sent = true;
            return Some(LoginEvent::Done);
        }
        None
    }
}

/// Cheap nonce (server ids need uniqueness, not determinism).
fn nonce_i64() -> i64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0x9E3779B97F4A7C15);
    let mut x = nanos | 1;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    x as i64
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::TcpStream;
    use std::time::Duration;

    fn login_packet(protocol: i32, username: &str) -> PacketData {
        PacketData::Login {
            protocol_version: protocol,
            username: username.to_string(),
            password: String::new(),
            map_seed: 0,
            dimension: 0,
        }
    }

    fn read_msg(stream: &mut TcpStream) -> (u8, Vec<u8>) {
        let mut id = [0u8; 1];
        stream.read_exact(&mut id).unwrap();
        let mut len = [0u8; 2];
        stream.read_exact(&mut len).unwrap();
        let len = u16::from_be_bytes(len) as usize;
        let mut body = vec![0u8; len];
        stream.read_exact(&mut body).unwrap();
        (id[0], body)
    }

    fn write_login(stream: &mut TcpStream, protocol: i32, username: &str) {
        let mut b = Vec::new();
        put_u8(&mut b, 1);
        crate::network::put_i32(&mut b, protocol);
        crate::network::put_str(&mut b, username);
        crate::network::put_str(&mut b, "");
        crate::network::put_i64(&mut b, 0);
        crate::network::put_i8(&mut b, 0);
        stream.write_all(&b).unwrap();
    }

    #[test]
    fn test_offline_login_accepts_and_trims() {
        let mut s = LoginSession::new(false);
        s.on_packet(PacketData::Handshake { username: "Steve".to_string() });
        assert_eq!(s.outbox.len(), 1);
        assert_eq!(s.outbox[0][0], 2);
        s.on_packet(login_packet(6, "Steve  \n"));
        match s.poll() {
            Some(LoginEvent::Accepted { username }) => assert_eq!(username, "Steve"),
            other => panic!("expected accept, got {}", other.is_some()),
        }
        assert!(s.poll().is_none());
    }

    #[test]
    fn test_protocol_mismatch_kicks() {
        let mut s = LoginSession::new(false);
        s.on_packet(login_packet(5, "Steve"));
        assert_eq!(s.outbox.len(), 1);
        assert_eq!(s.outbox[0][0], 255);
        match s.poll() {
            Some(LoginEvent::Done) => {}
            _ => panic!("expected done"),
        }
        let mut s = LoginSession::new(false);
        s.on_packet(login_packet(7, "Steve"));
        assert_eq!(s.outbox[0][0], 255);
    }

    #[test]
    fn test_login_timeout_kicks() {
        let mut s = LoginSession::new(false);
        for _ in 0..599 {
            assert!(s.poll().is_none());
        }
        match s.poll() {
            Some(LoginEvent::Done) => {}
            _ => panic!("expected timeout done"),
        }
        assert_eq!(s.outbox.len(), 1);
        assert_eq!(s.outbox[0][0], 255);
    }

    #[test]
    fn test_online_verify_yes_and_no() {
        let yes: VerifyFn = Box::new(|_, _| Ok("YES".to_string()));
        let mut s = LoginSession::new(true).with_verify(yes);
        s.on_packet(PacketData::Handshake { username: "Steve".to_string() });
        assert_eq!(s.outbox[0][0], 2);
        assert_ne!(&s.outbox[0][3..], b"-");
        s.on_packet(login_packet(6, "Steve"));
        let mut ev = None;
        for _ in 0..100 {
            if let Some(e) = s.poll() {
                ev = Some(e);
                break;
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        match ev {
            Some(LoginEvent::Accepted { username }) => assert_eq!(username, "Steve"),
            _ => panic!("expected verified accept"),
        }

        let no: VerifyFn = Box::new(|_, _| Ok("NO".to_string()));
        let mut s = LoginSession::new(true).with_verify(no);
        s.on_packet(login_packet(6, "Steve"));
        let mut ev = None;
        for _ in 0..100 {
            if let Some(e) = s.poll() {
                ev = Some(e);
                break;
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        match ev {
            Some(LoginEvent::Done) => {}
            _ => panic!("expected verified kick"),
        }
        assert_eq!(s.outbox.last().unwrap()[0], 255);
    }

    #[test]
    fn test_online_duplicate_login_kicks() {
        let slow: VerifyFn = Box::new(|_, _| {
            std::thread::sleep(Duration::from_millis(200));
            Ok("YES".to_string())
        });
        let mut s = LoginSession::new(true).with_verify(slow);
        s.on_packet(login_packet(6, "Steve"));
        s.on_packet(login_packet(6, "Steve"));
        match s.poll() {
            Some(LoginEvent::Done) => {}
            _ => panic!("expected duplicate kick"),
        }
    }

    #[test]
    fn test_conn_loopback() {
        let listener = bind_listener("127.0.0.1:0").unwrap();
        let addr = listener.local_addr().unwrap();
        let handle = std::thread::spawn(move || {
            let (stream, _) = listener.accept().unwrap();
            Conn::new(stream).unwrap()
        });
        let mut client = TcpStream::connect(addr).unwrap();
        client.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
        // Client -> server: handshake.
        let mut hb = Vec::new();
        put_u8(&mut hb, 2);
        crate::network::put_str(&mut hb, "Steve");
        client.write_all(&hb).unwrap();
        let conn = handle.join().unwrap();
        std::thread::sleep(Duration::from_millis(100));
        let events = conn.drain();
        assert!(events.iter().any(|e| matches!(
            e,
            ConnEvent::Packet(PacketData::Handshake { username }) if username == "Steve"
        )));
        // Server -> client: kick bytes arrive intact.
        conn.send(pkt_kick("Bye"));
        assert_eq!(read_msg(&mut client), (255, b"Bye".to_vec()));
        // Client drop surfaces as Dropped.
        drop(client);
        let mut saw_drop = false;
        for _ in 0..100 {
            if conn.drain().iter().any(|e| matches!(e, ConnEvent::Dropped)) {
                saw_drop = true;
                break;
            }
            std::thread::sleep(Duration::from_millis(20));
        }
        assert!(saw_drop);
    }

    #[test]
    fn test_conn_full_login_over_socket() {
        let listener = bind_listener("127.0.0.1:0").unwrap();
        let addr = listener.local_addr().unwrap();
        let handle = std::thread::spawn(move || {
            let (stream, _) = listener.accept().unwrap();
            Conn::new(stream).unwrap()
        });
        let mut client = TcpStream::connect(addr).unwrap();
        client.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
        let conn = handle.join().unwrap();
        let mut login = LoginSession::new(false);
        // Handshake round trip.
        let mut hb = Vec::new();
        put_u8(&mut hb, 2);
        crate::network::put_str(&mut hb, "Alex");
        client.write_all(&hb).unwrap();
        std::thread::sleep(Duration::from_millis(100));
        for ev in conn.drain() {
            if let ConnEvent::Packet(p) = ev {
                login.on_packet(p);
            }
        }
        for msg in login.outbox.drain(..) {
            conn.send(msg);
        }
        assert_eq!(read_msg(&mut client).1, b"-".to_vec());
        // Login round trip.
        write_login(&mut client, 6, "Alex");
        std::thread::sleep(Duration::from_millis(100));
        for ev in conn.drain() {
            if let ConnEvent::Packet(p) = ev {
                login.on_packet(p);
            }
        }
        match login.poll() {
            Some(LoginEvent::Accepted { username }) => assert_eq!(username, "Alex"),
            _ => panic!("expected accept over socket"),
        }
    }
}
