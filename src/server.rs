//! Native server: owns the `World`, the session set, config, and
//! persistence handles (mirrors `MinecraftServer` +
//! `ServerConfigurationManager` + the `NetworkListenThread` accept/tick
//! pump + the `NetServerHandler` chunk streamer).
//!
//! MAP OF THIS FILE:
//! - `server_tests.rs` — unit/integration tests (`#[path]` submodule).
//! - below: constants → `Settings`/`load_settings` → `Server` struct →
//!   open/save → tick (`tick`, `tracker_tick`, drains) → sessions/pump →
//!   chunks/streaming → console.
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

use crate::commands::{ConsoleCommandTag, FfiString, rust_parse_console_command};
use crate::entity_table::{AnimalEnt, AnimalKind, Entity, EntityId, MobEnt, MobKind, PlayerEnt};
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

/// Chunk key for the in-memory maps (low u32 = x, high u32 = z).
/// NOTE: intentionally different from the LevelDB `chunk_key_bytes`
/// layout in persist.rs; the two key spaces never mix.
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
    // Admin-list writes must not fail silently (lost ops/bans on restart).
    if let Err(e) = std::fs::write(path, admin_render_list(list)) {
        crate::server_log::warning(&format!("cannot write {path}: {e}"));
    }
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
        world.level_name = settings.level_name.clone();
        let store =
            ChunkStore::open(&format!("{level_dir}/db")).map_err(|e| format!("cannot open chunk store: {e}"))?;
        if !world.load_level_from(level_dir) {
            Self::find_safe_spawn(&mut world);
            let (scx, scz) = (world.spawn[0].div_euclid(16), world.spawn[2].div_euclid(16));
            world.ensure_area(scx, scz, 1);
        }
        if let Err(e) = std::fs::create_dir_all(player_dir) {
            crate::server_log::warning(&format!("cannot create {player_dir}: {e}"));
        }
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
    fn remove_session(&mut self, _cid: ConnId, sess: Session) {
        let ip = ip_of(&sess.conn.remote).to_string();
        sess.conn.close();
        if let Some(n) = self.ip_count.get_mut(&ip) {
            *n -= 1;
            if *n <= 0 {
                self.ip_count.remove(&ip);
            }
        }
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
                SessionBroadcast::Tell { target, text } => {
                    // Java /tell: whisper to the named player, miss note
                    // back to the sender when offline.
                    let bytes = pkt_chat(&text);
                    let mut delivered = false;
                    if let Some(eid) = self.entity_named(&target) {
                        if let Some(cid) = self.players.get(&eid).copied() {
                            if let Some(other) = self.sessions.get(&cid) {
                                other.conn.send(bytes.clone());
                                delivered = true;
                            }
                        }
                    }
                    if !delivered {
                        sess.conn.send(pkt_chat("§cThere's no player by that name online."));
                    }
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
            let hp = match self.world.entities.get(eid) {
                Some(Entity::Player(p)) => p.living.health as i8,
                _ => 20,
            };
            play.outbox.push(pkt_health(hp));
            play.last_health = hp;
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
        // Drop the row explicitly: dead players are spared by the tick
        // purge (respawn needs them), so logout must clean up itself.
        self.world.entities.remove(eid);
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

    /// Graceful shutdown (mirrors the `run` tail): kick everyone with
    /// the players still listed (like C++, no leave chat here), save
    /// players with held snapshots, then flush the world.
    pub fn shutdown(&mut self) {
        self.running = false;
        log::info("Stopping server");
        let cids: Vec<ConnId> = self.sessions.keys().copied().collect();
        for cid in cids {
            if let Some(mut sess) = self.sessions.remove(&cid) {
                let held = match &mut sess.state {
                    SessionState::Play(play, _) => {
                        play.outbox.push(pkt_kick("Server shutting down"));
                        play.held_id
                    }
                    SessionState::Login(_) => 0,
                };
                sess.flush();
                if let SessionState::Play(play, _) = &sess.state {
                    self.save_player(play.player, held);
                }
                self.remove_session(cid, sess);
            }
        }
        self.save_world();
        log::info("Server stopped.");
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
    /// Initial chunk queue: the view square sorted center-out (Java
    /// `PlayerManager` sends the view, not padding — the 3x3 populate
    /// neighborhood is ensured per send, not queued).
    fn initial_stream(pcx: i32, pcz: i32, view: i32) -> PlayStream {
        let mut queue = Vec::new();
        for cx in pcx - view..=pcx + view {
            for cz in pcz - view..=pcz + view {
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

    /// Tracker pass (mirrors `EntityTracker::tick`): destroy packets for
    /// retired entries (dead or vanished rows, e.g. killed mobs that used
    /// to hang client-side), then per-entity updates for the live ones.
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
        // Retire dead/vanished entries with destroy packets (mirrors
        // `EntityTrackerEntry.func_604_a`), so killed mobs stop hanging
        // client-side. Runs before the tick loop on a shared outbox.
        let mut out = Vec::new();
        let gone: Vec<EntityId> =
            self.world.tracker.tracked_ids().into_iter().filter(|id| !live.contains(id)).collect();
        for id in gone {
            self.world.tracker.remove(id, &mut out);
        }
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
                // Java EntityTracker: player 512/2, item 64/20, arrow 64/5,
                // boat 160/5, mobs+animals 160/3 — each clamped to the view
                // distance in blocks (EntityTracker.java:42-44).
                Some(e @ Entity::Player(_)) => TrackedEntity::from_entity(
                    e, held, 512.min(self.settings.view_distance * 16), 2, false,
                ),
                Some(e @ Entity::Item(_)) => TrackedEntity::from_entity(
                    e, 0, 64.min(self.settings.view_distance * 16), 20, true,
                ),
                Some(e @ Entity::Arrow(_)) => TrackedEntity::from_entity(
                    e, 0, 64.min(self.settings.view_distance * 16), 5, true,
                ),
                Some(e @ Entity::Boat(_)) => TrackedEntity::from_entity(
                    e, 0, 160.min(self.settings.view_distance * 16), 5, true,
                ),
                Some(e @ Entity::Mob(_)) | Some(e @ Entity::Animal(_)) => {
                    TrackedEntity::from_entity(
                        e, 0, 160.min(self.settings.view_distance * 16), 3, false,
                    )
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
    /// Ship pickup feedback (mirrors the collect packet in
    /// `EntityPlayerMP.onUpdate` plus the inventory sync): `Packet22Collect`
    /// to the item's watchers and picker (the client plays `random.pop`,
    /// flies the item over and removes it), then a full inventory sync for
    /// the picker. Drained before `tracker_tick` so Collect precedes the
    /// destroy for the dead item row.
    fn drain_pickup_events(&mut self) {
        let pickups = std::mem::take(&mut self.world.item_pickups);
        if pickups.is_empty() {
            return;
        }
        let mut out = Vec::new();
        for (item_id, player_id) in &pickups {
            self.world.tracker.collect_fx(*item_id, *player_id, &mut out);
        }
        self.route_outbox(out);
        for (_, player_id) in &pickups {
            if let Some(cid) = self.players.get(player_id).copied() {
                if let Some(Session { state: SessionState::Play(play, _), .. }) =
                    self.sessions.get_mut(&cid)
                {
                    play.send_inventory(&self.world);
                }
            }
        }
    }

    /// Ship death animations (mirrors the status-3 broadcast in
    /// `EntityLiving.onDeath` via `WorldServer.func_9206_a`): drained
    /// before `tracker_tick` so the animation precedes the destroy packet
    /// for the same tick's kills.
    fn drain_death_events(&mut self) {
        let deaths = std::mem::take(&mut self.world.death_events);
        let statuses = std::mem::take(&mut self.world.status_events);
        if deaths.is_empty() && statuses.is_empty() {
            return;
        }
        let mut out = Vec::new();
        for id in deaths {
            self.world.tracker.death_fx(id, &mut out);
        }
        for (id, status) in statuses {
            self.world.tracker.status_fx(id, status, &mut out);
        }
        self.route_outbox(out);
    }

    /// Health watch (mirrors the `Packet8` send in
    /// `EntityPlayerMP.func_175_i`): push 0x08 whenever a player's health
    /// changed since the last send, so fall, mob, burn and drown damage
    /// all reach the HUD — previously only login/respawn/eat synced it.
    fn push_health_changes(&mut self) {
        let mut changed: Vec<(ConnId, i8)> = Vec::new();
        for (eid, cid) in &self.players {
            let health = match self.world.entities.get(*eid) {
                Some(Entity::Player(p)) => p.living.health as i8,
                _ => continue,
            };
            let last = match self.sessions.get(cid) {
                Some(Session { state: SessionState::Play(play, _), .. }) => play.last_health,
                _ => continue,
            };
            if health != last {
                changed.push((*cid, health));
            }
        }
        for (cid, health) in changed {
            if let Some(sess) = self.sessions.get_mut(&cid) {
                sess.conn.send(pkt_health(health));
                if let SessionState::Play(play, _) = &mut sess.state {
                    play.last_health = health;
                }
            }
        }
    }

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
        self.drain_pickup_events();
        self.drain_death_events();
        self.push_health_changes();
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

    /// Console line entry point (mirrors `handleCommand`).
    fn handle_console(&mut self, line: &str) {
        let (tag, arg1, arg2, count) = parse_console(line);
        match tag {
            ConsoleCommandTag::Help => {
                log::info(
                    "Console commands:\n   help  or  ?               shows this message\n   kick <player>             removes a player from the server\n   ban <player>              bans a player from the server\n   pardon <player>           pardons a banned player\n   ban-ip <ip>               bans an IP address\n   pardon-ip <ip>            pardons a banned IP address\n   op <player>               turns a player into an op\n   deop <player>             removes op status\n   tp <player1> <player2>    teleports player1 to player2\n   give <player> <id> [num]  gives a player a resource\n   summon <mob> [count] [player] spawns debug mobs near player\n   tell <player> <message>   sends a private message\n   stop                      gracefully stops the server\n   save-all                  forces a server-wide level save\n   list                      lists all connected players\n   say <message>             broadcasts a message",
                );
            }
            ConsoleCommandTag::List => {
                log::info(&format!("Connected players: {}", self.player_list()));
            }
            ConsoleCommandTag::Stop => {
                log::info("Stopping the server..");
                self.running = false;
            }
            ConsoleCommandTag::SaveAll => {
                log::info("Forcing save..");
                self.save_world();
                self.save_players();
                log::info("Save complete.");
            }
            ConsoleCommandTag::SaveOff => {
                self.settings.auto_save_interval = 0;
                log::info("Automatic saving is now disabled.");
            }
            ConsoleCommandTag::SaveOn => {
                self.settings.auto_save_interval = AUTO_SAVE_INTERVAL_TICKS_DEFAULT;
                log::info("Automatic saving is now enabled.");
            }
            ConsoleCommandTag::Give => {
                self.console_give(&arg1, &arg2, count);
            }
            ConsoleCommandTag::Op => {
                self.ops.insert(admin_normalize(&arg1));
                self.save_ops();
                log::info(&format!("Opping {arg1}"));
                self.send_chat_to_player(&arg1, "§eYou are now op!");
            }
            ConsoleCommandTag::Deop => {
                self.ops.remove(&admin_normalize(&arg1));
                self.save_ops();
                log::info(&format!("De-opping {arg1}"));
                self.send_chat_to_player(&arg1, "§eYou are no longer op!");
            }
            ConsoleCommandTag::BanIp => {
                self.banned_ips.insert(admin_normalize(&arg1));
                write_list(&self.banned_ips_path, &self.banned_ips);
                log::info(&format!("Banning ip {arg1}"));
            }
            ConsoleCommandTag::PardonIp => {
                self.banned_ips.remove(&admin_normalize(&arg1));
                write_list(&self.banned_ips_path, &self.banned_ips);
                log::info(&format!("Pardoning ip {arg1}"));
            }
            ConsoleCommandTag::Ban => {
                self.banned_players.insert(admin_normalize(&arg1));
                write_list(&self.banned_players_path, &self.banned_players);
                log::info(&format!("Banning {arg1}"));
                self.kick_player(&arg1, "Banned by admin");
            }
            ConsoleCommandTag::Pardon => {
                self.banned_players.remove(&admin_normalize(&arg1));
                write_list(&self.banned_players_path, &self.banned_players);
                log::info(&format!("Pardoning {arg1}"));
            }
            ConsoleCommandTag::Kick => {
                if self.kick_player(&arg1, "Kicked by admin") {
                    log::info(&format!("Kicking {arg1}"));
                } else {
                    log::info(&format!("Can't find user {arg1}. No kick."));
                }
            }
            ConsoleCommandTag::Tp => {
                let (a, b) = (self.entity_named(&arg1), self.entity_named(&arg2));
                match (a, b) {
                    (None, _) => log::info(&format!("Can't find user {arg1}. No tp.")),
                    (_, None) => log::info(&format!("Can't find user {arg2}. No tp.")),
                    (Some(e1), Some(e2)) => {
                        let (pos, yaw, pitch) = match self.world.entities.get(e2) {
                            Some(e) => (e.body().pos, e.body().yaw, e.body().pitch),
                            None => return,
                        };
                        if let Some(cid) = self.players.get(&e1).copied() {
                            if let Some(sess) = self.sessions.get_mut(&cid) {
                                if let SessionState::Play(play, _) = &mut sess.state {
                                    play.teleport_to(
                                        &mut self.world,
                                        e1,
                                        pos[0],
                                        pos[1],
                                        pos[2],
                                        yaw,
                                        pitch,
                                    );
                                }
                            }
                        }
                        log::info(&format!("Teleporting {arg1} to {arg2}."));
                    }
                }
            }
            ConsoleCommandTag::Summon => {
                self.console_summon(&arg1, count, &arg2);
            }
            ConsoleCommandTag::Say => {
                log::info(&format!("[Server] {arg1}"));
                self.broadcast_chat(format!("§d[Server] {arg1}"));
            }
            ConsoleCommandTag::Tell => {
                log::info(&format!("[CONSOLE->{arg1}] {arg2}"));
                if !self.send_chat_to_player(&arg1, &format!("§7CONSOLE whispers {arg2}")) {
                    log::info("There's no player by that name online.");
                }
            }
            ConsoleCommandTag::Unknown => {
                log::info("Unknown console command. Type \"help\" for help.");
            }
        }
    }
}

impl Server {
    /// Comma-joined online names, sorted for determinism (C++ walks its
    /// join-ordered vector; order here is cosmetic admin output).
    fn player_list(&self) -> String {
        let mut names: Vec<String> = self
            .players
            .keys()
            .filter_map(|eid| match self.world.entities.get(*eid) {
                Some(Entity::Player(p)) => Some(p.username.clone()),
                _ => None,
            })
            .collect();
        names.sort();
        names.join(", ")
    }

    /// Chat to one named player (mirrors `sendChatToPlayer`).
    fn send_chat_to_player(&mut self, name: &str, msg: &str) -> bool {
        let bytes = pkt_chat(msg);
        match self.entity_named(name).and_then(|eid| self.players.get(&eid).copied()) {
            Some(cid) => match self.sessions.get(&cid) {
                Some(sess) => {
                    sess.conn.send(bytes);
                    true
                }
                None => false,
            },
            None => false,
        }
    }

    /// Player entity by case-insensitive name (mirrors `getPlayerEntity`).
    fn entity_named(&self, name: &str) -> Option<EntityId> {
        let lower = admin_normalize(name);
        self.players.keys().find_map(|eid| match self.world.entities.get(*eid) {
            Some(Entity::Player(p)) if admin_normalize(&p.username) == lower => Some(*eid),
            _ => None,
        })
    }

    fn save_ops(&self) {
        let ordered: BTreeSet<String> = self.ops.iter().cloned().collect();
        write_list(&self.ops_path, &ordered);
    }

    /// Console give (mirrors `give <player> <id> [count]`): validates the
    /// id, merges through the shared inventory path, syncs the session.
    fn console_give(&mut self, target: &str, id_raw: &str, count: i32) {
        let eid = match self.entity_named(target) {
            Some(e) => e,
            None => {
                log::info(&format!("Can't find user {target}. No give."));
                return;
            }
        };
        let id: i32 = match id_raw.parse() {
            Ok(v) => v,
            Err(_) => {
                log::info(&format!("Invalid item id {id_raw}."));
                return;
            }
        };
        if !crate::item_data::alpha_item_is_valid(id) {
            log::info(&format!("Invalid item id {id}."));
            return;
        }
        let count = count.clamp(1, 64);
        let rem = self.world.player_add_item(
            eid,
            crate::inventory::FfiItemStack {
                stack_size: count,
                animations_to_go: 0,
                item_id: id,
                item_damage: 0,
            },
        );
        let given = count - rem;
        if let Some(cid) = self.players.get(&eid).copied() {
            if let Some(sess) = self.sessions.get_mut(&cid) {
                if let SessionState::Play(play, _) = &mut sess.state {
                    play.send_inventory(&self.world);
                }
            }
        }
        if rem > 0 {
            log::info(&format!("Gave {given} of {id} to {target} ({rem} did not fit)."));
        } else {
            log::info(&format!("Gave {given} of {id} to {target}."));
        }
    }

    /// Debug spawner (mirrors the console `summon`): ring placement
    /// around the anchor with ground clamp and random yaw.
    fn console_summon(&mut self, raw_name: &str, count: i32, target: &str) {
        if raw_name.is_empty() {
            log::info("Usage: summon <mob> [count] [player]");
            return;
        }
        let anchor = if !target.is_empty() {
            match self.entity_named(target) {
                Some(e) => e,
                None => {
                    log::info(&format!("Can't find user {target}. No summon."));
                    return;
                }
            }
        } else if let Some(oldest) = self.players.keys().copied().min() {
            oldest
        } else {
            log::info("No online players to anchor summon.");
            return;
        };
        let name = raw_name.to_ascii_lowercase();
        enum Kind {
            Mob(MobKind),
            Animal(AnimalKind),
        }
        let kind = match name.as_str() {
            "pig" => Kind::Animal(AnimalKind::Pig),
            "sheep" => Kind::Animal(AnimalKind::Sheep),
            "cow" => Kind::Animal(AnimalKind::Cow),
            "chicken" => Kind::Animal(AnimalKind::Chicken),
            "zombie" => Kind::Mob(MobKind::Zombie),
            "skeleton" => Kind::Mob(MobKind::Skeleton),
            "spider" => Kind::Mob(MobKind::Spider),
            "creeper" => Kind::Mob(MobKind::Creeper),
            _ => {
                log::info(&format!(
                    "Unknown mob {raw_name}. Try pig/sheep/cow/chicken/zombie/skeleton/spider/creeper."
                ));
                return;
            }
        };
        let apos = match self.world.entities.get(anchor) {
            Some(e) => e.body().pos,
            None => return,
        };
        let anchor_name = match self.world.entities.get(anchor) {
            Some(Entity::Player(p)) => p.username.clone(),
            _ => String::new(),
        };
        let count = count.clamp(1, 64);
        for i in 0..count {
            let angle = (i as f64 / count.max(1) as f64) * std::f64::consts::TAU;
            let radius = 2.0 + (i % 3) as f64;
            let sx = apos[0] + angle.cos() * radius;
            let sz = apos[2] + angle.sin() * radius;
            let ground = self.world.get_height_value(sx.floor() as i32, sz.floor() as i32) as f64;
            let sy = apos[1].max(ground);
            let id = self.world.entities.alloc_id();
            let yaw = self.world.rng_next_f32() * 360.0;
            match kind {
                Kind::Mob(k) => {
                    let mut m = MobEnt::new(id, k);
                    m.living.body.set_position(sx, sy, sz);
                    m.living.body.yaw = yaw;
                    self.world.entities.insert(Entity::Mob(m));
                }
                Kind::Animal(k) => {
                    let mut a = AnimalEnt::new(id, k);
                    a.living.body.set_position(sx, sy, sz);
                    a.living.body.yaw = yaw;
                    self.world.entities.insert(Entity::Animal(a));
                }
            }
        }
        log::info(&format!("Spawned {count} {raw_name} near {anchor_name}."));
    }
}

/// Owned console parse (the FFI parser borrows the input line, so the
/// slices are copied out before the call returns).
fn parse_console(line: &str) -> (ConsoleCommandTag, String, String, i32) {
    fn take(f: FfiString) -> String {
        if f.ptr.is_null() || f.len == 0 {
            return String::new();
        }
        // SAFETY: `FfiString` borrows from `line`, which outlives `parsed`
        // below; `take` copies out before `line` is dropped.
        let bytes =
            unsafe { std::slice::from_raw_parts(f.ptr as *const u8, f.len as usize) };
        String::from_utf8_lossy(bytes).into_owned()
    }
    let parsed = rust_parse_console_command(line);
    (parsed.tag, take(parsed.arg1), take(parsed.arg2), parsed.count)
}

#[cfg(test)]
#[path = "server_tests.rs"]
mod tests;

