// Traffic core (docs/traffic.md).
//
// ONE SOURCE, TWO HOMES - the road streaming core's arrangement. The editor
// compiles this file inside namespace roadlanes (src/roadlanes.cpp, for
// --vehicle-check's host simulation), and the codegen pastes it VERBATIM into
// class TerrainGame of a generated game that runs traffic
// (roadlanes::coreSource, embedded at build by CMake). So the rules for this
// file are the class body's: structs with member functions only - no free
// functions, no namespace-scope statics, no #include, and no C block comments
// (the paste can land inside one). std::vector and <math.h> are all it uses.
//
// What lives here is everything about a traffic car that does not touch the
// engine: the lane graph in its runtime form, the signal cycle, where a car is
// on its path, which way it goes at the next node, whether it may enter the
// junction (lights, the cars already in it, the cars it has to give way to),
// and the three numbers it hands the vehicle sim - throttle, brake and steer,
// the SAME numbers the pad and the waypoint AI fill.

// One lane between two nodes, or one connection through a node.
struct TfSeg {
  int first = 0;       // first point in TfGraph::pts (x, y, z triples)
  int count = 0;       // its points
  int kind = 0;        // 0 a lane, 1 a connection through a node
  int node = -1;       // the node a connection crosses (-1 for a lane)
  int nextFirst = 0;   // successors in TfGraph::next
  int nextCount = 0;
  int group = -1;      // a connection at a signalled node: its approach's phase (0 .. phases-1)
  int rank = 0;        // a connection's priority: a higher rank goes first
  int turn = 0;        // a connection: 0 straight, 1 near side, 2 far side, 3 back
  int confFirst = 0;   // the connections this one crosses, in TfGraph::conf
  int confCount = 0;
  // A lane on a road with several lanes each way: the lane beside it in the
  // same direction toward the centre line (inner) and toward the kerb
  // (outer), -1 for none. What a lane change moves a car onto.
  int inner = -1;
  int outer = -1;
  float len = 0.0F;
};

struct TfNode {
  int signal = 0;        // 1 = traffic lights
  // Phases in the signal cycle: 2 at a four-way node (opposite arms share a
  // phase), one per arm at any other node (a T runs three).
  int phases = 2;
  float offset = 0.0F;   // seconds into the cycle at clock 0
};

struct TfGraph {
  // x, y, z per point. The game points this straight at its baked table
  // (no copy - a city's lanes are tens of thousands of points); the host
  // keeps its own in ownPts (use()).
  const float* pts = nullptr;
  int pointCount = 0;
  std::vector<float> ownPts;
  std::vector<float> arc;    // per point: distance from its segment's start
  std::vector<float> vmax;   // per point: the speed its curve allows
  std::vector<TfSeg> segs;
  std::vector<int> next;
  std::vector<int> conf;
  std::vector<TfNode> nodes;
  std::vector<int> lanes;    // the kind-0 segments (spawn candidates)
  float green = 12.0F, amber = 3.0F, allRed = 2.0F;
  float speed = 11.0F;       // the lane speed, units/s

  // Arcs and curve speeds, once the points are in. latAcc is the sideways
  // acceleration a traffic driver accepts (units/s^2).
  void use() {
    pts = ownPts.data();
    pointCount = (int)(ownPts.size() / 3);
  }
  void finish(float latAcc) {
    arc.assign((size_t)pointCount, 0.0F);
    vmax.assign((size_t)pointCount, speed);
    lanes.clear();
    for (int si = 0; si < (int)segs.size(); ++si) {
      TfSeg& g = segs[(size_t)si];
      if (g.kind == 0) lanes.push_back(si);
      float a = 0.0F;
      for (int k = 0; k < g.count; ++k) {
        const int i = g.first + k;
        if (k > 0) {
          const float dx = pts[(size_t)i * 3] - pts[(size_t)(i - 1) * 3];
          const float dz = pts[(size_t)i * 3 + 2] - pts[(size_t)(i - 1) * 3 + 2];
          a += sqrtf(dx * dx + dz * dz);
        }
        arc[(size_t)i] = a;
        if (k > 0 && k + 1 < g.count) {
          // The circle through this point and its neighbours: R = abc / 4A.
          const float* p0 = &pts[(size_t)(i - 1) * 3];
          const float* p1 = &pts[(size_t)i * 3];
          const float* p2 = &pts[(size_t)(i + 1) * 3];
          const float ax = p1[0] - p0[0], az = p1[2] - p0[2];
          const float bx = p2[0] - p1[0], bz = p2[2] - p1[2];
          const float cx = p2[0] - p0[0], cz = p2[2] - p0[2];
          const float cr = ax * bz - az * bx;
          const float la = sqrtf(ax * ax + az * az), lb = sqrtf(bx * bx + bz * bz);
          const float lc = sqrtf(cx * cx + cz * cz);
          const float area2 = cr < 0.0F ? -cr : cr;
          if (area2 > 1e-5F) {
            const float r = la * lb * lc / (2.0F * area2);
            const float v = sqrtf(latAcc * r);
            if (v < vmax[(size_t)i]) vmax[(size_t)i] = v;
          }
        }
      }
      g.len = a;
    }
    // A curve's speed holds over its whole length, not only at its sharpest
    // sample: take each point's neighbours into account.
    for (const TfSeg& g : segs)
      for (int k = 1; k + 1 < g.count; ++k) {
        const size_t i = (size_t)(g.first + k);
        const float m = vmax[i - 1] < vmax[i + 1] ? vmax[i - 1] : vmax[i + 1];
        if (m < vmax[i] && g.kind == 1) vmax[i] = 0.5F * (vmax[i] + m);
      }
  }

