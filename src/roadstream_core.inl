// Road streaming core (docs/roads.md "Road streaming").
//
// ONE SOURCE, TWO HOMES. The editor compiles this file inside namespace
// roadstream (src/roadstream.cpp, for --vehicle-check), and the codegen pastes
// it VERBATIM into class TerrainGame of a generated game whose project streams
// its roads (roadstream::coreSource, embedded at build by CMake). So the rules
// for this file are the class body's: structs with member functions only - no
// free functions, no namespace-scope statics, no #include, and no C block
// comments (the paste can land inside one). std::vector is all it uses.
//
// What lives here is everything about streaming that does not touch the
// engine: the XZ boxes of the streamed items, the static coarse grid that
// says which items touch which cell, the per-chunk triangle index that
// replaces the global ROADINDEX grid while chunks come and go, the ring's
// load/keep decisions, and roadSurfaceAt's triangle test (the spill lift
// needs it before any chunk exists).

// An item's footprint on the ground plane.
struct RsBox {
  float x0, z0, x1, z1;
  // Squared distance from (x, z) to the box, 0 inside it.
  float dist2(float x, float z) const {
    const float dx = x < x0 ? x0 - x : (x > x1 ? x - x1 : 0.0F);
    const float dz = z < z0 ? z0 - z : (z > z1 ? z - z1 : 0.0F);
    return dx * dx + dz * dz;
  }
};

// Every item, keyed by the cells its box touches. Built once per scene over
// ALL items, resident or not: an item's box never changes, so neither does
// this - load and unload only flip the item's own state.
struct RsGrid {
  float x0 = 0.0F, z0 = 0.0F, cell = 64.0F, inv = 1.0F / 64.0F;
  int nx = 0, nz = 0;
  std::vector<unsigned int> start;    // nx * nz + 1 prefix offsets
  std::vector<unsigned short> items;  // item ids, by cell

  int cellX(float x) const {
    const int i = (int)((x - x0) * inv);
    return i < 0 ? 0 : (i > nx - 1 ? nx - 1 : i);
  }
  int cellZ(float z) const {
    const int i = (int)((z - z0) * inv);
    return i < 0 ? 0 : (i > nz - 1 ? nz - 1 : i);
  }
  // The cell (x, z) falls in, -1 outside the grid (no item there at all).
  int cellAt(float x, float z) const {
    if (nx <= 0 || x < x0 || z < z0) return -1;
    const int ix = (int)((x - x0) * inv), iz = (int)((z - z0) * inv);
    if (ix >= nx || iz >= nz) return -1;
    return iz * nx + ix;
  }
  void build(const RsBox* boxes, int count, float cellSize) {
    cell = cellSize > 1.0F ? cellSize : 1.0F;
    inv = 1.0F / cell;
    start.clear();
    items.clear();
    nx = nz = 0;
    if (count <= 0) return;
    float mnx = boxes[0].x0, mnz = boxes[0].z0, mxx = boxes[0].x1, mxz = boxes[0].z1;
    for (int i = 1; i < count; ++i) {
      if (boxes[i].x0 < mnx) mnx = boxes[i].x0;
      if (boxes[i].z0 < mnz) mnz = boxes[i].z0;
      if (boxes[i].x1 > mxx) mxx = boxes[i].x1;
      if (boxes[i].z1 > mxz) mxz = boxes[i].z1;
    }
    // Half a cell of margin: a query a hair outside the outermost box still
    // lands in a cell, and the items there say "no surface" honestly.
    x0 = mnx - 0.5F * cell;
    z0 = mnz - 0.5F * cell;
    nx = (int)((mxx - x0) * inv) + 2;
    nz = (int)((mxz - z0) * inv) + 2;
    start.assign((size_t)nx * (size_t)nz + 1, 0U);
    for (int pass = 0; pass < 2; ++pass) {
      std::vector<unsigned int> cursor;
      if (pass == 1) {
        for (size_t k = 1; k < start.size(); ++k) start[k] += start[k - 1];
        items.assign((size_t)start.back(), 0);
        cursor.assign(start.begin(), start.end() - 1);
      }
      for (int i = 0; i < count; ++i) {
        const int ix0 = cellX(boxes[i].x0), ix1 = cellX(boxes[i].x1);
        const int iz0 = cellZ(boxes[i].z0), iz1 = cellZ(boxes[i].z1);
        for (int iz = iz0; iz <= iz1; ++iz)
          for (int ix = ix0; ix <= ix1; ++ix) {
            const size_t k = (size_t)iz * (size_t)nx + (size_t)ix;
            if (pass == 0)
              ++start[k + 1];
            else
              items[cursor[k]++] = (unsigned short)i;
          }
      }
    }
  }
};

// The ROADINDEX grid (buildRoadHeightIndex) of ONE chunk: ~4-unit cells over
// the chunk's own box, each listing the triangles whose box touches it by the
// index of their LAST vertex - the global index's packing without the chunk
// half, because the chunk is implied. It is built when the chunk is built and
// freed with it, which is what makes a load or an unload cost one chunk's
// worth of work instead of a rebuild of the whole city's index.
struct RsLocalIndex {
  float x0 = 0.0F, z0 = 0.0F, inv = 0.25F;
  int nx = 0, nz = 0;
  std::vector<unsigned int> start;
  std::vector<unsigned short> items;

