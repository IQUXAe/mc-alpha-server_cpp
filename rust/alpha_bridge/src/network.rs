use std::net::{TcpStream, Shutdown};
use std::thread;
use std::sync::{Arc, Mutex, Condvar};
use std::sync::atomic::{AtomicBool, Ordering, AtomicUsize};
use std::collections::VecDeque;
use libc::{c_char, c_int, size_t};
use std::ffi::CStr;
use std::io::{Read, Write};

#[repr(C)]
pub struct RustNetworkManager {
    is_running: Arc<AtomicBool>,
    is_server_terminating: Arc<AtomicBool>,
    is_terminating: Arc<AtomicBool>,
    termination_reason: Arc<Mutex<String>>,
    
    // Read queue (popped by C++ processReadPackets)
    read_queue: Arc<Mutex<VecDeque<(u8, Vec<u8>)>>>,
    
    // Write queue (pushed by C++ addToSendQueue, popped by Rust write thread)
    // Tuple: (payload_bytes, is_chunk_data)
    write_queue: Arc<(Mutex<VecDeque<(Vec<u8>, bool)>>, Condvar)>,
    
    send_queue_byte_length: Arc<AtomicUsize>,
    stream: TcpStream,
    read_thread: Option<thread::JoinHandle<()>>,
    write_thread: Option<thread::JoinHandle<()>>,
}

unsafe fn c_to_str<'a>(ptr: *const c_char) -> &'a str {
    if ptr.is_null() {
        return "";
    }
    CStr::from_ptr(ptr).to_str().unwrap_or("")
}

fn read_exact(stream: &mut TcpStream, buf: &mut [u8]) -> std::io::Result<()> {
    stream.read_exact(buf)
}

fn read_u8(stream: &mut TcpStream) -> std::io::Result<u8> {
    let mut buf = [0; 1];
    stream.read_exact(&mut buf)?;
    Ok(buf[0])
}

fn read_u16(stream: &mut TcpStream) -> std::io::Result<u16> {
    let mut buf = [0; 2];
    stream.read_exact(&mut buf)?;
    Ok(u16::from_be_bytes(buf))
}

fn read_i32(stream: &mut TcpStream) -> std::io::Result<i32> {
    let mut buf = [0; 4];
    stream.read_exact(&mut buf)?;
    Ok(i32::from_be_bytes(buf))
}

fn read_i64(stream: &mut TcpStream) -> std::io::Result<i64> {
    let mut buf = [0; 8];
    stream.read_exact(&mut buf)?;
    Ok(i64::from_be_bytes(buf))
}

fn read_and_write_utf(stream: &mut TcpStream, payload: &mut Vec<u8>) -> std::io::Result<()> {
    let len = read_u16(stream)?;
    payload.extend_from_slice(&len.to_be_bytes());
    if len > 0 {
        let mut bytes = vec![0; len as usize];
        read_exact(stream, &mut bytes)?;
        payload.extend_from_slice(&bytes);
    }
    Ok(())
}

