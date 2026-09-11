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
    closer: Sender<()>,
    release: Option<TcpStream>,
    pub remote: String,
}

impl Conn {
    pub fn new(stream: TcpStream) -> std::io::Result<Self> {
        let remote = stream.peer_addr().map(|a| a.to_string()).unwrap_or_default();
        // Blocking reads like the C++ network manager: idle policy lives
        // one level up (login 600 / play 1200 ticks with dead bypass), so
        // the socket layer must not pre-empt it (death screens go quiet).
        stream.set_read_timeout(None)?;
        let release = stream.try_clone().ok();
        let mut reader = stream.try_clone()?;
        let mut writer = stream;
        let (tx_in, inbound) = mpsc::channel();
        let (outbound, rx_out): (Sender<Vec<u8>>, Receiver<Vec<u8>>) = mpsc::channel();
        let (closer, close_rx): (Sender<()>, Receiver<()>) = mpsc::channel();

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
            .spawn({
                let remote = remote.clone();
                move || {
                // Drain-then-FIN: queued kick bytes must reach the client,
                // so close only stops the writer after the queue empties
                // (a Both-shutdown here would RST pending data away).
                loop {
                    while let Ok(msg) = rx_out.try_recv() {
                        if writer.write_all(&msg).is_err() || writer.flush().is_err() {
                            return;
                        }
                    }
                    if close_rx.try_recv().is_ok() {
                        let _ = writer.flush();
                        let _ = writer.shutdown(std::net::Shutdown::Write);
                        return;
                    }
                    match rx_out.recv_timeout(Duration::from_millis(20)) {
                        Ok(msg) => {
                            if writer.write_all(&msg).is_err() || writer.flush().is_err() {
                                return;
                            }
                        }
                        Err(mpsc::RecvTimeoutError::Timeout) => {}
                        Err(mpsc::RecvTimeoutError::Disconnected) => return,
                    }
                }
                }
            })
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e))?;

        Ok(Self { inbound, outbound, closer, release, remote })
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

    /// Graceful close: the writer drains queued bytes and FINs (the
    /// server calls this on kick/timeout/shutdown); the blocked reader
    /// is released separately so ghost threads cannot linger.
    pub fn close(&self) {
        let _ = self.closer.send(());
        if let Some(s) = &self.release {
            let _ = s.shutdown(std::net::Shutdown::Read);
        }
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
    fn test_conn_close_unblocks_reader() {
        let listener = bind_listener("127.0.0.1:0").unwrap();
        let addr = listener.local_addr().unwrap();
        let handle = std::thread::spawn(move || {
            let (stream, _) = listener.accept().unwrap();
            Conn::new(stream).unwrap()
        });
        let client = TcpStream::connect(addr).unwrap();
        client.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
        let conn = handle.join().unwrap();
        conn.close();
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

// ---- play session state ----

/// Reach for melee (mirrors `kMaxAttackReach`).
const ATTACK_REACH_SQ: f64 = 25.0;

/// An authenticated player connection: packet dispatch against the world
/// (mirrors the `NetServerHandler` handlers). Chunk streaming, entity
/// tracking fan-out, and saves belong to the server tick.
pub struct PlaySession {
    pub player: EntityId,
    pub outbox: Vec<Vec<u8>>,
    pub dig: FfiDigState,
    pub has_moved: bool,
    pub last: [f64; 3],
    pub held_id: i32,
    pub held_fallback: Option<FfiItemStack>,
    pub keepalive_tick: u32,
    pub gone: bool,
}

impl PlaySession {
    pub fn new(player: EntityId) -> Self {
        Self {
            player,
            outbox: Vec::new(),
            dig: alpha_dig_state_new(),
            has_moved: false,
            last: [0.0; 3],
            held_id: 0,
            held_fallback: None,
            keepalive_tick: 0,
            gone: false,
        }
    }

    fn kick(&mut self, reason: &str) -> Option<SessionOutcome> {
        self.outbox.push(pkt_kick(reason));
        self.gone = true;
        Some(SessionOutcome::Kick(reason.to_string()))
    }

    fn username<'a>(&self, world: &'a World) -> &'a str {
        match world.entities.get(self.player) {
            Some(Entity::Player(p)) => &p.username,
            _ => "",
        }
    }

    fn is_op(&self, ctx: &SessionCtx) -> bool {
        // Ops store lowercased like C++; the query lowercases too
        // (mirrors `isOp`, so mixed-case names keep their rights).
        ctx.ops.contains(&self.username(ctx.world).to_ascii_lowercase())
    }

    /// Teleport (mirrors `NetServerHandler::teleport`, stance y+1.62).
    pub fn teleport_to(
        &mut self,
        world: &mut World,
        id: EntityId,
        x: f64,
        y: f64,
        z: f64,
        yaw: f32,
        pitch: f32,
    ) {
        if let Some(e) = world.entities.get_mut(id) {
            e.body_mut().set_position(x, y, z);
            e.body_mut().yaw = yaw;
            e.body_mut().pitch = pitch;
        }
        if id == self.player {
            self.last = [x, y, z];
            self.has_moved = false;
        }
        self.outbox.push(pkt_teleport(x, y, z, yaw, pitch));
    }

    /// Full inventory sync (mirrors `sendInventory`: main/craft/armor).
    pub fn send_inventory(&mut self, world: &World) {
        let (main, crafting, armor) = match world.entities.get(self.player) {
            Some(Entity::Player(p)) => (
                p.inventory.main.to_vec(),
                p.inventory.crafting.to_vec(),
                p.inventory.armor.to_vec(),
            ),
            _ => return,
        };
        self.outbox.push(pkt_inventory_section(-1, &main));
        self.outbox.push(pkt_inventory_section(-2, &crafting));
        self.outbox.push(pkt_inventory_section(-3, &armor));
    }

    /// Tile-entity packet for one cell, if a tile row exists.
    pub fn send_tile(&mut self, world: &World, x: i32, y: i32, z: i32) {
        if let Some(tile) = world.tiles.get(&(x, y, z)) {
            self.outbox.push(tile_packet(x, y, z, tile));
        }
    }

    /// Block-change rollback packet with current cell state.
    pub fn send_block_change(&mut self, world: &World, x: i32, y: i32, z: i32) {
        self.outbox.push(pkt_block_change(
            x,
            y,
            z,
            world.get_block_id(x, y, z),
            world.get_block_meta(x, y, z),
        ));
    }

    /// Held-item sync (mirrors `syncHeldItemSelection`).
    fn sync_held(&mut self, world: &mut World) {
        if self.held_id <= 0 {
            self.held_fallback = None;
            return;
        }
        let mut found = false;
        if let Some(Entity::Player(p)) = world.entities.get_mut(self.player) {
            for i in 0..35 {
                if let Some(s) = p.inventory.main[i] {
                    if s.item_id == self.held_id {
                        p.inventory.current = i as i32;
                        found = true;
                        break;
                    }
                }
            }
            if !found {
                p.inventory.main[35] = None;
                p.inventory.current = 35;
            }
        }
        if found {
            self.held_fallback = None;
        } else if self.held_fallback.map(|s| s.item_id) != Some(self.held_id) {
            self.held_fallback = Some(FfiItemStack {
                stack_size: 1,
                animations_to_go: 0,
                item_id: self.held_id,
                item_damage: 0,
            });
        }
    }

    /// Login-path held restore (mirrors `restoreHeldItem`, called only
    /// with the saved id when positive): point `current` at the slot
    /// holding that item, else stage the fallback copy in the last slot.
    pub fn restore_held(&mut self, world: &mut World, item_id: i32) {
        if item_id <= 0 {
            if let Some(Entity::Player(p)) = world.entities.get_mut(self.player) {
                p.inventory.current = 0;
            }
            return;
        }
        self.held_id = item_id;
        self.sync_held(world);
    }

    /// Selected stack (mirrors `getSelectedItemStack`): real slot first,
    /// then the fallback copy.
    fn selected_stack(&self, world: &World) -> Option<FfiItemStack> {
        if let Some(Entity::Player(p)) = world.entities.get(self.player) {
            let cur = p.inventory.current;
            if cur >= 0 && (cur as usize) < p.inventory.main.len() {
                if let Some(s) = p.inventory.main[cur as usize] {
                    return Some(s);
                }
            }
        }
        if let Some(s) = self.held_fallback {
            if self.held_id > 0 && s.item_id == self.held_id {
                return Some(s);
            }
        }
        None
    }

    /// Water probe for digging/movement (mirrors the entity probe; native
    /// rows never store `in_water`).
    fn in_water(world: &World, id: EntityId) -> bool {
        let bb = match world.entities.get(id) {
            Some(e) => e.body().bounding_box.expand(0.0, -0.4, 0.0),
            None => return false,
        };
        for x in floor_double(bb.min_x)..=floor_double(bb.max_x) {
            for y in floor_double(bb.min_y)..=floor_double(bb.max_y) {
                for z in floor_double(bb.min_z)..=floor_double(bb.max_z) {
                    if world.material_at(x, y, z) == crate::material::Material::WATER {
                        return true;
                    }
                }
            }
        }
        false
    }

    /// Per-tick upkeep (mirrors the handler `tick` minus chunk streaming:
    /// keep-alive every 20 ticks).
    pub fn tick(&mut self, _ctx: &mut SessionCtx) -> Option<SessionOutcome> {
        if self.gone {
            return None;
        }
        self.keepalive_tick += 1;
        if self.keepalive_tick % 20 == 0 {
            self.outbox.push(pkt_keepalive());
        }
        None
    }

    /// Dispatch one inbound packet (mirrors the handler switch).
    pub fn pump(&mut self, ctx: &mut SessionCtx, pkt: PacketData) -> Option<SessionOutcome> {
        if self.gone {
            return None;
        }
        // Borrow split: the world for rows, the session for state.
        // Handlers take both explicitly to keep this readable.
        match pkt {
            PacketData::Flying { on_ground } => {
                self.movement(ctx, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false, false, on_ground)
            }
            PacketData::PlayerPosition { x, y, stance, z, on_ground } => {
                self.movement(ctx, x, y, stance, z, 0.0, 0.0, true, false, on_ground)
            }
            PacketData::PlayerLook { yaw, pitch, on_ground } => {
                self.movement(ctx, 0.0, 0.0, 0.0, 0.0, yaw, pitch, false, true, on_ground)
            }
            PacketData::PlayerLookMove { x, y, stance, z, yaw, pitch, on_ground } => {
                self.movement(ctx, x, y, stance, z, yaw, pitch, true, true, on_ground)
            }
            PacketData::BlockDig { status, x, y, z, face } => {
                self.dig(ctx, status, x, y as i32, z, face)
            }
            PacketData::Place { item_id, x, y, z, direction } => {
                self.place(ctx, item_id, x, y as i32, z, direction)
            }
            PacketData::UseEntity { player_entity_id, target_entity_id, is_left_click } => {
                self.use_entity(ctx, player_entity_id, target_entity_id, is_left_click)
            }
            PacketData::Chat { message } => self.chat(ctx, &message),
            PacketData::Respawn => self.respawn(ctx),
            PacketData::BlockItemSwitch { item_id, .. } => {
                self.held_switch(ctx, item_id);
                None
            }
            PacketData::ArmAnimation { animate, .. } => {
                self.arm(ctx, animate);
                None
            }
            PacketData::PlayerInventory { inventory_type, slots } => {
                self.apply_inventory(ctx, inventory_type, &slots);
                None
            }
            PacketData::ComplexEntity { x, y, z, nbt_data } => {
                self.complex_entity(ctx, x, y as i32, z, &nbt_data);
                None
            }
            PacketData::PickupSpawn { .. } => None,
            PacketData::KickDisconnect { .. } => {
                self.gone = true;
                Some(SessionOutcome::Gone)
            }
            _ => None,
        }
    }

    // ---- movement (mirrors processMovement) ----

    #[allow(clippy::too_many_arguments)]
    fn movement(
        &mut self,
        ctx: &mut SessionCtx,
        x: f64,
        y: f64,
        stance: f64,
        z: f64,
        yaw: f32,
        pitch: f32,
        mut moving: bool,
        rotating: bool,
        on_ground: bool,
    ) -> Option<SessionOutcome> {
        let me = self.player;
        let (pos, cur_yaw, cur_pitch, riding) = match ctx.world.entities.get(me) {
            Some(e) => (e.body().pos, e.body().yaw, e.body().pitch, e.body().riding),
            None => return None,
        };
        if !self.has_moved {
            self.last = pos;
            self.has_moved = true;
        }
        let final_yaw = if rotating { yaw } else { cur_yaw };
        let final_pitch = if rotating { pitch } else { cur_pitch };
        // Riding branch: look + drift only.
        if riding >= 0 {
            if let Some(e) = ctx.world.entities.get_mut(me) {
                let b = e.body_mut();
                b.yaw = final_yaw;
                b.pitch = final_pitch;
                b.on_ground = on_ground;
                b.motion = [0.0; 3];
                if moving && y == -999.0 && stance == -999.0 {
                    b.motion[0] = x;
                    b.motion[2] = z;
                }
            }
            ctx.world.entities.update_rider_position(riding);
            if let Some(e) = ctx.world.entities.get(me) {
                self.last = e.body().pos;
            }
            return None;
        }
        if moving && y == -999.0 && stance == -999.0 {
            moving = false;
        }
        let [bx, by, bz] = self.last;
        // Reset to the last accepted state, like C++.
        if let Some(e) = ctx.world.entities.get_mut(me) {
            let b = e.body_mut();
            b.set_position(bx, by, bz);
            b.yaw = final_yaw;
            b.pitch = final_pitch;
            b.motion = [0.0; 3];
        }
        if !moving {
            if let Some(e) = ctx.world.entities.get_mut(me) {
                let b = e.body_mut();
                b.yaw = final_yaw;
                b.pitch = final_pitch;
                b.on_ground = on_ground;
            }
            self.last = [bx, by, bz];
            return None;
        }
        let in_water = Self::in_water(ctx.world, me);
        let fall = ctx
            .world
            .entities
            .get(me)
            .map(|e| e.body().fall_distance)
            .unwrap_or(0.0);
        let input = FfiMovementInput {
            from_x: bx,
            from_y: by,
            from_z: bz,
            to_x: x,
            to_y: y,
            to_z: z,
            stance,
            on_ground,
            is_in_water: in_water,
            fall_distance: fall,
        };
        let res = unsafe { alpha_movement_validate(&input) };
        match res.status {
            1 => return self.kick("Illegal stance"),
            2 => return self.kick("Illegal position"),
            3 => return self.kick("Moved too quickly"),
            4 => {
                self.teleport_to(ctx.world, me, bx, by, bz, final_yaw, final_pitch);
                return None;
            }
            _ => {}
        }
        // Client-authoritative glide: suppress fall accumulation, then
        // re-derive fall from the validator like C++.
        if let Some(e) = ctx.world.entities.get_mut(me) {
            e.body_mut().suppress_fall_state = true;
        }
        ctx.world.move_body(me, x - bx, y - by, z - bz);
        if let Some(e) = ctx.world.entities.get_mut(me) {
            e.body_mut().suppress_fall_state = false;
            e.body_mut().yaw = final_yaw;
            e.body_mut().pitch = final_pitch;
        }
        let (px, py, pz, fall2) = match ctx.world.entities.get(me) {
            Some(e) => (e.body().pos[0], e.body().pos[1], e.body().pos[2], e.body().fall_distance),
            None => return None,
        };
        let fall_input = FfiMovementInput {
            from_x: bx,
            from_y: by,
            from_z: bz,
            to_x: px,
            to_y: py,
            to_z: pz,
            stance,
            on_ground,
            is_in_water: in_water,
            fall_distance: fall2,
        };
        let fall_res = unsafe { alpha_movement_validate(&fall_input) };
        if fall_res.fall_damage > 0 {
            ctx.world.attack_living(me, fall_res.fall_damage, None);
        }
        if let Some(e) = ctx.world.entities.get_mut(me) {
            e.body_mut().fall_distance = fall_res.new_fall_distance;
            e.body_mut().on_ground = on_ground;
        }
        if let Some(e) = ctx.world.entities.get(me) {
            self.last = e.body().pos;
        }
        None
    }

    // ---- digging (mirrors handleBlockDig + harvestBlock) ----

    fn dig(
        &mut self,
        ctx: &mut SessionCtx,
        status: i8,
        x: i32,
        y: i32,
        z: i32,
        _face: i8,
    ) -> Option<SessionOutcome> {
        self.sync_held(ctx.world);
        if y < 0 || y >= crate::world::WORLD_HEIGHT {
            return None;
        }
        let me = self.player;
        let (px, py, pz, on_ground) = match ctx.world.entities.get(me) {
            Some(e) => (e.body().pos[0], e.body().pos[1], e.body().pos[2], e.body().on_ground),
            None => return None,
        };
        let dist_sq = (px - (x as f64 + 0.5)).powi(2)
            + (py - (y as f64 + 0.5)).powi(2)
            + (pz - (z as f64 + 0.5)).powi(2);
        if (status == 0 || status == 1) && dist_sq > 36.0 {
            return None;
        }
        let protected = {
            let sp = ctx.world.spawn;
            (x - sp[0]).abs().max((z - sp[2]).abs()) <= ctx.spawn_protection
        };
        if status == 0 {
            if !protected || self.is_op(ctx) {
                // Clear client-side chest prediction before digging starts.
                if ctx.world.get_block_id(x, y, z) == 54 {
                    if let Some(TileData::Chest(_)) = ctx.world.tiles.get(&(x, y, z)) {
                        use crate::tile_entity_chest::chest_create;
                        self.outbox.push(tile_packet(x, y, z, &TileData::Chest(chest_create())));
                    }
                }
                let bid = ctx.world.get_block_id(x, y, z);
                if bid == 0 {
                    return None;
                }
                let input = self.dig_input(ctx, bid as i32);
                if alpha_dig_on_click(input) {
                    self.harvest(ctx, x, y, z);
                }
            }
        } else if status == 2 {
            unsafe { crate::player_digging::alpha_dig_cancel(&mut self.dig) };
        } else if status == 1 {
            if !protected || self.is_op(ctx) {
                let bid = ctx.world.get_block_id(x, y, z);
                let input = self.dig_input(ctx, bid as i32);
                let done = unsafe { alpha_dig_on_tick(&mut self.dig, x, y, z, input) };
                if done {
                    self.harvest(ctx, x, y, z);
                }
            }
        } else if status == 3 {
            if dist_sq < 256.0 {
                self.send_block_change(ctx.world, x, y, z);
            }
        }
        let _ = on_ground;
        None
    }

    fn dig_input(&self, ctx: &SessionCtx, block_id: i32) -> FfiDigInput {
        let held = self.selected_stack(ctx.world).map(|s| s.item_id).unwrap_or(0);
        let (in_water, on_ground) = match ctx.world.entities.get(self.player) {
            Some(e) => (Self::in_water(ctx.world, self.player), e.body().on_ground),
            None => (false, false),
        };
        FfiDigInput { block_id, held_item_id: held, in_water, on_ground }
    }

    /// Break a block (mirrors `harvestBlock` + `removeBlock`): container
    /// scatter first, air set, tool wear, then the block drop when the
    /// held tool can harvest.
    fn harvest(&mut self, ctx: &mut SessionCtx, x: i32, y: i32, z: i32) {
        let bid = ctx.world.get_block_id(x, y, z);
        if bid == 0 {
            return;
        }
        if matches!(bid, 54 | 61 | 62 | 63 | 68) {
            ctx.world.scatter_container_tile(x, y, z);
        } else {
            ctx.world.tiles.remove(&(x, y, z));
        }
        let removed = ctx.world.apply_set_notify(x, y, z, 0);
        // Tool wear on the real held slot (pick/spade/axe only).
        let cur = match ctx.world.entities.get(self.player) {
            Some(Entity::Player(p)) => p.inventory.current,
            _ => -1,
        };
        if cur >= 0 && cur < 36 {
            let mut slot = match ctx.world.entities.get(self.player) {
                Some(Entity::Player(p)) => p.inventory.main[cur as usize],
                _ => None,
            };
            if let Some(mut s) = slot {
                if s.item_id > 0 && s.item_id < 32000 {
                    let kind = alpha_item_tool_kind(s.item_id);
                    if kind == crate::item_data::ItemToolKind::Pickaxe as i32
                        || kind == crate::item_data::ItemToolKind::Spade as i32
                        || kind == crate::item_data::ItemToolKind::Axe as i32
                    {
                        let max = alpha_item_max_damage(s.item_id);
                        crate::inventory::item_stack_damage(&mut s as *mut _, 1, max);
                        if s.stack_size <= 0 || s.item_damage > max {
                            slot = None;
                        } else {
                            slot = Some(s);
                        }
                    }
                }
            }
            if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(self.player) {
                p.inventory.main[cur as usize] = slot;
            }
        }
        // Block drop when harvestable (uses the pre-removal id like C++).
        if removed {
            let held_id = self.selected_stack(ctx.world).map(|s| s.item_id).unwrap_or(0);
            if alpha_mining_can_harvest(bid as i32, held_id) {
                ctx.world.drop_block_for(bid, x, y, z);
            }
        }
    }
}

