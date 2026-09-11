//! Port of `src/core/Logger.h` + `src/core/Logger.cpp`.
//!
//! Levels, filtering and the timestamped format are 1:1 with C++:
//! `YYYY-MM-DD HH:MM:SS.mmm [PREFIX] message` with prefixes `[D]`,
//! `[INFO]`, `[WARNING]` and `[SEVERE]`. Messages below the static minimum
//! level (default `INFO`) are dropped, and warnings/errors go to stderr
//! while the rest goes to stdout.
//!
//! Differences: logging is synchronous (the C++ background flusher queue is
//! omitted as trivial), and the console-line provider is an optional boxed
//! closure re-printed after each line, without any extra dependencies.

use std::io::Write;
use std::sync::{
    Mutex, OnceLock,
    atomic::{AtomicU8, Ordering},
};

/// Severity levels, ordered `DEBUG < INFO < WARNING < SEVERE` like the C++
/// `enum class LogLevel` so `<` filtering matches.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u8)]
pub enum LogLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Severe = 3,
}

impl LogLevel {
    /// Wire prefix used in the formatted line (mirrors `formatMessage`).
    pub const fn prefix(self) -> &'static str {
        match self {
            LogLevel::Debug => "[D]",
            LogLevel::Info => "[INFO]",
            LogLevel::Warning => "[WARNING]",
            LogLevel::Severe => "[SEVERE]",
        }
    }

    fn from_u8(value: u8) -> Self {
        match value {
            0 => LogLevel::Debug,
            2 => LogLevel::Warning,
            3 => LogLevel::Severe,
            _ => LogLevel::Info,
        }
    }
}

static MIN_LEVEL: AtomicU8 = AtomicU8::new(LogLevel::Info as u8);

/// Mirrors `Logger::setLevel()`.
pub fn set_level(level: LogLevel) {
    MIN_LEVEL.store(level as u8, Ordering::Relaxed);
}

/// Mirrors `Logger::getLevel()`.
pub fn get_level() -> LogLevel {
    LogLevel::from_u8(MIN_LEVEL.load(Ordering::Relaxed))
}

/// True when `level` passes the current minimum-level filter.
pub fn should_log(level: LogLevel) -> bool {
    get_level() <= level
}

type ConsoleLineProvider = Box<dyn Fn() -> String + Send + Sync>;

fn provider_slot() -> &'static Mutex<Option<ConsoleLineProvider>> {
    static SLOT: OnceLock<Mutex<Option<ConsoleLineProvider>>> = OnceLock::new();
    SLOT.get_or_init(|| Mutex::new(None))
}

fn lock_slot() -> std::sync::MutexGuard<'static, Option<ConsoleLineProvider>> {
    match provider_slot().lock() {
        Ok(guard) => guard,
        Err(poisoned) => poisoned.into_inner(),
    }
}

/// Mirrors `Logger::setConsoleLineProvider()`.
pub fn set_console_line_provider(provider: ConsoleLineProvider) {
    *lock_slot() = Some(provider);
}

/// Removes the console-line provider, if any.
pub fn clear_console_line_provider() {
    *lock_slot() = None;
}

fn current_console_line() -> Option<String> {
    let slot = lock_slot();
    match slot.as_ref() {
        Some(provider) => Some(provider()),
        None => None,
    }
}

fn stdout_is_tty() -> bool {
    (unsafe { libc::isatty(libc::STDOUT_FILENO) }) != 0
}

fn clear_console_line() {
    if stdout_is_tty() {
        print!("\r\x1b[2K\r");
    }
}

fn print_console_line() {
    if let Some(line) = current_console_line() {
        clear_console_line();
        print!("{line}");
        let _ = std::io::stdout().flush();
    }
}

/// Local-time stamp in the C++ `put_time` layout `%Y-%m-%d %H:%M:%S.mmm`.
fn current_timestamp() -> String {
    let now = std::time::SystemTime::now();
    let since_epoch = match now.duration_since(std::time::UNIX_EPOCH) {
        Ok(elapsed) => elapsed,
        Err(_) => std::time::Duration::ZERO,
    };
    let secs: libc::time_t = since_epoch.as_secs() as libc::time_t;
    let millis = since_epoch.subsec_millis();
    let mut broken = std::mem::MaybeUninit::<libc::tm>::uninit();
    let filled = unsafe { libc::localtime_r(&secs, broken.as_mut_ptr()) };
    if filled.is_null() {
        return format!("1970-01-01 00:00:00.{millis:03} ");
    }
    let tm = unsafe { broken.assume_init() };
    format!(
        "{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{millis:03} ",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec,
    )
}

