//! Native server: owns the `World`, the session set, config, and
//! persistence handles (mirrors `MinecraftServer` +
//! `ServerConfigurationManager` + the `NetworkListenThread` accept/tick
//! pump + the `NetServerHandler` chunk streamer).
//!
//! Tick order mirrors `serverTick`: time broadcast, world tick, player
//! autosave, tracker pass, session pump (logins, packets, keep-alive,
//! chunk streaming), furnace-update drain, console lines. Delivery is an
//! immediate `Conn::send` into the per-socket write thread, like the C++
//! send queue.
//!
//! Deliberate divergences from C++ (all safe direction):
//! - Duplicate login fully logs the old session out (save + tracker
//!   destroy, no "left" chat like C++): C++ kicks the socket but deletes
//!   the player row without saving and leaves a ghost entity on watchers.
//! - `sendAllToPlayer` after chunk sends is skipped: the per-tick tracker
//!   pass picks new visibility up the same tick.
//! - The C++ `save-modified-chunks-only` property is read by C++ but never
//!   used there; it is not parsed here either.
//! - RNG is the native deterministic stream (documented everywhere), so a
//!   fresh world's seed and spawn walk match in shape, not bit-for-bit.

use std::collections::{BTreeSet, HashMap, HashSet};

use crate::entity_table::{Entity, EntityId, PlayerEnt};
use crate::network::PacketData;
use crate::persist::ChunkStore;
use crate::server_admin::{admin_normalize, admin_parse_list, admin_render_list};
use crate::server_config::ServerConfig;
use crate::server_constants::{
    AUTO_SAVE_INTERVAL_TICKS_DEFAULT, CHUNKS_PER_TICK, TICKS_PER_SECOND, VIEW_DISTANCE_DEFAULT,
    VIEW_DISTANCE_MAX, VIEW_DISTANCE_MIN,
};
use crate::server_log as log;
use crate::session::{
    Conn, ConnEvent, LoginEvent, LoginSession, PlaySession,     SessionBroadcast, SessionCtx,
    SessionOutcome, pkt_arm, pkt_block_change, pkt_chat, pkt_health, pkt_kick, pkt_login_response,
    pkt_map_chunk, pkt_pre_chunk, pkt_spawn_pos, pkt_time, tile_packet,
};
use crate::tracker::{Observer, Outbox, TrackedEntity};
use crate::world::World;

/// Protocol version the server speaks (Alpha 1.2.6 client, like C++).
pub const PROTOCOL_VERSION: i32 = 6;
/// Per-IP connection cap (mirrors `kMaxConnectionsPerIp`).
pub const MAX_CONNECTIONS_PER_IP: i32 = 5;
/// Idle ticks before a play session times out (mirrors the C++ 1200).
pub const READ_TIMEOUT_TICKS: u32 = 1200;
/// Inbound packets processed per connection per tick (mirrors the C++
/// `kMaxPacketsPerTick`; more is a rate-limit kick).
pub const MAX_PACKETS_PER_TICK: usize = 50;
/// Opaque connection handle.
pub type ConnId = u64;

/// Chunk key (mirrors `NetServerHandler::chunkKey`: low u32 = x, high = z).
pub fn chunk_key(x: i32, z: i32) -> i64 {
    (x as u32 as i64) | ((z as u32 as i64) << 32)
}

/// Inverse of [`chunk_key`] (mirrors the C++ decode in the unload pass).
pub fn chunk_of_key(key: i64) -> (i32, i32) {
    ((key & 0xFFFF_FFFF) as u32 as i32, ((key >> 32) & 0xFFFF_FFFF) as u32 as i32)
}

/// Runtime settings parsed from `server.properties` (mirrors the C++
/// `initialize` property block, including clamps and seed rules).
#[derive(Clone, Debug)]
pub struct Settings {
    pub server_ip: String,
    pub port: i32,
    pub online_mode: bool,
    pub spawn_animals: bool,
    pub spawn_monsters: bool,
    pub pvp: bool,
    pub difficulty: i32,
    pub view_distance: i32,
    pub auto_save_interval: i32,
    pub spawn_protection: i32,
    pub max_players: i32,
    pub level_name: String,
    pub seed: i64,
    pub dimension: i8,
}

/// Leading-integer seed parse (mirrors `std::stoll`: optional `-`,
/// digits, trailing garbage ignored, emptyjunk is an error).
fn parse_seed_int(s: &str) -> Option<i64> {
    let b = s.as_bytes();
    let mut i = 0;
    let neg = if b.first() == Some(&b'-') {
        i = 1;
        true
    } else {
        false
    };
    if i >= b.len() || !b[i].is_ascii_digit() {
        return None;
    }
    let mut acc: i64 = 0;
    while i < b.len() && b[i].is_ascii_digit() {
        let d = (b[i] - b'0') as i64;
        if neg {
            acc = acc.checked_mul(10)?.checked_sub(d)?;
        } else {
            acc = acc.checked_mul(10)?.checked_add(d)?;
        }
        i += 1;
    }
    Some(acc)
}

/// Java `String.hashCode` over the seed bytes (mirrors the C++ fallback:
/// `hash * 31 + int8 byte`, wrapping like the 32-bit overflow there).
fn java_string_hash(s: &str) -> i64 {
    let mut h: i32 = 0;
    for byte in s.bytes() {
        h = h.wrapping_mul(31).wrapping_add(byte as i8 as i32);
    }
    h as i64
}

/// Load settings out of an opened properties file (missing keys persist
/// their defaults back, exactly like the C++ getters).
pub fn load_settings(cfg: &mut ServerConfig) -> Settings {
    let difficulty = cfg.get_int("difficulty", 2).clamp(0, 3);
    let view_distance = cfg.get_int("view-distance", VIEW_DISTANCE_DEFAULT)
        .clamp(VIEW_DISTANCE_MIN, VIEW_DISTANCE_MAX);
    let spawn_protection = cfg.get_int("spawn-protection-radius", 16).max(0);
    let level_seed = cfg.get_string("level-seed", "");
    let seed = if level_seed.is_empty() {
        0
    } else if let Some(n) = parse_seed_int(&level_seed) {
        n
    } else {
        java_string_hash(&level_seed)
    };
    Settings {
        server_ip: cfg.get_string("server-ip", ""),
        port: cfg.get_int("server-port", 25565),
        online_mode: cfg.get_bool("online-mode", true),
        spawn_animals: cfg.get_bool("spawn-animals", true),
        spawn_monsters: cfg.get_bool("spawn-monsters", true),
        pvp: cfg.get_bool("pvp", true),
        difficulty,
        view_distance,
        auto_save_interval: cfg.get_int("auto-save-interval", AUTO_SAVE_INTERVAL_TICKS_DEFAULT),
        spawn_protection,
        max_players: cfg.get_int("max-players", 20),
        level_name: cfg.get_string("level-name", "world"),
        seed,
        dimension: if cfg.get_bool("hellworld", false) { -1 } else { 0 },
    }
}

/// Per-play-connection chunk streaming state (mirrors the C++
/// `sentChunks_` / `chunksToLoad_` / `lastChunk` handler fields).
#[derive(Clone, Debug, Default)]
pub struct PlayStream {
    pub sent: HashSet<i64>,
    pub queue: Vec<(i32, i32)>,
    pub last_cx: i32,
    pub last_cz: i32,
}

/// One connection: socket plus login/play state and idle accounting.
pub struct Session {
    pub conn: Conn,
    pub state: SessionState,
    pub idle: u32,
}

/// Login handshake versus authenticated play (mirrors the pending/active
/// split in `NetworkListenThread`).
pub enum SessionState {
    Login(LoginSession),
    Play(PlaySession, PlayStream),
}

/// The native server (mirrors `MinecraftServer` + friends; see module docs).
pub struct Server {
    pub settings: Settings,
    pub world: World,
    pub store: ChunkStore,
    pub level_dir: String,
    pub player_dir: String,
    pub ops: HashSet<String>,
    pub banned_players: BTreeSet<String>,
    pub banned_ips: BTreeSet<String>,
    pub ops_path: String,
    pub banned_players_path: String,
    pub banned_ips_path: String,
    pub sessions: HashMap<ConnId, Session>,
    pub players: HashMap<EntityId, ConnId>,
    pub players_by_chunk: HashMap<i64, HashSet<EntityId>>,
    pub next_conn: ConnId,
    pub ip_count: HashMap<String, i32>,
    pub tick_count: u64,
    pub console: Vec<String>,
    pub running: bool,
}