// ---- play session (mirrors NetServerHandler packet handlers) ----

use std::collections::HashSet;

use crate::entity_table::{AnimalKind, Entity, EntityId};
use crate::inventory::FfiItemStack;
use crate::item_data::{alpha_item_food_heal, alpha_item_max_damage, alpha_item_tool_kind};
use crate::item_use::alpha_item_food_bite;
use crate::item_verbs::{
    BoatThrow, FlintOut, ItemUseWorld, item_block_use, item_boat_aim, item_boat_throw,
    item_flint_use, item_hoe_use, item_seeds_use, item_sign_use,
};
use crate::math_helper::floor_double;
use crate::network::{put_f32, put_f64, put_i16};
use crate::player_combat::alpha_combat_get_weapon_damage;
use crate::player_digging::{
    FfiDigInput, FfiDigState, alpha_dig_on_click, alpha_dig_on_tick, alpha_dig_state_new,
};
use crate::player_mining::alpha_mining_can_harvest;
use crate::player_movement::{FfiMovementInput, alpha_movement_validate};
use crate::server_admin::{ChatWorld, rust_chat_command};
use crate::world::{TileData, World};

// ---- extra outbound builders ----

/// Teleport / look-move (id 13, stance y+1.62 like C++).
pub fn pkt_teleport(x: f64, y: f64, z: f64, yaw: f32, pitch: f32) -> Vec<u8> {
    let mut b = Vec::with_capacity(42);
    put_u8(&mut b, 13);
    put_f64(&mut b, x);
    put_f64(&mut b, y + 1.62f32 as f64);
    put_f64(&mut b, y);
    put_f64(&mut b, z);
    put_f32(&mut b, yaw);
    put_f32(&mut b, pitch);
    b.push(0);
    b
}