  // One phase's slot: green, amber, then all-red clearance.
  float slot() const { return green + amber + allRed; }
  // A four-way node's cycle (two phases).
  float cycle() const { return 2.0F * slot(); }
  // Node n's cycle: one slot per phase.
  float cycleOf(int n) const {
    const int p = n >= 0 && n < (int)nodes.size() ? nodes[(size_t)n].phases : 2;
    return (float)(p > 1 ? p : 2) * slot();
  }

  // The light an approach of phase `group` sees at node `n` at time `t`:
  // 0 green, 1 amber, 2 red. The cycle is one slot per phase in turn, phase
  // k's slot starting k slots in; each slot is green, amber, then all-red
  // clearance, so no two phases are ever green or amber together.
  int light(int n, int group, float t) const {
    if (n < 0 || n >= (int)nodes.size() || group < 0 || !nodes[(size_t)n].signal) return 0;
    const float c = cycleOf(n);
    float ph = fmodf(t + nodes[(size_t)n].offset, c);
    if (ph < 0.0F) ph += c;
    if (group > 0) {
      ph -= slot() * (float)group;
      while (ph < 0.0F) ph += c;
    }
    if (ph < green) return 0;
    if (ph < green + amber) return 1;
    return 2;
  }

  // Is lane b lane a, or a lane beside it in the same direction of the same
  // road stretch?
  bool siblings(int a, int b) const {
    if (a < 0 || b < 0) return false;
    if (a == b) return true;
    for (int k = segs[(size_t)a].inner, hop = 0; k >= 0 && hop < 8; k = segs[(size_t)k].inner, ++hop)
      if (k == b) return true;
    for (int k = segs[(size_t)a].outer, hop = 0; k >= 0 && hop < 8; k = segs[(size_t)k].outer, ++hop)
      if (k == b) return true;
    return false;
  }
  // The lane beside a one step toward its sibling b, -1 when b is a itself
  // or not a sibling.
  int stepToward(int a, int b) const {
    if (a < 0 || b < 0 || a == b) return -1;
    for (int k = segs[(size_t)a].inner, hop = 0; k >= 0 && hop < 8; k = segs[(size_t)k].inner, ++hop)
      if (k == b) return segs[(size_t)a].inner;
    for (int k = segs[(size_t)a].outer, hop = 0; k >= 0 && hop < 8; k = segs[(size_t)k].outer, ++hop)
      if (k == b) return segs[(size_t)a].outer;
    return -1;
  }

  // The point at arc `s` along segment `seg` (clamped): x, y, z, then the
  // unit direction of travel tx, tz.
  void at(int seg, float s, float* out) const {
    const TfSeg& g = segs[(size_t)seg];
    if (s < 0.0F) s = 0.0F;
    if (s > g.len) s = g.len;
    int lo = g.first, hi = g.first + g.count - 1;
    while (hi - lo > 1) {
      const int mid = (lo + hi) / 2;
      if (arc[(size_t)mid] <= s) lo = mid;
      else hi = mid;
    }
    const float span = arc[(size_t)hi] - arc[(size_t)lo];
    const float f = span > 1e-6F ? (s - arc[(size_t)lo]) / span : 0.0F;
    const float* a = &pts[(size_t)lo * 3];
    const float* b = &pts[(size_t)hi * 3];
    out[0] = a[0] + (b[0] - a[0]) * f;
    out[1] = a[1] + (b[1] - a[1]) * f;
    out[2] = a[2] + (b[2] - a[2]) * f;
    const float dx = b[0] - a[0], dz = b[2] - a[2];
    const float l = sqrtf(dx * dx + dz * dz);
    out[3] = l > 1e-6F ? dx / l : 0.0F;
    out[4] = l > 1e-6F ? dz / l : 1.0F;
  }

  // The arc of the point of `seg` nearest (x, z), searched around s0 - a car
  // moves a few units a frame, so a window of points is the whole search.
  float project(int seg, float s0, float x, float z) const {
    const TfSeg& g = segs[(size_t)seg];
    if (g.count < 2) return 0.0F;
    int lo = g.first, hi = g.first + g.count - 1;
    while (hi - lo > 1) {
      const int mid = (lo + hi) / 2;
      if (arc[(size_t)mid] <= s0) lo = mid;
      else hi = mid;
    }
    int a = lo - 3, b = lo + 6;
    if (a < g.first) a = g.first;
    if (b > g.first + g.count - 1) b = g.first + g.count - 1;
    float best = 1e30F, bestS = s0;
    for (int i = a; i < b; ++i) {
      const float* p = &pts[(size_t)i * 3];
      const float* q = &pts[(size_t)(i + 1) * 3];
      const float dx = q[0] - p[0], dz = q[2] - p[2];
      const float l2 = dx * dx + dz * dz;
      float f = l2 > 1e-9F ? ((x - p[0]) * dx + (z - p[2]) * dz) / l2 : 0.0F;
      if (f < 0.0F) f = 0.0F;
      if (f > 1.0F) f = 1.0F;
      const float ex = p[0] + dx * f - x, ez = p[2] + dz * f - z;
      const float d2 = ex * ex + ez * ez;
      if (d2 < best) {
        best = d2;
        bestS = arc[(size_t)i] + (arc[(size_t)(i + 1)] - arc[(size_t)i]) * f;
      }
    }
    return bestS;
  }

