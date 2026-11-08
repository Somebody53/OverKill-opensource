//! Enemy brains: fight the closest visible target, share sightings with the team,
//! and otherwise explore hotspots or walk to the last known enemy position.
//! Rust port of the former src/ai.py, called from app.cpp through the C interface below.

use std::cmp::{Ordering, Reverse};
use std::collections::{BinaryHeap, HashMap, HashSet, VecDeque};
use std::f64::consts::PI;
use std::sync::Mutex;

const HOLD_TIME: f64 = 0.25;
const DEFAULT_COOLDOWN: (f64, f64) = (0.7, 1.4);
const DEFAULT_STRAFE: f64 = 0.5;
const TURN_TIME: (f64, f64) = (1.0, 2.5);
const SEARCH_TIME: (f64, f64) = (0.4, 1.2);
const SEARCH_SPEED: f64 = 2.8;
const EXPLORE_TIME: (f64, f64) = (8.0, 16.0);
const EXPLORE_RADIUS: f64 = 30.0;
const ARRIVE: f64 = 0.7;
const STUCK_TIME: f64 = 1.0;
const MAX_SEARCH: usize = 3000;
const MAX_PLANS: i32 = 6;
const SIGHT_TIME: f64 = 12.0;
const HEAT_DECAY: f64 = 0.01;
const HEAT_CAP: f64 = 5.0;
const HEAT_PERIOD: f64 = 0.5;
const BANDS: [f64; 4] = [4.0, 12.0, 24.0, 36.0];
const BAND_EDGES: [f64; 3] = [8.0, 18.0, 30.0];
const BAND_SLACK: f64 = 3.0;
const BAND_TIME: (f64, f64) = (4.0, 7.0);
const LEARN_EPSILON: f64 = 0.1;
const REACT_TIME: (f64, f64) = (0.2, 0.4);
const SCOPE_TIME: (f64, f64) = (0.9, 1.3);
const SNIPER_WEAPON: i32 = 4;
const FLANK_OFFSET: f64 = 8.0;

fn weapon_cooldown(weapon: i32) -> (f64, f64) {
    match weapon {
        0 => (0.7, 1.0),
        2 => (0.35, 0.5),
        3 => (0.15, 0.22),
        4 => (1.8, 2.2),
        _ => DEFAULT_COOLDOWN,
    }
}

fn weapon_strafe(weapon: i32) -> f64 {
    match weapon {
        0 | 2 => 0.5,
        3 => 0.7,
        4 => 0.2,
        _ => DEFAULT_STRAFE,
    }
}

/// Small xorshift generator; the brain only needs cheap randomness.
struct Rng(u64);

impl Rng {
    fn new() -> Rng {
        let nanos = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos() as u64)
            .unwrap_or(0x9E37_79B9_7F4A_7C15);
        Rng(nanos | 1)
    }

    fn next_f64(&mut self) -> f64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        (self.0 >> 11) as f64 / (1u64 << 53) as f64
    }

    fn range(&mut self, lo: f64, hi: f64) -> f64 {
        lo + (hi - lo) * self.next_f64()
    }

    fn below(&mut self, n: usize) -> usize {
        ((self.next_f64() * n as f64) as usize).min(n.saturating_sub(1))
    }
}

#[derive(Clone, Copy)]
struct Cand {
    target: i32,
    dist: f64,
    clear: bool,
    aim_x: f64,
    aim_z: f64,
}

#[derive(Clone, Copy)]
struct Unit {
    uid: i32,
    x: f64,
    z: f64,
    dir_x: f64,
    dir_z: f64,
    team: i32,
    weapon: i32,
    last: i32,
}

struct Brain {
    turn: f64,
    fire: f64,
    path: VecDeque<usize>,
    retarget: f64,
    stuck: f64,
    last: Option<(f64, f64)>,
    seen: i64,
    band: Option<usize>,
    band_timer: f64,
    pending: Option<(i32, usize)>,
    engaged: bool,
    react: f64,
}