/// Block change (id 53).
pub fn pkt_block_change(x: i32, y: i32, z: i32, block_type: u8, meta: u8) -> Vec<u8> {
    let mut b = Vec::with_capacity(12);
    put_u8(&mut b, 53);
    crate::network::put_i32(&mut b, x);
    put_i8(&mut b, y as i8);
    crate::network::put_i32(&mut b, z);
    put_u8(&mut b, block_type);
    put_u8(&mut b, meta);
    b
}

fn put_slot(buf: &mut Vec<u8>, s: Option<FfiItemStack>) {
    match s {
        Some(v) if v.stack_size > 0 => {
            put_i16(buf, v.item_id as i16);
            put_i8(buf, v.stack_size as i8);
            put_i16(buf, v.item_damage as i16);
        }
        _ => {
            put_i16(buf, -1);
            put_i8(buf, 0);
            put_i16(buf, 0);
        }
    }
}

/// Inventory section (id 5, negative types like C++).
pub fn pkt_inventory_section(inv_type: i32, slots: &[Option<FfiItemStack>]) -> Vec<u8> {
    let mut b = Vec::with_capacity(7 + slots.len() * 5);
    put_u8(&mut b, 5);
    crate::network::put_i32(&mut b, inv_type);
    put_i16(&mut b, slots.len() as i16);
    for s in slots {
        put_slot(&mut b, *s);
    }
    b
}

/// Tile entity (id 59, gzipped NBT).
pub fn pkt_tile_entity(x: i32, y: i32, z: i32, nbt_gz: &[u8]) -> Vec<u8> {
    let mut b = Vec::with_capacity(13 + nbt_gz.len());
    put_u8(&mut b, 59);
    crate::network::put_i32(&mut b, x);
    put_i16(&mut b, y as i16);
    crate::network::put_i32(&mut b, z);
    put_i16(&mut b, nbt_gz.len() as i16);
    b.extend_from_slice(nbt_gz);
    b
}

/// Respawn (id 9, empty).
pub fn pkt_respawn() -> Vec<u8> {
    vec![9]
}

/// Keep-alive (id 0).
pub fn pkt_keepalive() -> Vec<u8> {
    vec![0]
}

/// Arm animation (id 18).
pub fn pkt_arm(entity_id: i32, animate: i8) -> Vec<u8> {
    let mut b = Vec::with_capacity(6);
    put_u8(&mut b, 18);
    crate::network::put_i32(&mut b, entity_id);
    put_i8(&mut b, animate);
    b
}

/// Pre-chunk (id 50): int x, int z, byte mode — mirrors
/// `RustPackets::preChunk` (same field order as the network encoder).
pub fn pkt_pre_chunk(x: i32, z: i32, mode: bool) -> Vec<u8> {
    let mut b = Vec::with_capacity(10);
    put_u8(&mut b, 50);
    crate::network::put_i32(&mut b, x);
    crate::network::put_i32(&mut b, z);
    put_u8(&mut b, if mode { 1 } else { 0 });
    b
}

/// Map chunk (id 51): int x, short y, int z, size bytes minus one, int
/// payload length, bytes — mirrors `NetServerHandler::sendMapChunk`.
/// Full chunks send `(px * 16, 0, pz * 16, 16, 128, 16, payload)`.
pub fn pkt_map_chunk(
    x: i32,
    y: i32,
    z: i32,
    size_x: i32,
    size_y: i32,
    size_z: i32,
    data: &[u8],
) -> Vec<u8> {
    let mut b = Vec::with_capacity(18 + data.len());
    put_u8(&mut b, 51);
    crate::network::put_i32(&mut b, x);
    put_i16(&mut b, y as i16);
    crate::network::put_i32(&mut b, z);
    put_u8(&mut b, (size_x - 1) as u8);
    put_u8(&mut b, (size_y - 1) as u8);
    put_u8(&mut b, (size_z - 1) as u8);
    crate::network::put_i32(&mut b, data.len() as i32);
    b.extend_from_slice(data);
    b
}

// ---- session context (server-owned services) ----

/// Cross-session events for the server tick (fan-out, saves).
pub enum SessionBroadcast {
    Chat(String),
    ArmSwing(EntityId),
    TileChanged(i32, i32, i32),
}

/// Server services borrowed by one pump call (tests fake this; the
/// server slice owns the real one).
pub struct SessionCtx<'a> {
    pub world: &'a mut World,
    pub ops: &'a HashSet<String>,
    pub spawn_protection: i32,
    pub pvp: bool,
    pub broadcast: &'a mut Vec<SessionBroadcast>,
}

/// Terminal session outcome.
pub enum SessionOutcome {
    Kick(String),
    Gone,
}

// ---- use/chat bridge (session-scoped driver shims) ----

#[derive(Clone, Copy)]
struct UseCtx {
    world: *mut World,
    player: EntityId,
    session: *mut PlaySession,
    ops: *const HashSet<String>,
}

thread_local! {
    static USE_CTX: std::cell::Cell<UseCtx> = std::cell::Cell::new(UseCtx {
        world: std::ptr::null_mut(),
        player: -1,
        session: std::ptr::null_mut(),
        ops: std::ptr::null(),
    });
}

fn with_use_ctx<T>(f: impl FnOnce(&mut World, EntityId, &mut PlaySession, &HashSet<String>) -> T, dflt: T) -> T {
    USE_CTX.with(|c| unsafe {
        let ctx = c.get();
        if ctx.world.is_null() || ctx.session.is_null() || ctx.ops.is_null() {
            return dflt;
        }
        f(&mut *ctx.world, ctx.player, &mut *ctx.session, &*ctx.ops)
    })
}

struct UseGuard;
impl UseGuard {
    fn enter(world: *mut World, player: EntityId, session: *mut PlaySession, ops: *const HashSet<String>) -> Self {
        USE_CTX.with(|c| {
            c.set(UseCtx { world, player, session, ops });
        });
        UseGuard
    }
}
impl Drop for UseGuard {
    fn drop(&mut self) {
        USE_CTX.with(|c| {
            c.set(UseCtx {
                world: std::ptr::null_mut(),
                player: -1,
                session: std::ptr::null_mut(),
                ops: std::ptr::null(),
            });
        });
    }
}

extern "C" fn use_next_int(bound: i32) -> i32 {
    if bound <= 0 {
        return 0;
    }
    with_use_ctx(|w, _, _, _| w.rng_next_int(bound), 0)
}

extern "C" fn use_next_f64() -> f64 {
    with_use_ctx(|w, _, _, _| w.rng_next_f64(), 0.0)
}

extern "C" fn use_get_id(x: i32, y: i32, z: i32) -> u8 {
    with_use_ctx(|w, _, _, _| w.get_block_id(x, y, z), 0)
}

extern "C" fn use_set_notify(x: i32, y: i32, z: i32, id: u8) -> bool {
    with_use_ctx(|w, _, _, _| w.apply_set_notify(x, y, z, id), false)
}

extern "C" fn use_set_meta_notify(x: i32, y: i32, z: i32, id: u8, meta: u8) -> bool {
    with_use_ctx(|w, _, _, _| w.apply_set_meta_notify(x, y, z, id, meta), false)
}

extern "C" fn use_set_quiet(x: i32, y: i32, z: i32, id: u8) -> bool {
    // setBlockWithNotifyNoClientUpdate == notify path natively (mark is a no-op).
    with_use_ctx(|w, _, _, _| w.apply_set_notify(x, y, z, id), false)
}

extern "C" fn use_set_meta(x: i32, y: i32, z: i32, meta: u8) {
    with_use_ctx(|w, _, _, _| w.set_block_meta(x, y, z, meta), false);
}

extern "C" fn use_does_attach(x: i32, y: i32, z: i32) -> bool {
    with_use_ctx(|w, _, _, _| w.block_allows_attachment(x, y, z), false)
}

extern "C" fn use_mat_burning(x: i32, y: i32, z: i32) -> bool {
    with_use_ctx(
        |w, _, _, _| {
            crate::world::material_of(crate::block::alpha_block_properties_get(
                w.get_block_id(x, y, z) as u32,
            ).material)
            .get_burning()
        },
        false,
    )
}

extern "C" fn use_mat_solid(x: i32, y: i32, z: i32) -> bool {
    with_use_ctx(|w, _, _, _| w.material_at(x, y, z).is_solid(), false)
}

extern "C" fn use_collidable(x: i32, y: i32, z: i32) -> bool {
    with_use_ctx(
        |w, _, _, _| {
            let bid = w.get_block_id(x, y, z);
            let props = crate::block::alpha_block_properties_get(bid as u32);
            bid != 0
                && props.block_type != crate::block::BlockType::Fluid as u8
                && crate::world::has_collision_box(props.block_type)
        },
        false,
    )
}