fn read_packet_payload(stream: &mut TcpStream, packet_id: u8) -> std::io::Result<Vec<u8>> {
    let mut payload = Vec::new();
    match packet_id {
        0 | 9 => {} // No fields
        1 => {
            // protocolVersion (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // username (UTF string)
            read_and_write_utf(stream, &mut payload)?;
            // password (UTF string)
            read_and_write_utf(stream, &mut payload)?;
            // mapSeed (int64)
            let val = read_i64(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // dimension (byte)
            let val = read_u8(stream)?;
            payload.push(val);
        }
        2 => {
            // username (UTF string)
            read_and_write_utf(stream, &mut payload)?;
        }
        3 => {
            // message (UTF string)
            read_and_write_utf(stream, &mut payload)?;
        }
        5 => {
            // type (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // itemCount (int16)
            let item_count = read_u16(stream)?;
            payload.extend_from_slice(&item_count.to_be_bytes());
            for _ in 0..item_count {
                let item_id = read_u16(stream)?;
                payload.extend_from_slice(&item_id.to_be_bytes());
                if (item_id as i16) >= 0 {
                    let count = read_u8(stream)?;
                    payload.push(count);
                    let damage = read_u16(stream)?;
                    payload.extend_from_slice(&damage.to_be_bytes());
                }
            }
        }
        7 => {
            // playerEntityId (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // targetEntityId (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // isLeftClick (bool/byte)
            let val = read_u8(stream)?;
            payload.push(val);
        }
        10 => {
            // onGround (bool/byte)
            let val = read_u8(stream)?;
            payload.push(val);
        }
        11 => {
            // x (double/i64)
            let val = read_i64(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // y (double/i64)
            let val = read_i64(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // stance (double/i64)
            let val = read_i64(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // z (double/i64)
            let val = read_i64(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // onGround (bool/byte)
            let val = read_u8(stream)?;
            payload.push(val);
        }
        12 => {
            // yaw (float/i32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // pitch (float/i32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // onGround (bool/byte)
            let val = read_u8(stream)?;
            payload.push(val);
        }
        13 => {
            // x (double/i64), y (double/i64), stance (double/i64), z (double/i64)
            for _ in 0..4 {
                let val = read_i64(stream)?;
                payload.extend_from_slice(&val.to_be_bytes());
            }
            // yaw (float/i32), pitch (float/i32)
            for _ in 0..2 {
                let val = read_i32(stream)?;
                payload.extend_from_slice(&val.to_be_bytes());
            }
            // onGround (bool/byte)
            let val = read_u8(stream)?;
            payload.push(val);
        }
        14 => {
            // status (byte)
            let val = read_u8(stream)?;
            payload.push(val);
            // x (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // y (byte)
            let val = read_u8(stream)?;
            payload.push(val);
            // z (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // face (byte)
            let val = read_u8(stream)?;
            payload.push(val);
        }
        15 => {
            // itemId (short)
            let val = read_u16(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // x (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // y (byte)
            let val = read_u8(stream)?;
            payload.push(val);
            // z (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // direction (byte)
            let val = read_u8(stream)?;
            payload.push(val);
        }
        16 => {
            // entityId (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // itemId (short)
            let val = read_u16(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
        }
        18 => {
            // entityId (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // animate (byte)
            let val = read_u8(stream)?;
            payload.push(val);
        }
        21 => {
            // entityId (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // itemId (short)
            let val = read_u16(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // count (byte)
            let val = read_u8(stream)?;
            payload.push(val);
            // x, y, z (int32)
            for _ in 0..3 {
                let val = read_i32(stream)?;
                payload.extend_from_slice(&val.to_be_bytes());
            }
            // rotation, pitch, roll (byte)
            for _ in 0..3 {
                let val = read_u8(stream)?;
                payload.push(val);
            }
        }
        59 => {
            // x (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // y (short)
            let val = read_u16(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // z (int32)
            let val = read_i32(stream)?;
            payload.extend_from_slice(&val.to_be_bytes());
            // len (short)
            let len = read_u16(stream)?;
            payload.extend_from_slice(&len.to_be_bytes());
            // bytes
            let mut bytes = vec![0; len as usize];
            read_exact(stream, &mut bytes)?;
            payload.extend_from_slice(&bytes);
        }
        255 => {
            // reason (UTF string)
            read_and_write_utf(stream, &mut payload)?;
        }
        _ => {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                format!("Unsupported incoming packet ID: {}", packet_id),
            ));
        }
    }
    Ok(payload)
}

impl RustNetworkManager {
    fn shutdown(&self, reason: String) {
        if !self.is_running.swap(false, Ordering::SeqCst) {
            return;
        }
        {
            let mut tr = self.termination_reason.lock().unwrap();
            *tr = reason;
        }
        self.is_terminating.store(true, Ordering::SeqCst);
        let _ = self.stream.shutdown(Shutdown::Both);
        
        let &(_, ref cv) = &*self.write_queue;
        cv.notify_all();
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_create(socket_fd: c_int) -> *mut RustNetworkManager {
    use std::os::unix::io::FromRawFd;
    let stream = TcpStream::from_raw_fd(socket_fd);
    let _ = stream.set_nodelay(true);
    
    let is_running = Arc::new(AtomicBool::new(true));
    let is_server_terminating = Arc::new(AtomicBool::new(false));
    let is_terminating = Arc::new(AtomicBool::new(false));
    let termination_reason = Arc::new(Mutex::new(String::new()));
    let read_queue = Arc::new(Mutex::new(VecDeque::new()));
    let write_queue = Arc::new((Mutex::new(VecDeque::<(Vec<u8>, bool)>::new()), Condvar::new()));
    let send_queue_byte_length = Arc::new(AtomicUsize::new(0));

    let is_running_c = is_running.clone();
    let is_terminating_c = is_terminating.clone();
    let termination_reason_c = termination_reason.clone();
    let read_queue_c = read_queue.clone();
    let write_queue_c = write_queue.clone();
    let mut stream_read = stream.try_clone().unwrap();

    let read_thread = thread::spawn(move || {
        while is_running_c.load(Ordering::SeqCst) {
            match read_u8(&mut stream_read) {
                Ok(packet_id) => {
                    match read_packet_payload(&mut stream_read, packet_id) {
                        Ok(payload) => {
                            let mut q = read_queue_c.lock().unwrap();
                            q.push_back((packet_id, payload));
                        }
                        Err(e) => {
                            if !is_terminating_c.load(Ordering::SeqCst) {
                                let err_reason = format!("Internal exception: {}", e);
                                {
                                    let mut tr = termination_reason_c.lock().unwrap();
                                    *tr = err_reason;
                                }
                                is_terminating_c.store(true, Ordering::SeqCst);
                                is_running_c.store(false, Ordering::SeqCst);
                                let _ = stream_read.shutdown(Shutdown::Both);
                                let &(_, ref cv) = &*write_queue_c;
                                cv.notify_all();
                            }
                            return;
                        }
                    }
                }
                Err(_) => {
                    if !is_terminating_c.load(Ordering::SeqCst) {
                        {
                            let mut tr = termination_reason_c.lock().unwrap();
                            *tr = "End of stream".to_string();
                        }
                        is_terminating_c.store(true, Ordering::SeqCst);
                        is_running_c.store(false, Ordering::SeqCst);
                        let _ = stream_read.shutdown(Shutdown::Both);
                        let &(_, ref cv) = &*write_queue_c;
                        cv.notify_all();
                    }
                    return;
                }
            }
        }
    });

    let is_running_c = is_running.clone();
    let is_terminating_c = is_terminating.clone();
    let termination_reason_c = termination_reason.clone();
    let write_queue_c = write_queue.clone();
    let send_queue_byte_length_c = send_queue_byte_length.clone();
    let mut stream_write = stream.try_clone().unwrap();

    let write_thread = thread::spawn(move || {
        let &(ref lock, ref cv) = &*write_queue_c;
        while is_running_c.load(Ordering::SeqCst) {
            let mut pkt = None;
            {
                let mut q = lock.lock().unwrap();
                loop {
                    if !is_running_c.load(Ordering::SeqCst) {
                        return;
                    }
                    
                    // Prioritize data_packets (non-chunk packets first)
                    // Find first packet with is_chunk_data == false
                    let mut found_idx = None;
                    for (i, (_, is_chunk)) in q.iter().enumerate() {
                        if !is_chunk {
                            found_idx = Some(i);
                            break;
                        }
                    }
                    
                    if let Some(idx) = found_idx {
                        pkt = Some(q.remove(idx).unwrap().0);
                        break;
                    } else if !q.is_empty() {
                        pkt = Some(q.pop_front().unwrap().0);
                        break;
                    }
                    
                    q = cv.wait(q).unwrap();
                }
            }

            if let Some(data) = pkt {
                let len = data.len();
                if stream_write.write_all(&data).is_err() {
                    if !is_terminating_c.load(Ordering::SeqCst) {
                        {
                            let mut tr = termination_reason_c.lock().unwrap();
                            *tr = "Write error".to_string();
                        }
                        is_terminating_c.store(true, Ordering::SeqCst);
                        is_running_c.store(false, Ordering::SeqCst);
                        let _ = stream_write.shutdown(Shutdown::Both);
                    }
                    return;
                }
                send_queue_byte_length_c.fetch_sub(len, Ordering::SeqCst);
            }
        }
    });

    Box::into_raw(Box::new(RustNetworkManager {
        is_running,
        is_server_terminating,
        is_terminating,
        termination_reason,
        read_queue,
        write_queue,
        send_queue_byte_length,
        stream,
        read_thread: Some(read_thread),
        write_thread: Some(write_thread),
    }))
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_destroy(manager: *mut RustNetworkManager) {
    if manager.is_null() {
        return;
    }
    
    let mut m = Box::from_raw(manager);
    m.is_running.store(false, Ordering::SeqCst);
    let _ = m.stream.shutdown(Shutdown::Both);
    
    let &(_, ref cv) = &*m.write_queue;
    cv.notify_all();

    if let Some(t) = m.read_thread.take() {
        let _ = t.join();
    }
    if let Some(t) = m.write_thread.take() {
        let _ = t.join();
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_send(
    manager: *mut RustNetworkManager,
    data_ptr: *const u8,
    data_len: size_t,
    is_chunk_data: bool,
) {
    if manager.is_null() || data_ptr.is_null() || data_len == 0 {
        return;
    }
    let m = &*manager;
    if m.is_server_terminating.load(Ordering::SeqCst) {
        return;
    }
    let bytes = std::slice::from_raw_parts(data_ptr, data_len).to_vec();
    m.send_queue_byte_length.fetch_add(data_len, Ordering::SeqCst);
    
    let &(ref lock, ref cv) = &*m.write_queue;
    {
        let mut q = lock.lock().unwrap();
        q.push_back((bytes, is_chunk_data));
    }
    cv.notify_one();
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_poll(
    manager: *mut RustNetworkManager,
    out_packet_id: *mut u8,
    out_payload: *mut *mut u8,
    out_payload_len: *mut size_t,
) -> bool {
    if manager.is_null() {
        return false;
    }
    let m = &*manager;
    let mut q = m.read_queue.lock().unwrap();
    if let Some((id, mut payload)) = q.pop_front() {
        *out_packet_id = id;
        let len = payload.len();
        *out_payload_len = len;
        if len > 0 {
            payload.shrink_to_fit();
            *out_payload = payload.as_mut_ptr();
            std::mem::forget(payload);
        } else {
            *out_payload = std::ptr::null_mut();
        }
        true
    } else {
        false
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_free_buffer(ptr: *mut u8, len: size_t) {
    if !ptr.is_null() && len > 0 {
        let _ = Vec::from_raw_parts(ptr, len, len);
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_shutdown(
    manager: *mut RustNetworkManager,
    reason: *const c_char,
) {
    if manager.is_null() {
        return;
    }
    let m = &*manager;
    let reason_str = c_to_str(reason).to_string();
    m.shutdown(reason_str);
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_is_running(manager: *mut RustNetworkManager) -> bool {
    if manager.is_null() {
        return false;
    }
    (*manager).is_running.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_is_terminating(manager: *mut RustNetworkManager) -> bool {
    if manager.is_null() {
        return false;
    }
    (*manager).is_terminating.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_get_termination_reason(
    manager: *mut RustNetworkManager,
    out_buf: *mut c_char,
    max_len: size_t,
) {
    if manager.is_null() || out_buf.is_null() || max_len == 0 {
        return;
    }
    let m = &*manager;
    let reason = m.termination_reason.lock().unwrap();
    let c_str = std::ffi::CString::new(reason.as_str()).unwrap_or_default();
    let bytes = c_str.as_bytes_with_nul();
    let len = std::cmp::min(bytes.len(), max_len);
    std::ptr::copy_nonoverlapping(bytes.as_ptr() as *const c_char, out_buf, len);
    *out_buf.add(len - 1) = 0;
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_get_send_queue_length(manager: *mut RustNetworkManager) -> size_t {
    if manager.is_null() {
        return 0;
    }
    (*manager).send_queue_byte_length.load(Ordering::SeqCst) as size_t
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_server_shutdown(manager: *mut RustNetworkManager) {
    if manager.is_null() {
        return;
    }
    (*manager).is_server_terminating.store(true, Ordering::SeqCst);
    let &(_, ref cv) = &*(*manager).write_queue;
    cv.notify_all();
}