impl Brain {
    fn new(rng: &mut Rng) -> Brain {
        Brain {
            turn: 0.0,
            fire: 0.0,
            path: VecDeque::new(),
            retarget: rng.range(0.0, 4.0),
            stuck: 0.0,
            last: None,
            seen: -1,
            band: None,
            band_timer: 0.0,
            pending: None,
            engaged: false,
            react: 0.0,
        }
    }
}

#[derive(Clone, Copy)]
struct Sight {
    x: f64,
    z: f64,
    time: f64,
    stamp: u64,
}

fn band_index(dist: f64) -> usize {
    BAND_EDGES.iter().position(|&edge| dist < edge).unwrap_or(BAND_EDGES.len())
}

fn closest(cands: &[Cand]) -> Cand {
    *cands
        .iter()
        .min_by(|a, b| a.dist.partial_cmp(&b.dist).unwrap_or(Ordering::Equal))
        .expect("closest called with no candidates")
}

struct Ai {
    rng: Rng,
    nav: Vec<u8>,
    cols: usize,
    cell: f64,
    half: f64,
    visited: HashSet<usize>,
    heat: HashMap<usize, f64>,
    heat_clock: f64,
    board: HashMap<i32, Sight>,
    stamp: u64,
    plans_left: i32,
    experience: HashMap<i32, [[u32; 2]; 4]>,
    brains: HashMap<i32, Brain>,
}

impl Ai {
    fn new() -> Ai {
        Ai {
            rng: Rng::new(),
            nav: Vec::new(),
            cols: 0,
            cell: 1.0,
            half: 100.0,
            visited: HashSet::new(),
            heat: HashMap::new(),
            heat_clock: 0.0,
            board: HashMap::new(),
            stamp: 0,
            plans_left: 0,
            experience: HashMap::new(),
            brains: HashMap::new(),
        }
    }

    fn has_nav(&self) -> bool {
        !self.nav.is_empty()
    }

    fn set_map(&mut self, nav: Vec<u8>, cols: usize, cell: f64, half: f64) {
        self.nav = if nav.len() == cols * cols { nav } else { Vec::new() };
        self.cols = cols;
        self.cell = cell;
        self.half = half;
        self.heat.clear();
        self.heat_clock = 0.0;
        self.visited.clear();
        self.board.clear();
        self.brains.clear();
    }

    fn cell_of(&self, x: f64, z: f64) -> usize {
        if self.cols == 0 {
            return 0;
        }
        let max = self.cols as i64 - 1;
        let i = (((x + self.half) / self.cell) as i64).clamp(0, max);
        let j = (((z + self.half) / self.cell) as i64).clamp(0, max);
        (j * self.cols as i64 + i) as usize
    }

    fn cell_xz(&self, idx: usize) -> (f64, f64) {
        let i = idx % self.cols;
        let j = idx / self.cols;
        (
            -self.half + (i as f64 + 0.5) * self.cell,
            -self.half + (j as f64 + 0.5) * self.cell,
        )
    }

    /// Open neighbours of a cell; edge flags already include the node tests.
    fn steps(&self, idx: usize, out: &mut Vec<usize>) {
        out.clear();
        let c = self.cols;
        let n = &self.nav;
        let i = idx % c;
        let j = idx / c;
        if i + 1 < c && n[idx] & 2 != 0 && n[idx + 1] & 1 != 0 {
            out.push(idx + 1);
        }
        if i > 0 && n[idx - 1] & 2 != 0 && n[idx - 1] & 1 != 0 {
            out.push(idx - 1);
        }
        if j + 1 < c && n[idx] & 4 != 0 && n[idx + c] & 1 != 0 {
            out.push(idx + c);
        }
        if j > 0 && n[idx - c] & 4 != 0 && n[idx - c] & 1 != 0 {
            out.push(idx - c);
        }
    }