extern "C" fn use_can_stay(id: u8, x: i32, y: i32, z: i32) -> bool {
    // canBlockStay drivers under the tick table (base rule is true).
    with_use_ctx(
        |w, _, _, _| {
            crate::world::with_tick_bridge(w as *mut World, || unsafe {
                let t = crate::world::tick_table_ref();
                match id {
                    37 | 38 | 31 => crate::block_ticks::block_flower_can_stay(t, x, y, z),
                    39 | 40 => crate::block_ticks::block_mushroom_can_stay(t, x, y, z),
                    50 => crate::block_ticks::block_torch_can_stay(t, x, y, z),
                    81 => crate::block_ticks::block_cactus_can_stay(t, x, y, z),
                    83 => crate::block_ticks::block_reed_can_stay(t, x, y, z),
                    6 => crate::block_ticks::block_sapling_can_stay(t, x, y, z),
                    59 => crate::block_ticks::block_crops_can_stay(t, id, x, y, z),
                    _ => true,
                }
            })
        },
        false,
    )
}

extern "C" fn use_placement_clear(id: u8, x: i32, y: i32, z: i32) -> bool {
    // mirrors isPlacementVolumeClear: no live boat/living intersecting.
    with_use_ctx(
        |w, _, _, _| {
            let props = crate::block::alpha_block_properties_get(id as u32);
            if !crate::world::has_collision_box(props.block_type) {
                return true;
            }
            let mask = crate::aabb::AxisAlignedBB::get_bounding_box(
                x as f64 + props.min_x as f64,
                y as f64 + props.min_y as f64,
                z as f64 + props.min_z as f64,
                x as f64 + props.max_x as f64,
                y as f64 + props.max_y as f64,
                z as f64 + props.max_z as f64,
            );
            for oid in w.entities.alive_ids() {
                let blocks = match w.entities.get(oid) {
                    Some(Entity::Boat(_)) | Some(Entity::Mob(_)) | Some(Entity::Animal(_))
                    | Some(Entity::Player(_)) => true,
                    _ => false,
                };
                if blocks {
                    if let Some(e) = w.entities.get(oid) {
                        if mask.intersects_with(&e.body().bounding_box) {
                            return false;
                        }
                    }
                }
            }
            true
        },
        false,
    )
}

extern "C" fn use_block_placed(id: u8, x: i32, y: i32, z: i32, side: i32) {
    // Torch facing like onBlockPlaced (tiles already exist via added).
    if id != 50 {
        return;
    }
    with_use_ctx(
        |w, _, _, _| {
            crate::world::with_tick_bridge(w as *mut World, || unsafe {
                let meta = crate::block_ticks::block_torch_attach_meta(
                    crate::world::tick_table_ref(),
                    side,
                    x,
                    y,
                    z,
                );
                w.set_block_id(x, y, z, id);
                w.set_block_meta(x, y, z, meta);
            })
        },
        (),
    );
}

extern "C" fn use_have_block(id: u8) -> bool {
    with_use_ctx(|_, _, _, _| crate::world::World::native_registered(id), false)
}

extern "C" fn use_spawn_item(
    item_id: i32,
    count: i32,
    damage: i32,
    fx: f64,
    fy: f64,
    fz: f64,
    mx: f64,
    my: f64,
    mz: f64,
) {
    with_use_ctx(
        |w, _, _, _| {
            let eid = w.spawn_item_entity(item_id, count, damage, fx, fy, fz);
            if let Some(Entity::Item(e)) = w.entities.get_mut(eid) {
                e.body.motion = [mx, my, mz];
            }
        },
        (),
    );
}

extern "C" fn use_send_te(x: i32, y: i32, z: i32) {
    with_use_ctx(
        |w, _, sess, _| {
            if let Some(tile) = w.tiles.get(&(x, y, z)) {
                sess.outbox.push(tile_packet(x, y, z, tile));
            }
        },
        (),
    );
}

extern "C" fn use_ray_trace(
    sx: f64,
    sy: f64,
    sz: f64,
    ex: f64,
    ey: f64,
    ez: f64,
    out_x: *mut i32,
    out_y: *mut i32,
    out_z: *mut i32,
) -> bool {
    if out_x.is_null() || out_y.is_null() || out_z.is_null() {
        return false;
    }
    with_use_ctx(
        |w, _, _, _| match w.ray_trace_hit_liquids([sx, sy, sz], [ex, ey, ez]) {
            Some([x, y, z]) => unsafe {
                *out_x = x;
                *out_y = y;
                *out_z = z;
                true
            },
            None => false,
        },
        false,
    )
}

static USE_TABLE: ItemUseWorld = ItemUseWorld {
    next_int: Some(use_next_int),
    next_f64_01: Some(use_next_f64),
    get_block_id: Some(use_get_id),
    set_block_notify: Some(use_set_notify),
    set_block_meta_notify: Some(use_set_meta_notify),
    set_block_quiet: Some(use_set_quiet),
    set_block_meta: Some(use_set_meta),
    does_attach: Some(use_does_attach),
    material_burning: Some(use_mat_burning),
    material_solid: Some(use_mat_solid),
    collidable_box: Some(use_collidable),
    block_can_stay: Some(use_can_stay),
    placement_clear: Some(use_placement_clear),
    block_placed: Some(use_block_placed),
    have_block: Some(use_have_block),
    spawn_item: Some(use_spawn_item),
    send_te_packet: Some(use_send_te),
    ray_trace: Some(use_ray_trace),
};

extern "C" fn chat_is_op() -> bool {
    with_use_ctx(
        |w, pid, _, ops| match w.entities.get(pid) {
            // Ops store lowercased; the query lowercases like C++ isOp.
            Some(Entity::Player(p)) => ops.contains(&p.username.to_ascii_lowercase()),
            _ => false,
        },
        false,
    )
}

extern "C" fn chat_registered(id: i32) -> bool {
    (0..256).contains(&id) && crate::world::World::native_registered(id as u8)
}

extern "C" fn chat_give(item_id: i32, count: i32, damage: i32) {
    with_use_ctx(
        |w, pid, _, _| {
            if let Some(e) = w.entities.get(pid) {
                let (px, py, pz) = (e.body().pos[0], e.body().pos[1], e.body().pos[2]);
                w.spawn_item_entity(item_id, count, damage, px, py, pz);
            }
        },
        (),
    );
}

extern "C" fn chat_teleport(x: f64, y: f64, z: f64, yaw: f32, pitch: f32) {
    with_use_ctx(
        |w, pid, sess, _| {
            sess.teleport_to(w, pid, x, y, z, yaw, pitch);
        },
        (),
    );
}

extern "C" fn chat_send(msg_ptr: *const u8, msg_len: usize) {
    if msg_ptr.is_null() {
        return;
    }
    let msg = unsafe { std::slice::from_raw_parts(msg_ptr, msg_len) };
    let text = String::from_utf8_lossy(msg).to_string();
    with_use_ctx(|_, _, sess, _| sess.outbox.push(pkt_chat(&text)), ());
}

static CHAT_TABLE: ChatWorld = ChatWorld {
    is_op: Some(chat_is_op),
    block_registered: Some(chat_registered),
    give_item: Some(chat_give),
    teleport: Some(chat_teleport),
    send_chat: Some(chat_send),
};

/// Tile-entity packet (id 59, gzipped NBT like sendTileEntityPacket).
pub fn tile_packet(x: i32, y: i32, z: i32, tile: &TileData) -> Vec<u8> {
    use crate::nbt::write_root;
    use std::io::Write as _;
    // Root is the tile compound itself, like C++ writeRoot.
    let comp = crate::persist::tile_nbt(x, y, z, tile);
    let mut bytes = Vec::new();
    write_root(&mut bytes, "", &comp).ok();
    let mut enc = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::default());
    enc.write_all(&bytes).ok();
    let gz = enc.finish().ok().unwrap_or_default();
    pkt_tile_entity(x, y, z, &gz)
}

impl PlaySession {
    /// Write a mutated stack into the real current slot (air-use path).
    fn write_back_current(&mut self, ctx: &mut SessionCtx, s: FfiItemStack) {
        let cur = match ctx.world.entities.get(self.player) {
            Some(Entity::Player(p)) => p.inventory.current,
            _ => return,
        };
        if cur < 0 || cur >= 36 {
            return;
        }
        if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(self.player) {
            p.inventory.main[cur as usize] =
                if s.stack_size > 0 && s.item_id > 0 { Some(s) } else { None };
        }
    }
}

impl PlaySession {
    // ---- entity interaction (mirrors handleUseEntity) ----