  // The exit lane of a connection (its one successor), -1 for anything else.
  int exitOf(int seg) const {
    if (seg < 0) return -1;
    const TfSeg& g = segs[(size_t)seg];
    return g.kind == 1 && g.nextCount > 0 ? next[(size_t)g.nextFirst] : -1;
  }
};

// One traffic car. The caller copies the body in (x, z, yaw, speed) before
// every step and reads the pedals back.
struct TfCar {
  int on = 0;            // driving the graph
  int seg = -1;          // where it is
  int nxt = -1;          // where it goes next (chosen on entering `seg`)
  int hold = -1;         // the connection it has reserved (-1 none)
  int atLine = 0;        // stopped at a stop line this step (telemetry)
  float s = 0.0F;        // arc along seg
  unsigned int rng = 1U;
  float x = 0.0F, z = 0.0F, yaw = 0.0F, speed = 0.0F;
  float half = 2.2F;     // half the body length, units
  float halfW = 1.0F;    // half the body width, units (what a lane change clears)
  float brake = 10.0F;   // the definition's braking deceleration
  float stopT = 0.0F;    // seconds at a standstill
  int passed = 0;        // nodes driven through (telemetry, the check)
  // Lane changes (a road with several lanes each way). `want` is the
  // movement the car means to take at the end of this stretch, `wantLane`
  // the lane it leaves from; `chg` the lane it is moving into right now,
  // blended over chgLen units of that lane from chgS0 (chgS = where it is).
  int want = -1;
  int wantLane = -1;
  int chg = -1;
  float chgS0 = 0.0F, chgS = 0.0F, chgLen = 0.0F;
  float chgAt = -100.0F;  // clock when the last change ended
  int changes = 0;        // lane changes completed (telemetry, the check)
  float lead = 1e9F;      // last step's bumper-to-bumper gap to what is ahead
};

// A vehicle that is not traffic (the player's car, a waypoint racer, a parked
// car): traffic sees it only as something in the way.
struct TfObstacle {
  float x, z, yaw, half;
  float speed;
};

struct TfSim {
  TfGraph g;
  std::vector<TfCar> cars;
  std::vector<unsigned short> occ;   // per segment: cars holding it
  std::vector<TfObstacle> obst;      // this frame's other vehicles
  float clock = 0.0F;
  float gapTime = 3.5F;    // seconds of clear road a yielding car needs
  int leftHand = 0;        // only the lane graph's offsets know; kept for telemetry
  int changing = 1;        // 0 = cars keep their lane (Preferences > Traffic > Lane changes)
  int laneChanges = 0;     // lane changes begun to reach a turn's lane (telemetry)
  int overtakes = 0;       // ... and to pass a slow or stopped car

  void reset(int n) {
    cars.assign((size_t)(n > 0 ? n : 0), TfCar());
    occ.assign(g.segs.size(), 0);
    for (int i = 0; i < (int)cars.size(); ++i) cars[(size_t)i].rng = 2654435761U * (unsigned int)(i + 1) + 12345U;
  }

  unsigned int rnd(TfCar& c) {
    c.rng = c.rng * 1664525U + 1013904223U;
    return c.rng >> 8;
  }

  // The next segment after `seg`: a random successor, straight on three
  // times as likely as a turn and a turn back only when nothing else goes.
  int pickNext(TfCar& c, int seg) {
    if (seg < 0) return -1;
    const TfSeg& G = g.segs[(size_t)seg];
    if (G.nextCount <= 0) return -1;
    if (G.nextCount == 1) return g.next[(size_t)G.nextFirst];
    int total = 0;
    for (int k = 0; k < G.nextCount; ++k) {
      const TfSeg& n = g.segs[(size_t)g.next[(size_t)(G.nextFirst + k)]];
      total += n.turn == 0 ? 3 : (n.turn == 3 ? 0 : 1);
    }
    if (total <= 0) return g.next[(size_t)G.nextFirst];
    int r = (int)(rnd(c) % (unsigned int)total);
    for (int k = 0; k < G.nextCount; ++k) {
      const int ns = g.next[(size_t)(G.nextFirst + k)];
      const TfSeg& n = g.segs[(size_t)ns];
      r -= n.turn == 0 ? 3 : (n.turn == 3 ? 0 : 1);
      if (r < 0) return ns;
    }
    return g.next[(size_t)G.nextFirst];
  }