    /// A* path of cell indices from start (excluded) to goal, or None.
    fn find_path(&self, start: usize, goal: usize) -> Option<Vec<usize>> {
        let c = self.cols;
        let goal_i = (goal % c) as i64;
        let goal_j = (goal / c) as i64;
        let heuristic = |idx: usize| -> i64 {
            ((idx % c) as i64 - goal_i).abs() + ((idx / c) as i64 - goal_j).abs()
        };

        let mut parent: HashMap<usize, usize> = HashMap::new();
        let mut cost: HashMap<usize, i64> = HashMap::new();
        parent.insert(start, usize::MAX);
        cost.insert(start, 0);
        let mut heap = BinaryHeap::new();
        heap.push(Reverse((heuristic(start), 0i64, start)));
        let mut expanded = 0usize;
        let mut buf = Vec::with_capacity(4);

        while expanded < MAX_SEARCH {
            let Some(Reverse((_, g_cost, cur))) = heap.pop() else {
                break;
            };
            if g_cost > cost[&cur] {
                continue;
            }
            if cur == goal {
                let mut path = Vec::new();
                let mut at = cur;
                while at != start {
                    path.push(at);
                    at = parent[&at];
                }
                path.reverse();
                return Some(path);
            }
            expanded += 1;
            self.steps(cur, &mut buf);
            for &nxt in &buf {
                let new_cost = g_cost + 1;
                if new_cost < *cost.get(&nxt).unwrap_or(&(new_cost + 1)) {
                    cost.insert(nxt, new_cost);
                    parent.insert(nxt, cur);
                    heap.push(Reverse((new_cost + heuristic(nxt), new_cost, nxt)));
                }
            }
        }
        None
    }

    /// Best nearby open cell: unexplored, busy with past sightings, and close.
    fn pick_goal(&mut self, x: f64, z: f64) -> Option<usize> {
        let mut best = None;
        let mut best_score = -1e9;
        for _ in 0..40 {
            let angle = self.rng.range(0.0, 2.0 * PI);
            let dist = self.rng.range(2.0, EXPLORE_RADIUS);
            let idx = self.cell_of(x + dist * angle.cos(), z + dist * angle.sin());
            if self.nav[idx] & 1 == 0 {
                continue;
            }
            let visited_bonus = if self.visited.contains(&idx) { 0.0 } else { 2.0 };
            let score = visited_bonus + 0.5 * self.heat.get(&idx).copied().unwrap_or(0.0)
                - dist / EXPLORE_RADIUS;
            if score > best_score {
                best = Some(idx);
                best_score = score;
            }
        }
        best
    }

    fn plan(&mut self, brain: &mut Brain, x: f64, z: f64) {
        let goal = self.pick_goal(x, z);
        let start = self.cell_of(x, z);
        let path = goal.and_then(|g| self.find_path(start, g));
        match path {
            Some(p) if !p.is_empty() => {
                brain.path = p.into();
                brain.retarget = self.rng.range(EXPLORE_TIME.0, EXPLORE_TIME.1);
            }
            _ => {
                brain.path.clear();
                brain.retarget = 1.0;
            }
        }
        brain.stuck = 0.0;
    }

    /// Spend one of this call's path searches; false once the budget is used up.
    fn can_plan(&mut self) -> bool {
        if self.plans_left <= 0 {
            return false;
        }
        self.plans_left -= 1;
        true
    }

    /// Pick a preferred distance band for this weapon: mostly the best-scoring one, sometimes random.
    fn learned_band(&mut self, weapon: i32) -> usize {
        let stats = self.experience.get(&weapon).copied();
        let Some(stats) = stats else {
            return self.rng.below(BANDS.len());
        };
        if self.rng.next_f64() < LEARN_EPSILON {
            return self.rng.below(BANDS.len());
        }
        let mut best = 0;
        let mut best_score = -1.0;
        for (band, [shots, hits]) in stats.iter().enumerate() {
            let s = *shots as f64;
            let h = *hits as f64;
            let score = (h + 1.0) / (s + 2.0) + 0.6 / (s + 1.0).sqrt();
            if score > best_score {
                best = band;
                best_score = score;
            }
        }
        best
    }