fn read_list(path: &str) -> BTreeSet<String> {
    match std::fs::read_to_string(path) {
        Ok(body) => admin_parse_list(&body),
        Err(_) => BTreeSet::new(),
    }
}

fn write_list(path: &str, list: &BTreeSet<String>) {
    // I/O errors are ignored like the unchecked C++ stream writes.
    let _ = std::fs::write(path, admin_render_list(list));
}

/// Strip the port off a peer address (`ip:port`, like the C++ parse of
/// `/ip:port`; IPv6 keeps its last segment cut the same way).
fn ip_of(remote: &str) -> &str {
    match remote.rfind(':') {
        Some(i) => &remote[..i],
        None => remote,
    }
}

impl Server {
    /// Open a server: properties, admin lists, chunk store, and the world
    /// (level.dat when present, else fresh seed + spawn search + prewarm,
    /// mirroring `initialize`).
    pub fn open(
        props_path: &str,
        level_dir: &str,
        player_dir: &str,
        ops_path: &str,
        banned_players_path: &str,
        banned_ips_path: &str,
    ) -> Result<Self, String> {
        let mut cfg = ServerConfig::open(props_path);
        let settings = load_settings(&mut cfg);
        let seed = if settings.seed == 0 {
            use std::time::{SystemTime, UNIX_EPOCH};
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|d| (d.as_nanos() as i64).wrapping_add(1))
                .unwrap_or(0x9E3779B97F4A7C15u64 as i64)
                .wrapping_add(1)
        } else {
            settings.seed
        };
        let mut world = World::new(seed);
        world.difficulty = settings.difficulty;
        world.spawn_monsters = settings.spawn_monsters;
        world.spawn_animals = settings.spawn_animals;
        let store =
            ChunkStore::open(level_dir).map_err(|e| format!("cannot open chunk store: {e}"))?;
        if !world.load_level_from(level_dir) {
            Self::find_safe_spawn(&mut world);
            let (scx, scz) = (world.spawn[0].div_euclid(16), world.spawn[2].div_euclid(16));
            world.ensure_area(scx, scz, 1);
        }
        let _ = std::fs::create_dir_all(player_dir);
        Ok(Self {
            settings,
            world,
            store,
            level_dir: level_dir.to_string(),
            player_dir: player_dir.to_string(),
            ops: read_list(ops_path).into_iter().collect(),
            banned_players: read_list(banned_players_path),
            banned_ips: read_list(banned_ips_path),
            ops_path: ops_path.to_string(),
            banned_players_path: banned_players_path.to_string(),
            banned_ips_path: banned_ips_path.to_string(),
            sessions: HashMap::new(),
            players: HashMap::new(),
            players_by_chunk: HashMap::new(),
            next_conn: 1,
            ip_count: HashMap::new(),
            tick_count: 0,
            console: Vec::new(),
            running: true,
        })
    }

    /// Fresh-world spawn search (mirrors `findSafeSpawnPoint`: random
    /// walk over chunk columns, first grass/sand top at height >= 1;
    /// the native RNG stream stands in for the C++ mt19937).
    fn find_safe_spawn(world: &mut World) {
        let (mut sx, mut sz) = (0, 0);
        for _ in 0..10000 {
            world.ensure_chunk(sx, sz);
            let mut found = None;
            for x in 0..16 {
                for z in 0..16 {
                    let y = world.get_height_value(sx * 16 + x, sz * 16 + z);
                    if y < 1 {
                        continue;
                    }
                    let ground = world.get_block_id(sx * 16 + x, y - 1, sz * 16 + z);
                    if ground == 2 || ground == 12 {
                        found = Some([sx * 16 + x, y, sz * 16 + z]);
                        break;
                    }
                }
                if found.is_some() {
                    break;
                }
            }
            if let Some(sp) = found {
                world.spawn = sp;
                return;
            }
            sx += world.rng_next_int(3) - 1;
            sz += world.rng_next_int(3) - 1;
        }
    }

    /// Queue a console line (mirrors `addCommand`).
    pub fn queue_console(&mut self, line: String) {
        self.console.push(line);
    }

    /// Accept one socket (mirrors the accept loop gate: per-IP cap,
    /// silently dropped past it). Returns the new connection id.
    pub fn accept(&mut self, stream: std::net::TcpStream) -> Option<ConnId> {
        let remote = stream.peer_addr().map(|a| a.to_string()).unwrap_or_default();
        let ip = ip_of(&remote).to_string();
        let count = self.ip_count.get(&ip).copied().unwrap_or(0);
        if count >= MAX_CONNECTIONS_PER_IP {
            log::info(&format!("Connection limit reached for IP {ip}"));
            return None;
        }
        let conn = Conn::new(stream).ok()?;
        let id = self.next_conn;
        self.next_conn += 1;
        *self.ip_count.entry(ip).or_insert(0) += 1;
        let login = LoginSession::new(self.settings.online_mode);
        self.sessions.insert(id, Session { conn, state: SessionState::Login(login), idle: 0 });
        Some(id)
    }

    /// Is this connection authenticated play?
    fn is_play(&self, cid: ConnId) -> bool {
        matches!(self.sessions.get(&cid).map(|s| &s.state), Some(SessionState::Play(_, _)))
    }

    /// Username on a connection (empty when unknown).
    fn name_of(&self, cid: ConnId) -> String {
        let eid = match self.entity_of(cid) {
            Some(e) => e,
            None => return String::new(),
        };
        match self.world.entities.get(eid) {
            Some(Entity::Player(p)) => p.username.clone(),
            _ => String::new(),
        }
    }

    /// Player entity behind a connection, if any.
    fn entity_of(&self, cid: ConnId) -> Option<EntityId> {
        self.sessions.get(&cid).and_then(|s| match &s.state {
            SessionState::Play(sess, _) => Some(sess.player),
            SessionState::Login(_) => None,
        })
    }
}

impl Session {
    /// Ship queued bytes to the socket.
    fn flush(&mut self) {
        let outbox = match &mut self.state {
            SessionState::Login(l) => &mut l.outbox,
            SessionState::Play(p, _) => &mut p.outbox,
        };
        for msg in outbox.drain(..) {
            self.conn.send(msg);
        }
    }
}

impl Server {
    /// Drop a removed session: close the socket, release its IP slot.
    fn remove_session(&mut self, cid: ConnId, sess: Session) {
        let ip = ip_of(&sess.conn.remote).to_string();
        sess.conn.close();
        if let Some(n) = self.ip_count.get_mut(&ip) {
            *n -= 1;
            if *n <= 0 {
                self.ip_count.remove(&ip);
            }
        }
        let _ = cid;
    }

    /// Send owned tracker bytes to the owning connections (addressed to
    /// player ids; rows gone mid-tick are dropped like C++).
    fn route_outbox(&mut self, out: Vec<Outbox>) {
        for o in out {
            if let Some(cid) = self.players.get(&o.to).copied() {
                if let Some(sess) = self.sessions.get(&cid) {
                    sess.conn.send(o.bytes);
                }
            }
        }
    }

    /// Chat to every authenticated player (mirrors `broadcastPacket`).
    fn broadcast_chat(&mut self, msg: String) {
        let bytes = pkt_chat(&msg);
        let cids: Vec<ConnId> = self.sessions.keys().copied().collect();
        for cid in cids {
            if self.is_play(cid) {
                if let Some(sess) = self.sessions.get(&cid) {
                    sess.conn.send(bytes.clone());
                }
            }
        }
    }