    fn use_entity(
        &mut self,
        ctx: &mut SessionCtx,
        player_entity_id: i32,
        target_entity_id: i32,
        is_left_click: bool,
    ) -> Option<SessionOutcome> {
        let me = self.player;
        if player_entity_id != me {
            return None;
        }
        self.sync_held(ctx.world);
        let target = match ctx.world.entities.get(target_entity_id) {
            Some(t) if target_entity_id != me && !t.body().dead => target_entity_id,
            _ => return None,
        };
        if matches!(ctx.world.entities.get(target), Some(Entity::Arrow(_))) {
            return None;
        }
        if let Some(Entity::Player(_)) = ctx.world.entities.get(target) {
            if !ctx.pvp {
                return None;
            }
        }
        // Reach: attacker eye to target box within 25.
        let eye = match ctx.world.entities.get(me) {
            Some(e) => {
                let b = e.body();
                [b.pos[0], b.pos[1] + World::living_eye_height_pub(e), b.pos[2]]
            }
            None => return None,
        };
        let tb = match ctx.world.entities.get(target) {
            Some(e) => e.body().clone(),
            None => return None,
        };
        let cx = eye[0].clamp(tb.bounding_box.min_x, tb.bounding_box.max_x);
        let cy = eye[1].clamp(tb.bounding_box.min_y, tb.bounding_box.max_y);
        let cz = eye[2].clamp(tb.bounding_box.min_z, tb.bounding_box.max_z);
        if (eye[0] - cx).powi(2) + (eye[1] - cy).powi(2) + (eye[2] - cz).powi(2) > ATTACK_REACH_SQ
        {
            return None;
        }
        if !ctx.world.attack_los(me, target) {
            return None;
        }
        if !is_left_click {
            // Cow milking with a held bucket.
            if let Some(Entity::Animal(a)) = ctx.world.entities.get(target) {
                if a.kind == AnimalKind::Cow {
                    let cur = match ctx.world.entities.get(me) {
                        Some(Entity::Player(p)) => p.inventory.current,
                        _ => -1,
                    };
                    let bucket = cur >= 0
                        && cur < 36
                        && matches!(
                            ctx.world.entities.get(me),
                            Some(Entity::Player(p)) if p.inventory.main[cur as usize]
                                .map(|s| s.item_id == 325)
                                .unwrap_or(false)
                        );
                    if bucket {
                        if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(me) {
                            p.inventory.main[cur as usize] = Some(FfiItemStack {
                                stack_size: 1,
                                animations_to_go: 0,
                                item_id: 335,
                                item_damage: 0,
                            });
                        }
                        self.send_inventory(ctx.world);
                        return None;
                    }
                }
            }
            // Boat mount toggle with rider rules.
            if matches!(ctx.world.entities.get(target), Some(Entity::Boat(_))) {
                let rider =
                    ctx.world.entities.get(target).map(|e| e.body().ridden_by).unwrap_or(-1);
                if rider >= 0 && rider != me {
                    let rider_is_player =
                        matches!(ctx.world.entities.get(rider), Some(Entity::Player(_)));
                    if rider_is_player {
                        return None;
                    }
                    ctx.world.entities.mount(rider, None);
                }
                let riding =
                    ctx.world.entities.get(me).map(|e| e.body().riding).unwrap_or(-1);
                ctx.world.entities.mount(me, if riding == target { None } else { Some(target) });
                ctx.world.entities.update_rider_position(target);
                return None;
            }
            return None;
        }
        // Left click: melee with the selected stack.
        let alive = match ctx.world.entities.get(me) {
            Some(Entity::Player(p)) => !p.living.body.dead && p.living.health > 0,
            _ => false,
        };
        if !alive {
            return None;
        }
        let held = self.selected_stack(ctx.world);
        let damage = held.map(|s| alpha_combat_get_weapon_damage(s.item_id).max(1)).unwrap_or(1);
        if damage <= 0 {
            return None;
        }
        ctx.world.attack_living(target, damage, Some(me));
        // Tool wear only against living targets.
        let living_target = matches!(
            ctx.world.entities.get(target),
            Some(Entity::Mob(_)) | Some(Entity::Animal(_)) | Some(Entity::Player(_))
        );
        if let Some(mut s) = held {
            if living_target {
                let kind = alpha_item_tool_kind(s.item_id);
                let wear = if kind == crate::item_data::ItemToolKind::Pickaxe as i32
                    || kind == crate::item_data::ItemToolKind::Spade as i32
                    || kind == crate::item_data::ItemToolKind::Axe as i32
                {
                    2
                } else if kind == crate::item_data::ItemToolKind::Sword as i32 {
                    1
                } else {
                    0
                };
                if wear > 0 {
                    let max = alpha_item_max_damage(s.item_id);
                    crate::inventory::item_stack_damage(&mut s as *mut _, wear, max);
                    if s.stack_size <= 0 {
                        let is_fallback = match ctx.world.entities.get(me) {
                            Some(Entity::Player(p)) => p.inventory.current == 35,
                            _ => false,
                        };
                        if is_fallback {
                            self.held_fallback = None;
                            self.held_id = 0;
                            if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(me) {
                                p.inventory.current = 0;
                            }
                        } else {
                            let cur = match ctx.world.entities.get(me) {
                                Some(Entity::Player(p)) => p.inventory.current,
                                _ => -1,
                            };
                            if cur >= 0 && cur < 36 {
                                if let Some(Entity::Player(p)) =
                                    ctx.world.entities.get_mut(me)
                                {
                                    p.inventory.main[cur as usize] = None;
                                }
                            }
                        }
                        self.send_inventory(ctx.world);
                    } else {
                        let cur = match ctx.world.entities.get(me) {
                            Some(Entity::Player(p)) => p.inventory.current,
                            _ => -1,
                        };
                        if cur >= 0 && cur < 35 {
                            if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(me) {
                                p.inventory.main[cur as usize] = Some(s);
                            }
                        } else if cur == 35 {
                            self.held_fallback = Some(s);
                        }
                    }
                }
            }
        }
        None
    }

    // ---- chat / commands / respawn / misc ----

    fn chat(&mut self, ctx: &mut SessionCtx, message: &str) -> Option<SessionOutcome> {
        // Byte-truncate at 100 like C++ substr (on a char boundary so the
        // String stays valid).
        let mut msg = message.to_string();
        while msg.len() > 100 {
            msg.pop();
        }
        if msg.starts_with('/') {
            let yaw = ctx.world.entities.get(self.player).map(|e| e.body().yaw).unwrap_or(0.0);
            let pitch =
                ctx.world.entities.get(self.player).map(|e| e.body().pitch).unwrap_or(0.0);
            let _guard = UseGuard::enter(
                ctx.world as *mut World,
                self.player,
                self as *mut PlaySession,
                ctx.ops as *const HashSet<String>,
            );
            unsafe {
                rust_chat_command(&CHAT_TABLE, msg.as_ptr(), msg.len(), yaw, pitch);
            }
            return None;
        }
        let username = self.username(ctx.world).to_string();
        ctx.broadcast.push(SessionBroadcast::Chat(format!("<{username}> {msg}")));
        None
    }

    fn respawn(&mut self, ctx: &mut SessionCtx) -> Option<SessionOutcome> {
        let me = self.player;
        let alive = match ctx.world.entities.get(me) {
            Some(Entity::Player(p)) => p.living.health > 0,
            _ => return None,
        };
        if alive {
            return None;
        }
        let sp = ctx.world.spawn;
        if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(me) {
            p.living.body.dead = false;
            p.living.health = p.living.max_health;
            p.living.hurt_time = 0;
            p.living.death_time = 0;
            p.living.body.fire = 0;
            p.living.body.air = 300;
            p.living.body.fall_distance = 0.0;
            p.living.body.motion = [0.0; 3];
            p.respawn_ticks = 60;
        }
        let (sx, sy, sz) = (sp[0] as f64 + 0.5, sp[1] as f64, sp[2] as f64 + 0.5);
        if let Some(e) = ctx.world.entities.get_mut(me) {
            e.body_mut().set_position(sx, sy, sz);
            e.body_mut().yaw = 0.0;
            e.body_mut().pitch = 0.0;
        }
        self.last = [sx, sy, sz];
        self.has_moved = false;
        // Packet order mirrors C++: respawn, health, teleport, inventory.
        self.outbox.push(pkt_respawn());
        self.outbox.push(pkt_teleport(sx, sy, sz, 0.0, 0.0));
        self.outbox.push(pkt_health(match ctx.world.entities.get(me) {
            Some(Entity::Player(p)) => p.living.health as i8,
            _ => 0,
        }));
        self.send_inventory(ctx.world);
        None
    }

    fn held_switch(&mut self, ctx: &mut SessionCtx, item_id: i16) {
        if item_id == 0 {
            self.held_id = 0;
            self.held_fallback = None;
            if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(self.player) {
                p.inventory.current = 0;
            }
            return;
        }
        self.held_id = item_id as i32;
        self.sync_held(ctx.world);
    }

    fn arm(&mut self, ctx: &mut SessionCtx, animate: i8) {
        if animate == 1 {
            ctx.broadcast.push(SessionBroadcast::ArmSwing(self.player));
        } else if animate == 104 {
            if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(self.player) {
                p.living.sneaking = true;
            }
        } else if animate == 105 {
            if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(self.player) {
                p.living.sneaking = false;
            }
        }
    }

    fn apply_inventory(
        &mut self,
        ctx: &mut SessionCtx,
        inv_type: i32,
        slots: &[crate::network::FfiSlotData],
    ) {
        fn apply(bank: &mut [Option<FfiItemStack>], slots: &[crate::network::FfiSlotData], skip: usize) {
            let n = slots.len().min(bank.len());
            for i in 0..n {
                if i == skip {
                    continue;
                }
                let id = slots[i].item_id as i32;
                if id >= 0 && id < 32000 {
                    let dmg = if crate::item_data::alpha_item_max_damage(id) > 0 {
                        slots[i].damage as i32
                    } else {
                        0
                    };
                    let count = slots[i].count as i32;
                    bank[i] = if count > 0 {
                        Some(FfiItemStack {
                            stack_size: count,
                            animations_to_go: 0,
                            item_id: id,
                            item_damage: dmg,
                        })
                    } else {
                        None
                    };
                } else {
                    bank[i] = None;
                }
            }
        }
        let me = self.player;
        if inv_type == -1 {
            if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(me) {
                apply(&mut p.inventory.main, slots, 35);
            }
            if self.held_id > 0 {
                let mut found = false;
                if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(me) {
                    for i in 0..35 {
                        if p.inventory.main[i].map(|s| s.item_id) == Some(self.held_id) {
                            p.inventory.current = i as i32;
                            found = true;
                            break;
                        }
                    }
                    if found {
                        p.inventory.main[35] = None;
                    }
                }
                if found {
                    self.held_fallback = None;
                } else {
                    self.held_id = 0;
                    self.held_fallback = None;
                    if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(me) {
                        p.inventory.main[35] = None;
                    }
                }
            }
        } else if inv_type == -2 {
            if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(me) {
                apply(&mut p.inventory.crafting, slots, usize::MAX);
            }
        } else if inv_type == -3 {
            if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(me) {
                apply(&mut p.inventory.armor, slots, usize::MAX);
            }
        }
    }

    fn complex_entity(
        &mut self,
        ctx: &mut SessionCtx,
        x: i32,
        y: i32,
        z: i32,
        nbt_gz: &[u8],
    ) {
        use crate::nbt::{NbtTag, read_root};
        use std::io::Read as _;
        if nbt_gz.is_empty() || nbt_gz.len() > 65536 {
            return;
        }
        let mut dec = flate2::read::GzDecoder::new(nbt_gz);
        let mut raw = Vec::new();
        if dec.read_to_end(&mut raw).is_err() || raw.len() > 524288 || raw.is_empty() {
            return;
        }
        let mut cursor = std::io::Cursor::new(raw);
        let nbt = match read_root(&mut cursor) {
            Ok((_, root)) => root,
            Err(_) => return,
        };
        let (nx, ny, nz) = (
            match nbt.map.get("x") {
                Some(NbtTag::Int(v)) => *v,
                _ => return,
            },
            match nbt.map.get("y") {
                Some(NbtTag::Int(v)) => *v,
                _ => return,
            },
            match nbt.map.get("z") {
                Some(NbtTag::Int(v)) => *v,
                _ => return,
            },
        );
        if nx != x || ny != y || nz != z {
            return;
        }
        let tile = match ctx.world.tiles.get(&(x, y, z)) {
            Some(t) => *t,
            None => return,
        };
        match tile {
            TileData::Sign(mut s) => {
                for i in 0..4 {
                    if let Some(NbtTag::String(text)) = nbt.map.get(&format!("Text{}", i + 1)) {
                        let bytes = text.as_bytes();
                        let len = bytes.len().min(15);
                        s.lines[i] = [0u8; 16];
                        s.lines[i][..len].copy_from_slice(&bytes[..len]);
                    }
                }
                ctx.world.tiles.insert((x, y, z), TileData::Sign(s));
            }
            TileData::Furnace(mut s) => {
                if let Some(NbtTag::Short(v)) = nbt.map.get("BurnTime") {
                    s.burn_time = *v;
                }
                if let Some(NbtTag::Short(v)) = nbt.map.get("CookTime") {
                    s.cook_time = *v;
                }
                if let Some(NbtTag::Short(v)) = nbt.map.get("ItemBurnTime") {
                    s.current_item_burn_time = *v;
                }
                if let Some(NbtTag::List(l)) = nbt.map.get("Items") {
                    for elem in &l.elements {
                        if let NbtTag::Compound(im) = elem {
                            let slot = match im.map.get("Slot") {
                                Some(NbtTag::Byte(b)) => *b as usize,
                                _ => continue,
                            };
                            if slot < s.slots.len() {
                                s.slots[slot] = crate::persist::read_stack(&im.map);
                            }
                        }
                    }
                }
                ctx.world.tiles.insert((x, y, z), TileData::Furnace(s));
            }
            TileData::Chest(mut s) => {
                if let Some(NbtTag::List(l)) = nbt.map.get("Items") {
                    for elem in &l.elements {
                        if let NbtTag::Compound(im) = elem {
                            let slot = match im.map.get("Slot") {
                                Some(NbtTag::Byte(b)) => *b as usize,
                                _ => continue,
                            };
                            if slot < s.slots.len() {
                                s.slots[slot] = crate::persist::read_stack(&im.map);
                            }
                        }
                    }
                }
                ctx.world.tiles.insert((x, y, z), TileData::Chest(s));
            }
        }
        ctx.broadcast.push(SessionBroadcast::TileChanged(x, y, z));
        ctx.world.mark_chunk_modified(x.div_euclid(16), z.div_euclid(16));
    }
}

