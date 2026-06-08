use std::collections::{BinaryHeap, HashMap};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PathfinderWorldAccessor {
    pub world: *mut std::ffi::c_void,
    pub is_liquid: unsafe extern "C" fn(world: *mut std::ffi::c_void, x: i32, y: i32, z: i32) -> bool,
    pub blocks_movement: unsafe extern "C" fn(world: *mut std::ffi::c_void, x: i32, y: i32, z: i32) -> bool,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FfiPathPoint {
    pub x: i32,
    pub y: i32,
    pub z: i32,
}

#[derive(Clone, Debug)]
struct PathPointNode {
    x: i32,
    y: i32,
    z: i32,
    total_path_distance: f32, // g score
    distance_to_next: f32,    // h score
    previous: Option<(i32, i32, i32)>,
    is_closed: bool,
}

impl PathPointNode {
    fn distance_to(&self, other_x: f32, other_y: f32, other_z: f32) -> f32 {
        let dx = other_x - self.x as f32;
        let dy = other_y - self.y as f32;
        let dz = other_z - self.z as f32;
        (dx * dx + dy * dy + dz * dz).sqrt()
    }

    fn distance_to_node(&self, other: &PathPointNode) -> f32 {
        self.distance_to(other.x as f32, other.y as f32, other.z as f32)
    }
}

#[derive(Copy, Clone, Debug)]
struct HeapEntry {
    key: (i32, i32, i32),
    f_score: f32,
}

impl PartialEq for HeapEntry {
    fn eq(&self, other: &Self) -> bool {
        self.f_score == other.f_score
    }
}

impl Eq for HeapEntry {}

impl PartialOrd for HeapEntry {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for HeapEntry {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        other.f_score.partial_cmp(&self.f_score).unwrap_or(std::cmp::Ordering::Equal)
    }
}

fn get_vertical_offset(
    accessor: &PathfinderWorldAccessor,
    x: i32, y: i32, z: i32,
    size_x: i32, size_y: i32, size_z: i32,
) -> i32 {
    for ix in x..(x + size_x) {
        for iy in y..(y + size_y) {
            for iz in z..(z + size_z) {
                unsafe {
                    if (accessor.is_liquid)(accessor.world, ix, iy, iz) {
                        return -1;
                    }
                    if (accessor.blocks_movement)(accessor.world, ix, iy, iz) {
                        return 0;
                    }
                }
            }
        }
    }
    1
}

fn get_safe_point(
    accessor: &PathfinderWorldAccessor,
    x: i32,
    mut y: i32,
    z: i32,
    size_x: i32,
    size_y: i32,
    size_z: i32,
    vertical_step: i32,
    nodes: &mut HashMap<(i32, i32, i32), PathPointNode>,
) -> Option<(i32, i32, i32)> {
    let mut safe_found = false;
    if get_vertical_offset(accessor, x, y, z, size_x, size_y, size_z) > 0 {
        safe_found = true;
    }

    if !safe_found && vertical_step > 0 && get_vertical_offset(accessor, x, y + vertical_step, z, size_x, size_y, size_z) > 0 {
        safe_found = true;
        y += vertical_step;
    }

    if safe_found {
        let mut fall_distance = 0;
        while y > 0 {
            let vertical_offset = get_vertical_offset(accessor, x, y - 1, z, size_x, size_y, size_z);
            if vertical_offset <= 0 {
                break;
            }
            if vertical_offset < 0 {
                return None;
            }
            fall_distance += 1;
            if fall_distance >= 4 {
                return None;
            }
            y -= 1;
        }

        if y > 0 {
            nodes.entry((x, y, z)).or_insert_with(|| PathPointNode {
                x,
                y,
                z,
                total_path_distance: f32::MAX,
                distance_to_next: 0.0,
                previous: None,
                is_closed: false,
            });
            return Some((x, y, z));
        }
    }
    None
}

#[no_mangle]
pub unsafe extern "C" fn rust_pathfinder_find_path(
    accessor: PathfinderWorldAccessor,
    start_x: f64, start_y: f64, start_z: f64,
    target_x: f64, target_y: f64, target_z: f64,
    entity_width: f32,
    entity_height: f32,
    max_distance: f32,
    out_points: *mut FfiPathPoint,
    max_points: i32,
) -> i32 {
    let start_x_floor = start_x.floor() as i32;
    let start_y_floor = start_y.floor() as i32;
    let start_z_floor = start_z.floor() as i32;

    let target_x_floor = (target_x - (entity_width / 2.0) as f64).floor() as i32;
    let target_y_floor = target_y.floor() as i32;
    let target_z_floor = (target_z - (entity_width / 2.0) as f64).floor() as i32;

    let size_x = (entity_width + 1.0).floor() as i32;
    let size_y = (entity_height + 1.0).floor() as i32;
    let size_z = (entity_width + 1.0).floor() as i32;

    let start_key = (start_x_floor, start_y_floor, start_z_floor);
    let target_key = (target_x_floor, target_y_floor, target_z_floor);

    let mut nodes = HashMap::new();
    let mut heap = BinaryHeap::new();

    let start_target_dist = ((target_x_floor - start_x_floor) as f32 * (target_x_floor - start_x_floor) as f32 +
                             (target_y_floor - start_y_floor) as f32 * (target_y_floor - start_y_floor) as f32 +
                             (target_z_floor - start_z_floor) as f32 * (target_z_floor - start_z_floor) as f32).sqrt();

    nodes.insert(start_key, PathPointNode {
        x: start_x_floor,
        y: start_y_floor,
        z: start_z_floor,
        total_path_distance: 0.0,
        distance_to_next: start_target_dist,
        previous: None,
        is_closed: false,
    });

    heap.push(HeapEntry {
        key: start_key,
        f_score: start_target_dist,
    });

    let mut best_key = start_key;
    let mut target_found = false;

    while let Some(HeapEntry { key, f_score: _ }) = heap.pop() {
        let current_node = match nodes.get(&key) {
            Some(n) => n.clone(),
            None => continue,
        };

        if current_node.is_closed {
            continue;
        }

        if let Some(n) = nodes.get_mut(&key) {
            n.is_closed = true;
        }

        if key == target_key {
            target_found = true;
            break;
        }

        let best_dist = nodes[&best_key].distance_to(target_x_floor as f32, target_y_floor as f32, target_z_floor as f32);
        let cur_dist = current_node.distance_to(target_x_floor as f32, target_y_floor as f32, target_z_floor as f32);
        if cur_dist < best_dist {
            best_key = key;
        }

        let mut vertical_step = 0;
        if get_vertical_offset(&accessor, current_node.x, current_node.y + 1, current_node.z, size_x, size_y, size_z) > 0 {
            vertical_step = 1;
        }

        let neighbors = [
            (current_node.x, current_node.y, current_node.z + 1),
            (current_node.x - 1, current_node.y, current_node.z),
            (current_node.x + 1, current_node.y, current_node.z),
            (current_node.x, current_node.y, current_node.z - 1),
        ];

        for &(nx, ny, nz) in &neighbors {
            if let Some(safe_coord) = get_safe_point(&accessor, nx, ny, nz, size_x, size_y, size_z, vertical_step, &mut nodes) {
                let cand_node = nodes.get(&safe_coord).unwrap().clone();
                if cand_node.is_closed {
                    continue;
                }

                let dist_to_target = cand_node.distance_to(target_x_floor as f32, target_y_floor as f32, target_z_floor as f32);
                if dist_to_target < max_distance {
                    let path_distance = current_node.total_path_distance + current_node.distance_to_node(&cand_node);
                    let current_g = nodes[&safe_coord].total_path_distance;

                    if path_distance < current_g {
                        if let Some(n) = nodes.get_mut(&safe_coord) {
                            n.previous = Some(key);
                            n.total_path_distance = path_distance;
                            n.distance_to_next = dist_to_target;
                        }
                        heap.push(HeapEntry {
                            key: safe_coord,
                            f_score: path_distance + dist_to_target,
                        });
                    }
                }
            }
        }
    }

    let end_key = if target_found { target_key } else { best_key };
    if end_key == start_key {
        return 0;
    }

    let mut path = Vec::new();
    let mut curr = Some(end_key);
    while let Some(k) = curr {
        path.push(k);
        curr = nodes.get(&k).and_then(|n| n.previous);
    }
    path.reverse();

    let count = path.len().min(max_points as usize);
    let slice = std::slice::from_raw_parts_mut(out_points, count);
    for i in 0..count {
        slice[i] = FfiPathPoint {
            x: path[i].0,
            y: path[i].1,
            z: path[i].2,
        };
    }
    count as i32
}
