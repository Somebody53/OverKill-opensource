"""Enemy brains: fight the closest visible target, share sightings with the team,
and otherwise explore hotspots or walk to the last known enemy position.
"""
import heapq
import math
import random

HOLD_TIME = 0.25
# Weapon enum indices from app.cpp: 0 pistol, 1 knife, 2 AK-47, 3 SMG, 4 AWP.
DEFAULT_COOLDOWN = (0.7, 1.4)
WEAPON_COOLDOWN = {0: (0.7, 1.0), 2: (0.35, 0.5), 3: (0.15, 0.22), 4: (1.8, 2.2)}
DEFAULT_STRAFE = 0.5
WEAPON_STRAFE = {0: 0.5, 2: 0.5, 3: 0.7, 4: 0.2}
TURN_TIME = (1.0, 2.5)
SEARCH_TIME = (0.4, 1.2)
SEARCH_SPEED = 2.8          # matches EnemySearchSpeed in app.cpp
EXPLORE_TIME = (8.0, 16.0)
EXPLORE_RADIUS = 30.0
ARRIVE = 0.7
STUCK_TIME = 1.0
MAX_SEARCH = 3000           # A* node expansions per path
MAX_PLANS = 6               # path searches per think() call, the rest wait a frame
SIGHT_TIME = 12.0           # seconds a sighting stays on the team board
HEAT_DECAY = 0.01           # per second, how fast old sightings fade
HEAT_CAP = 5.0
HEAT_PERIOD = 0.5           # heat decays in steps this often instead of every frame
# Learned preferred distances: band i is preferred at BANDS[i] and used for sightings below BAND_EDGES[i].
BANDS = (4.0, 12.0, 24.0, 36.0)
BAND_EDGES = (8.0, 18.0, 30.0)
BAND_SLACK = 3.0
BAND_TIME = (4.0, 7.0)
LEARN_EPSILON = 0.1

brains = {}

# Map grid from set_map: flags per cell (see buildNavGrid in app.cpp).
NAV = None
COLS = 0
CELL = 1.0
HALF = 100.0
VISITED = set()
HEAT = {}                   # sparse per-cell sighting counts, the learned hotspots
HEAT_CLOCK = 0.0
BOARD = {}                  # team -> [x, z, time left, stamp] of the last sighting
STAMP = 0
PLANS_LEFT = 0
EXPERIENCE = {}             # weapon -> per band [shots, hits], learned across all enemies


class Brain:
    def __init__(self):
        self.turn = 0.0
        self.fire = 0.0
        self.path = []
        self.retarget = random.uniform(0.0, 4.0)
        self.stuck = 0.0
        self.last = None
        self.seen = -1
        self.band = None
        self.band_timer = 0.0
        self.pending = None     # (weapon, band) of the last shot, scored when the result arrives


def set_map(cells, cols, cell, half):
    """Store the walkable grid of a new map and forget what was explored."""
    global NAV, COLS, CELL, HALF, HEAT, HEAT_CLOCK
    NAV = [ord(c) - 48 for c in cells]
    COLS, CELL, HALF = cols, cell, half
    HEAT = {}
    HEAT_CLOCK = 0.0
    VISITED.clear()
    BOARD.clear()
    brains.clear()


def cell_of(x, z):
    i = min(COLS - 1, max(0, int((x + HALF) / CELL)))
    j = min(COLS - 1, max(0, int((z + HALF) / CELL)))
    return j * COLS + i


def cell_xz(idx):
    j, i = divmod(idx, COLS)
    return -HALF + (i + 0.5) * CELL, -HALF + (j + 0.5) * CELL


def steps(idx):
    """Open neighbours of a cell; edge flags already include the node tests."""
    i, j = idx % COLS, idx // COLS
    if i + 1 < COLS and NAV[idx] & 2 and NAV[idx + 1] & 1:
        yield idx + 1
    if i > 0 and NAV[idx - 1] & 2 and NAV[idx - 1] & 1:
        yield idx - 1
    if j + 1 < COLS and NAV[idx] & 4 and NAV[idx + COLS] & 1:
        yield idx + COLS
    if j > 0 and NAV[idx - COLS] & 4 and NAV[idx - COLS] & 1:
        yield idx - COLS