    /// Fan out one session's broadcasts (chat, swing, tile updates).
    /// Swing goes to watchers only (mirrors `swingItem` broadcast, which
    /// skips the source player); tiles go to chunk-loaded players.
    /// NOTE: the source session is held by the caller (out of the map),
    /// so chat/tiles are delivered to it directly: C++ broadcasts to
    /// every player including the source.
    fn fan_out(&mut self, sess: &mut Session, bcast: Vec<SessionBroadcast>) {
        for b in bcast {
            match b {
                SessionBroadcast::Chat(msg) => {
                    let bytes = pkt_chat(&msg);
                    let cids: Vec<ConnId> = self.sessions.keys().copied().collect();
                    for cid in cids {
                        if let Some(other) = self.sessions.get(&cid) {
                            other.conn.send(bytes.clone());
                        }
                    }
                    sess.conn.send(bytes);
                }
                SessionBroadcast::ArmSwing(eid) => {
                    let bytes = pkt_arm(eid, 1);
                    let cids: Vec<ConnId> = self
                        .world
                        .tracker
                        .watchers(eid)
                        .iter()
                        .filter_map(|pid| self.players.get(pid).copied())
                        .collect();
                    for cid in cids {
                        if let Some(other) = self.sessions.get(&cid) {
                            other.conn.send(bytes.clone());
                        }
                    }
                }
                SessionBroadcast::TileChanged(x, y, z) => {
                    let key = chunk_key(x.div_euclid(16), z.div_euclid(16));
                    for cid in self.conns_with_chunk(key) {
                        if let Some(other) = self.sessions.get_mut(&cid) {
                            if let SessionState::Play(play, _) = &mut other.state {
                                play.send_tile(&self.world, x, y, z);
                            }
                        }
                    }
                    // ...plus the source session itself when loaded.
                    if let SessionState::Play(play, stream) = &mut sess.state {
                        if stream.sent.contains(&key) {
                            play.send_tile(&self.world, x, y, z);
                        }
                    }
                }
            }
        }
    }

    /// Connections whose player has this chunk loaded.
    fn conns_with_chunk(&self, key: i64) -> Vec<ConnId> {
        match self.players_by_chunk.get(&key) {
            Some(set) => set.iter().filter_map(|eid| self.players.get(eid).copied()).collect(),
            None => Vec::new(),
        }
    }

    /// Pump one connection: feed inbound packets, run the session leg,
    /// flush replies, fan out, stream chunks, apply outcomes.
    fn pump_one(&mut self, cid: ConnId) {
        let mut sess = match self.sessions.remove(&cid) {
            Some(s) => s,
            None => return,
        };
        let events = sess.conn.drain();
        if events.is_empty() {
            sess.idle = sess.idle.saturating_add(1);
        } else {
            sess.idle = 0;
        }
        let mut packets = Vec::new();
        let mut dropped = false;
        let mut over_rate = false;
        for ev in events {
            match ev {
                ConnEvent::Packet(p) => {
                    packets.push(p);
                    if packets.len() > MAX_PACKETS_PER_TICK {
                        over_rate = true;
                        break;
                    }
                }
                ConnEvent::Dropped => {
                    dropped = true;
                    break;
                }
            }
        }
        // Rate-limit shutdown mirrors C++: the error path logs out with
        // a leave message when a player row exists behind the socket.
        if over_rate {
            if let SessionState::Play(play, _) = &sess.state {
                let (eid, held) = (play.player, play.held_id);
                sess.flush();
                self.logout(eid, held, true);
            }
            self.remove_session(cid, sess);
            return;
        }
        if let SessionState::Login(login) = &mut sess.state {
            if dropped {
                login.on_drop();
            }
            for p in packets {
                login.on_packet(p);
            }
            let event = login.poll();
            sess.flush();
            match event {
                None => {
                    self.sessions.insert(cid, sess);
                }
                Some(LoginEvent::Done) => {
                    self.remove_session(cid, sess);
                }
                Some(LoginEvent::Accepted { username }) => {
                    self.join(cid, sess, username);
                }
            }
            return;
        }
        self.play_leg(cid, sess, packets, dropped);
    }

    /// Authenticated packet leg: dispatch, keep-alive, idle timeout,
    /// streaming, then outcome handling.
    fn play_leg(&mut self, cid: ConnId, mut sess: Session, packets: Vec<PacketData>, dropped: bool) {
        let mut outcome = if dropped { Some(SessionOutcome::Gone) } else { None };
        let mut bcast = Vec::new();
        if outcome.is_none() {
            if let SessionState::Play(play, _) = &mut sess.state {
                let mut ctx = SessionCtx {
                    world: &mut self.world,
                    ops: &self.ops,
                    spawn_protection: self.settings.spawn_protection,
                    pvp: self.settings.pvp,
                    broadcast: &mut bcast,
                };
                for p in packets {
                    match play.pump(&mut ctx, p) {
                        Some(o) => {
                            outcome = Some(o);
                            break;
                        }
                        None => {}
                    }
                }
                if outcome.is_none() {
                    let mut ctx = SessionCtx {
                        world: &mut self.world,
                        ops: &self.ops,
                        spawn_protection: self.settings.spawn_protection,
                        pvp: self.settings.pvp,
                        broadcast: &mut bcast,
                    };
                    play.tick(&mut ctx);
                }
            }
        }
        // Idle timeout mirrors the C++ read timeout (dead players bypass).
        if outcome.is_none() && sess.idle >= READ_TIMEOUT_TICKS {
            let dead = match &sess.state {
                SessionState::Play(play, _) => match self.world.entities.get(play.player) {
                    Some(Entity::Player(p)) => p.living.body.dead || p.living.health <= 0,
                    _ => true,
                },
                SessionState::Login(_) => false,
            };
            if !dead {
                if let SessionState::Play(play, _) = &mut sess.state {
                    play.outbox.push(pkt_kick("Timed out"));
                }
                outcome = Some(SessionOutcome::Gone);
            } else {
                sess.idle = 0;
            }
        }
        sess.flush();
        self.fan_out(&mut sess, bcast);
        let still_play = matches!(sess.state, SessionState::Play(_, _));
        if still_play && outcome.is_none() {
            self.stream_tick(cid, &mut sess);
            sess.flush();
        }
        match outcome {
            None => {
                self.sessions.insert(cid, sess);
            }
            // Session kicks carry their reason in the flushed bytes;
            // admin kicks never print a leave message (mirrors C++).
            Some(SessionOutcome::Kick(_)) => {
                if let SessionState::Play(play, _) = &sess.state {
                    let (eid, held) = (play.player, play.held_id);
                    self.logout(eid, held, false);
                }
                self.remove_session(cid, sess);
            }
            Some(SessionOutcome::Gone) => {
                if let SessionState::Play(play, _) = &sess.state {
                    let (eid, held) = (play.player, play.held_id);
                    self.logout(eid, held, true);
                }
                self.remove_session(cid, sess);
            }
        }
    }
}

impl Server {
    /// Kick a not-yet-joined login (mirrors `kickUser`): kick bytes,
    /// socket close, entry drop.
    fn drop_login(&mut self, cid: ConnId, mut sess: Session, username: &str, remote: &str, reason: &str) {
        log::info(&format!("Disconnecting {username} [{remote}]: {reason}"));
        if let SessionState::Login(login) = &mut sess.state {
            login.outbox.push(pkt_kick(reason));
        }
        sess.flush();
        self.remove_session(cid, sess);
    }