  // The way on from lane `seg` when the road has several lanes each way:
  // first WHICH movement (straight on three times as likely as either turn,
  // over everything the stretch's lanes offer), then the lane nearest this
  // one that offers it - a near-side turn leaves from the kerb lane, a
  // far-side turn from the centre lane. The car remembers it (want,
  // wantLane) and changes lanes to get there; until it does, it carries on
  // its own lane's way (straight on when there is one), so a change it never
  // finds a gap for costs it the turn and never strands it. A single lane
  // each way is pickNext, unchanged.
  int route(TfCar& c, int seg) {
    c.want = -1;
    c.wantLane = -1;
    if (seg < 0) return -1;
    const TfSeg& G = g.segs[(size_t)seg];
    if (!changing || G.kind != 0 || (G.inner < 0 && G.outer < 0)) return pickNext(c, seg);
    int lanes[8];
    int n = 0, own = 0;
    int k = seg;
    for (int hop = 0; hop < 8 && g.segs[(size_t)k].outer >= 0; ++hop) k = g.segs[(size_t)k].outer;
    for (; k >= 0 && n < 8; k = g.segs[(size_t)k].inner) {
      if (k == seg) own = n;
      lanes[n++] = k;
    }
    int has[3] = {0, 0, 0};
    for (int i = 0; i < n; ++i) {
      const TfSeg& L = g.segs[(size_t)lanes[i]];
      for (int q = 0; q < L.nextCount; ++q) {
        const int t = g.segs[(size_t)g.next[(size_t)(L.nextFirst + q)]].turn;
        if (t >= 0 && t < 3) has[t] = 1;
      }
    }
    const int total = has[0] * 3 + has[1] + has[2];
    if (total <= 0) return pickNext(c, seg);
    int r = (int)(rnd(c) % (unsigned int)total);
    int turn = 2;
    if (r < has[0] * 3) {
      turn = 0;
    } else {
      r -= has[0] * 3;
      if (has[1] && r < 1) turn = 1;
    }
    int bestD = 99, bestConn = -1, bestLane = -1;
    for (int i = 0; i < n; ++i) {
      const int d = i > own ? i - own : own - i;
      if (d >= bestD) continue;
      const TfSeg& L = g.segs[(size_t)lanes[i]];
      for (int q = 0; q < L.nextCount; ++q) {
        const int ns = g.next[(size_t)(L.nextFirst + q)];
        if (g.segs[(size_t)ns].turn != turn) continue;
        bestD = d;
        bestConn = ns;
        bestLane = lanes[i];
        break;
      }
    }
    if (bestConn < 0) return pickNext(c, seg);
    c.want = bestConn;
    c.wantLane = bestLane;
    if (bestLane == seg) return bestConn;
    for (int q = 0; q < G.nextCount; ++q) {
      const int ns = g.next[(size_t)(G.nextFirst + q)];
      if (g.segs[(size_t)ns].turn == 0) return ns;
    }
    return pickNext(c, seg);
  }

  // After a lane change onto `lane`: the car's intended movement as THIS lane
  // offers it (the same turn onto the same road), else it keeps wanting it
  // and takes the lane's own way meanwhile.
  int adopt(TfCar& c, int lane) {
    if (c.want < 0) return route(c, lane);
    const TfSeg& L = g.segs[(size_t)lane];
    const int wantTurn = g.segs[(size_t)c.want].turn;
    const int wantExit = g.exitOf(c.want);
    for (int q = 0; q < L.nextCount; ++q) {
      const int ns = g.next[(size_t)(L.nextFirst + q)];
      if (ns == c.want ||
          (g.segs[(size_t)ns].turn == wantTurn && g.siblings(g.exitOf(ns), wantExit))) {
        c.want = ns;
        c.wantLane = lane;
        return ns;
      }
    }
    for (int q = 0; q < L.nextCount; ++q) {
      const int ns = g.next[(size_t)(L.nextFirst + q)];
      if (g.segs[(size_t)ns].turn == 0) return ns;
    }
    return pickNext(c, lane);
  }

  void take(TfCar& c, int conn) {
    if (c.hold == conn) return;
    drop(c);
    c.hold = conn;
    ++occ[(size_t)conn];
  }
  void drop(TfCar& c) {
    if (c.hold >= 0 && occ[(size_t)c.hold] > 0) --occ[(size_t)c.hold];
    c.hold = -1;
  }

  // Puts car `ci` on `seg` at arc `s`.
  void place(int ci, int seg, float s) {
    TfCar& c = cars[(size_t)ci];
    drop(c);
    c.on = 1;
    c.seg = seg;
    c.s = s;
    c.chg = -1;
    c.chgAt = clock;
    c.lead = 1e9F;
    c.nxt = route(c, seg);
    c.stopT = 0.0F;
  }
  void remove(int ci) {
    TfCar& c = cars[(size_t)ci];
    drop(c);
    c.on = 0;
    c.seg = c.nxt = c.chg = -1;
  }

  // Follows the body along the graph: re-projects it onto its segment and
  // moves it on at the segment's end. Entering a connection it never reserved
  // (it could not stop) reserves it then; leaving one frees it.
  void track(int ci) {
    TfCar& c = cars[(size_t)ci];
    if (!c.on || c.seg < 0) return;
    c.s = g.project(c.seg, c.s, c.x, c.z);
    for (int hop = 0; hop < 3; ++hop) {
      const TfSeg& G = g.segs[(size_t)c.seg];
      if (c.s < G.len - 0.05F) break;
      if (c.nxt < 0) {
        c.s = G.len;
        break;
      }
      const float over = c.s - G.len;
      const int from = c.seg;
      if (g.segs[(size_t)c.nxt].kind == 1 && c.hold != c.nxt) take(c, c.nxt);
      if (G.kind == 1 && c.hold == from) drop(c);
      if (G.kind == 1) ++c.passed;
      c.seg = c.nxt;
      c.chg = -1;
      c.nxt = route(c, c.seg);
      c.s = g.project(c.seg, over > 0.0F ? over : 0.0F, c.x, c.z);
    }
  }

  // The point `d` ahead of the car along its path (its segment, the next, and
  // a connection's exit lane), as at(): x, y, z, tx, tz.
  void ahead(const TfCar& c, float d, float* out) const {
    int seg = c.seg;
    float s = c.s + d;
    for (int hop = 0; hop < 3 && s > g.segs[(size_t)seg].len; ++hop) {
      const int ns = hop == 0 ? c.nxt : g.exitOf(seg);
      if (ns < 0) break;
      s -= g.segs[(size_t)seg].len;
      seg = ns;
    }
    g.at(seg, s, out);
  }

