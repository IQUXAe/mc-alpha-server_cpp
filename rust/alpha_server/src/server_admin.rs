//! Server administration ported from C++ `ServerConfigurationManager`
//! (ban/op/IP lists) and `NetServerHandler::handleCommand` (player
//! `/give` and `/tp`) — mirrors Java `ServerConfigurationManager` lists
//! and vanilla op commands.
//!
//! Two layers:
//! - Name lists: lowercase normalization, `banned-*.txt`/`ops.txt` file
//!   format (trim trailing space/CR/LF, skip empties, sorted+deduplicated
//!   like `std::set`), membership and mutation.
//! - Chat driver: tokenizing, strict argument parsing, permission and range
//!   checks, and effect callbacks into C++ (give/teleport/chat). C++ keeps
//!   sockets, the entity table, and the world.
//!
//! Number parsing is deliberately STRICT (trailing garbage rejected, like
//! Java `parseInt`/`parseDouble`): the old C++ `from_chars` accepted
//! prefixes such as `12ab` as `12`. Doubles must additionally be finite
//! (`from_chars` rejects `inf`/`nan`, Rust `parse` does not).

use std::collections::BTreeSet;

/// Lowercase ASCII like the C++ `::tolower` loop over latin names.
pub fn admin_normalize(name: &str) -> String {
    name.to_ascii_lowercase()
}

/// Parse a ban/op list file body: trim trailing space/CR/LF per line,
/// skip empties, insert lowercased (sorted+deduplicated via `BTreeSet`,
/// matching `std::set<std::string>` iteration order on save).
pub fn admin_parse_list(body: &str) -> BTreeSet<String> {
    let mut out = BTreeSet::new();
    for line in body.lines() {
        let trimmed = line.trim_end_matches(['\r', '\n', ' ']);
        if !trimmed.is_empty() {
            out.insert(admin_normalize(trimmed));
        }
    }
    out
}

/// Serialize a list back (one entry per line, trailing newline each).
pub fn admin_render_list(list: &BTreeSet<String>) -> String {
    let mut out = String::new();
    for entry in list {
        out.push_str(entry);
        out.push('\n');
    }
    out
}

/// Strict `from_chars<int>`-shaped parse used by command args: optional
/// sign, ASCII digits, nothing else. Returns the value on full match.
pub fn parse_command_int(s: &str) -> Option<i32> {
    let b = s.as_bytes();
    if b.is_empty() {
        return None;
    }
    let mut i = 0;
    let neg = if b[0] == b'+' || b[0] == b'-' {
        i = 1;
        b[0] == b'-'
    } else {
        false
    };
    if i >= b.len() || !b[i].is_ascii_digit() {
        return None;
    }
    let mut acc: i64 = 0;
    while i < b.len() && b[i].is_ascii_digit() {
        acc = acc * 10 + (b[i] - b'0') as i64;
        if acc > i32::MAX as i64 + 1 {
            return None;
        }
        i += 1;
    }
    if i != b.len() {
        return None;
    }
    let v = if neg { -acc } else { acc };
    if v < i32::MIN as i64 || v > i32::MAX as i64 {
        return None;
    }
    Some(v as i32)
}

/// Strict finite-double parse for command args: `[+-]? (digits [. digits]
/// | . digits) ([eE] [+-]? digits)?`, full match, finite result.
pub fn parse_command_double(s: &str) -> Option<f64> {
    let b = s.as_bytes();
    if b.is_empty() {
        return None;
    }
    let mut i = 0;
    if b[0] == b'+' || b[0] == b'-' {
        i = 1;
    }
    let mut digits = 0;
    while i < b.len() && b[i].is_ascii_digit() {
        digits += 1;
        i += 1;
    }
    if i < b.len() && b[i] == b'.' {
        i += 1;
        while i < b.len() && b[i].is_ascii_digit() {
            digits += 1;
            i += 1;
        }
    }
    if digits == 0 {
        return None;
    }
    if i < b.len() && (b[i] == b'e' || b[i] == b'E') {
        i += 1;
        if i < b.len() && (b[i] == b'+' || b[i] == b'-') {
            i += 1;
        }
        let mut exp_digits = 0;
        while i < b.len() && b[i].is_ascii_digit() {
            exp_digits += 1;
            i += 1;
        }
        if exp_digits == 0 {
            return None;
        }
    }
    if i != b.len() {
        return None;
    }
    let v: f64 = s.parse().ok()?;
    if v.is_finite() {
        Some(v)
    } else {
        None
    }
}