    /// Complete a login (mirrors `configManager->login` + `doLogin`):
    /// validate, ban/full/duplicate gates, row load-or-create, tracker
    /// add, and the join packet sequence. Failures kick and drop.
    fn join(&mut self, cid: ConnId, mut sess: Session, username: String) {
        let remote = sess.conn.remote.clone();
        if username.is_empty() || username.len() > 16 {
            self.drop_login(cid, sess, &username, &remote, "Invalid username length");
            return;
        }
        if !username.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'_') {
            self.drop_login(cid, sess, &username, &remote, "Invalid username characters");
            return;
        }
        let lower = admin_normalize(&username);
        if self.banned_players.contains(&lower) {
            self.drop_login(cid, sess, &username, &remote, "You are banned from this server!");
            return;
        }
        if self.banned_ips.contains(ip_of(&remote)) {
            self.drop_login(
                cid,
                sess,
                &username,
                &remote,
                "Your IP address is banned from this server!",
            );
            return;
        }
        if self.players.len() >= self.settings.max_players as usize {
            self.drop_login(cid, sess, &username, &remote, "The server is full!");
            return;
        }
        // Duplicate login: the old session is fully logged out (save +
        // tracker destroy, no leave chat) and its socket closed. C++
        // deletes the row without saving and ghosts it on watchers.
        if let Some(old_cid) =
            self.players.iter().find_map(|(eid, ocid)| match self.world.entities.get(*eid) {
                Some(Entity::Player(p)) if admin_normalize(&p.username) == lower => Some(*ocid),
                _ => None,
            })
        {
            if let Some(mut old) = self.sessions.remove(&old_cid) {
                if let SessionState::Play(play, _) = &mut old.state {
                    play.outbox.push(pkt_kick("You logged in from another location"));
                    let (eid, held) = (play.player, play.held_id);
                    old.flush();
                    log::info(&format!(
                        "Disconnecting {username}: You logged in from another location"
                    ));
                    self.logout(eid, held, false);
                }
                self.remove_session(old_cid, old);
            }
        }
        // Row: saved position wins like C++ (spawn set, then the file
        // overwrites); fresh rows start at spawn.
        let eid = match self.world.load_player_from(&self.player_dir, &username) {
            Some(id) => id,
            None => {
                let id = self.world.entities.alloc_id();
                let mut p = PlayerEnt::new(id, &username);
                let sp = self.world.spawn;
                p.living.body.set_position(sp[0] as f64 + 0.5, sp[1] as f64, sp[2] as f64 + 0.5);
                self.world.entities.insert(Entity::Player(p));
                id
            }
        };
        let (pos, held_saved) = match self.world.entities.get(eid) {
            Some(Entity::Player(p)) => (p.living.body.pos, p.held_item_id),
            _ => ([0.0, 64.0, 0.0], 0),
        };
        log::info(&format!("{username} [{remote}] logged in with entity id {eid}"));
        // Join chat goes to the players already online (mirrors the C++
        // broadcast before playerLoggedIn, which excludes the newcomer).
        self.broadcast_chat(format!("§e{username} joined the game."));
        self.players.insert(eid, cid);
        self.world.tracker.add(&TrackedEntity::player(eid, &username, pos, 0));
        // Initial chunk queue (mirrors `sendChunks`).
        let (pcx, pcz) = (pos[0].floor() as i32, pos[2].floor() as i32);
        let stream = Self::initial_stream(pcx.div_euclid(16), pcz.div_euclid(16), self.settings.view_distance);
        sess.state = SessionState::Play(PlaySession::new(eid), stream);
        sess.idle = 0;
        if let SessionState::Play(play, _) = &mut sess.state {
            play.outbox.push(pkt_login_response(eid, self.world.seed, self.settings.dimension));
            let sp = self.world.spawn;
            play.outbox.push(pkt_spawn_pos(sp[0], sp[1], sp[2]));
            if held_saved > 0 {
                play.restore_held(&mut self.world, held_saved);
            }
            let (ppos, yaw, pitch) = match self.world.entities.get(eid) {
                Some(e) => (e.body().pos, e.body().yaw, e.body().pitch),
                None => (pos, 0.0, 0.0),
            };
            play.teleport_to(&mut self.world, eid, ppos[0], ppos[1], ppos[2], yaw, pitch);
            play.outbox.push(pkt_health(match self.world.entities.get(eid) {
                Some(Entity::Player(p)) => p.living.health as i8,
                _ => 20,
            }));
            play.send_inventory(&self.world);
            play.outbox.push(pkt_time(self.world.time));
        }
        sess.flush();
        self.sessions.insert(cid, sess);
    }
}

impl Server {
    /// Log a player out (mirrors `playerLoggedOut`): unmount both ways,
    /// tracker destroy, chunk-mapping cleanup, held snapshot, player
    /// save, death mark; optional leave chat; world flush when empty.
    fn logout(&mut self, eid: EntityId, held: i32, leave: bool) {
        let username = match self.world.entities.get(eid) {
            Some(Entity::Player(p)) => p.username.clone(),
            _ => {
                self.players.remove(&eid);
                return;
            }
        };
        let (riding, ridden_by) = match self.world.entities.get(eid) {
            Some(e) => (e.body().riding, e.body().ridden_by),
            None => (crate::entity_table::NO_ENTITY, crate::entity_table::NO_ENTITY),
        };
        if riding != crate::entity_table::NO_ENTITY {
            self.world.entities.mount(eid, None);
        }
        if ridden_by != crate::entity_table::NO_ENTITY {
            self.world.entities.mount(ridden_by, None);
        }
        let mut out = Vec::new();
        self.world.tracker.remove(eid, &mut out);
        self.route_outbox(out);
        for set in self.players_by_chunk.values_mut() {
            set.remove(&eid);
        }
        self.players_by_chunk.retain(|_, set| !set.is_empty());
        if let Some(Entity::Player(p)) = self.world.entities.get_mut(eid) {
            p.held_item_id = held;
            p.living.body.dead = true;
        }
        if !self.world.save_player_to(&self.player_dir, eid) {
            log::warning(&format!("Failed to save player data for {username}"));
        }
        self.players.remove(&eid);
        if leave {
            self.broadcast_chat(format!("§e{username} left the game."));
        } else {
            log::info(&format!("{username} left the game."));
        }
        if self.players.is_empty() {
            log::info("Last player left; flushing world state to disk.");
            self.save_world();
        }
    }

    /// Save one player row (held snapshot folded in like `syncHeldItems`).
    fn save_player(&mut self, eid: EntityId, held: i32) {
        if let Some(Entity::Player(p)) = self.world.entities.get_mut(eid) {
            p.held_item_id = held;
        }
        self.world.save_player_to(&self.player_dir, eid);
    }

    /// Save all online players (mirrors `savePlayerStates`).
    fn save_players(&mut self) {
        let pairs: Vec<(EntityId, i32)> = self
            .players
            .iter()
            .map(|(eid, cid)| {
                let held = match self.sessions.get(cid) {
                    Some(Session { state: SessionState::Play(play, _), .. }) => play.held_id,
                    _ => 0,
                };
                (*eid, held)
            })
            .collect();
        for (eid, held) in pairs {
            self.save_player(eid, held);
        }
    }

    /// Flush level.dat plus every loaded chunk (mirrors the C++ flushing
    /// `saveWorld`; live boats pin their chunks like the C++ touch-up,
    /// staged unloads are recalled first since native staging is
    /// memory-only while C++ unloads save through).
    fn save_world(&mut self) {
        self.world.save_level_to(&self.level_dir);
        self.world.recall_all_staged();
        for eid in self.world.entities.alive_ids() {
            if let Some(Entity::Boat(b)) = self.world.entities.get(eid) {
                let (cx, cz) = (
                    (b.body.pos[0].floor() as i32).div_euclid(16),
                    (b.body.pos[2].floor() as i32).div_euclid(16),
                );
                self.world.mark_chunk_modified(cx, cz);
            }
        }
        let coords = self.world.loaded_chunk_coords();
        let mut saved = 0;
        for (cx, cz) in coords {
            if self.world.save_chunk_to(&mut self.store, cx, cz) {
                if let Some(c) = self.world.chunk_ref_mut(cx, cz) {
                    c.clear_modified();
                }
                saved += 1;
            }
        }
        log::info(&format!("Saved level.dat and flushed {saved} loaded chunks to disk."));
    }

    /// Kick a player by name (mirrors the console `kick`).
    fn kick_player(&mut self, name: &str, reason: &str) -> bool {
        let lower = admin_normalize(name);
        let found = self.players.iter().find_map(|(eid, cid)| {
            match self.world.entities.get(*eid) {
                Some(Entity::Player(p)) if admin_normalize(&p.username) == lower => {
                    Some((*eid, *cid))
                }
                _ => None,
            }
        });
        match found {
            Some((eid, cid)) => {
                if let Some(mut sess) = self.sessions.remove(&cid) {
                    let held = match &mut sess.state {
                        SessionState::Play(play, _) => {
                            play.outbox.push(pkt_kick(reason));
                            play.held_id
                        }
                        SessionState::Login(_) => 0,
                    };
                    sess.flush();
                    log::info(&format!("Disconnecting {name}: {reason}"));
                    // A pre-join kick has no player row to log out.
                    if matches!(sess.state, SessionState::Play(_, _)) {
                        self.logout(eid, held, false);
                    }
                    self.remove_session(cid, sess);
                }
                true
            }
            None => false,
        }
    }
}