  // How far car j is ahead of car c ALONG c's path (centre to centre), or a
  // negative number when it is not on it.
  float pathGap(const TfCar& c, const TfCar& j) const {
    if (j.seg == c.seg) return j.s > c.s ? j.s - c.s : -1.0F;
    const float rest = g.segs[(size_t)c.seg].len - c.s;
    if (c.nxt >= 0 && j.seg == c.nxt) return rest + j.s;
    const int ex = g.exitOf(c.nxt);
    if (ex >= 0 && j.seg == ex) return rest + g.segs[(size_t)c.nxt].len + j.s;
    return -1.0F;
  }

  // Bumper-to-bumper distance to whatever is ahead: traffic on the path, and
  // anything at all (traffic or not) inside a narrow box along the heading.
  // A car changing lanes is on both: it follows what is ahead in either, and
  // a car moving into a lane is ahead of whoever is behind it there. `who`
  // gets the car it is (-1 nothing, -2 - k obstacle k).
  float leaderGapOf(int ci, int* who) const {
    const TfCar& c = cars[(size_t)ci];
    float best = 1e9F;
    *who = -1;
    const float fx = sinf(c.yaw * 0.017453293F), fz = cosf(c.yaw * 0.017453293F);
    for (int j = 0; j < (int)cars.size(); ++j) {
      if (j == ci || !cars[(size_t)j].on) continue;
      const TfCar& o = cars[(size_t)j];
      float d = pathGap(c, o);
      if (d >= 0.0F && c.chg >= 0 && o.seg != c.chg && o.chg != c.chg) {
        // Changing lanes, the car still in the old lane ahead is only in the
        // way while the two would touch side by side - and it may be passed
        // closer (1 unit, not 2): this is how a car that stopped right
        // behind a breakdown steers out round it.
        const float ofx = sinf(o.yaw * 0.017453293F), ofz = cosf(o.yaw * 0.017453293F);
        const float lat = (c.x - o.x) * ofz - (c.z - o.z) * ofx;
        const float clear = c.halfW + o.halfW + 0.15F;
        d = lat > clear || lat < -clear ? -1.0F : d + 1.0F;
      }
      if (d < 0.0F && c.chg >= 0 && o.seg == c.chg && o.s > c.chgS) d = o.s - c.chgS;
      if (d < 0.0F && c.chg >= 0 && o.chg == c.chg && o.chgS > c.chgS) d = o.chgS - c.chgS;
      if (d < 0.0F && o.chg >= 0 && o.chg == c.seg && c.seg >= 0 && o.chgS > c.s) d = o.chgS - c.s;
      if (d < 0.0F) {
        const float rx = o.x - c.x, rz = o.z - c.z;
        const float ah = rx * fx + rz * fz;
        const float lat = rx * fz - rz * fx;
        if (ah > 0.5F && ah < 24.0F && lat < 1.3F && lat > -1.3F) d = ah;
      }
      if (d >= 0.0F && d - c.half - o.half < best) {
        best = d - c.half - o.half;
        *who = j;
      }
    }
    for (int k = 0; k < (int)obst.size(); ++k) {
      const TfObstacle& o = obst[(size_t)k];
      const float rx = o.x - c.x, rz = o.z - c.z;
      const float ah = rx * fx + rz * fz;
      const float lat = rx * fz - rz * fx;
      if (ah > 0.5F && ah < 24.0F && lat < 1.6F && lat > -1.6F && ah - c.half - o.half < best) {
        best = ah - c.half - o.half;
        *who = -2 - k;
      }
    }
    return best;
  }
  float leaderGap(int ci) const {
    int who = -1;
    return leaderGapOf(ci, &who);
  }

  // Is there room on lane `tgt` at arc `sT` for car `ci` to move in: clear
  // road ahead (more of it the faster it closes on what is there) and behind
  // (more the faster what is there closes on it). Cars on the lane, cars
  // moving into it, cars coming onto it out of a node, and anything else
  // (the player, a parked car) standing on it.
  bool gapClear(int ci, int tgt, float sT) const {
    const TfCar& c = cars[(size_t)ci];
    const float v = c.speed > 0.0F ? c.speed : 0.0F;
    auto roomy = [&](float d, float oHalf, float oSpeed) {
      const float ov = oSpeed > 0.0F ? oSpeed : 0.0F;
      const float room = (d >= 0.0F ? d : -d) - c.half - oHalf;
      if (d >= 0.0F) return room > 3.0F + (v > ov ? (v - ov) * 2.5F : 0.0F);
      return room > 3.0F + ov * 0.6F + (ov > v ? (ov - v) * 2.5F : 0.0F);
    };
    for (int j = 0; j < (int)cars.size(); ++j) {
      if (j == ci || !cars[(size_t)j].on) continue;
      const TfCar& o = cars[(size_t)j];
      float a = 0.0F;
      if (o.seg == tgt) a = o.s;
      else if (o.chg == tgt) a = o.chgS;
      else if (o.seg >= 0 && g.segs[(size_t)o.seg].kind == 1 && g.exitOf(o.seg) == tgt)
        a = o.s - g.segs[(size_t)o.seg].len;
      else continue;
      if (!roomy(a - sT, o.half, o.speed)) return false;
    }
    float P[5];
    g.at(tgt, sT, P);
    for (const TfObstacle& o : obst) {
      const float along = (o.x - P[0]) * P[3] + (o.z - P[2]) * P[4];
      if (along > 40.0F || along < -40.0F) continue;
      float Q[5];
      g.at(tgt, sT + along, Q);
      const float ex = o.x - Q[0], ez = o.z - Q[2];
      if (ex * ex + ez * ez > 2.2F * 2.2F) continue;
      if (!roomy(along, o.half, o.speed)) return false;
    }
    return true;
  }

