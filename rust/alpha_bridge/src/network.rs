use std::net::{TcpStream, Shutdown};
use std::thread;
use std::sync::{Arc, Mutex, Condvar};
use std::sync::atomic::{AtomicBool, Ordering, AtomicUsize};
use std::collections::VecDeque;
use libc::{c_char, c_int, size_t};
use std::ffi::{CStr, CString};
use std::io::{Read, Write};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiSlotData {
    pub item_id: i16,
    pub count: i8,
    pub damage: i16,
}

pub enum PacketData {
    KeepAlive,
    Login {
        protocol_version: i32,
        username: String,
        password: String,
        map_seed: i64,
        dimension: i8,
    },
    Handshake {
        username: String,
    },
    Chat {
        message: String,
    },
    PlayerInventory {
        inventory_type: i32,
        slots: Vec<FfiSlotData>,
    },
    UseEntity {
        player_entity_id: i32,
        target_entity_id: i32,
        is_left_click: bool,
    },
    Respawn,
    Flying {
        on_ground: bool,
    },
    PlayerPosition {
        x: f64,
        y: f64,
        stance: f64,
        z: f64,
        on_ground: bool,
    },
    PlayerLook {
        yaw: f32,
        pitch: f32,
        on_ground: bool,
    },
    PlayerLookMove {
        x: f64,
        y: f64,
        stance: f64,
        z: f64,
        yaw: f32,
        pitch: f32,
        on_ground: bool,
    },
    BlockDig {
        status: i8,
        x: i32,
        y: i8,
        z: i32,
        face: i8,
    },
    Place {
        item_id: i16,
        x: i32,
        y: i8,
        z: i32,
        direction: i8,
    },
    BlockItemSwitch {
        entity_id: i32,
        item_id: i16,
    },
    ArmAnimation {
        entity_id: i32,
        animate: i8,
    },
    PickupSpawn {
        entity_id: i32,
        item_id: i16,
        count: i8,
        x: i32,
        y: i32,
        z: i32,
        rotation: i8,
        pitch: i8,
        roll: i8,
    },
    ComplexEntity {
        x: i32,
        y: i16,
        z: i32,
        nbt_data: Vec<u8>,
    },
    KickDisconnect {
        reason: String,
    },
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket1Login {
    pub protocol_version: i32,
    pub username: *const c_char,
    pub password: *const c_char,
    pub map_seed: i64,
    pub dimension: i8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket2Handshake {
    pub username: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket3Chat {
    pub message: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket5PlayerInventory {
    pub inventory_type: i32,
    pub item_count: i16,
    pub slots: *const FfiSlotData,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket7UseEntity {
    pub player_entity_id: i32,
    pub target_entity_id: i32,
    pub is_left_click: bool,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket10Flying {
    pub on_ground: bool,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket11PlayerPosition {
    pub x: f64,
    pub y: f64,
    pub stance: f64,
    pub z: f64,
    pub on_ground: bool,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket12PlayerLook {
    pub yaw: f32,
    pub pitch: f32,
    pub on_ground: bool,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket13PlayerLookMove {
    pub x: f64,
    pub y: f64,
    pub stance: f64,
    pub z: f64,
    pub yaw: f32,
    pub pitch: f32,
    pub on_ground: bool,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket14BlockDig {
    pub status: i8,
    pub x: i32,
    pub y: i8,
    pub z: i32,
    pub face: i8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket15Place {
    pub item_id: i16,
    pub x: i32,
    pub y: i8,
    pub z: i32,
    pub direction: i8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket16BlockItemSwitch {
    pub entity_id: i32,
    pub item_id: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket18ArmAnimation {
    pub entity_id: i32,
    pub animate: i8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket21PickupSpawn {
    pub entity_id: i32,
    pub item_id: i16,
    pub count: i8,
    pub x: i32,
    pub y: i32,
    pub z: i32,
    pub rotation: i8,
    pub pitch: i8,
    pub roll: i8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket59ComplexEntity {
    pub x: i32,
    pub y: i16,
    pub z: i32,
    pub nbt_data: *const u8,
    pub nbt_len: size_t,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RustPacket255KickDisconnect {
    pub reason: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub union RustPacketUnion {
    pub login: RustPacket1Login,
    pub handshake: RustPacket2Handshake,
    pub chat: RustPacket3Chat,
    pub inventory: RustPacket5PlayerInventory,
    pub use_entity: RustPacket7UseEntity,
    pub flying: RustPacket10Flying,
    pub position: RustPacket11PlayerPosition,
    pub look: RustPacket12PlayerLook,
    pub look_move: RustPacket13PlayerLookMove,
    pub block_dig: RustPacket14BlockDig,
    pub place: RustPacket15Place,
    pub item_switch: RustPacket16BlockItemSwitch,
    pub arm_anim: RustPacket18ArmAnimation,
    pub pickup_spawn: RustPacket21PickupSpawn,
    pub complex_entity: RustPacket59ComplexEntity,
    pub kick: RustPacket255KickDisconnect,
}

#[repr(C)]
pub struct RustPacket {
    pub packet_id: u8,
    pub data: RustPacketUnion,
}

#[repr(C)]
pub struct RustNetworkManager {
    is_running: Arc<AtomicBool>,
    is_server_terminating: Arc<AtomicBool>,
    is_terminating: Arc<AtomicBool>,
    termination_reason: Arc<Mutex<String>>,
    
    // Read queue (popped by C++ processReadPackets)
    read_queue: Arc<Mutex<VecDeque<PacketData>>>,
    
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

fn read_utf(stream: &mut TcpStream) -> std::io::Result<String> {
    let len = read_u16(stream)? as usize;
    if len == 0 {
        return Ok(String::new());
    }
    let mut bytes = vec![0u8; len];
    read_exact(stream, &mut bytes)?;
    String::from_utf8(bytes).map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))
}

fn read_f64(stream: &mut TcpStream) -> std::io::Result<f64> {
    let val = read_i64(stream)?;
    Ok(f64::from_bits(val as u64))
}

fn read_f32(stream: &mut TcpStream) -> std::io::Result<f32> {
    let val = read_i32(stream)?;
    Ok(f32::from_bits(val as u32))
}

fn read_bool(stream: &mut TcpStream) -> std::io::Result<bool> {
    let val = read_u8(stream)?;
    Ok(val != 0)
}

fn read_packet_payload(stream: &mut TcpStream, packet_id: u8) -> std::io::Result<PacketData> {
    match packet_id {
        0 => Ok(PacketData::KeepAlive),
        1 => {
            let protocol_version = read_i32(stream)?;
            let username = read_utf(stream)?;
            let password = read_utf(stream)?;
            let map_seed = read_i64(stream)?;
            let dimension = read_u8(stream)? as i8;
            Ok(PacketData::Login {
                protocol_version,
                username,
                password,
                map_seed,
                dimension,
            })
        }
        2 => {
            let username = read_utf(stream)?;
            Ok(PacketData::Handshake { username })
        }
        3 => {
            let message = read_utf(stream)?;
            Ok(PacketData::Chat { message })
        }
        5 => {
            let inventory_type = read_i32(stream)?;
            let item_count = read_u16(stream)?;
            let mut slots = Vec::with_capacity(item_count as usize);
            for _ in 0..item_count {
                let item_id = read_u16(stream)? as i16;
                let mut slot = FfiSlotData { item_id, count: 0, damage: 0 };
                if item_id >= 0 {
                    slot.count = read_u8(stream)? as i8;
                    slot.damage = read_u16(stream)? as i16;
                }
                slots.push(slot);
            }
            Ok(PacketData::PlayerInventory {
                inventory_type,
                slots,
            })
        }
        7 => {
            let player_entity_id = read_i32(stream)?;
            let target_entity_id = read_i32(stream)?;
            let is_left_click = read_bool(stream)?;
            Ok(PacketData::UseEntity {
                player_entity_id,
                target_entity_id,
                is_left_click,
            })
        }
        9 => Ok(PacketData::Respawn),
        10 => {
            let on_ground = read_bool(stream)?;
            Ok(PacketData::Flying { on_ground })
        }
        11 => {
            let x = read_f64(stream)?;
            let y = read_f64(stream)?;
            let stance = read_f64(stream)?;
            let z = read_f64(stream)?;
            let on_ground = read_bool(stream)?;
            Ok(PacketData::PlayerPosition {
                x,
                y,
                stance,
                z,
                on_ground,
            })
        }
        12 => {
            let yaw = read_f32(stream)?;
            let pitch = read_f32(stream)?;
            let on_ground = read_bool(stream)?;
            Ok(PacketData::PlayerLook {
                yaw,
                pitch,
                on_ground,
            })
        }
        13 => {
            let x = read_f64(stream)?;
            let y = read_f64(stream)?;
            let stance = read_f64(stream)?;
            let z = read_f64(stream)?;
            let yaw = read_f32(stream)?;
            let pitch = read_f32(stream)?;
            let on_ground = read_bool(stream)?;
            Ok(PacketData::PlayerLookMove {
                x,
                y,
                stance,
                z,
                yaw,
                pitch,
                on_ground,
            })
        }
        14 => {
            let status = read_u8(stream)? as i8;
            let x = read_i32(stream)?;
            let y = read_u8(stream)? as i8;
            let z = read_i32(stream)?;
            let face = read_u8(stream)? as i8;
            Ok(PacketData::BlockDig {
                status,
                x,
                y,
                z,
                face,
            })
        }
        15 => {
            let item_id = read_u16(stream)? as i16;
            let x = read_i32(stream)?;
            let y = read_u8(stream)? as i8;
            let z = read_i32(stream)?;
            let direction = read_u8(stream)? as i8;
            Ok(PacketData::Place {
                item_id,
                x,
                y,
                z,
                direction,
            })
        }
        16 => {
            let entity_id = read_i32(stream)?;
            let item_id = read_u16(stream)? as i16;
            Ok(PacketData::BlockItemSwitch {
                entity_id,
                item_id,
            })
        }
        18 => {
            let entity_id = read_i32(stream)?;
            let animate = read_u8(stream)? as i8;
            Ok(PacketData::ArmAnimation {
                entity_id,
                animate,
            })
        }
        21 => {
            let entity_id = read_i32(stream)?;
            let item_id = read_u16(stream)? as i16;
            let count = read_u8(stream)? as i8;
            let x = read_i32(stream)?;
            let y = read_i32(stream)?;
            let z = read_i32(stream)?;
            let rotation = read_u8(stream)? as i8;
            let pitch = read_u8(stream)? as i8;
            let roll = read_u8(stream)? as i8;
            Ok(PacketData::PickupSpawn {
                entity_id,
                item_id,
                count,
                x,
                y,
                z,
                rotation,
                pitch,
                roll,
            })
        }
        59 => {
            let x = read_i32(stream)?;
            let y = read_u16(stream)? as i16;
            let z = read_i32(stream)?;
            let len = read_u16(stream)? as usize;
            let mut nbt_data = vec![0; len];
            read_exact(stream, &mut nbt_data)?;
            Ok(PacketData::ComplexEntity {
                x,
                y,
                z,
                nbt_data,
            })
        }
        255 => {
            let reason = read_utf(stream)?;
            Ok(PacketData::KickDisconnect { reason })
        }
        _ => {
            Err(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                format!("Unsupported incoming packet ID: {}", packet_id),
            ))
        }
    }
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
                        Ok(pkt_data) => {
                            let mut q = read_queue_c.lock().unwrap();
                            q.push_back(pkt_data);
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

unsafe fn to_ffi_packet(pkt: PacketData) -> RustPacket {
    match pkt {
        PacketData::KeepAlive => RustPacket {
            packet_id: 0,
            data: std::mem::zeroed(),
        },
        PacketData::Login { protocol_version, username, password, map_seed, dimension } => {
            let u_c = CString::new(username).unwrap_or_default().into_raw();
            let p_c = CString::new(password).unwrap_or_default().into_raw();
            RustPacket {
                packet_id: 1,
                data: RustPacketUnion {
                    login: RustPacket1Login {
                        protocol_version,
                        username: u_c,
                        password: p_c,
                        map_seed,
                        dimension,
                    }
                }
            }
        }
        PacketData::Handshake { username } => {
            let u_c = CString::new(username).unwrap_or_default().into_raw();
            RustPacket {
                packet_id: 2,
                data: RustPacketUnion {
                    handshake: RustPacket2Handshake {
                        username: u_c,
                    }
                }
            }
        }
        PacketData::Chat { message } => {
            let m_c = CString::new(message).unwrap_or_default().into_raw();
            RustPacket {
                packet_id: 3,
                data: RustPacketUnion {
                    chat: RustPacket3Chat {
                        message: m_c,
                    }
                }
            }
        }
        PacketData::PlayerInventory { inventory_type, slots } => {
            let count = slots.len() as i16;
            let mut raw_slots = slots.clone();
            raw_slots.shrink_to_fit();
            let slots_ptr = raw_slots.as_mut_ptr();
            std::mem::forget(raw_slots);
            RustPacket {
                packet_id: 5,
                data: RustPacketUnion {
                    inventory: RustPacket5PlayerInventory {
                        inventory_type,
                        item_count: count,
                        slots: slots_ptr,
                    }
                }
            }
        }
        PacketData::UseEntity { player_entity_id, target_entity_id, is_left_click } => {
            RustPacket {
                packet_id: 7,
                data: RustPacketUnion {
                    use_entity: RustPacket7UseEntity {
                        player_entity_id,
                        target_entity_id,
                        is_left_click,
                    }
                }
            }
        }
        PacketData::Respawn => RustPacket {
            packet_id: 9,
            data: std::mem::zeroed(),
        },
        PacketData::Flying { on_ground } => {
            RustPacket {
                packet_id: 10,
                data: RustPacketUnion {
                    flying: RustPacket10Flying {
                        on_ground,
                    }
                }
            }
        }
        PacketData::PlayerPosition { x, y, stance, z, on_ground } => {
            RustPacket {
                packet_id: 11,
                data: RustPacketUnion {
                    position: RustPacket11PlayerPosition {
                        x,
                        y,
                        stance,
                        z,
                        on_ground,
                    }
                }
            }
        }
        PacketData::PlayerLook { yaw, pitch, on_ground } => {
            RustPacket {
                packet_id: 12,
                data: RustPacketUnion {
                    look: RustPacket12PlayerLook {
                        yaw,
                        pitch,
                        on_ground,
                    }
                }
            }
        }
        PacketData::PlayerLookMove { x, y, stance, z, yaw, pitch, on_ground } => {
            RustPacket {
                packet_id: 13,
                data: RustPacketUnion {
                    look_move: RustPacket13PlayerLookMove {
                        x,
                        y,
                        stance,
                        z,
                        yaw,
                        pitch,
                        on_ground,
                    }
                }
            }
        }
        PacketData::BlockDig { status, x, y, z, face } => {
            RustPacket {
                packet_id: 14,
                data: RustPacketUnion {
                    block_dig: RustPacket14BlockDig {
                        status,
                        x,
                        y,
                        z,
                        face,
                    }
                }
            }
        }
        PacketData::Place { item_id, x, y, z, direction } => {
            RustPacket {
                packet_id: 15,
                data: RustPacketUnion {
                    place: RustPacket15Place {
                        item_id,
                        x,
                        y,
                        z,
                        direction,
                    }
                }
            }
        }
        PacketData::BlockItemSwitch { entity_id, item_id } => {
            RustPacket {
                packet_id: 16,
                data: RustPacketUnion {
                    item_switch: RustPacket16BlockItemSwitch {
                        entity_id,
                        item_id,
                    }
                }
            }
        }
        PacketData::ArmAnimation { entity_id, animate } => {
            RustPacket {
                packet_id: 18,
                data: RustPacketUnion {
                    arm_anim: RustPacket18ArmAnimation {
                        entity_id,
                        animate,
                    }
                }
            }
        }
        PacketData::PickupSpawn { entity_id, item_id, count, x, y, z, rotation, pitch, roll } => {
            RustPacket {
                packet_id: 21,
                data: RustPacketUnion {
                    pickup_spawn: RustPacket21PickupSpawn {
                        entity_id,
                        item_id,
                        count,
                        x,
                        y,
                        z,
                        rotation,
                        pitch,
                        roll,
                    }
                }
            }
        }
        PacketData::ComplexEntity { x, y, z, nbt_data } => {
            let mut raw_nbt = nbt_data.clone();
            raw_nbt.shrink_to_fit();
            let nbt_ptr = raw_nbt.as_mut_ptr();
            let nbt_len = raw_nbt.len();
            std::mem::forget(raw_nbt);
            RustPacket {
                packet_id: 59,
                data: RustPacketUnion {
                    complex_entity: RustPacket59ComplexEntity {
                        x,
                        y,
                        z,
                        nbt_data: nbt_ptr,
                        nbt_len,
                    }
                }
            }
        }
        PacketData::KickDisconnect { reason } => {
            let r_c = CString::new(reason).unwrap_or_default().into_raw();
            RustPacket {
                packet_id: 255,
                data: RustPacketUnion {
                    kick: RustPacket255KickDisconnect {
                        reason: r_c,
                    }
                }
            }
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_poll_parsed(
    manager: *mut RustNetworkManager,
) -> *mut RustPacket {
    if manager.is_null() {
        return std::ptr::null_mut();
    }
    let m = &*manager;
    let mut q = m.read_queue.lock().unwrap();
    if let Some(pkt_data) = q.pop_front() {
        let ffi_pkt = to_ffi_packet(pkt_data);
        Box::into_raw(Box::new(ffi_pkt))
    } else {
        std::ptr::null_mut()
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_network_manager_free_packet(packet_ptr: *mut RustPacket) {
    if packet_ptr.is_null() {
        return;
    }
    let packet = Box::from_raw(packet_ptr);
    match packet.packet_id {
        1 => {
            if !packet.data.login.username.is_null() {
                let _ = CString::from_raw(packet.data.login.username as *mut c_char);
            }
            if !packet.data.login.password.is_null() {
                let _ = CString::from_raw(packet.data.login.password as *mut c_char);
            }
        }
        2 => {
            if !packet.data.handshake.username.is_null() {
                let _ = CString::from_raw(packet.data.handshake.username as *mut c_char);
            }
        }
        3 => {
            if !packet.data.chat.message.is_null() {
                let _ = CString::from_raw(packet.data.chat.message as *mut c_char);
            }
        }
        5 => {
            if !packet.data.inventory.slots.is_null() {
                let _ = Vec::from_raw_parts(
                    packet.data.inventory.slots as *mut FfiSlotData,
                    packet.data.inventory.item_count as usize,
                    packet.data.inventory.item_count as usize,
                );
            }
        }
        59 => {
            if !packet.data.complex_entity.nbt_data.is_null() {
                let _ = Vec::from_raw_parts(
                    packet.data.complex_entity.nbt_data as *mut u8,
                    packet.data.complex_entity.nbt_len,
                    packet.data.complex_entity.nbt_len,
                );
            }
        }
        255 => {
            if !packet.data.kick.reason.is_null() {
                let _ = CString::from_raw(packet.data.kick.reason as *mut c_char);
            }
        }
        _ => {}
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