/// Effects for the chat driver. All strings cross as `(ptr, len)` UTF-8.
/// `send_chat` delivers one line back to the issuing player.
#[repr(C)]
pub struct ChatWorld {
    pub is_op: Option<extern "C" fn() -> bool>,
    pub block_registered: Option<extern "C" fn(id: i32) -> bool>,
    pub give_item: Option<extern "C" fn(item_id: i32, count: i32, damage: i32)>,
    pub teleport: Option<extern "C" fn(x: f64, y: f64, z: f64, yaw: f32, pitch: f32)>,
    pub send_chat: Option<extern "C" fn(msg_ptr: *const u8, msg_len: usize)>,
}

fn chat(world: &ChatWorld, msg: &str) {
    if let Some(f) = world.send_chat {
        f(msg.as_ptr(), msg.len());
    }
}

fn is_op(world: &ChatWorld) -> bool {
    world.is_op.map(|f| f()).unwrap_or(false)
}

/// Player `/give` + `/tp` dispatcher (mirrors
/// `NetServerHandler::handleCommand`). `msg` must start with `/`; anything
/// else is ignored. All user-facing strings match the C++ originals.
#[no_mangle]
pub unsafe extern "C" fn rust_chat_command(
    world: *const ChatWorld,
    msg_ptr: *const u8,
    msg_len: usize,
    player_yaw: f32,
    player_pitch: f32,
) {
    if world.is_null() || msg_ptr.is_null() {
        return;
    }
    let world = unsafe { &*world };
    let raw = unsafe { std::slice::from_raw_parts(msg_ptr, msg_len) };
    let Ok(msg) = std::str::from_utf8(raw) else {
        return;
    };
    let body = match msg.strip_prefix('/') {
        Some(b) => b,
        None => return,
    };
    let args: Vec<&str> = body.split_whitespace().collect();
    if args.is_empty() {
        return;
    }
    let cmd = args[0];
    if !is_op(world) && (cmd == "give" || cmd == "tp") {
        chat(world, "You do not have permission to use this command");
        return;
    }
    if cmd == "give" {
        if args.len() < 2 {
            chat(world, "Usage: /give <itemId> [count] [damage]");
            return;
        }
        let (Some(item_id), count, damage) = (
            parse_command_int(args[1]),
            args.get(2).and_then(|s| parse_command_int(s)).unwrap_or(1),
            args.get(3).and_then(|s| parse_command_int(s)).unwrap_or(0),
        )
        else {
            chat(world, "Invalid command arguments");
            return;
        };
        // NOTE: `args.get(2)` on a missing slot yields the default WITHOUT
        // parsing; only present-but-broken tokens error out. Re-check:
        if (args.len() >= 3 && parse_command_int(args[2]).is_none())
            || (args.len() >= 4 && parse_command_int(args[3]).is_none())
        {
            chat(world, "Invalid command arguments");
            return;
        }
        let count = count.clamp(1, 64);
        if item_id <= 0 || item_id >= 32000 {
            chat(world, "Invalid item id");
            return;
        }
        if item_id < 256 {
            let registered = world.block_registered.map(|f| f(item_id)).unwrap_or(false);
            if !registered {
                chat(world, &format!("Unknown block id: {item_id}"));
                return;
            }
        }
        if let Some(f) = world.give_item {
            f(item_id, count, damage);
        }
        chat(world, &format!("Gave {count}x {item_id}"));
    } else if cmd == "tp" {
        if args.len() < 4 {
            chat(world, "Usage: /tp <x> <y> <z>");
            return;
        }
        let (Some(tx), Some(ty), Some(tz)) = (
            parse_command_double(args[1]),
            parse_command_double(args[2]),
            parse_command_double(args[3]),
        )
        else {
            chat(world, "Invalid command arguments");
            return;
        };
        if let Some(f) = world.teleport {
            f(tx, ty, tz, player_yaw, player_pitch);
        }
        // C++ std::to_string(double) prints 6 decimals; match it exactly.
        chat(world, &format!("Teleported to {tx:.6}, {ty:.6}, {tz:.6}"));
    } else {
        chat(world, &format!("Unknown command: {cmd}"));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Mutex, MutexGuard};

    #[test]
    fn test_list_roundtrip() {
        assert_eq!(admin_normalize("NotCH"), "notch");
        let parsed = admin_parse_list("Notch\n\n  \nsteve \r\nNOTCH\n");
        let mut expected = BTreeSet::new();
        expected.insert("notch".to_string());
        expected.insert("steve".to_string());
        assert_eq!(parsed, expected);
        assert_eq!(admin_render_list(&parsed), "notch\nsteve\n");
        assert_eq!(admin_render_list(&BTreeSet::new()), "");
    }

    #[test]
    fn test_strict_int() {
        assert_eq!(parse_command_int("5"), Some(5));
        assert_eq!(parse_command_int("-12"), Some(-12));
        assert_eq!(parse_command_int("+7"), Some(7));
        assert_eq!(parse_command_int("12ab"), None);
        assert_eq!(parse_command_int(""), None);
        assert_eq!(parse_command_int("1_0"), None);
        assert_eq!(parse_command_int("9999999999"), None);
    }

    #[test]
    fn test_strict_double() {
        assert_eq!(parse_command_double("1.5"), Some(1.5));
        assert_eq!(parse_command_double("-3"), Some(-3.0));
        assert_eq!(parse_command_double("1e3"), Some(1000.0));
        assert_eq!(parse_command_double("inf"), None);
        assert_eq!(parse_command_double("nan"), None);
        assert_eq!(parse_command_double("1.5x"), None);
        assert_eq!(parse_command_double(""), None);
        assert_eq!(parse_command_double("1_0"), None);
    }

    struct Fake {
        log: Vec<String>,
        gave: Vec<(i32, i32, i32)>,
        teleported: Vec<(f64, f64, f64)>,
        op: bool,
    }

    static FAKE: Mutex<Option<Fake>> = Mutex::new(None);

    fn fake() -> MutexGuard<'static, Option<Fake>> {
        match FAKE.lock() {
            Ok(g) => g,
            Err(poisoned) => poisoned.into_inner(),
        }
    }

    fn reset(op: bool) {
        *fake() = Some(Fake { log: Vec::new(), gave: Vec::new(), teleported: Vec::new(), op });
    }

    extern "C" fn s_is_op() -> bool {
        fake().as_ref().map(|f| f.op).unwrap_or(false)
    }
    extern "C" fn s_registered(id: i32) -> bool {
        (1..100).contains(&id)
    }
    extern "C" fn s_give(id: i32, count: i32, damage: i32) {
        if let Some(f) = fake().as_mut() {
            f.gave.push((id, count, damage));
        }
    }
    extern "C" fn s_tp(x: f64, y: f64, z: f64, _yaw: f32, _pitch: f32) {
        if let Some(f) = fake().as_mut() {
            f.teleported.push((x, y, z));
        }
    }
    extern "C" fn s_chat(ptr: *const u8, len: usize) {
        if let Some(f) = fake().as_mut() {
            let s = unsafe { std::slice::from_raw_parts(ptr, len) };
            f.log.push(String::from_utf8_lossy(s).into_owned());
        }
    }

    fn table() -> ChatWorld {
        ChatWorld {
            is_op: Some(s_is_op),
            block_registered: Some(s_registered),
            give_item: Some(s_give),
            teleport: Some(s_tp),
            send_chat: Some(s_chat),
        }
    }

    fn run(cmd: &str) {
        let t = table();
        unsafe { rust_chat_command(&t, cmd.as_ptr(), cmd.len(), 0.0, 0.0) };
    }

    fn logs() -> Vec<String> {
        fake().as_ref().map(|f| f.log.clone()).unwrap_or_default()
    }

    #[test]
    fn test_chat_driver() {
        // Non-command input ignored.
        reset(true);
        run("hello");
        assert!(logs().is_empty());

        // Permission gate.
        reset(false);
        run("/give 5");
        assert_eq!(logs(), vec!["You do not have permission to use this command"]);

        // Give happy path with clamping.
        reset(true);
        run("/give 5 99 2");
        {
            let g = fake();
            let f = g.as_ref().unwrap_or_else(|| unreachable!());
            assert_eq!(f.gave, vec![(5, 64, 2)]);
        }
        assert_eq!(logs(), vec!["Gave 64x 5"]);

        // Give validation errors.
        reset(true);
        run("/give");
        assert_eq!(logs(), vec!["Usage: /give <itemId> [count] [damage]"]);
        reset(true);
        run("/give 0");
        assert_eq!(logs(), vec!["Invalid item id"]);
        reset(true);
        run("/give 200");
        assert_eq!(logs(), vec!["Unknown block id: 200"]);
        reset(true);
        run("/give 5x");
        assert_eq!(logs(), vec!["Invalid command arguments"]);

        // Tp happy path (6-decimal message like to_string).
        reset(true);
        run("/tp 10 64 -3");
        {
            let g = fake();
            let f = g.as_ref().unwrap_or_else(|| unreachable!());
            assert_eq!(f.teleported, vec![(10.0, 64.0, -3.0)]);
        }
        assert_eq!(logs(), vec!["Teleported to 10.000000, 64.000000, -3.000000"]);
        reset(true);
        run("/tp 10 64");
        assert_eq!(logs(), vec!["Usage: /tp <x> <y> <z>"]);
        reset(true);
        run("/tp 10 oo 3");
        assert_eq!(logs(), vec!["Invalid command arguments"]);

        // Unknown command.
        reset(true);
        run("/dance");
        assert_eq!(logs(), vec!["Unknown command: dance"]);

        // Null table is safe.
        unsafe { rust_chat_command(std::ptr::null(), "x".as_ptr(), 1, 0.0, 0.0) };
    }
}