  // Is `who` (leaderGapOf's) something car `ci` should pass rather than
  // queue behind: a car standing or crawling in its lane stretch that is not
  // itself queueing (not at a line, not close behind another, not near its
  // lane's end), or anything else in the lane (the player, a parked car)
  // standing still. Only on a road with another lane this way.
  bool passable(int ci, int who) const {
    const TfCar& c = cars[(size_t)ci];
    if (!changing || who == -1 || c.seg < 0) return false;
    const TfSeg& S = g.segs[(size_t)c.seg];
    if (S.kind != 0 || (S.inner < 0 && S.outer < 0)) return false;
    if (who >= 0) {
      const TfCar& o = cars[(size_t)who];
      const float oRem = o.seg >= 0 ? g.segs[(size_t)o.seg].len - o.s : 0.0F;
      return o.seg == c.seg && o.chg < 0 && o.speed < 0.45F * g.speed && !o.atLine &&
             o.lead > 8.0F && oRem > 6.0F;
    }
    const TfObstacle& o = obst[(size_t)(-2 - who)];
    return o.speed < 1.0F && o.speed > -1.0F;
  }

  // Should car `ci`, `lead` behind `who`, change lanes now - and to which?
  // To reach the lane its movement leaves from, or to pass a car (or
  // anything else) standing or crawling in its lane that is NOT simply
  // queueing (a car waiting at a line, or behind another, is not passed).
  // Only on a lane, holding no junction, not just after a change, and with
  // room to finish the change before it has to brake for the line.
  void considerChange(int ci, float lead, int who) {
    TfCar& c = cars[(size_t)ci];
    if (!changing || c.chg >= 0 || c.hold >= 0 || c.seg < 0 || clock - c.chgAt < 3.0F) return;
    const TfSeg& S = g.segs[(size_t)c.seg];
    if (S.kind != 0 || (S.inner < 0 && S.outer < 0)) return;
    const float v = c.speed > 0.0F ? c.speed : 0.0F;
    float L = 8.0F + 1.0F * v;
    if (L > 24.0F) L = 24.0F;
    const float remain = S.len - c.s - c.half;
    const float stopD = v * v / (2.0F * c.brake * 0.45F) + 3.0F;
    if (remain < L + stopD + 6.0F) return;
    int tgt = -1, reason = 0;
    if (c.wantLane >= 0 && c.wantLane != c.seg) {
      tgt = g.stepToward(c.seg, c.wantLane);
      reason = 1;
    }
    const bool pass = lead < 22.0F && passable(ci, who);
    if (pass) {
      // The move has to be over before the car reaches what it passes.
      if (L > lead + 1.0F) L = lead + 1.0F > 8.0F ? lead + 1.0F : 8.0F;
      // Pass on the side the car's movement wants, else toward the centre,
      // else toward the kerb.
      int side[3] = {tgt, S.inner, S.outer};
      tgt = -1;
      for (int k = 0; k < 3 && tgt < 0; ++k) {
        const int t = side[k];
        if (t < 0) continue;
        const float sT = g.project(t, c.s * g.segs[(size_t)t].len / (S.len > 1e-3F ? S.len : 1.0F), c.x, c.z);
        if (g.segs[(size_t)t].len - sT - c.half > L + stopD + 6.0F && gapClear(ci, t, sT)) tgt = t;
      }
      reason = 2;
    } else if (tgt >= 0) {
      // gapClear asks for more room ahead the slower the car there is, so
      // this never moves in behind a car it would only have to pass again.
      const float sT0 = g.project(tgt, c.s * g.segs[(size_t)tgt].len / (S.len > 1e-3F ? S.len : 1.0F), c.x, c.z);
      if (g.segs[(size_t)tgt].len - sT0 - c.half < L + stopD + 6.0F || !gapClear(ci, tgt, sT0)) tgt = -1;
    }
    if (tgt < 0) return;
    c.chg = tgt;
    c.chgS0 = c.chgS = g.project(tgt, c.s * g.segs[(size_t)tgt].len / (S.len > 1e-3F ? S.len : 1.0F), c.x, c.z);
    c.chgLen = L;
    if (reason == 2) ++overtakes;
    else ++laneChanges;
  }

  // A lane change in flight: how far along the new lane the car has come.
  // Done once it has covered the blend (or its old lane is about to end,
  // whatever is left of the move): it is then ON the new lane, and its way on
  // is its movement as that lane offers it.
  void laneStep(int ci) {
    TfCar& c = cars[(size_t)ci];
    if (c.chg < 0 || c.seg < 0) return;
    c.chgS = g.project(c.chg, c.chgS, c.x, c.z);
    const TfSeg& S = g.segs[(size_t)c.seg];
    // Done = the blend covered AND the body within 0.7 of the new lane (a
    // slow car lags its pursuit point), or twice the blend whatever, or the
    // old lane about to end.
    float P[5];
    g.at(c.chg, c.chgS, P);
    const float ex = P[0] - c.x, ez = P[2] - c.z;
    float run = c.chgS - c.chgS0;
    // Something ahead closer than the rest of the blend (the car it passes,
    // the end of a queue): finish the move within that room instead.
    if (c.lead < c.chgLen - run) {
      c.chgLen = run + (c.lead > 3.0F ? c.lead : 3.0F);
      if (c.chgLen < 6.0F) c.chgLen = 6.0F;
    }
    const bool there = run >= c.chgLen * 0.92F && ex * ex + ez * ez < 0.7F * 0.7F;
    if (!there && run < 2.0F * c.chgLen && S.kind == 0 && S.len - c.s > 4.0F) return;
    c.seg = c.chg;
    c.s = c.chgS;
    c.chg = -1;
    c.chgAt = clock;
    ++c.changes;
    c.nxt = adopt(c, c.seg);
    if (c.hold >= 0 && c.hold != c.nxt) drop(c);
  }