  // `v` is x, y, z, w per vertex (a Tyra::Vec4 array). stripRun > 0: runs of
  // that length, every consecutive triple a triangle; else a triangle list.
  // Returns false (and indexes nothing) for a chunk too big for the 16-bit
  // vertex index - no road chunk comes near it (they are capped at ~1 800).
  bool build(const float* v, size_t count, int stripRun) {
    start.clear();
    items.clear();
    nx = nz = 0;
    if (count < 3) return true;
    if (count >= 65536) return false;
    float mnx = v[0], mxx = v[0], mnz = v[2], mxz = v[2];
    for (size_t i = 1; i < count; ++i) {
      const float x = v[i * 4], z = v[i * 4 + 2];
      if (x < mnx) mnx = x;
      if (x > mxx) mxx = x;
      if (z < mnz) mnz = z;
      if (z > mxz) mxz = z;
    }
    float cellSize = 4.0F;
    // At most 64 cells a side, as the global grid caps itself at 128.
    while ((mxx - mnx) / cellSize > 63.0F || (mxz - mnz) / cellSize > 63.0F) cellSize *= 2.0F;
    x0 = mnx;
    z0 = mnz;
    inv = 1.0F / cellSize;
    nx = (int)((mxx - mnx) * inv) + 1;
    nz = (int)((mxz - mnz) * inv) + 1;
    start.assign((size_t)nx * (size_t)nz + 1, 0U);
    std::vector<unsigned int> cursor;
    const size_t run = stripRun > 0 ? (size_t)stripRun : count;
    const size_t step = stripRun > 0 ? (size_t)1 : (size_t)3;
    for (int pass = 0; pass < 2; ++pass) {
      for (size_t first = 0; first < count; first += run) {
        const size_t end = first + run < count ? first + run : count;
        for (size_t i = first + 2; i < end; i += step) {
          const float* a = v + (i - 2) * 4;
          const float* b = v + (i - 1) * 4;
          const float* d = v + i * 4;
          float tx0 = a[0] < b[0] ? a[0] : b[0];
          if (d[0] < tx0) tx0 = d[0];
          float tx1 = a[0] > b[0] ? a[0] : b[0];
          if (d[0] > tx1) tx1 = d[0];
          float tz0 = a[2] < b[2] ? a[2] : b[2];
          if (d[2] < tz0) tz0 = d[2];
          float tz1 = a[2] > b[2] ? a[2] : b[2];
          if (d[2] > tz1) tz1 = d[2];
          int ix0 = (int)((tx0 - x0) * inv), ix1 = (int)((tx1 - x0) * inv);
          int iz0 = (int)((tz0 - z0) * inv), iz1 = (int)((tz1 - z0) * inv);
          if (ix0 < 0) ix0 = 0;
          if (iz0 < 0) iz0 = 0;
          if (ix1 > nx - 1) ix1 = nx - 1;
          if (iz1 > nz - 1) iz1 = nz - 1;
          for (int iz = iz0; iz <= iz1; ++iz)
            for (int ix = ix0; ix <= ix1; ++ix) {
              const size_t k = (size_t)iz * (size_t)nx + (size_t)ix;
              if (pass == 0)
                ++start[k + 1];
              else
                items[cursor[k]++] = (unsigned short)i;
            }
        }
      }
      if (pass == 0) {
        for (size_t k = 1; k < start.size(); ++k) start[k] += start[k - 1];
        items.assign((size_t)start.back(), 0);
        cursor.assign(start.begin(), start.end() - 1);
      }
    }
    return true;
  }
  // The triangles to test at (x, z): [*b, *e). False = the point is outside
  // this chunk's box. The far edge is INCLUSIVE (clamped into the last cell),
  // so a point exactly on a chunk's boundary still finds that chunk's edge.
  bool cellRange(float x, float z, const unsigned short** b, const unsigned short** e) const {
    if (nx <= 0) return false;
    const float fx = (x - x0) * inv, fz = (z - z0) * inv;
    if (fx < 0.0F || fz < 0.0F) return false;
    int ix = (int)fx, iz = (int)fz;
    if (ix > nx - 1) {
      if (fx > (float)nx + 0.001F) return false;
      ix = nx - 1;
    }
    if (iz > nz - 1) {
      if (fz > (float)nz + 0.001F) return false;
      iz = nz - 1;
    }
    const size_t k = (size_t)iz * (size_t)nx + (size_t)ix;
    *b = items.data() + start[k];
    *e = items.data() + start[k + 1];
    return true;
  }
  size_t bytes() const { return start.capacity() * 4 + items.capacity() * 2; }
};