def find_path(start, goal):
    """A* path of cell indices from start (excluded) to goal, or None."""
    goal_i, goal_j = goal % COLS, goal // COLS

    def heuristic(idx):
        return abs(idx % COLS - goal_i) + abs(idx // COLS - goal_j)

    parent = {start: -1}
    cost = {start: 0}
    heap = [(heuristic(start), 0, start)]
    expanded = 0
    while heap and expanded < MAX_SEARCH:
        _, g_cost, cur = heapq.heappop(heap)
        if g_cost > cost[cur]:
            continue
        if cur == goal:
            path = []
            while cur != start:
                path.append(cur)
                cur = parent[cur]
            path.reverse()
            return path
        expanded += 1
        for nxt in steps(cur):
            new_cost = g_cost + 1
            if new_cost < cost.get(nxt, new_cost + 1):
                cost[nxt] = new_cost
                parent[nxt] = cur
                heapq.heappush(heap, (new_cost + heuristic(nxt), new_cost, nxt))
    return None


def pick_goal(x, z):
    """Best nearby open cell: unexplored, busy with past sightings, and close."""
    best, best_score = None, -1e9
    for _ in range(40):
        angle = random.uniform(0.0, 2.0 * math.pi)
        dist = random.uniform(2.0, EXPLORE_RADIUS)
        idx = cell_of(x + dist * math.cos(angle), z + dist * math.sin(angle))
        if not (NAV[idx] & 1):
            continue
        score = (0.0 if idx in VISITED else 2.0) + 0.5 * HEAT.get(idx, 0.0) - dist / EXPLORE_RADIUS
        if score > best_score:
            best, best_score = idx, score
    return best


def plan(brain, x, z):
    goal = pick_goal(x, z)
    path = find_path(cell_of(x, z), goal) if goal is not None else None
    if path:
        brain.path = path
        brain.retarget = random.uniform(*EXPLORE_TIME)
    else:
        brain.path = []
        brain.retarget = 1.0
    brain.stuck = 0.0


def can_plan():
    """Spend one of this call's path searches; False once the budget is used up."""
    global PLANS_LEFT
    if PLANS_LEFT <= 0:
        return False
    PLANS_LEFT -= 1
    return True


def band_index(dist):
    for i, edge in enumerate(BAND_EDGES):
        if dist < edge:
            return i
    return len(BAND_EDGES)


def learned_band(weapon):
    """Pick a preferred distance band for this weapon: mostly the best-scoring one, sometimes random."""
    stats = EXPERIENCE.get(weapon)
    if stats is None or random.random() < LEARN_EPSILON:
        return random.randrange(len(BANDS))
    best, best_score = 0, -1.0
    for band, (shots, hits) in enumerate(stats):
        score = (hits + 1) / (shots + 2) + 0.6 / math.sqrt(shots + 1)
        if score > best_score:
            best, best_score = band, score
    return best


def resolve(brain, last):
    """Score the brain's last shot once the game reports whether it hit (1) or missed (0)."""
    if brain.pending is None or last < 0:
        return
    weapon, band = brain.pending
    brain.pending = None
    stats = EXPERIENCE.setdefault(weapon, [[0, 0] for _ in BANDS])
    stats[band][0] += 1
    stats[band][1] += last


def plan_to(brain, x, z, goal, ttl):
    path = find_path(cell_of(x, z), goal)
    if not path:
        return False
    brain.path = path
    brain.retarget = ttl
    brain.stuck = 0.0
    return True


def think(units, dt):
    """Return (move_x, move_z, target, fire) for each unit, in input order.

    units: (uid, x, z, dir_x, dir_z, team, weapon, last, candidates)
    last: 1 if the unit's previous shot hit, 0 if it missed, -1 if it did not fire.
    candidates: (target, dist, clear, aim_x, aim_z), target -2 is the player.
    """
    global HEAT_CLOCK, PLANS_LEFT, STAMP
    PLANS_LEFT = MAX_PLANS
    HEAT_CLOCK += dt
    if HEAT_CLOCK >= HEAT_PERIOD:
        keep = math.exp(-HEAT_DECAY * HEAT_CLOCK)
        HEAT_CLOCK = 0.0
        for idx in list(HEAT):
            HEAT[idx] *= keep
            if HEAT[idx] < 0.01:
                del HEAT[idx]
    for team in list(BOARD):
        BOARD[team][2] -= dt
        if BOARD[team][2] <= 0.0:
            del BOARD[team]

    # Sightings reach the whole team board and mark hotspots.
    for uid, x, z, dx, dz, team, weapon, last, cands in units:
        if not cands:
            continue
        for cand in cands:
            idx = cell_of(cand[3], cand[4])
            HEAT[idx] = min(HEAT.get(idx, 0.0) + 1.0, HEAT_CAP)
        closest = min(cands, key=lambda c: c[1])
        STAMP += 1
        BOARD[team] = [closest[3], closest[4], SIGHT_TIME, STAMP]

    alive = set()
    results = []
    for uid, x, z, dx, dz, team, weapon, last, cands in units:
        alive.add(uid)
        brain = brains.setdefault(uid, Brain())
        resolve(brain, last)
        brain.turn -= dt
        brain.fire -= dt
        brain.band_timer -= dt
        track(brain, x, z, dt)
        if NAV is not None:
            VISITED.add(cell_of(x, z))
        if cands:
            closest = min(cands, key=lambda c: c[1])
            results.append(combat(brain, x, z, dx, dz, closest, weapon))
        else:
            results.append(wander(brain, team, x, z, dx, dz, dt))
    for uid in [u for u in brains if u not in alive]:
        del brains[uid]
    return results


def track(brain, x, z, dt):
    if brain.last is not None:
        moved = math.hypot(x - brain.last[0], z - brain.last[1])
        brain.stuck = brain.stuck + dt if moved < 0.3 * SEARCH_SPEED * dt else 0.0
    brain.last = (x, z)


def wander(brain, team, x, z, dx, dz, dt):
    brain.retarget -= dt

    while brain.path:
        gx, gz = cell_xz(brain.path[0])
        if math.hypot(gx - x, gz - z) >= ARRIVE:
            break
        brain.path.pop(0)
        if not brain.path:
            brain.retarget = 0.0

    board = BOARD.get(team)
    if NAV is not None and board is not None and brain.seen != board[3] and can_plan():
        brain.seen = board[3]
        plan_to(brain, x, z, cell_of(board[0], board[1]), board[2])

    if NAV is not None and (brain.retarget <= 0.0 or brain.stuck > STUCK_TIME) and can_plan():
        plan(brain, x, z)
    if brain.path:
        gx, gz = cell_xz(brain.path[0])
        ax, az = gx - x, gz - z
        length = math.hypot(ax, az)
        if length > 1e-4:
            return ax / length, az / length, -1, 0

    if brain.turn <= 0.0:
        brain.turn = random.uniform(*SEARCH_TIME)
        angle = random.uniform(0.0, 2.0 * math.pi)
        dx, dz = math.cos(angle), math.sin(angle)
    return dx, dz, -1, 0


def combat(brain, x, z, dx, dz, cand, weapon):
    target, _, clear, aim_x, aim_z = cand
    fx, fz = aim_x - x, aim_z - z
    flat = math.hypot(fx, fz)
    if flat > 1e-4:
        fx, fz = fx / flat, fz / flat
    sx, sz = -fz, fx

    side = -1.0 if dx * sx + dz * sz < 0.0 else 1.0
    if brain.turn <= 0.0:
        brain.turn = random.uniform(*TURN_TIME)
        side = -side
    if brain.band is None or brain.band_timer <= 0.0:
        brain.band = learned_band(weapon)
        brain.band_timer = random.uniform(*BAND_TIME)

    preferred = BANDS[brain.band]
    if flat > preferred + BAND_SLACK:
        radial = 1.0
    elif flat < preferred - BAND_SLACK:
        radial = -1.0
    else:
        radial = 0.0
    strafe = side * (1.0 if radial == 0.0 else WEAPON_STRAFE.get(weapon, DEFAULT_STRAFE))
    wx = fx * radial + sx * strafe
    wz = fz * radial + sz * strafe
    length = math.hypot(wx, wz)
    if length > 1e-4:
        wx, wz = wx / length, wz / length
    if brain.stuck > STUCK_TIME:
        angle = random.uniform(0.0, 2.0 * math.pi)
        wx, wz = math.cos(angle), math.sin(angle)
        brain.stuck = 0.0

    fire = 0
    if brain.fire <= 0.0:
        if clear:
            brain.fire = random.uniform(*WEAPON_COOLDOWN.get(weapon, DEFAULT_COOLDOWN))
            brain.pending = (weapon, band_index(flat))
            fire = 1
        else:
            brain.fire = HOLD_TIME
    return wx, wz, target, fire