  // How far into its lane change car c is, d units further on: 0 on the old
  // lane, 1 on the new, smoothstepped.
  float blendAt(const TfCar& c, float d) const {
    float w = (c.chgS - c.chgS0 + d) / (c.chgLen > 1.0F ? c.chgLen : 1.0F);
    w = w < 0.0F ? 0.0F : (w > 1.0F ? 1.0F : w);
    return w * w * (3.0F - 2.0F * w);
  }
  // The point `d` ahead of the car as ahead(), blended across a lane change
  // in flight - the far path's pose, so a car changing lanes out there slides
  // across instead of jumping.
  void pose(const TfCar& c, float d, float* out) const {
    ahead(c, d, out);
    if (c.chg < 0) return;
    float Q[5];
    g.at(c.chg, c.chgS + d, Q);
    const float w = blendAt(c, d);
    for (int k = 0; k < 3; ++k) out[k] += (Q[k] - out[k]) * w;
  }

  // May car `ci` enter connection `conn`, `remain` units short of its line?
  bool mayEnter(int ci, int conn, float remain) const {
    const TfCar& c = cars[(size_t)ci];
    const TfSeg& C = g.segs[(size_t)conn];
    const int lt = g.light(C.node, C.group, clock);
    if (lt == 2) return false;
    if (lt == 1) {
      // Amber: stop if a firm stop still fits short of the line - unless the
      // car is the one waiting AT the line to turn across oncoming traffic,
      // which clears the junction now that the oncoming stream has to stop
      // (the all-red that follows is its time to do it).
      const float v = c.speed > 0.0F ? c.speed : 0.0F;
      const bool waiting = remain < 2.0F && c.stopT > 1.0F && C.turn == 2;
      if (!waiting && remain > v * v / (2.0F * c.brake * 0.8F)) return false;
    }
    // A car already in a crossing path keeps it.
    for (int k = 0; k < C.confCount; ++k)
      if (occ[(size_t)g.conf[(size_t)(C.confFirst + k)]] > 0) return false;
    // Do not block the box: the exit lane needs room for this car.
    const int ex = g.exitOf(conn);
    if (ex >= 0)
      for (int j = 0; j < (int)cars.size(); ++j) {
        if (j == ci || !cars[(size_t)j].on) continue;
        const TfCar& o = cars[(size_t)j];
        if ((o.seg == ex && o.s - o.half < 2.0F * c.half + 1.5F) ||
            (o.chg == ex && o.chgS - o.half < 2.0F * c.half + 1.5F))
          return false;
      }
    // Give way: a car coming to a crossing path of higher rank, close in time.
    for (int k = 0; k < C.confCount; ++k) {
      const int d = g.conf[(size_t)(C.confFirst + k)];
      const TfSeg& D = g.segs[(size_t)d];
      if (D.rank <= C.rank) continue;
      if (g.light(D.node, D.group, clock) == 2) continue;
      for (int j = 0; j < (int)cars.size(); ++j) {
        if (j == ci || !cars[(size_t)j].on) continue;
        const TfCar& o = cars[(size_t)j];
        if (o.nxt != d || o.hold == d || g.segs[(size_t)o.seg].kind != 0) continue;
        const float rem = g.segs[(size_t)o.seg].len - o.s - o.half;
        // An oncoming car that its own amber will stop is no reason to wait.
        const float ov = o.speed > 0.0F ? o.speed : 0.0F;
        if (g.light(D.node, D.group, clock) == 1 && rem > ov * ov / (2.0F * o.brake * 0.8F))
          continue;
        const float v = o.speed > 2.0F ? o.speed : 2.0F;
        if (rem < 50.0F && rem / v < gapTime) return false;
      }
    }
    return true;
  }