impl PlaySession {
    // ---- placement (mirrors handlePlace + activeBlockOrUseItem) ----

    fn place(
        &mut self,
        ctx: &mut SessionCtx,
        item_id: i16,
        x: i32,
        y: i32,
        z: i32,
        direction: i8,
    ) -> Option<SessionOutcome> {
        let me = self.player;
        if direction == -1 {
            // Right-click in air: use the held item.
            let held = self.selected_stack(ctx.world);
            if let Some(s) = held {
                self.use_item_air(ctx, s);
            }
            return None;
        }
        if y < 0 || y >= crate::world::WORLD_HEIGHT {
            return None;
        }
        let dir = (direction as u8) as i32;
        let prot = {
            let sp = ctx.world.spawn;
            (x - sp[0]).abs().max((z - sp[2]).abs()) <= ctx.spawn_protection
        };
        if prot && !self.is_op(ctx) {
            self.send_block_change(ctx.world, x, y, z);
            return None;
        }
        // Chest/furnace contents first (client opens the GUI early).
        let clicked = ctx.world.get_block_id(x, y, z);
        if matches!(clicked, 54 | 61 | 62) {
            self.send_tile(ctx.world, x, y, z);
        }
        // Find the stack: selected if it matches, else first main match.
        let mut stack: Option<FfiItemStack> = None;
        let mut stack_slot: Option<usize> = None;
        if item_id >= 0 {
            if let Some(s) = self.selected_stack(ctx.world) {
                if s.item_id == item_id as i32 && s.stack_size > 0 {
                    stack = Some(s);
                    stack_slot = match ctx.world.entities.get(me) {
                        Some(Entity::Player(p)) => {
                            let cur = p.inventory.current;
                            (cur >= 0 && cur < 36).then_some(cur as usize)
                        }
                        _ => None,
                    };
                }
            }
            if stack.is_none() {
                if let Some(Entity::Player(p)) = ctx.world.entities.get(me) {
                    for (i, slot) in p.inventory.main.iter().enumerate() {
                        if let Some(s) = slot {
                            if s.item_id == item_id as i32 && s.stack_size > 0 {
                                stack = Some(*s);
                                stack_slot = Some(i);
                                break;
                            }
                        }
                    }
                }
            }
        }
        if let Some(mut s) = stack {
            let water = ctx.world.material_at(x, y, z) == crate::material::Material::WATER;
            if s.item_id == 333 && water {
                // Boat on water: spawn + consume, like C++.
                let bid = ctx.world.entities.alloc_id();
                let mut b = crate::entity_table::Body::new(bid, 1.5, 0.6, 0.3);
                b.set_position(x as f64 + 0.5, y as f64 + 1.5, z as f64 + 0.5);
                ctx.world.entities.insert(Entity::Boat(crate::entity_table::BoatEnt {
                    body: b,
                    time_since_hit: 0,
                    damage_taken: 0,
                    forward_dir: 1,
                }));
                if s.stack_size > 0 {
                    s.stack_size -= 1;
                }
                self.write_stack_slot(ctx, stack_slot, s);
            } else if matches!(clicked, 54 | 58 | 61 | 62) {
                // GUI blocks swallow the click (chest/furnace already synced).
                let _ = self.activated_block(ctx, clicked, x, y, z);
            } else {
                let used_first = self.active_block_or_use(ctx, &mut s, x, y, z, dir);
                if !used_first && s.item_id == 333 {
                    // Boat fallback: right-click throw on blocks.
                    self.use_item_air(ctx, s);
                } else {
                    self.write_stack_slot(ctx, stack_slot, s);
                }
                let _ = used_first;
            }
        }
        // Drop emptied stacks like C++.
        if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(me) {
            for slot in p.inventory.main.iter_mut() {
                if let Some(s) = slot {
                    if s.stack_size <= 0 {
                        *slot = None;
                    }
                }
            }
        }
        self.send_inventory(ctx.world);
        // Rollback views at both cells.
        self.send_block_change(ctx.world, x, y, z);
        let (nx, ny, nz) = match dir {
            0 => (x, y - 1, z),
            1 => (x, y + 1, z),
            2 => (x, y, z - 1),
            3 => (x, y, z + 1),
            4 => (x - 1, y, z),
            5 => (x + 1, y, z),
            _ => (x, y, z),
        };
        self.send_block_change(ctx.world, nx, ny, nz);
        None
    }

    /// Write a mutated held stack back to its slot (or the fallback copy).
    fn write_stack_slot(&mut self, ctx: &mut SessionCtx, slot: Option<usize>, s: FfiItemStack) {
        let live = s.stack_size > 0 && s.item_id > 0;
        let is_fallback = match ctx.world.entities.get(self.player) {
            Some(Entity::Player(p)) => p.inventory.current == 35,
            _ => false,
        };
        if is_fallback {
            self.held_fallback = if live { Some(s) } else { None };
            return;
        }
        if let Some(i) = slot {
            if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(self.player) {
                if i < 36 {
                    p.inventory.main[i] = if live { Some(s) } else { None };
                }
            }
        }
    }

    /// GUI block activation (mirrors blockActivated for chest/furnace/
    /// workbench; the chest tile-removal line in C++ is a data-eating bug
    /// and is deliberately NOT mirrored).
    fn activated_block(&mut self, ctx: &mut SessionCtx, clicked: u8, x: i32, y: i32, z: i32) -> bool {
        match clicked {
            54 => {
                if ctx.world.is_solid(x, y + 1, z) {
                    return true;
                }
                for (dx, dz) in [(-1, 0), (1, 0), (0, -1), (0, 1)] {
                    if ctx.world.get_block_id(x + dx, y, z + dz) == 54
                        && ctx.world.is_solid(x + dx, y + 1, z + dz)
                    {
                        return true;
                    }
                }
                self.send_tile(ctx.world, x, y, z);
                true
            }
            61 | 62 => {
                self.send_tile(ctx.world, x, y, z);
                true
            }
            58 => true,
            _ => false,
        }
    }

    /// Item-on-block routing (mirrors `activeBlockOrUseItem` + the
    /// per-item `onItemUse` stack rules).
    fn active_block_or_use(
        &mut self,
        ctx: &mut SessionCtx,
        s: &mut FfiItemStack,
        x: i32,
        y: i32,
        z: i32,
        side: i32,
    ) -> bool {
        let clicked = ctx.world.get_block_id(x, y, z);
        if clicked > 0 && matches!(clicked, 54 | 58 | 61 | 62) {
            return self.activated_block(ctx, clicked, x, y, z);
        }
        if s.stack_size <= 0 {
            return false;
        }
        let me = self.player;
        let yaw = ctx.world.entities.get(me).map(|e| e.body().yaw).unwrap_or(0.0);
        let _guard = UseGuard::enter(
            ctx.world as *mut World,
            me,
            self as *mut PlaySession,
            ctx.ops as *const HashSet<String>,
        );
        let used = unsafe {
            match s.item_id {
                290..=294 => {
                    if !item_hoe_use(&USE_TABLE, 295, x, y, z) {
                        false
                    } else {
                        let max = alpha_item_max_damage(s.item_id);
                        crate::inventory::item_stack_damage(&mut *s as *mut _, 1, max);
                        true
                    }
                }
                295 => {
                    if side != 1 {
                        false
                    } else if !item_seeds_use(&USE_TABLE, x, y, z, side) {
                        false
                    } else {
                        if s.stack_size > 0 {
                            s.stack_size -= 1;
                        }
                        true
                    }
                }
                259 => {
                    let max = alpha_item_max_damage(s.item_id);
                    let mut out = FlintOut { placed: false, new_damage: 0, broke: false };
                    if !item_flint_use(&USE_TABLE, s.item_damage, max, x, y, z, side, &mut out) {
                        false
                    } else {
                        s.item_damage = out.new_damage;
                        if out.broke {
                            s.stack_size = 0;
                        }
                        true
                    }
                }
                323 => {
                    if !item_sign_use(&USE_TABLE, x, y, z, side, yaw) {
                        false
                    } else {
                        if s.stack_size > 0 {
                            s.stack_size -= 1;
                        }
                        true
                    }
                }
                333 => false,
                1..=255 => {
                    if !item_block_use(&USE_TABLE, s.item_id as u8, s.stack_size, x, y, z, side, yaw)
                    {
                        false
                    } else {
                        if s.stack_size > 0 {
                            s.stack_size -= 1;
                        }
                        true
                    }
                }
                _ => false,
            }
        };
        drop(_guard);
        used
    }