impl Server {
    /// Initial chunk queue (mirrors `sendChunks`: the (r+3) square sorted
    /// center-out, padding included like C++).
    fn initial_stream(pcx: i32, pcz: i32, view: i32) -> PlayStream {
        let gen = view + 3;
        let mut queue = Vec::new();
        for cx in pcx - gen..=pcx + gen {
            for cz in pcz - gen..=pcz + gen {
                queue.push((cx, cz));
            }
        }
        queue.sort_by_key(|(cx, cz)| (cx - pcx) * (cx - pcx) + (cz - pcz) * (cz - pcz));
        PlayStream { sent: HashSet::new(), queue, last_cx: pcx, last_cz: pcz }
    }

    /// Per-tick chunk pump for one play session (mirrors the handler
    /// `tick` chunk half: unload on chunk change, queue rebuild, up to
    /// 15 sends with 3x3 population, tile packets on send).
    fn stream_tick(&mut self, _cid: ConnId, sess: &mut Session) {
        let (play, stream) = match &mut sess.state {
            SessionState::Play(p, s) => (p, s),
            SessionState::Login(_) => return,
        };
        let (pcx, pcz) = match self.world.entities.get(play.player) {
            Some(e) => (
                (e.body().pos[0].floor() as i32).div_euclid(16),
                (e.body().pos[2].floor() as i32).div_euclid(16),
            ),
            None => return,
        };
        let view = self.settings.view_distance;
        if stream.last_cx != pcx || stream.last_cz != pcz {
            stream.last_cx = pcx;
            stream.last_cz = pcz;
            // Unload what fell out of view (mirrors the C++ removal pass).
            let stale: Vec<i64> = stream
                .sent
                .iter()
                .copied()
                .filter(|k| {
                    let (sx, sz) = chunk_of_key(*k);
                    (sx - pcx).abs() > view || (sz - pcz).abs() > view
                })
                .collect();
            for key in stale {
                let (sx, sz) = chunk_of_key(key);
                play.outbox.push(pkt_pre_chunk(sx, sz, false));
                stream.sent.remove(&key);
                if let Some(set) = self.players_by_chunk.get_mut(&key) {
                    set.remove(&play.player);
                    if set.is_empty() {
                        self.players_by_chunk.remove(&key);
                    }
                }
            }
            // Rebuild: visible-not-sent first, then surviving queue tail.
            let gen = view + 3;
            let mut needed = Vec::new();
            for cx in pcx - gen..=pcx + gen {
                for cz in pcz - gen..=pcz + gen {
                    if (cx - pcx).abs() > view || (cz - pcz).abs() > view {
                        continue;
                    }
                    if !stream.sent.contains(&chunk_key(cx, cz)) {
                        needed.push((cx, cz));
                    }
                }
            }
            needed.sort_by_key(|(cx, cz)| (cx - pcx) * (cx - pcx) + (cz - pcz) * (cz - pcz));
            let mut queued: HashSet<i64> = needed.iter().map(|(cx, cz)| chunk_key(*cx, *cz)).collect();
            let mut merged = needed;
            for (cx, cz) in std::mem::take(&mut stream.queue) {
                let key = chunk_key(cx, cz);
                if !queued.contains(&key) && !stream.sent.contains(&key) {
                    queued.insert(key);
                    merged.push((cx, cz));
                }
            }
            stream.queue = merged;
        }
        // Send within budget (mirrors the 15-chunk pass; the native
        // ensure path loads the store first, then generates).
        let mut sent_now = 0;
        let mut i = 0;
        while i < stream.queue.len() && sent_now < CHUNKS_PER_TICK as usize {
            let (qx, qz) = stream.queue[i];
            for dx in -1..=1 {
                for dz in -1..=1 {
                    let (nx, nz) = (qx + dx, qz + dz);
                    if !self.world.has_chunk(nx, nz) && !self.world.recall_chunk(nx, nz) {
                        let _ = self.world.load_chunk_from(&mut self.store, nx, nz);
                    }
                    self.world.ensure_chunk(nx, nz);
                }
            }
            let populated = self
                .world
                .chunk_ref(qx, qz)
                .map(|c| c.is_terrain_populated)
                .unwrap_or(false);
            if !populated {
                i += 1;
                continue;
            }
            play.outbox.push(pkt_pre_chunk(qx, qz, true));
            if let Some(chunk) = self.world.chunk_ref(qx, qz) {
                play.outbox.push(pkt_map_chunk(
                    qx * 16,
                    0,
                    qz * 16,
                    16,
                    128,
                    16,
                    &chunk.map_compressed(),
                ));
            }
            // Tile entities ride the chunk like C++ (row order is map
            // order on both sides).
            let cells: Vec<(i32, i32, i32)> = self
                .world
                .tiles
                .keys()
                .copied()
                .filter(|(x, _, z)| {
                    x.div_euclid(16) == qx && z.div_euclid(16) == qz
                })
                .collect();
            for (x, y, z) in cells {
                if let Some(tile) = self.world.tiles.get(&(x, y, z)) {
                    play.outbox.push(tile_packet(x, y, z, tile));
                }
            }
            stream.sent.insert(chunk_key(qx, qz));
            self.players_by_chunk.entry(chunk_key(qx, qz)).or_default().insert(play.player);
            stream.queue.remove(i);
            sent_now += 1;
        }
    }

    /// Tracker pass (mirrors `EntityTracker::tick`): silently prune dead
    /// rows, add newcomers, tick every live entity against the player
    /// observers with the chunk-loaded gate, route the outbox.
    fn tracker_tick(&mut self) {
        let mut ids = self.world.entities.alive_ids();
        ids.sort_unstable();
        let live: HashSet<EntityId> = ids
            .iter()
            .copied()
            .filter(|id| match self.world.entities.get(*id) {
                Some(e) => !e.body().dead,
                None => false,
            })
            .collect();
        self.world.tracker.prune(&|id| live.contains(&id));
        let observers: Vec<Observer> = live
            .iter()
            .filter_map(|id| match self.world.entities.get(*id) {
                Some(Entity::Player(p)) if self.players.contains_key(id) => Some(Observer {
                    id: *id,
                    pos: p.living.body.pos,
                    // The dead see nothing (mirrors the C++ isDead skip).
                    alive: !p.living.body.dead,
                }),
                _ => None,
            })
            .collect();
        // Snapshot rows into owned tracked structs (params mirror C++).
        let mut tracked = Vec::new();
        let mut chunks = HashMap::new();
        for id in &live {
            let held = match self.world.entities.get(*id) {
                Some(Entity::Player(_)) => {
                    match self.players.get(id).and_then(|cid| self.sessions.get(cid)) {
                        Some(Session { state: SessionState::Play(play, _), .. }) => {
                            play.held_id as i16
                        }
                        _ => 0,
                    }
                }
                _ => 0,
            };
            let te = match self.world.entities.get(*id) {
                Some(e @ Entity::Player(_)) => TrackedEntity::from_entity(e, held, 512, 1, false),
                Some(e @ Entity::Item(_)) => TrackedEntity::from_entity(e, 0, 64, 20, true),
                Some(e @ Entity::Arrow(_)) => TrackedEntity::from_entity(e, 0, 64, 5, true),
                Some(e @ Entity::Boat(_)) => TrackedEntity::from_entity(e, 0, 160, 5, true),
                Some(e @ Entity::Mob(_)) | Some(e @ Entity::Animal(_)) => {
                    TrackedEntity::from_entity(e, 0, 160, 2, false)
                }
                Some(Entity::Falling(_)) | None => None,
            };
            if let Some(te) = te {
                chunks.insert(
                    te.id,
                    (
                        (te.pos[0].floor() as i32).div_euclid(16),
                        (te.pos[2].floor() as i32).div_euclid(16),
                    ),
                );
                tracked.push(te);
            }
        }
        let sessions = &self.sessions;
        let players = &self.players;
        let tracker = &mut self.world.tracker;
        let mut out = Vec::new();
        for te in &tracked {
            tracker.add(te);
            tracker.tick_entity(
                te,
                &observers,
                &|observer, entity| {
                    let (ecx, ecz) = chunks.get(&entity).copied().unwrap_or((0, 0));
                    match players.get(&observer).and_then(|cid| sessions.get(cid)) {
                        Some(Session { state: SessionState::Play(_, stream), .. }) => {
                            stream.sent.contains(&chunk_key(ecx, ecz))
                        }
                        _ => false,
                    }
                },
                &mut out,
            );
        }
        self.route_outbox(out);
    }
}

