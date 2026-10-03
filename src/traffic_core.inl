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
  int group = -1;      // a connection at a signalled node: its approach's phase (0 or 1)
  int rank = 0;        // a connection's priority: a higher rank goes first
  int turn = 0;        // a connection: 0 straight, 1 near side, 2 far side, 3 back
  int confFirst = 0;   // the connections this one crosses, in TfGraph::conf
  int confCount = 0;
  float len = 0.0F;
};

struct TfNode {
  int signal = 0;        // 1 = traffic lights
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

  float cycle() const { return 2.0F * (green + amber + allRed); }

  // The light an approach of phase `group` sees at node `n` at time `t`:
  // 0 green, 1 amber, 2 red. Group 1's half of the cycle is group 0's,
  // shifted by half a cycle; each half is green, amber, then all-red
  // clearance, so the two groups are never green or amber together.
  int light(int n, int group, float t) const {
    if (n < 0 || n >= (int)nodes.size() || group < 0 || !nodes[(size_t)n].signal) return 0;
    const float c = cycle();
    float ph = fmodf(t + nodes[(size_t)n].offset, c);
    if (ph < 0.0F) ph += c;
    if (group == 1) {
      ph -= 0.5F * c;
      if (ph < 0.0F) ph += c;
    }
    if (ph < green) return 0;
    if (ph < green + amber) return 1;
    return 2;
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
  float brake = 10.0F;   // the definition's braking deceleration
  float stopT = 0.0F;    // seconds at a standstill
  int passed = 0;        // nodes driven through (telemetry, the check)
};

// A vehicle that is not traffic (the player's car, a waypoint racer, a parked
// car): traffic sees it only as something in the way.
struct TfObstacle {
  float x, z, yaw, half;
};

struct TfSim {
  TfGraph g;
  std::vector<TfCar> cars;
  std::vector<unsigned short> occ;   // per segment: cars holding it
  std::vector<TfObstacle> obst;      // this frame's other vehicles
  float clock = 0.0F;
  float gapTime = 3.5F;    // seconds of clear road a yielding car needs
  int leftHand = 0;        // only the lane graph's offsets know; kept for telemetry

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
    c.nxt = pickNext(c, seg);
    c.stopT = 0.0F;
  }
  void remove(int ci) {
    TfCar& c = cars[(size_t)ci];
    drop(c);
    c.on = 0;
    c.seg = c.nxt = -1;
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
      c.nxt = pickNext(c, c.seg);
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
  float leaderGap(int ci) const {
    const TfCar& c = cars[(size_t)ci];
    float best = 1e9F;
    const float fx = sinf(c.yaw * 0.017453293F), fz = cosf(c.yaw * 0.017453293F);
    for (int j = 0; j < (int)cars.size(); ++j) {
      if (j == ci || !cars[(size_t)j].on) continue;
      const TfCar& o = cars[(size_t)j];
      float d = pathGap(c, o);
      if (d < 0.0F) {
        const float rx = o.x - c.x, rz = o.z - c.z;
        const float ah = rx * fx + rz * fz;
        const float lat = rx * fz - rz * fx;
        if (ah > 0.5F && ah < 24.0F && lat < 1.3F && lat > -1.3F) d = ah;
      }
      if (d >= 0.0F && d - c.half - o.half < best) best = d - c.half - o.half;
    }
    for (const TfObstacle& o : obst) {
      const float rx = o.x - c.x, rz = o.z - c.z;
      const float ah = rx * fx + rz * fz;
      const float lat = rx * fz - rz * fx;
      if (ah > 0.5F && ah < 24.0F && lat < 1.6F && lat > -1.6F && ah - c.half - o.half < best)
        best = ah - c.half - o.half;
    }
    return best;
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
        if (o.seg == ex && o.s - o.half < 2.0F * c.half + 1.5F) return false;
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
    track(ci);
    const float v = c.speed > 0.0F ? c.speed : 0.0F;
    // Steering: pure pursuit of a point `look` ahead on the path.
    const float look = (c.half > 2.0F ? 2.0F * c.half : 4.0F) + 0.35F * v;
    float P[5];
    ahead(c, look, P);
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
    const float lead = leaderGap(ci);  // bumper to bumper
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
          if (remain < stopD + 2.0F && lead > remain) take(c, c.nxt);
        } else {
          c.atLine = 1;
          const float r = remain - 0.6F;
          const float a = sqrtf(r > 0.0F ? 2.0F * b * r : 0.0F);
          if (a < allow) allow = a;
        }
      }
    }
    const float gap = lead - 2.0F;
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