    /// Right-click in air (mirrors `useItem`: food bites with heal,
    /// soup to bowl, boats via aim+throw).
    fn use_item_air(&mut self, ctx: &mut SessionCtx, mut s: FfiItemStack) -> bool {
        let me = self.player;
        let heal = alpha_item_food_heal(s.item_id);
        if heal > 0 {
            let bite = alpha_item_food_bite(s.stack_size, heal);
            if s.item_id == 282 {
                s.item_id = 281;
                s.stack_size = 1;
                s.item_damage = 0;
            } else {
                s.stack_size = bite.new_count;
            }
            if bite.heal > 0 {
                if let Some(Entity::Player(p)) = ctx.world.entities.get_mut(me) {
                    p.living.health = crate::entity_living::alpha_living_heal(
                        p.living.health,
                        p.living.max_health,
                        bite.heal,
                        p.living.body.dead,
                    );
                }
                self.outbox.push(pkt_health(match ctx.world.entities.get(me) {
                    Some(Entity::Player(p)) => p.living.health as i8,
                    _ => 0,
                }));
            }
            self.write_back_current(ctx, s);
            self.send_inventory(ctx.world);
            return true;
        }
        if s.item_id == 333 {
            let (pyaw, ppitch, ppos, pyoff, prev_yaw, prev_pitch, prev_pos) =
                match ctx.world.entities.get(me) {
                    Some(e) => {
                        let b = e.body();
                        (b.yaw, b.pitch, b.pos, b.y_offset as f64, b.prev_yaw, b.prev_pitch, b.prev_pos)
                    }
                    None => return false,
                };
            let mut aim = BoatThrow {
                lx: 0.0, ly: 0.0, lz: 0.0, sx: 0.0, sy: 0.0, sz: 0.0, ex: 0.0, ey: 0.0, ez: 0.0,
            };
            if !unsafe {
                item_boat_aim(
                    prev_yaw, pyaw, prev_pitch, ppitch, prev_pos[0], ppos[0], prev_pos[1], ppos[1],
                    prev_pos[2], ppos[2], pyoff, &mut aim,
                )
            } {
                return false;
            }
            let _guard = UseGuard::enter(
                ctx.world as *mut World,
                me,
                self as *mut PlaySession,
                ctx.ops as *const HashSet<String>,
            );
            let (mut hx, mut hy, mut hz) = (0, 0, 0);
            let ok = unsafe {
                item_boat_throw(&USE_TABLE, aim.sx, aim.sy, aim.sz, aim.ex, aim.ey, aim.ez, &mut hx, &mut hy, &mut hz)
            };
            drop(_guard);
            if !ok {
                return false;
            }
            let bid = ctx.world.entities.alloc_id();
            let mut b = crate::entity_table::Body::new(bid, 1.5, 0.6, 0.3);
            b.set_position(hx as f64 + 0.5, hy as f64 + 1.5, hz as f64 + 0.5);
            ctx.world.entities.insert(Entity::Boat(crate::entity_table::BoatEnt {
                body: b,
                time_since_hit: 0,
                damage_taken: 0,
                forward_dir: 1,
            }));
            if s.stack_size > 0 {
                s.stack_size -= 1;
            }
            self.write_back_current(ctx, s);
            self.send_inventory(ctx.world);
            return true;
        }
        false
    }
}

#[cfg(test)]
mod play_tests {
    use super::*;
    use crate::entity_table::{AnimalEnt, AnimalKind, MobEnt, MobKind, PlayerEnt};
    use crate::world::World;

    fn floor_world() -> World {
        let mut w = World::new(7);
        let mut c = crate::chunk::Chunk::new(0, 0);
        for x in 0..16 {
            for z in 0..16 {
                c.set_block_id(x, 63, z, 1);
            }
        }
        c.generate_height_map();
        w.insert_chunk(c);
        w
    }

    fn spawn_player(w: &mut World, name: &str, x: f64, y: f64, z: f64) -> EntityId {
        let id = w.entities.alloc_id();
        let mut p = PlayerEnt::new(id, name);
        p.living.body.set_position(x, y, z);
        p.respawn_ticks = 0;
        w.entities.insert(Entity::Player(p));
        id
    }

    fn spawn_mob(w: &mut World, kind: MobKind, x: f64, y: f64, z: f64) -> EntityId {
        let id = w.entities.alloc_id();
        let mut m = MobEnt::new(id, kind);
        m.living.body.set_position(x, y, z);
        w.entities.insert(Entity::Mob(m));
        id
    }