    /// Score the brain's last shot once the game reports whether it hit (1) or missed (0).
    fn resolve(&mut self, brain: &mut Brain, last: i32) {
        if last < 0 {
            return;
        }
        if let Some((weapon, band)) = brain.pending.take() {
            let stats = self.experience.entry(weapon).or_insert([[0; 2]; 4]);
            stats[band][0] += 1;
            stats[band][1] += last as u32;
        }
    }

    fn plan_to(&mut self, brain: &mut Brain, x: f64, z: f64, goal: usize, ttl: f64) -> bool {
        let start = self.cell_of(x, z);
        match self.find_path(start, goal) {
            Some(p) if !p.is_empty() => {
                brain.path = p.into();
                brain.retarget = ttl;
                brain.stuck = 0.0;
                true
            }
            _ => false,
        }
    }

    /// Walkable cell beside a sighting on this unit's side, so pursuers close in from two directions.
    fn flank_cell(&self, sight_x: f64, sight_z: f64, x: f64, z: f64, side: f64) -> usize {
        let dx = sight_x - x;
        let dz = sight_z - z;
        let len = dx.hypot(dz);
        if len > 1e-4 {
            let px = -dz / len * side * FLANK_OFFSET;
            let pz = dx / len * side * FLANK_OFFSET;
            let idx = self.cell_of(sight_x + px, sight_z + pz);
            if self.nav[idx] & 1 != 0 {
                return idx;
            }
        }
        self.cell_of(sight_x, sight_z)
    }

    fn wander(&mut self, brain: &mut Brain, team: i32, x: f64, z: f64, dx: f64, dz: f64, side: f64, dt: f64) -> (f64, f64, i32, i32) {
        let (mut dx, mut dz) = (dx, dz);
        brain.retarget -= dt;

        while let Some(&front) = brain.path.front() {
            let (gx, gz) = self.cell_xz(front);
            if (gx - x).hypot(gz - z) >= ARRIVE {
                break;
            }
            brain.path.pop_front();
            if brain.path.is_empty() {
                brain.retarget = 0.0;
            }
        }

        if let Some(board) = self.board.get(&team).copied() {
            if self.has_nav() && brain.seen != board.stamp as i64 && self.can_plan() {
                brain.seen = board.stamp as i64;
                let flank = self.flank_cell(board.x, board.z, x, z, side);
                if !self.plan_to(brain, x, z, flank, board.time) {
                    let direct = self.cell_of(board.x, board.z);
                    self.plan_to(brain, x, z, direct, board.time);
                }
            }
        }

        if self.has_nav()
            && (brain.retarget <= 0.0 || brain.stuck > STUCK_TIME)
            && self.can_plan()
        {
            self.plan(brain, x, z);
        }

        if let Some(&front) = brain.path.front() {
            let (gx, gz) = self.cell_xz(front);
            let ax = gx - x;
            let az = gz - z;
            let length = ax.hypot(az);
            if length > 1e-4 {
                return (ax / length, az / length, -1, 0);
            }
        }

        if brain.turn <= 0.0 {
            brain.turn = self.rng.range(SEARCH_TIME.0, SEARCH_TIME.1);
            let angle = self.rng.range(0.0, 2.0 * PI);
            dx = angle.cos();
            dz = angle.sin();
        }
        (dx, dz, -1, 0)
    }