impl Server {
    /// Ship queued furnace flips as block changes to chunk-loaded
    /// players (mirrors the C++ `markBlockNeedsUpdate` fan-out).
    fn drain_furnace_updates(&mut self) {
        let pending = std::mem::take(&mut self.world.furnace_updates);
        for [x, y, z] in pending {
            let bytes = pkt_block_change(x, y, z, self.world.get_block_id(x, y, z), self.world.get_block_meta(x, y, z));
            let cids = self.conns_with_chunk(chunk_key(x.div_euclid(16), z.div_euclid(16)));
            for cid in cids {
                if let Some(sess) = self.sessions.get(&cid) {
                    sess.conn.send(bytes.clone());
                }
            }
        }
    }

    /// One server tick (mirrors `serverTick`).
    pub fn tick(&mut self) {
        if !self.running {
            return;
        }
        self.tick_count += 1;
        if self.tick_count % TICKS_PER_SECOND as u64 == 0 {
            let bytes = pkt_time(self.world.time);
            let cids: Vec<ConnId> = self.sessions.keys().copied().collect();
            for cid in cids {
                if self.is_play(cid) {
                    if let Some(sess) = self.sessions.get(&cid) {
                        sess.conn.send(bytes.clone());
                    }
                }
            }
        }
        self.world.tick_world();
        if self.settings.auto_save_interval > 0
            && self.tick_count % self.settings.auto_save_interval as u64 == 0
        {
            self.save_players();
        }
        self.tracker_tick();
        let cids: Vec<ConnId> = self.sessions.keys().copied().collect();
        for cid in cids {
            self.pump_one(cid);
        }
        self.drain_furnace_updates();
        let lines = std::mem::take(&mut self.console);
        for line in lines {
            self.handle_console(&line);
        }
    }