// roadSurfaceAt's triangle test, transcribed: the cheap XZ box reject, then
// the barycentric weights with the same 0.0001 tolerance. The spill lift runs
// before any chunk of the scene exists, and it must find the heights
// roadSurfaceAt would have found over the complete road set - so it must be
// THIS arithmetic, not a nicer one.
struct RsTri {
  static float heightAt(const float* a, const float* b, const float* c, float x, float z) {
    float lo = a[0] < b[0] ? a[0] : b[0];
    if (c[0] < lo) lo = c[0];
    if (x < lo) return -1.0e30F;
    float hi = a[0] > b[0] ? a[0] : b[0];
    if (c[0] > hi) hi = c[0];
    if (x > hi) return -1.0e30F;
    lo = a[2] < b[2] ? a[2] : b[2];
    if (c[2] < lo) lo = c[2];
    if (z < lo) return -1.0e30F;
    hi = a[2] > b[2] ? a[2] : b[2];
    if (c[2] > hi) hi = c[2];
    if (z > hi) return -1.0e30F;
    const float den = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2]);
    if (fabsf(den) < 0.000001F) return -1.0e30F;
    const float wa = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / den;
    const float wb = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / den;
    const float wc = 1.0F - wa - wb;
    if (wa < -0.0001F || wb < -0.0001F || wc < -0.0001F) return -1.0e30F;
    return wa * a[1] + wb * b[1] + wc * c[1];
  }
  // The highest surface at (x, z) over every triangle of an array laid out as
  // `stride` floats per vertex (x, y, z first), chopped the way a chunk is.
  static float highest(const float* v, int stride, size_t count, int stripRun, float x,
                       float z) {
    float best = -1.0e30F;
    const size_t run = stripRun > 0 ? (size_t)stripRun : count;
    const size_t step = stripRun > 0 ? (size_t)1 : (size_t)3;
    for (size_t first = 0; first < count; first += run) {
      const size_t end = first + run < count ? first + run : count;
      for (size_t i = first + 2; i < end; i += step) {
        const float y = heightAt(v + (i - 2) * (size_t)stride, v + (i - 1) * (size_t)stride,
                                 v + i * (size_t)stride, x, z);
        if (y > best) best = y;
      }
    }
    return best;
  }
};

// The ring's decisions, separated from what acting on them costs. An item is
// WANTED while its box is within `radius` of a focus and KEPT while it is
// within `keep` (> radius) of one: the band between them is the hysteresis,
// so driving along the boundary does not load and drop the same chunk every
// other frame.
struct RsRing {
  std::vector<unsigned int> seen;  // per item: the stamp of the last scan
  unsigned int stamp = 0;

  static bool keepItem(const RsBox& b, const float* foci, int focusCount, float keep) {
    for (int f = 0; f < focusCount; ++f)
      if (b.dist2(foci[f * 2], foci[f * 2 + 1]) <= keep * keep) return true;
    return false;
  }
  // Every absent item within `radius` of a focus, into `out` NEAREST FIRST
  // (its squared distance in `keys`, same order). Walks only the grid cells
  // the radius reaches, not the whole item list.
  void wanted(const RsGrid& g, const RsBox* boxes, const unsigned char* resident, int count,
              const float* foci, int focusCount, float radius, std::vector<int>& out,
              std::vector<float>& keys) {
    out.clear();
    keys.clear();
    if (g.nx <= 0 || count <= 0) return;
    if ((int)seen.size() != count) {
      seen.assign((size_t)count, 0U);
      stamp = 0;
    }
    if (++stamp == 0U) {
      seen.assign((size_t)count, 0U);
      stamp = 1;
    }
    const float r2 = radius * radius;
    for (int f = 0; f < focusCount; ++f) {
      const float fx = foci[f * 2], fz = foci[f * 2 + 1];
      const int ix0 = g.cellX(fx - radius), ix1 = g.cellX(fx + radius);
      const int iz0 = g.cellZ(fz - radius), iz1 = g.cellZ(fz + radius);
      for (int iz = iz0; iz <= iz1; ++iz)
        for (int ix = ix0; ix <= ix1; ++ix) {
          const size_t k = (size_t)iz * (size_t)g.nx + (size_t)ix;
          for (unsigned int e = g.start[k]; e < g.start[k + 1]; ++e) {
            const int it = g.items[e];
            if (resident[it] || seen[(size_t)it] == stamp) continue;
            seen[(size_t)it] = stamp;
            float d = boxes[it].dist2(fx, fz);
            for (int f2 = 0; f2 < focusCount; ++f2) {
              const float d2 = boxes[it].dist2(foci[f2 * 2], foci[f2 * 2 + 1]);
              if (d2 < d) d = d2;
            }
            if (d > r2) continue;
            out.push_back(it);
            keys.push_back(d);
          }
        }
    }
    // Insertion sort by distance: the list is short once the ring has caught
    // up (the frame's new edge), and at a scene load it is sorted once.
    for (size_t i = 1; i < out.size(); ++i) {
      const int it = out[i];
      const float d = keys[i];
      size_t j = i;
      while (j > 0 && keys[j - 1] > d) {
        out[j] = out[j - 1];
        keys[j] = keys[j - 1];
        --j;
      }
      out[j] = it;
      keys[j] = d;
    }
  }
};