  // One step of car `ci`: where it is, then throttle (-1..1), brake (0..1)
  // and steer (-1..1, positive turns toward +yaw) - the waypoint AI's
  // conventions, so the vehicle sim cannot tell the two drivers apart.
  void drive(int ci, float* throttle, float* brake, float* steer) {
    TfCar& c = cars[(size_t)ci];
    *throttle = 0.0F;
    *brake = 1.0F;
    *steer = 0.0F;
    c.atLine = 0;
    if (!c.on || c.seg < 0) return;
    laneStep(ci);
    track(ci);
    const float v = c.speed > 0.0F ? c.speed : 0.0F;
    int who = -1;
    const float lead = leaderGapOf(ci, &who);  // bumper to bumper
    c.lead = lead;
    considerChange(ci, lead, who);
    // Steering: pure pursuit of a point `look` ahead on the path - across a
    // lane change, a point blended from the old lane onto the new one, so the
    // car drifts over smoothly along the blend's length.
    const float look = (c.half > 2.0F ? 2.0F * c.half : 4.0F) + 0.35F * v;
    float P[5];
    pose(c, look, P);
    const float want = atan2f(P[0] - c.x, P[2] - c.z) * 57.29578F;
    float err = want - c.yaw;
    while (err > 180.0F) err -= 360.0F;
    while (err < -180.0F) err += 360.0F;
    float st = err / 35.0F;
    *steer = st > 1.0F ? 1.0F : (st < -1.0F ? -1.0F : st);
    // Speed: the lowest of the lane's speed, every curve ahead (each braked
    // down to from here), the stop line and the car ahead.
    const float b = c.brake * 0.45F;
    float allow = g.speed;
    {
      const float horizon = v * v / (2.0F * b) + look + 8.0F;
      int seg = c.seg;
      float base = -c.s;  // arc of the segment's start, relative to the car
      for (int hop = 0; hop < 3 && seg >= 0 && base < horizon; ++hop) {
        const TfSeg& G = g.segs[(size_t)seg];
        for (int k = 0; k < G.count; ++k) {
          const int i = G.first + k;
          const float d = base + g.arc[(size_t)i];
          if (d < 0.0F) continue;
          if (d > horizon) break;
          const float vm = g.vmax[(size_t)i];
          const float a2 = vm * vm + 2.0F * b * (d > 1.0F ? d - 1.0F : 0.0F);
          if (a2 < allow * allow) allow = sqrtf(a2);
        }
        base += G.len;
        seg = hop == 0 ? c.nxt : g.exitOf(seg);
      }
    }
    const TfSeg& S = g.segs[(size_t)c.seg];
    const float remain = S.len - c.s - c.half;  // the front bumper to the line
    if (c.nxt < 0) {
      // A lane that goes nowhere: stop at its end.
      const float r = remain - 0.5F;
      const float a = sqrtf(r > 0.0F ? 2.0F * b * r : 0.0F);
      if (a < allow) allow = a;
    } else if (S.kind == 0 && g.segs[(size_t)c.nxt].kind == 1 && c.hold != c.nxt) {
      const float stopD = v * v / (2.0F * b) + 3.0F;
      if (remain < stopD + 6.0F) {
        if (mayEnter(ci, c.nxt, remain)) {
          // Only the FIRST car in line reserves: one queued behind it would
          // hold the junction for a car that cannot move, and gridlock it.
          if (remain < stopD + 2.0F && lead > remain && c.chg < 0) take(c, c.nxt);
        } else {
          c.atLine = 1;
          const float r = remain - 0.6F;
          const float a = sqrtf(r > 0.0F ? 2.0F * b * r : 0.0F);
          if (a < allow) allow = a;
        }
      }
    }
    // Behind something it means to pass, it stops further back (10 units, not
    // 2): the room to steer out round it from a standstill.
    const float gap = lead - (c.chg < 0 && passable(ci, who) ? 10.0F : 2.0F);
    {
      const float a = sqrtf(gap > 0.0F ? 2.0F * b * gap : 0.0F);
      if (a < allow) allow = a;
    }
    // Pedals.
    const float ae = err < 0.0F ? -err : err;
    if (allow < 0.3F && v < 0.6F) {
      *throttle = 0.0F;
      *brake = 1.0F;
    } else if (v > allow + 1.0F) {
      *throttle = 0.0F;
      const float k = (v - allow) / 3.0F;
      *brake = k < 0.3F ? 0.3F : (k > 1.0F ? 1.0F : k);
    } else if (v > allow) {
      *throttle = 0.0F;
      *brake = 0.0F;
    } else {
      *brake = 0.0F;
      *throttle = v > allow - 2.0F ? 0.35F : 0.8F;
      if (ae > 50.0F && *throttle > 0.3F) *throttle = 0.3F;
    }
  }

  // A free spot on a lane between rMin and rMax of (fx, fz), not inside the
  // camera's view cone (forward cfx, cfz; cos of the half angle viewCos),
  // `clear` units from every car. Returns the lane and its arc, or -1.
  int spawnPick(unsigned int& rng, float fx, float fz, float cfx, float cfz, float viewCos,
                float rMin, float rMax, float clear, float* sOut) const {
    const int n = (int)g.lanes.size();
    if (n <= 0) return -1;
    for (int tries = 0; tries < 24; ++tries) {
      rng = rng * 1664525U + 1013904223U;
      const int seg = g.lanes[(size_t)((rng >> 8) % (unsigned int)n)];
      const TfSeg& G = g.segs[(size_t)seg];
      rng = rng * 1664525U + 1013904223U;
      const float s = G.len * (float)((rng >> 8) & 0xFFFF) / 65536.0F;
      if (G.len < 8.0F) continue;
      float p[5];
      g.at(seg, s, p);
      const float dx = p[0] - fx, dz = p[2] - fz;
      const float d = sqrtf(dx * dx + dz * dz);
      if (d < rMin || d > rMax) continue;
      if (d > 1e-3F && (dx * cfx + dz * cfz) / d > viewCos) continue;
      bool roomy = true;
      for (const TfCar& o : cars)
        if (o.on && (o.x - p[0]) * (o.x - p[0]) + (o.z - p[2]) * (o.z - p[2]) < clear * clear)
          roomy = false;
      for (const TfObstacle& o : obst)
        if ((o.x - p[0]) * (o.x - p[0]) + (o.z - p[2]) * (o.z - p[2]) < clear * clear)
          roomy = false;
      if (!roomy) continue;
      *sOut = s;
      return seg;
    }
    return -1;
  }

  // Lane length within `r` of (fx, fz), a point every few units: what the
  // density setting multiplies.
  float laneLengthNear(float fx, float fz, float r) const {
    float total = 0.0F;
    for (int si : g.lanes) {
      const TfSeg& G = g.segs[(size_t)si];
      for (int k = 0; k + 1 < G.count; ++k) {
        const int i = G.first + k;
        const float dx = g.pts[(size_t)i * 3] - fx, dz = g.pts[(size_t)i * 3 + 2] - fz;
        if (dx * dx + dz * dz < r * r) total += g.arc[(size_t)(i + 1)] - g.arc[(size_t)i];
      }
    }
    return total;
  }
};
