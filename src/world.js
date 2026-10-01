// Static collision world made of axis-aligned boxes + uniform grid broadphase.
// Units are Source/Hammer units (1 unit ~ 1 inch). Y is up.
import * as THREE from 'three';

const DIST_EPSILON = 0.03125;
const CELL = 256;

export class TraceResult {
  constructor() {
    this.fraction = 1;
    this.endpos = new THREE.Vector3();
    this.normal = new THREE.Vector3();
    this.startsolid = false;
    this.allsolid = false;
    this.box = null;
  }
}

export class World {
  constructor() {
    this.boxes = [];
    this.grid = new Map();
    this.stamp = 1;
  }

  addBox(min, max, props = {}) {
    const b = { min: new THREE.Vector3(...min), max: new THREE.Vector3(...max), _s: 0, ...props };
    this.boxes.push(b);
    if (b.nonsolid) return b;
    const x0 = Math.floor(b.min.x / CELL), x1 = Math.floor(b.max.x / CELL);
    const z0 = Math.floor(b.min.z / CELL), z1 = Math.floor(b.max.z / CELL);
    for (let x = x0; x <= x1; x++) for (let z = z0; z <= z1; z++) {
      const k = x * 73856093 ^ z * 19349663;
      let arr = this.grid.get(k);
      if (!arr) { arr = []; this.grid.set(k, arr); }
      arr.push(b);
    }
    return b;
  }

  _candidates(minx, minz, maxx, maxz, out) {
    out.length = 0;
    const s = ++this.stamp;
    const x0 = Math.floor(minx / CELL), x1 = Math.floor(maxx / CELL);
    const z0 = Math.floor(minz / CELL), z1 = Math.floor(maxz / CELL);
    if ((x1 - x0 + 1) * (z1 - z0 + 1) > 400) {
      // very long trace: just test everything
      for (const b of this.boxes) if (!b.nonsolid) out.push(b);
      return out;
    }
    for (let x = x0; x <= x1; x++) for (let z = z0; z <= z1; z++) {
      const arr = this.grid.get(x * 73856093 ^ z * 19349663);
      if (!arr) continue;
      for (const b of arr) if (b._s !== s) { b._s = s; out.push(b); }
    }
    return out;
  }

  /**
   * Sweep an AABB (mins/maxs relative to origin) from start to end.
   */
  traceBox(start, end, mins, maxs, out = new TraceResult()) {
    const dx = end.x - start.x, dy = end.y - start.y, dz = end.z - start.z;
    const len = Math.sqrt(dx * dx + dy * dy + dz * dz);
    let best = 1, nAxis = -1, nSign = 0, hitBox = null;
    let startsolid = false, allsolid = false;
    const cands = this._candidates(
      Math.min(start.x, end.x) + mins.x - 1, Math.min(start.z, end.z) + mins.z - 1,
      Math.max(start.x, end.x) + maxs.x + 1, Math.max(start.z, end.z) + maxs.z + 1, _cands);
    const s = [start.x, start.y, start.z], d = [dx, dy, dz];
    const mn = [mins.x, mins.y, mins.z], mx = [maxs.x, maxs.y, maxs.z];
    for (let i = 0; i < cands.length; i++) {
      const b = cands[i];
      const bmin = [b.min.x - mx[0], b.min.y - mx[1], b.min.z - mx[2]];
      const bmax = [b.max.x - mn[0], b.max.y - mn[1], b.max.z - mn[2]];
      let tin = -Infinity, tout = Infinity, ax = -1, sg = 0, miss = false;
      for (let a = 0; a < 3; a++) {
        if (Math.abs(d[a]) < 1e-9) {
          if (s[a] <= bmin[a] || s[a] >= bmax[a]) { miss = true; break; }
        } else {
          const inv = 1 / d[a];
          let t1 = (bmin[a] - s[a]) * inv, t2 = (bmax[a] - s[a]) * inv;
          let sgn = -1;
          if (t1 > t2) { const t = t1; t1 = t2; t2 = t; sgn = 1; }
          if (t1 > tin) { tin = t1; ax = a; sg = sgn; }
          if (t2 < tout) tout = t2;
          if (tin > tout) { miss = true; break; }
        }
      }
      if (miss) continue;
      if (tout <= 0) continue;
      if (tin >= best) continue;
      if (tin < 0) {
        // start inside (or touching)
        if (ax < 0) { startsolid = true; if (tout >= 1) allsolid = true; continue; }
        const pen = -tin * Math.abs(d[ax]);
        if (pen < 0.1) { // touching the surface while moving into it
          best = 0; nAxis = ax; nSign = sg; hitBox = b;
        } else {
          startsolid = true;
          if (tout >= 1) allsolid = true;
        }
        continue;
      }
      best = tin; nAxis = ax; nSign = sg; hitBox = b;
    }
    let frac = best;
    if (frac < 1 && len > 0) frac = Math.max(0, best - DIST_EPSILON / len);
    out.fraction = frac;
    out.endpos.set(start.x + dx * frac, start.y + dy * frac, start.z + dz * frac);
    out.normal.set(0, 0, 0);
    if (nAxis >= 0) {
      if (nAxis === 0) out.normal.x = nSign; else if (nAxis === 1) out.normal.y = nSign; else out.normal.z = nSign;
    }
    out.startsolid = startsolid;
    out.allsolid = allsolid;
    out.box = hitBox;
    out.hitFraction = best; // un-pulled fraction (for rays)
    return out;
  }

  traceRay(start, end, out = new TraceResult()) {
    return this.traceBox(start, end, ZERO, ZERO, out);
  }

  // true if hull overlaps any solid
  isStuck(pos, mins, maxs) {
    const cands = this._candidates(pos.x + mins.x, pos.z + mins.z, pos.x + maxs.x, pos.z + maxs.z, _cands);
    for (const b of cands) {
      if (pos.x + mins.x < b.max.x - 0.01 && pos.x + maxs.x > b.min.x + 0.01 &&
          pos.y + mins.y < b.max.y - 0.01 && pos.y + maxs.y > b.min.y + 0.01 &&
          pos.z + mins.z < b.max.z - 0.01 && pos.z + maxs.z > b.min.z + 0.01) return true;
    }
    return false;
  }

  // visibility check between two points
  visible(a, b) {
    const tr = this.traceRay(a, b, _vtr);
    return tr.hitFraction >= 0.999 && !tr.startsolid;
  }
}

const ZERO = new THREE.Vector3();
const _cands = [];
const _vtr = new TraceResult();