    fn combat(&mut self, brain: &mut Brain, x: f64, z: f64, dx: f64, dz: f64, cand: Cand, weapon: i32) -> (f64, f64, i32, i32) {
        let mut fx = cand.aim_x - x;
        let mut fz = cand.aim_z - z;
        let flat = fx.hypot(fz);
        if flat > 1e-4 {
            fx /= flat;
            fz /= flat;
        }
        let sx = -fz;
        let sz = fx;

        let mut side = if dx * sx + dz * sz < 0.0 { -1.0 } else { 1.0 };
        if brain.turn <= 0.0 {
            brain.turn = self.rng.range(TURN_TIME.0, TURN_TIME.1);
            side = -side;
        }
        if brain.band.is_none() || brain.band_timer <= 0.0 {
            // Snipers keep to the long bands (24 or 36 units) instead of closing in.
            brain.band = Some(if weapon == SNIPER_WEAPON {
                2 + self.rng.below(2)
            } else {
                self.learned_band(weapon)
            });
            brain.band_timer = self.rng.range(BAND_TIME.0, BAND_TIME.1);
        }
        let band = brain.band.unwrap_or(0);

        let preferred = BANDS[band];
        let radial = if flat > preferred + BAND_SLACK {
            1.0
        } else if flat < preferred - BAND_SLACK {
            -1.0
        } else {
            0.0
        };
        let strafe = side * if radial == 0.0 { 1.0 } else { weapon_strafe(weapon) };
        let mut wx = fx * radial + sx * strafe;
        let mut wz = fz * radial + sz * strafe;
        let length = wx.hypot(wz);
        if length > 1e-4 {
            wx /= length;
            wz /= length;
        }
        if brain.stuck > STUCK_TIME {
            let angle = self.rng.range(0.0, 2.0 * PI);
            wx = angle.cos();
            wz = angle.sin();
            brain.stuck = 0.0;
        }

        // Enemies need a moment to react to a new contact, and snipers need longer to scope in.
        let mut fire = 0;
        if brain.fire <= 0.0 && brain.react <= 0.0 {
            if cand.clear {
                let (lo, hi) = weapon_cooldown(weapon);
                brain.fire = self.rng.range(lo, hi);
                brain.pending = Some((weapon, band_index(flat)));
                fire = 1;
            } else {
                brain.fire = HOLD_TIME;
            }
        }
        (wx, wz, cand.target, fire)
    }

    fn think(&mut self, units: &[Unit], cands: &[Vec<Cand>], dt: f64) -> Vec<(f64, f64, i32, i32)> {
        self.plans_left = MAX_PLANS;
        self.heat_clock += dt;
        if self.heat_clock >= HEAT_PERIOD {
            let keep = (-HEAT_DECAY * self.heat_clock).exp();
            self.heat_clock = 0.0;
            self.heat.retain(|_, v| {
                *v *= keep;
                *v >= 0.01
            });
        }
        self.board.retain(|_, s| {
            s.time -= dt;
            s.time > 0.0
        });

        // Sightings reach the whole team board and mark hotspots.
        for (unit, list) in units.iter().zip(cands) {
            if list.is_empty() {
                continue;
            }
            for c in list {
                let idx = self.cell_of(c.aim_x, c.aim_z);
                let heat = self.heat.entry(idx).or_insert(0.0);
                *heat = (*heat + 1.0).min(HEAT_CAP);
            }
            let near = closest(list);
            self.stamp += 1;
            self.board.insert(
                unit.team,
                Sight { x: near.aim_x, z: near.aim_z, time: SIGHT_TIME, stamp: self.stamp },
            );
        }

        let mut out = Vec::with_capacity(units.len());
        for (unit, list) in units.iter().zip(cands) {
            let mut brain = self
                .brains
                .remove(&unit.uid)
                .unwrap_or_else(|| Brain::new(&mut self.rng));
            self.resolve(&mut brain, unit.last);
            brain.turn -= dt;
            brain.fire -= dt;
            brain.band_timer -= dt;
            if list.is_empty() {
                brain.engaged = false;
            } else if !brain.engaged {
                brain.engaged = true;
                let (lo, hi) = if unit.weapon == SNIPER_WEAPON { SCOPE_TIME } else { REACT_TIME };
                brain.react = self.rng.range(lo, hi);
            }
            brain.react -= dt;
            if let Some((lx, lz)) = brain.last {
                let moved = (unit.x - lx).hypot(unit.z - lz);
                brain.stuck = if moved < 0.3 * SEARCH_SPEED * dt { brain.stuck + dt } else { 0.0 };
            }
            brain.last = Some((unit.x, unit.z));
            if self.has_nav() {
                let idx = self.cell_of(unit.x, unit.z);
                self.visited.insert(idx);
            }
            let result = if list.is_empty() {
                let side = if unit.uid % 2 == 0 { 1.0 } else { -1.0 };
                self.wander(&mut brain, unit.team, unit.x, unit.z, unit.dir_x, unit.dir_z, side, dt)
            } else {
                let near = closest(list);
                self.combat(&mut brain, unit.x, unit.z, unit.dir_x, unit.dir_z, near, unit.weapon)
            };
            out.push(result);
            self.brains.insert(unit.uid, brain);
        }
        let alive: HashSet<i32> = units.iter().map(|u| u.uid).collect();
        self.brains.retain(|uid, _| alive.contains(uid));
        out
    }
}