    /// Console line entry point (wired to real handlers in the console
    /// slice; until then lines are acknowledged and dropped).
    fn handle_console(&mut self, line: &str) {
        let _ = line;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::{TcpListener, TcpStream};
    use std::sync::atomic::{AtomicU64, Ordering};
    use std::time::Duration;

    static TMP: AtomicU64 = AtomicU64::new(0);

    fn tmpdir(tag: &str) -> String {
        let id = TMP.fetch_add(1, Ordering::Relaxed);
        let dir = format!("/tmp/opencode_srv_{}_{}_{}", tag, std::process::id(), id);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn mk_server(props_extra: &str) -> Server {
        let base = tmpdir("srv");
        let props = format!("{base}/server.properties");
        std::fs::write(
            &props,
            format!("online-mode=false\nlevel-seed=7\nspawn-monsters=false\nspawn-animals=false\n{props_extra}"),
        )
        .unwrap();
        let mut s = Server::open(
            &props,
            &format!("{base}/world"),
            &format!("{base}/world/players"),
            &format!("{base}/ops.txt"),
            &format!("{base}/banned-players.txt"),
            &format!("{base}/banned-ips.txt"),
        )
        .unwrap();
        // No unload churn under the streaming square (corners sit past
        // the default radius); production keeps the configured radius.
        s.world.unload_radius = 20;
        // Pre-populate the streaming square so tests never pay for
        // generation: queue is (view 3 + pad 3) and each send ensures
        // a 3x3 around it, so cover spawn +/- 9. Generation is
        // synchronous here; a miss would stall the tick for seconds.
        let (scx, scz) = (s.world.spawn[0].div_euclid(16), s.world.spawn[2].div_euclid(16));
        for cx in scx - 9..=scx + 9 {
            for cz in scz - 9..=scz + 9 {
                if !s.world.has_chunk(cx, cz) {
                    let mut c = crate::chunk::Chunk::new(cx, cz);
                    c.is_terrain_populated = true;
                    s.world.insert_chunk(c);
                } else if let Some(c) = s.world.chunk_ref_mut(cx, cz) {
                    c.is_terrain_populated = true;
                }
            }
        }
        s
    }

    fn pair(srv: &mut Server) -> (TcpStream, ConnId) {
        let l = TcpListener::bind("127.0.0.1:0").unwrap();
        let addr = l.local_addr().unwrap();
        let mut client = TcpStream::connect(addr).unwrap();
        client.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
        let (stream, _) = l.accept().unwrap();
        let cid = srv.accept(stream).unwrap();
        (client, cid)
    }

    fn w_str(buf: &mut Vec<u8>, s: &str) {
        buf.extend_from_slice(&(s.len() as u16).to_be_bytes());
        buf.extend_from_slice(s.as_bytes());
    }

    fn cli_handshake(user: &str) -> Vec<u8> {
        let mut b = vec![2];
        w_str(&mut b, user);
        b
    }

    fn cli_login(proto: i32, user: &str) -> Vec<u8> {
        let mut b = vec![1];
        b.extend_from_slice(&proto.to_be_bytes());
        w_str(&mut b, user);
        w_str(&mut b, "");
        b.extend_from_slice(&0i64.to_be_bytes());
        b.push(0);
        b
    }

    fn cli_chat(msg: &str) -> Vec<u8> {
        let mut b = vec![3];
        w_str(&mut b, msg);
        b
    }

    /// Tick until the client has something, then take it (kills the
    /// write-then-tick race under parallel-test load).
    fn pump_until(client: &mut TcpStream, srv: &mut Server) -> (u8, String) {
        for _ in 0..200 {
            srv.tick();
            if let Some(p) = next_pkt_opt(client, Duration::from_millis(50)) {
                return p;
            }
        }
        panic!("no packet after 200 ticks");
    }

    /// Tick until a packet matches (tracker chatter never quiesces while
    /// two players stand together, so targeted reads skip ahead).
    fn pump_match(
        client: &mut TcpStream,
        srv: &mut Server,
        want: &dyn Fn(&(u8, String)) -> bool,
    ) -> (u8, String) {
        for _ in 0..400 {
            srv.tick();
            while let Some(p) = next_pkt_opt(client, Duration::from_millis(50)) {
                if want(&p) {
                    return p;
                }
            }
        }
        panic!("no matching packet after 400 ticks");
    }

    /// Drain everything buffered, keeping pre/map chunk pairs together;
    /// time and keep-alive interleave freely. Returns packet count.
    fn drain_all(client: &mut TcpStream) -> usize {
        let mut n = 0;
        while let Some((id, _)) = next_pkt_opt(client, Duration::from_millis(200)) {
            n += 1;
            if id == 50 {
                loop {
                    let (nid, _) = next_pkt(client);
                    n += 1;
                    if nid == 51 {
                        break;
                    }
                    assert!(nid == 4 || nid == 0, "between pre/map chunk: {nid}");
                }
            }
        }
        n
    }

    fn r_u16(c: &mut TcpStream) -> u16 {
        let mut b = [0u8; 2];
        c.read_exact(&mut b).unwrap();
        u16::from_be_bytes(b)
    }

    fn r_str(c: &mut TcpStream) -> String {
        let n = r_u16(c) as usize;
        let mut b = vec![0u8; n];
        c.read_exact(&mut b).unwrap();
        String::from_utf8_lossy(&b).into_owned()
    }

    fn r_skip(c: &mut TcpStream, n: usize) {
        let mut b = vec![0u8; n];
        c.read_exact(&mut b).unwrap();
    }

    /// Next packet: id plus captured string for text packets.
    /// The id byte waits up to `wait`; the body then gets 5 seconds
    /// (it is always already in flight on loopback).
    fn next_pkt_opt(c: &mut TcpStream, wait: Duration) -> Option<(u8, String)> {
        c.set_read_timeout(Some(wait)).ok()?;
        let mut id = [0u8; 1];
        c.read_exact(&mut id).ok()?;
        c.set_read_timeout(Some(Duration::from_secs(5))).ok()?;
        let text = match id[0] {
            0 => String::new(),
            1 => {
                r_skip(c, 4);
                r_str(c);
                r_str(c);
                r_skip(c, 9);
                String::new()
            }
            2 | 3 | 255 => r_str(c),
            4 => {
                r_skip(c, 8);
                String::new()
            }
            5 => {
                r_skip(c, 4);
                let n = r_u16(c) as usize;
                r_skip(c, n * 5);
                String::new()
            }
            6 => {
                r_skip(c, 12);
                String::new()
            }
            8 => {
                r_skip(c, 1);
                String::new()
            }
            9 => String::new(),
            13 => {
                r_skip(c, 41);
                String::new()
            }
            16 => {
                r_skip(c, 6);
                String::new()
            }
            18 => {
                r_skip(c, 5);
                String::new()
            }
            20 => {
                r_skip(c, 4);
                let name = r_str(c);
                r_skip(c, 16);
                name
            }
            21 => {
                r_skip(c, 4 + 2 + 1 + 12 + 3);
                String::new()
            }
            22 => {
                r_skip(c, 8);
                String::new()
            }
            23 => {
                r_skip(c, 17);
                String::new()
            }
            24 => {
                r_skip(c, 4 + 1 + 12 + 2);
                String::new()
            }
            28 => {
                r_skip(c, 10);
                String::new()
            }
            29 => {
                r_skip(c, 4);
                String::new()
            }
            30 => {
                r_skip(c, 4);
                String::new()
            }
            31 => {
                r_skip(c, 7);
                String::new()
            }
            32 => {
                r_skip(c, 6);
                String::new()
            }
            33 => {
                r_skip(c, 9);
                String::new()
            }
            34 => {
                r_skip(c, 18);
                String::new()
            }
            38 => {
                r_skip(c, 5);
                String::new()
            }
            39 => {
                r_skip(c, 8);
                String::new()
            }
            50 => {
                r_skip(c, 9);
                String::new()
            }
            51 => {
                r_skip(c, 4 + 2 + 4 + 3);
                let n = {
                    let mut b = [0u8; 4];
                    c.read_exact(&mut b).unwrap();
                    i32::from_be_bytes(b) as usize
                };
                r_skip(c, n);
                String::new()
            }
            53 => {
                r_skip(c, 11);
                String::new()
            }
            59 => {
                r_skip(c, 4 + 2 + 4);
                let n = r_u16(c) as usize;
                r_skip(c, n);
                String::new()
            }
            other => panic!("unexpected packet id {other}"),
        };
        Some((id[0], text))
    }

    fn next_pkt(c: &mut TcpStream) -> (u8, String) {
        next_pkt_opt(c, Duration::from_secs(5)).expect("packet within timeout")
    }

    /// Join a player up to the join burst (through the time packet).
    /// Chunk streaming continues in the background; use [`drain_chunks`]
    /// when the test needs a quiet buffer afterwards.
    fn join_burst(srv: &mut Server, client: &mut TcpStream, name: &str) {
        client.write_all(&cli_handshake(name)).unwrap();
        assert_eq!(pump_until(client, srv).0, 2);
        client.write_all(&cli_login(6, name)).unwrap();
        let mut ids = Vec::new();
        for _ in 0..8 {
            ids.push(pump_until(client, srv).0);
        }
        // The newcomer never hears its own join chat (mirrors the C++
        // broadcast before playerLoggedIn, which excludes the newcomer).
        assert_eq!(ids, vec![1, 6, 13, 8, 5, 5, 5, 4], "join burst for {name}");
    }

    /// Flush one session's chunk queue, draining packets as they arrive
    /// (tracker chatter is ignored here).
    fn drain_chunks(srv: &mut Server, client: &mut TcpStream) {
        for _ in 0..60 {
            srv.tick();
            let got = drain_all(client);
            let empty = match srv.sessions.values().find_map(|s| match &s.state {
                SessionState::Play(_, stream) => Some(stream.queue.is_empty()),
                SessionState::Login(_) => None,
            }) {
                Some(e) => e,
                None => break,
            };
            if empty && got == 0 {
                break;
            }
        }
        drain_all(client);
    }

    /// Join a player, returning after the join burst (through the time
    /// packet); chunk packets that follow are drained.
    fn join(srv: &mut Server, client: &mut TcpStream, name: &str) {
        join_burst(srv, client, name);
        drain_chunks(srv, client);
    }

    #[test]
    fn settings_parse_clamps_and_seed_rules() {
        let base = tmpdir("props");
        let props = format!("{base}/server.properties");
        std::fs::write(
            &props,
            "view-distance=99\ndifficulty=9\nlevel-seed=123abc\nmax-players=3\n",
        )
        .unwrap();
        let mut cfg = ServerConfig::open(&props);
        let s = load_settings(&mut cfg);
        assert_eq!(s.view_distance, 15);
        assert_eq!(s.difficulty, 3);
        assert_eq!(s.seed, 123);
        assert_eq!(s.max_players, 3);
        // String seeds hash like Java; empty means random-at-open.
        std::fs::write(&props, "level-seed=abc\n").unwrap();
        let mut cfg = ServerConfig::open(&props);
        assert_eq!(load_settings(&mut cfg).seed, 96354);
        std::fs::write(&props, "").unwrap();
        let mut cfg = ServerConfig::open(&props);
        assert_eq!(load_settings(&mut cfg).seed, 0);
        // Negative distances floor at the minimum.
        std::fs::write(&props, "view-distance=1\nspawn-protection-radius=-5\n").unwrap();
        let mut cfg = ServerConfig::open(&props);
        let s = load_settings(&mut cfg);
        assert_eq!(s.view_distance, 3);
        assert_eq!(s.spawn_protection, 0);
    }

    #[test]
    fn offline_login_sends_join_burst() {
        let mut srv = mk_server("");
        let (mut client, _cid) = pair(&mut srv);
        join(&mut srv, &mut client, "Steve");
        assert_eq!(srv.players.len(), 1);
    }

    /// Login attempt that must end in a kick with the given reason.
    fn login_kicked(props_extra: &str, name: &str, proto: i32, reason_part: &str) {
        let mut srv = mk_server(props_extra);
        let (mut client, _cid) = pair(&mut srv);
        client.write_all(&cli_handshake(name)).unwrap();
        assert_eq!(pump_until(&mut client, &mut srv).0, 2);
        client.write_all(&cli_login(proto, name)).unwrap();
        let (id, text) = pump_until(&mut client, &mut srv);
        assert_eq!(id, 255);
        assert!(text.contains(reason_part), "kick text: {text}");
        assert!(srv.players.is_empty());
    }

    #[test]
    fn login_rejects_bad_protocol_and_names() {
        login_kicked("", "Steve", 5, "Outdated client!");
        login_kicked("", "Steve", 7, "Outdated server!");
        login_kicked("", "Bad Name!", 6, "Invalid username characters");
        login_kicked("", "", 6, "Invalid username length");
    }

    #[test]
    fn login_rejects_banned_and_full() {
        // Banned name (file holds lowercase like C++).
        let base = tmpdir("ban");
        let props = format!("{base}/server.properties");
        std::fs::write(&props, "online-mode=false\nlevel-seed=7\n").unwrap();
        std::fs::write(format!("{base}/banned-players.txt"), "steve\n").unwrap();
        let mut banned = Server::open(
            &props,
            &format!("{base}/world"),
            &format!("{base}/world/players"),
            &format!("{base}/ops.txt"),
            &format!("{base}/banned-players.txt"),
            &format!("{base}/banned-ips.txt"),
        )
        .unwrap();
        let (mut client, _cid) = pair(&mut banned);
        client.write_all(&cli_handshake("Steve")).unwrap();
        assert_eq!(pump_until(&mut client, &mut banned).0, 2);
        client.write_all(&cli_login(6, "Steve")).unwrap();
        let (id, text) = pump_until(&mut client, &mut banned);
        assert_eq!(id, 255);
        assert!(text.contains("banned"), "kick text: {text}");
    }

    #[test]
    fn login_rejects_full_server() {
        let mut srv = mk_server("max-players=1\n");
        let (mut a, _cid) = pair(&mut srv);
        join(&mut srv, &mut a, "Steve");
        let (mut b, _cid) = pair(&mut srv);
        b.write_all(&cli_handshake("Alex")).unwrap();
        assert_eq!(pump_until(&mut b, &mut srv).0, 2);
        b.write_all(&cli_login(6, "Alex")).unwrap();
        let (id, text) = pump_until(&mut b, &mut srv);
        assert_eq!(id, 255);
        assert!(text.contains("full"), "kick text: {text}");
        assert_eq!(srv.players.len(), 1);
    }

    #[test]
    fn chat_broadcasts_to_everyone() {
        let mut srv = mk_server("");
        let (mut a, _cid) = pair(&mut srv);
        join(&mut srv, &mut a, "Steve");
        let (mut b, _cid) = pair(&mut srv);
        join(&mut srv, &mut b, "Alex");
        // B's join notice landed on A first (past any chatter).
        assert_eq!(
            pump_match(&mut a, &mut srv, &|p| p == &(3, "§eAlex joined the game.".to_string())),
            (3, "§eAlex joined the game.".to_string())
        );
        a.write_all(&cli_chat("hi all")).unwrap();
        // Sender and newcomer both hear it, formatted once by the session.
        // Tracker keep-alives may sit ahead in the buffers; skip to chat.
        let mut heard_a = false;
        let mut heard_b = false;
        for _ in 0..40 {
            srv.tick();
            while let Some(p) = next_pkt_opt(&mut a, Duration::from_millis(50)) {
                if p == (3, "<Steve> hi all".to_string()) {
                    heard_a = true;
                }
            }
            while let Some(p) = next_pkt_opt(&mut b, Duration::from_millis(50)) {
                if p == (3, "<Steve> hi all".to_string()) {
                    heard_b = true;
                }
            }
            if heard_a && heard_b {
                break;
            }
        }
        assert!(heard_a && heard_b);
    }

    #[test]
    fn quit_broadcasts_leave_and_saves() {
        let mut srv = mk_server("");
        let (mut a, _cid) = pair(&mut srv);
        join(&mut srv, &mut a, "Steve");
        let (mut b, _cid) = pair(&mut srv);
        join(&mut srv, &mut b, "Alex");
        assert_eq!(
            pump_match(&mut a, &mut srv, &|p| p == &(3, "§eAlex joined the game.".to_string())),
            (3, "§eAlex joined the game.".to_string())
        );
        // Client quit (255 ends the socket; the server sees Dropped).
        let mut bye = vec![255u8];
        w_str(&mut bye, "Quitting");
        b.write_all(&bye).unwrap();
        let mut saw_leave = false;
        for _ in 0..40 {
            srv.tick();
            while let Some(p) = next_pkt_opt(&mut a, Duration::from_millis(50)) {
                if p.0 == 3 && p.1.contains("Alex") && p.1.contains("left the game") {
                    saw_leave = true;
                }
            }
            if saw_leave {
                break;
            }
        }
        assert!(saw_leave);
        assert_eq!(srv.players.len(), 1);
        // The quitter's row was saved (lowercase name like C++).
        assert!(std::path::Path::new(&format!("{}/alex.dat", srv.player_dir)).exists());
    }

    #[test]
    fn idle_connection_times_out() {
        let mut srv = mk_server("");
        let (mut a, cid) = pair(&mut srv);
        join(&mut srv, &mut a, "Steve");
        if let Some(sess) = srv.sessions.get_mut(&cid) {
            sess.idle = READ_TIMEOUT_TICKS - 1;
        }
        srv.tick();
        let (id, text) = next_pkt(&mut a);
        assert_eq!(id, 255);
        assert!(text.contains("Timed out"), "kick text: {text}");
        assert!(srv.players.is_empty());
    }

    #[test]
    fn time_broadcasts_every_second_and_autosave_runs() {
        let mut srv = mk_server("auto-save-interval=5\n");
        let (mut a, _cid) = pair(&mut srv);
        join(&mut srv, &mut a, "Steve");
        for _ in 0..20 {
            srv.tick();
        }
        // Drain to the time packet (keep-alive may also appear).
        let mut saw_time = false;
        while let Some((id, _)) = next_pkt_opt(&mut a, Duration::from_millis(300)) {
            if id == 4 {
                saw_time = true;
                break;
            }
        }
        assert!(saw_time);
        // Autosave wrote the player file (lowercase name like C++).
        let mut found = false;
        for entry in std::fs::read_dir(format!(
            "{}/world/players",
            srv.level_dir.rsplit_once('/').map(|(b, _)| b).unwrap_or("")
        ))
        .unwrap()
        {
            let entry = entry.unwrap();
            if entry.file_name().to_string_lossy() == "steve.dat" {
                found = true;
            }
        }
        assert!(found);
    }

    #[test]
    fn furnace_flips_reach_loaded_players() {
        let mut srv = mk_server("");
        let (mut a, _cid) = pair(&mut srv);
        join(&mut srv, &mut a, "Steve");
        // Idle furnace with fuel right next to spawn.
        let (fx, fz) = (srv.world.spawn[0], srv.world.spawn[2]);
        srv.world.set_block_id(fx, 64, fz, 61);
        let mut f = crate::tile_entity_furnace::furnace_create();
        f.slots[0] = crate::inventory::FfiItemStack {
            stack_size: 1,
            animations_to_go: 0,
            item_id: 4,
            item_damage: 0,
        };
        f.slots[1] = crate::inventory::FfiItemStack {
            stack_size: 1,
            animations_to_go: 0,
            item_id: 5,
            item_damage: 0,
        };
        srv.world.tiles.insert((fx, 64, fz), crate::world::TileData::Furnace(f));
        // Drain first (streaming/tracker noise), then tick into the flip.
        while next_pkt_opt(&mut a, Duration::from_millis(200)).is_some() {}
        srv.tick();
        let mut saw_flip = false;
        while let Some((id, _)) = next_pkt_opt(&mut a, Duration::from_millis(300)) {
            if id == 53 {
                saw_flip = true;
                break;
            }
        }
        assert!(saw_flip);
        assert_eq!(srv.world.get_block_id(fx, 64, fz), 62);
    }

    #[test]
    fn tracker_introduces_players_to_each_other() {
        let mut srv = mk_server("");
        let (mut a, _cid) = pair(&mut srv);
        join(&mut srv, &mut a, "Steve");
        let (mut b, _cid) = pair(&mut srv);
        // Burst only: the introductions below happen while B streams,
        // and a full drain would throw them away.
        join_burst(&mut srv, &mut b, "Alex");
        // Both stand at spawn: tick until each sees a named spawn.
        let mut saw_a = false;
        let mut saw_b = false;
        for _ in 0..40 {
            srv.tick();
            while let Some((id, text)) = next_pkt_opt(&mut a, Duration::from_millis(200)) {
                if id == 20 && text == "Alex" {
                    saw_a = true;
                }
            }
            while let Some((id, text)) = next_pkt_opt(&mut b, Duration::from_millis(200)) {
                if id == 20 && text == "Steve" {
                    saw_b = true;
                }
            }
            if saw_a && saw_b {
                break;
            }
        }
        assert!(saw_a && saw_b);
    }

    #[test]
    fn duplicate_login_kicks_old_and_keeps_new() {
        let mut srv = mk_server("");
        let (mut a, _cid) = pair(&mut srv);
        join(&mut srv, &mut a, "Steve");
        let (mut b, _cid) = pair(&mut srv);
        b.write_all(&cli_handshake("Steve")).unwrap();
        assert_eq!(pump_until(&mut b, &mut srv).0, 2);
        b.write_all(&cli_login(6, "steve")).unwrap();
        // Old socket got the duplicate kick (leftover chunk/tracker
        // chatter may sit ahead of it in the buffer).
        let (id, text) = pump_match(&mut a, &mut srv, &|p| p.0 == 255);
        assert_eq!(id, 255);
        assert!(text.contains("another location"), "kick text: {text}");
        // ...and the new login completed (starts with the login burst).
        let mut ids = Vec::new();
        for _ in 0..8 {
            ids.push(pump_until(&mut b, &mut srv).0);
        }
        assert_eq!(ids, vec![1, 6, 13, 8, 5, 5, 5, 4]);
        assert_eq!(srv.players.len(), 1);
    }
}
