use libc::{c_char, size_t};

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum ConsoleCommandTag {
    Help = 0,
    List,
    Stop,
    SaveAll,
    SaveOff,
    SaveOn,
    Op,
    Deop,
    BanIp,
    PardonIp,
    Ban,
    Pardon,
    Kick,
    Tp,
    Give,
    Summon,
    Say,
    Tell,
    Unknown,
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct FfiString {
    pub ptr: *const c_char,
    pub len: size_t,
}

impl FfiString {
    pub fn from_str(s: &str) -> Self {
        // Never hand out null: an empty arg points at a static NUL so
        // callers can always read `len` bytes safely.
        static NUL: u8 = 0;
        Self {
            ptr: if s.is_empty() {
                &NUL as *const u8 as *const c_char
            } else {
                s.as_ptr() as *const c_char
            },
            len: s.len() as size_t,
        }
    }
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct RustParsedCommand {
    pub tag: ConsoleCommandTag,
    pub arg1: FfiString,
    pub arg2: FfiString,
    pub count: i32,
}

fn trim_left(s: &str) -> &str {
    s.trim_start_matches(' ')
}

fn split_two(s: &str) -> (&str, &str) {
    match s.find(' ') {
        Some(idx) => {
            let first = &s[..idx];
            let second = trim_left(&s[idx + 1..]);
            (first, second)
        }
        None => (s, ""),
    }
}

fn arg_of(sv: &str, prefix_len: usize) -> &str {
    if prefix_len >= sv.len() {
        ""
    } else {
        trim_left(&sv[prefix_len..])
    }
}

pub fn rust_parse_console_command(cmd: &str) -> RustParsedCommand {
    if cmd.is_empty() {
        return RustParsedCommand {
            tag: ConsoleCommandTag::Unknown,
            arg1: FfiString::from_str(""),
            arg2: FfiString::from_str(""),
            count: 1,
        };
    }

    let lower = cmd.to_lowercase();

    let mut tag = ConsoleCommandTag::Unknown;
    let mut arg1 = "";
    let mut arg2 = "";
    let mut count = 1;

    if lower.starts_with("help") || lower.starts_with("?") {
        tag = ConsoleCommandTag::Help;
    } else if lower.starts_with("list") {
        tag = ConsoleCommandTag::List;
    } else if lower.starts_with("stop") {
        tag = ConsoleCommandTag::Stop;
    } else if lower.starts_with("save-all") {
        tag = ConsoleCommandTag::SaveAll;
    } else if lower.starts_with("save-off") {
        tag = ConsoleCommandTag::SaveOff;
    } else if lower.starts_with("save-on") {
        tag = ConsoleCommandTag::SaveOn;
    } else if lower.starts_with("op ") {
        tag = ConsoleCommandTag::Op;
        arg1 = arg_of(cmd, 3);
    } else if lower.starts_with("deop ") {
        tag = ConsoleCommandTag::Deop;
        arg1 = arg_of(cmd, 5);
    } else if lower.starts_with("ban-ip ") {
        tag = ConsoleCommandTag::BanIp;
        arg1 = arg_of(cmd, 7);
    } else if lower.starts_with("pardon-ip ") {
        tag = ConsoleCommandTag::PardonIp;
        arg1 = arg_of(cmd, 10);
    } else if lower.starts_with("ban ") {
        tag = ConsoleCommandTag::Ban;
        arg1 = arg_of(cmd, 4);
    } else if lower.starts_with("pardon ") {
        tag = ConsoleCommandTag::Pardon;
        arg1 = arg_of(cmd, 7);
    } else if lower.starts_with("kick ") {
        tag = ConsoleCommandTag::Kick;
        arg1 = arg_of(cmd, 5);
    } else if lower.starts_with("tp ") {
        tag = ConsoleCommandTag::Tp;
        let rest = arg_of(cmd, 3);
        let (p1, p2) = split_two(rest);
        arg1 = p1;
        arg2 = p2;
    } else if lower.starts_with("say ") {
        tag = ConsoleCommandTag::Say;
        arg1 = arg_of(cmd, 4);
    } else if lower.starts_with("tell ") {
        tag = ConsoleCommandTag::Tell;
        let rest = arg_of(cmd, 5);
        let (target, msg) = split_two(rest);
        arg1 = target;
        arg2 = msg;
    } else if lower.starts_with("give ") {
        // Java: give <player> <id> [count]. count clamps 1..64.
        tag = ConsoleCommandTag::Give;
        let rest = arg_of(cmd, 5);
        let (target, tail) = split_two(rest);
        arg1 = target;
        let (id_str, count_str) = split_two(tail);
        arg2 = id_str;
        if !count_str.is_empty() {
            if let Ok(c) = count_str.parse::<i32>() {
                count = std::cmp::max(1, std::cmp::min(64, c));
            }
        }
    } else if lower.starts_with("summon ") {
        tag = ConsoleCommandTag::Summon;
        let rest = arg_of(cmd, 7);
        let (entity_name_raw, tail) = split_two(rest);
        arg1 = entity_name_raw;

        if !tail.is_empty() {
            let (arg1_str, arg2_str) = split_two(tail);
            if !arg1_str.is_empty() {
                let is_number = arg1_str.chars().all(|c| c.is_ascii_digit());
                if is_number {
                    if let Ok(c) = arg1_str.parse::<i32>() {
                        count = std::cmp::max(1, std::cmp::min(64, c));
                    }
                    arg2 = arg2_str;
                } else {
                    arg2 = arg1_str;
                }
            }
        }
    }

    RustParsedCommand {
        tag,
        arg1: FfiString::from_str(arg1),
        arg2: FfiString::from_str(arg2),
        count,
    }
}