/// Mirrors the C++ `formatMessage()`: timestamp, prefix and message.
pub fn format_message(level: LogLevel, msg: &str) -> String {
    format!("{}{} {}", current_timestamp(), level.prefix(), msg)
}

/// Mirrors `Logger::log()`: drops filtered levels, prints the formatted
/// line (stderr for warnings/errors), then re-prints the console line.
pub fn log(level: LogLevel, msg: &str) {
    if !should_log(level) {
        return;
    }
    let line = format_message(level, msg);
    match level {
        LogLevel::Warning | LogLevel::Severe => eprintln!("{line}"),
        LogLevel::Debug | LogLevel::Info => println!("{line}"),
    }
    print_console_line();
}

/// Mirrors `Logger::debug()`.
pub fn debug(msg: &str) {
    log(LogLevel::Debug, msg);
}

/// Mirrors `Logger::info()`.
pub fn info(msg: &str) {
    log(LogLevel::Info, msg);
}

/// Mirrors `Logger::warning()`.
pub fn warning(msg: &str) {
    log(LogLevel::Warning, msg);
}

/// Mirrors `Logger::severe()`.
pub fn severe(msg: &str) {
    log(LogLevel::Severe, msg);
}

/// Mirrors `Logger::refreshConsoleLine()`.
pub fn refresh_console_line() {
    print_console_line();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn levels_are_ordered_like_cpp_enum() {
        assert!(LogLevel::Debug < LogLevel::Info);
        assert!(LogLevel::Info < LogLevel::Warning);
        assert!(LogLevel::Warning < LogLevel::Severe);
    }

    #[test]
    fn prefixes_match_cpp_format_message() {
        assert_eq!(LogLevel::Debug.prefix(), "[D]");
        assert_eq!(LogLevel::Info.prefix(), "[INFO]");
        assert_eq!(LogLevel::Warning.prefix(), "[WARNING]");
        assert_eq!(LogLevel::Severe.prefix(), "[SEVERE]");
    }

    #[test]
    fn formatted_line_carries_prefix_and_message() {
        for level in [
            LogLevel::Debug,
            LogLevel::Info,
            LogLevel::Warning,
            LogLevel::Severe,
        ] {
            let line = format_message(level, "hello");
            assert!(line.contains(level.prefix()));
            assert!(line.ends_with("hello"));
            // Timestamp shape: "YYYY-MM-DD HH:MM:SS.mmm ...".
            assert_eq!(line.as_bytes().get(4), Some(&b'-'));
            assert_eq!(line.as_bytes().get(10), Some(&b' '));
            assert_eq!(line.as_bytes().get(19), Some(&b'.'));
        }
    }

    #[test]
    fn unknown_stored_level_falls_back_to_info() {
        assert_eq!(LogLevel::from_u8(0), LogLevel::Debug);
        assert_eq!(LogLevel::from_u8(1), LogLevel::Info);
        assert_eq!(LogLevel::from_u8(2), LogLevel::Warning);
        assert_eq!(LogLevel::from_u8(3), LogLevel::Severe);
        assert_eq!(LogLevel::from_u8(99), LogLevel::Info);
    }

    // Single global-mutating test so parallel test threads cannot race on
    // the shared minimum level / console-line provider.
    #[test]
    fn level_filtering_and_console_provider() {
        set_level(LogLevel::Info);
        assert_eq!(get_level(), LogLevel::Info);
        assert!(!should_log(LogLevel::Debug));
        assert!(should_log(LogLevel::Info));
        assert!(should_log(LogLevel::Warning));
        assert!(should_log(LogLevel::Severe));

        set_level(LogLevel::Warning);
        assert!(!should_log(LogLevel::Info));
        assert!(should_log(LogLevel::Warning));
        assert!(should_log(LogLevel::Severe));

        set_level(LogLevel::Debug);
        assert!(should_log(LogLevel::Debug));

        set_level(LogLevel::Severe);
        assert!(!should_log(LogLevel::Warning));
        assert!(should_log(LogLevel::Severe));

        // Filtered-out lines are dropped without touching the provider.
        set_console_line_provider(Box::new(|| "> ".to_string()));
        log(LogLevel::Debug, "dropped");
        // Visible lines and explicit refresh re-print the provider line.
        log(LogLevel::Severe, "shown");
        refresh_console_line();
        clear_console_line_provider();
        refresh_console_line();

        set_level(LogLevel::Info);
        assert_eq!(get_level(), LogLevel::Info);
    }

    #[test]
    fn level_helpers_do_not_panic() {
        set_level(LogLevel::Debug);
        debug("d");
        info("i");
        warning("w");
        severe("s");
        set_level(LogLevel::Info);
    }
}