static STATE: Mutex<Option<Ai>> = Mutex::new(None);

/// One enemy: its sight data, the range of its candidates in the shared candidate array.
#[repr(C)]
pub struct AiUnit {
    pub uid: i32,
    pub x: f64,
    pub z: f64,
    pub dir_x: f64,
    pub dir_z: f64,
    pub team: i32,
    pub weapon: i32,
    pub last: i32,
    pub cand_start: i32,
    pub cand_count: i32,
}

#[repr(C)]
pub struct AiCand {
    pub target: i32,
    pub dist: f64,
    pub clear: i32,
    pub aim_x: f64,
    pub aim_z: f64,
}

#[repr(C)]
pub struct AiReply {
    pub move_x: f64,
    pub move_z: f64,
    pub target: i32,
    pub fire: i32,
}

/// Stores the walkable grid (one byte per cell, digits '0'..'7') of a new map.
///
/// # Safety
/// `cells` must point to `cols * cols` readable bytes, or be null.
#[no_mangle]
pub unsafe extern "C" fn ai_set_map(cells: *const u8, cols: i32, cell: f64, half: f64) {
    let cols = cols.max(0) as usize;
    let count = cols * cols;
    let nav: Vec<u8> = if cells.is_null() || count == 0 {
        Vec::new()
    } else {
        std::slice::from_raw_parts(cells, count)
            .iter()
            .map(|b| b.wrapping_sub(b'0'))
            .collect()
    };
    let mut guard = STATE.lock().unwrap_or_else(|e| e.into_inner());
    guard
        .get_or_insert_with(Ai::new)
        .set_map(nav, cols, cell, half);
}

/// Computes one move and fire decision per unit; writes `count` replies to `out`.
///
/// # Safety
/// `units` must point to `count` AiUnit values, `cands` to the candidate array they index,
/// and `out` to `count` writable AiReply values.
#[no_mangle]
pub unsafe extern "C" fn ai_think(
    units: *const AiUnit,
    count: i32,
    cands: *const AiCand,
    dt: f64,
    out: *mut AiReply,
) {
    if units.is_null() || out.is_null() || count <= 0 {
        return;
    }
    let n = count as usize;
    let raw = std::slice::from_raw_parts(units, n);
    let mut unit_list = Vec::with_capacity(n);
    let mut cand_lists = Vec::with_capacity(n);
    for u in raw {
        unit_list.push(Unit {
            uid: u.uid,
            x: u.x,
            z: u.z,
            dir_x: u.dir_x,
            dir_z: u.dir_z,
            team: u.team,
            weapon: u.weapon,
            last: u.last,
        });
        let start = u.cand_start.max(0) as usize;
        let len = u.cand_count.max(0) as usize;
        let list: Vec<Cand> = if cands.is_null() || len == 0 {
            Vec::new()
        } else {
            std::slice::from_raw_parts(cands.add(start), len)
                .iter()
                .map(|c| Cand {
                    target: c.target,
                    dist: c.dist,
                    clear: c.clear != 0,
                    aim_x: c.aim_x,
                    aim_z: c.aim_z,
                })
                .collect()
        };
        cand_lists.push(list);
    }

    let mut guard = STATE.lock().unwrap_or_else(|e| e.into_inner());
    let ai = guard.get_or_insert_with(Ai::new);
    let replies = ai.think(&unit_list, &cand_lists, dt);
    let outs = std::slice::from_raw_parts_mut(out, n);
    for (slot, (mx, mz, target, fire)) in outs.iter_mut().zip(replies) {
        *slot = AiReply { move_x: mx, move_z: mz, target, fire };
    }
}