    fn ctx<'a>(
        w: &'a mut World,
        ops: &'a HashSet<String>,
        bcast: &'a mut Vec<SessionBroadcast>,
    ) -> SessionCtx<'a> {
        SessionCtx { world: w, ops, spawn_protection: 0, pvp: true, broadcast: bcast }
    }

    fn no_ops() -> HashSet<String> {
        HashSet::new()
    }

    fn health_of(w: &World, id: EntityId) -> i16 {
        match w.entities.get(id).unwrap() {
            Entity::Mob(m) => m.living.health,
            Entity::Player(p) => p.living.health,
            _ => unreachable!(),
        }
    }

    #[test]
    fn test_walk_and_teleport_kick() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 4.5);
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        // One-block step east passes.
        let out = sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::PlayerLookMove {
                x: 4.5, y: 64.0, stance: 65.62, z: 4.5, yaw: 0.0, pitch: 0.0, on_ground: true,
            },
        );
        assert!(out.is_none());
        assert!((w.entities.get(player).unwrap().body().pos[0] - 4.5).abs() < 1e-6);
        // Cross-map jump is rejected with a kick.
        let out = sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::PlayerLookMove {
                x: 1000.0, y: 64.0, stance: 65.62, z: 1000.0, yaw: 0.0, pitch: 0.0,
                on_ground: true,
            },
        );
        assert!(matches!(out, Some(SessionOutcome::Kick(_))));
        assert_eq!(sess.outbox.last().unwrap()[0], 255);
    }

    #[test]
    fn test_dig_instant_torch() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 6.5);
        w.set_block_id(3, 64, 4, 50);
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::BlockDig { status: 0, x: 3, y: 64, z: 4, face: 1 },
        );
        assert_eq!(w.get_block_id(3, 64, 4), 0);
        assert!(w.entities.alive_ids().into_iter().any(|oid| matches!(
            w.entities.get(oid).unwrap(),
            Entity::Item(e) if e.item_id == 50
        )));
    }

    #[test]
    fn test_dig_dirt_progressive_with_hands() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 6.5);
        w.set_block_id(3, 64, 4, 3);
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        for _ in 0..500 {
            sess.pump(
                &mut ctx(&mut w, &ops, &mut bc),
                PacketData::BlockDig { status: 1, x: 3, y: 64, z: 4, face: 1 },
            );
            if w.get_block_id(3, 64, 4) == 0 {
                break;
            }
        }
        assert_eq!(w.get_block_id(3, 64, 4), 0);
        // Bare hands harvest dirt.
        assert!(w.entities.alive_ids().into_iter().any(|oid| matches!(
            w.entities.get(oid).unwrap(),
            Entity::Item(e) if e.item_id == 3
        )));
    }

    #[test]
    fn test_dig_stone_wears_pick() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 6.5);
        w.set_block_id(3, 64, 4, 1);
        if let Some(Entity::Player(p)) = w.entities.get_mut(player) {
            p.inventory.main[0] = Some(FfiItemStack {
                stack_size: 1, animations_to_go: 0, item_id: 270, item_damage: 0,
            });
        }
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        for _ in 0..3000 {
            sess.pump(
                &mut ctx(&mut w, &ops, &mut bc),
                PacketData::BlockDig { status: 1, x: 3, y: 64, z: 4, face: 1 },
            );
            if w.get_block_id(3, 64, 4) == 0 {
                break;
            }
        }
        assert_eq!(w.get_block_id(3, 64, 4), 0);
        let dmg = match w.entities.get(player).unwrap() {
            Entity::Player(p) => p.inventory.main[0].map(|s| s.item_damage),
            _ => unreachable!(),
        };
        assert!(dmg.unwrap_or(0) > 0, "stone pick should wear, dmg={dmg:?}");
    }

    #[test]
    fn test_dig_cancel_and_protection() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 6.5);
        w.set_block_id(3, 64, 4, 1);
        // Stone near spawn (3,4 vs spawn 0,64,0): protected at radius 16.
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        {
            let mut c = ctx(&mut w, &ops, &mut bc);
            c.spawn_protection = 16;
            sess.pump(&mut c, PacketData::BlockDig { status: 0, x: 3, y: 64, z: 4, face: 1 });
        }
        assert_eq!(w.get_block_id(3, 64, 4), 1);
        // Ops dig through.
        let mut ops_set = HashSet::new();
        ops_set.insert("steve".to_string());
        w.set_block_id(3, 64, 4, 50);
        {
            let mut c = SessionCtx {
                world: &mut w, ops: &ops_set, spawn_protection: 16, pvp: true, broadcast: &mut bc,
            };
            sess.pump(&mut c, PacketData::BlockDig { status: 0, x: 3, y: 64, z: 4, face: 1 });
        }
        assert_eq!(w.get_block_id(3, 64, 4), 0);
        // Cancel resets progress.
        w.set_block_id(5, 64, 5, 1);
        {
            let mut c = ctx(&mut w, &ops, &mut bc);
            sess.pump(&mut c, PacketData::BlockDig { status: 1, x: 5, y: 64, z: 5, face: 1 });
        }
        assert!(sess.dig.has_target);
        {
            let mut c = ctx(&mut w, &ops, &mut bc);
            sess.pump(&mut c, PacketData::BlockDig { status: 2, x: 5, y: 64, z: 5, face: 1 });
        }
        assert_eq!(sess.dig.cur_damage, 0.0);
    }

    #[test]
    fn test_place_dirt_and_rollback() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 6.5);
        if let Some(Entity::Player(p)) = w.entities.get_mut(player) {
            p.inventory.main[0] = Some(FfiItemStack {
                stack_size: 10, animations_to_go: 0, item_id: 3, item_damage: 0,
            });
        }
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::Place { item_id: 3, x: 3, y: 63, z: 4, direction: 1 },
        );
        assert_eq!(w.get_block_id(3, 64, 4), 3);
        let count = match w.entities.get(player).unwrap() {
            Entity::Player(p) => p.inventory.main[0].map(|s| s.stack_size),
            _ => unreachable!(),
        };
        assert_eq!(count, Some(9));
        // Rollback + inventory sync packets queued (3 sections + 2 changes).
        assert!(sess.outbox.len() >= 5);
        assert_eq!(sess.outbox[sess.outbox.len() - 2][0], 53);
    }

    #[test]
    fn test_eat_apple_heals() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 6.5);
        w.attack_living(player, 5, None);
        assert_eq!(health_of(&w, player), 15);
        if let Some(Entity::Player(p)) = w.entities.get_mut(player) {
            p.inventory.main[0] = Some(FfiItemStack {
                stack_size: 3, animations_to_go: 0, item_id: 260, item_damage: 0,
            });
        }
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::Place { item_id: 260, x: 0, y: 0, z: 0, direction: -1 },
        );
        assert_eq!(health_of(&w, player), 19);
        let count = match w.entities.get(player).unwrap() {
            Entity::Player(p) => p.inventory.main[0].map(|s| s.stack_size),
            _ => unreachable!(),
        };
        assert_eq!(count, Some(2));
    }

    #[test]
    fn test_punch_zombie_and_milk_cow() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 4.5);
        let zombie = spawn_mob(&mut w, MobKind::Zombie, 4.5, 64.0, 4.5);
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::UseEntity {
                player_entity_id: player, target_entity_id: zombie, is_left_click: true,
            },
        );
        assert_eq!(health_of(&w, zombie), 19);
        // Milk with a held bucket.
        let cid = w.entities.alloc_id();
        let mut cow = AnimalEnt::new(cid, AnimalKind::Cow);
        cow.living.body.set_position(4.5, 64.0, 5.5);
        w.entities.insert(Entity::Animal(cow));
        if let Some(Entity::Player(p)) = w.entities.get_mut(player) {
            p.inventory.main[0] = Some(FfiItemStack {
                stack_size: 1, animations_to_go: 0, item_id: 325, item_damage: 0,
            });
        }
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::UseEntity {
                player_entity_id: player, target_entity_id: cid, is_left_click: false,
            },
        );
        let held = match w.entities.get(player).unwrap() {
            Entity::Player(p) => p.inventory.main[0].map(|s| s.item_id),
            _ => unreachable!(),
        };
        assert_eq!(held, Some(335));
    }

    #[test]
    fn test_boat_mount_toggle() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 4.5);
        let bid = w.entities.alloc_id();
        let mut b = crate::entity_table::Body::new(bid, 1.5, 0.6, 0.3);
        b.set_position(4.5, 64.0, 4.5);
        w.entities.insert(Entity::Boat(crate::entity_table::BoatEnt {
            body: b, time_since_hit: 0, damage_taken: 0, forward_dir: 1,
        }));
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        let use_boat = PacketData::UseEntity {
            player_entity_id: player, target_entity_id: bid, is_left_click: false,
        };
        sess.pump(&mut ctx(&mut w, &ops, &mut bc), use_boat);
        // PacketData is consumed; rebuild for the toggle.
        let riding = w.entities.get(player).unwrap().body().riding;
        assert_eq!(riding, bid);
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::UseEntity {
                player_entity_id: player, target_entity_id: bid, is_left_click: false,
            },
        );
        assert_eq!(w.entities.get(player).unwrap().body().riding, -1);
    }

    #[test]
    fn test_chat_and_give() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 4.5);
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::Chat { message: "hi all".to_string() },
        );
        assert!(bc.iter().any(|b| matches!(b, SessionBroadcast::Chat(t) if t == "<Steve> hi all")));
        // Non-op /give spawns nothing.
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::Chat { message: "/give 3 5".to_string() },
        );
        assert!(w.entities.alive_ids().into_iter().all(|oid| !matches!(
            w.entities.get(oid).unwrap(),
            Entity::Item(_)
        )));
        // Op /give drops dirt at the player.
        let mut ops_set = HashSet::new();
        ops_set.insert("steve".to_string());
        {
            let mut c = SessionCtx {
                world: &mut w, ops: &ops_set, spawn_protection: 0, pvp: true, broadcast: &mut bc,
            };
            sess.pump(&mut c, PacketData::Chat { message: "/give 3 5".to_string() });
        }
        assert!(w.entities.alive_ids().into_iter().any(|oid| matches!(
            w.entities.get(oid).unwrap(),
            Entity::Item(e) if e.item_id == 3 && e.count == 5
        )));
    }

    #[test]
    fn test_respawn_resets() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 30.5, 64.0, 30.5);
        w.attack_living(player, 100, None);
        assert!(w.entities.get(player).unwrap().body().dead);
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        sess.pump(&mut ctx(&mut w, &ops, &mut bc), PacketData::Respawn);
        let p = w.entities.get(player).unwrap().body().clone();
        assert!(!p.dead);
        assert_eq!((p.pos[0], p.pos[2]), (0.5, 0.5));
        assert_eq!(health_of(&w, player), 20);
        assert_eq!(sess.outbox[0], vec![9]);
    }

    #[test]
    fn test_held_switch_and_sneak() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 4.5);
        if let Some(Entity::Player(p)) = w.entities.get_mut(player) {
            p.inventory.main[2] = Some(FfiItemStack {
                stack_size: 4, animations_to_go: 0, item_id: 5, item_damage: 0,
            });
        }
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::BlockItemSwitch { entity_id: player, item_id: 5 },
        );
        assert_eq!(
            match w.entities.get(player).unwrap() {
                Entity::Player(p) => p.inventory.current,
                _ => unreachable!(),
            },
            2
        );
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::ArmAnimation { entity_id: player, animate: 104 },
        );
        assert!(matches!(
            w.entities.get(player).unwrap(),
            Entity::Player(p) if p.living.sneaking
        ));
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::ArmAnimation { entity_id: player, animate: 1 },
        );
        assert!(bc.iter().any(|b| matches!(b, SessionBroadcast::ArmSwing(id) if *id == player)));
    }

    #[test]
    fn test_sign_update_round_trip() {
        use crate::nbt::{NbtCompound, NbtTag, write_root};
        use std::collections::BTreeMap;
        use std::io::Write as _;
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 6.5);
        w.set_block_id(3, 64, 4, 63);
        w.tiles.insert(
            (3, 64, 4),
            TileData::Sign(crate::tile_entity_sign::sign_create()),
        );
        // Client sends gzipped sign NBT.
        let mut map = BTreeMap::new();
        map.insert("x".to_string(), NbtTag::Int(3));
        map.insert("y".to_string(), NbtTag::Int(64));
        map.insert("z".to_string(), NbtTag::Int(4));
        map.insert("Text1".to_string(), NbtTag::String("hello".to_string()));
        let mut raw = Vec::new();
        write_root(&mut raw, "", &NbtCompound { map }).unwrap();
        let mut enc = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::default());
        enc.write_all(&raw).unwrap();
        let gz = enc.finish().unwrap();
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::ComplexEntity { x: 3, y: 64, z: 4, nbt_data: gz },
        );
        assert!(matches!(
            w.tiles.get(&(3, 64, 4)),
            Some(TileData::Sign(s)) if &s.lines[0][..5] == b"hello"
        ));
        assert!(bc.iter().any(|b| matches!(b, SessionBroadcast::TileChanged(3, 64, 4))));
        // Wrong coords are rejected.
        let mut raw = Vec::new();
        let mut map = BTreeMap::new();
        map.insert("x".to_string(), NbtTag::Int(9));
        map.insert("y".to_string(), NbtTag::Int(64));
        map.insert("z".to_string(), NbtTag::Int(4));
        write_root(&mut raw, "", &NbtCompound { map }).unwrap();
        let mut enc = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::default());
        enc.write_all(&raw).unwrap();
        sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::ComplexEntity { x: 3, y: 64, z: 4, nbt_data: enc.finish().unwrap() },
        );
        assert!(matches!(
            w.tiles.get(&(3, 64, 4)),
            Some(TileData::Sign(s)) if &s.lines[1][..1] == b"\x00"
        ));
    }

    #[test]
    fn test_restore_held_selects_slot_or_fallback() {
        use crate::inventory::FfiItemStack;
        let mut w = floor_world();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 4.5);
        if let Some(Entity::Player(p)) = w.entities.get_mut(player) {
            p.inventory.main[2] = Some(FfiItemStack {
                stack_size: 5,
                animations_to_go: 0,
                item_id: 3,
                item_damage: 0,
            });
        }
        let mut sess = PlaySession::new(player);
        // Saved id present: point at its slot.
        sess.restore_held(&mut w, 3);
        assert_eq!(sess.held_id, 3);
        assert!(sess.held_fallback.is_none());
        match w.entities.get(player).unwrap() {
            Entity::Player(p) => assert_eq!(p.inventory.current, 2),
            _ => unreachable!(),
        }
        // Saved id gone: fallback copy in the last slot.
        sess.restore_held(&mut w, 9999);
        match w.entities.get(player).unwrap() {
            Entity::Player(p) => assert_eq!(p.inventory.current, 35),
            _ => unreachable!(),
        }
        assert_eq!(sess.held_fallback.map(|s| s.item_id), Some(9999));
        // Non-positive id resets to the first slot.
        sess.restore_held(&mut w, 0);
        match w.entities.get(player).unwrap() {
            Entity::Player(p) => assert_eq!(p.inventory.current, 0),
            _ => unreachable!(),
        }
    }

    #[test]
    fn test_kick_and_drop_end_session() {
        let mut w = floor_world();
        let ops = no_ops();
        let player = spawn_player(&mut w, "Steve", 3.5, 64.0, 4.5);
        let mut sess = PlaySession::new(player);
        let mut bc = Vec::new();
        let out = sess.pump(
            &mut ctx(&mut w, &ops, &mut bc),
            PacketData::KickDisconnect { reason: "bye".to_string() },
        );
        assert!(matches!(out, Some(SessionOutcome::Gone)));
        assert!(sess.pump(&mut ctx(&mut w, &ops, &mut bc), PacketData::Respawn).is_none());
    }

    #[test]
    fn test_pre_chunk_bytes_match_encoder() {
        assert_eq!(pkt_pre_chunk(3, -2, true), vec![50, 0, 0, 0, 3, 255, 255, 255, 254, 1]);
        assert_eq!(pkt_pre_chunk(0, 0, false).last(), Some(&0));
    }

    #[test]
    fn test_map_chunk_bytes_match_send_map_chunk() {
        let data = vec![0x78, 0x9C, 0x01];
        let pkt = pkt_map_chunk(16, 0, -16, 16, 128, 16, &data);
        // id 51, x=16, y=0 (short), z=-16, sizes-1 (15,127,15), len=3.
        let head = vec![
            51, 0, 0, 0, 16, 0, 0, 255, 255, 255, 240, 15, 127, 15, 0, 0, 0, 3,
        ];
        assert_eq!(&pkt[..18], &head[..]);
        assert_eq!(&pkt[18..], &data[..]);
    }

    #[test]
    fn test_map_chunk_carries_compressed_chunk() {
        use std::io::Read;
        let mut c = crate::chunk::Chunk::new(0, 0);
        c.set_block_id(1, 64, 1, 1);
        let payload = c.map_compressed();
        let pkt = pkt_map_chunk(0, 0, 0, 16, 128, 16, &payload);
        let len = i32::from_be_bytes(pkt[14..18].try_into().unwrap());
        assert_eq!(len as usize, payload.len());
        let mut decoder = flate2::read::ZlibDecoder::new(&pkt[18..]);
        let mut back = Vec::new();
        decoder.read_to_end(&mut back).unwrap();
        assert_eq!(back, c.map_raw());
    }
}
