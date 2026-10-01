// Map definitions: built on a 64-unit cell grid with a tiny "brush" API,
// then compiled into collision boxes, render meshes, nav graph and spawns.
export const CS = 64; // cell size in units

class Grid {
  constructor(w, h, wallMat, wallH) {
    this.w = w; this.h = h; this.wallH = wallH;
    this.cells = [];
    for (let r = 0; r < h; r++) for (let c = 0; c < w; c++)
      this.cells.push({ f: 0, fm: 'concrete', o: 'wall', om: wallMat, wh: wallH, c: null, tags: new Set(), lava: false });
  }
  at(c, r) { return (c < 0 || r < 0 || c >= this.w || r >= this.h) ? null : this.cells[r * this.w + c]; }
  each(c0, r0, c1, r1, fn) {
    for (let r = Math.min(r0, r1); r <= Math.max(r0, r1); r++)
      for (let c = Math.min(c0, c1); c <= Math.max(c0, c1); c++) { const k = this.at(c, r); if (k) fn(k, c, r); }
  }
  floor(c0, r0, c1, r1, h = 0, mat = 'concrete') { this.each(c0, r0, c1, r1, k => { k.f = h; k.fm = mat; k.o = null; k.lava = false; }); return this; }
  lava(c0, r0, c1, r1, h = -48) { this.each(c0, r0, c1, r1, k => { k.f = h; k.fm = 'lava'; k.o = null; k.lava = true; }); return this; }
  raise(c0, r0, c1, r1, h, mat) { this.each(c0, r0, c1, r1, k => { k.f = h; if (mat) k.fm = mat; }); return this; }
  wall(c0, r0, c1, r1, mat, wh) { this.each(c0, r0, c1, r1, k => { k.o = 'wall'; if (mat) k.om = mat; k.wh = wh || this.wallH; }); return this; }
  wallStyle(c0, r0, c1, r1, mat, wh) { this.each(c0, r0, c1, r1, k => { if (k.o === 'wall') { k.om = mat; if (wh) k.wh = wh; } }); return this; }
  crate(c, r, size = 64, mat = 'crate') { const k = this.at(c, r); if (k) { k.o = 'crate'; k.size = size; k.om = mat; } return this; }
  half(c0, r0, c1, r1, mat = 'concrete', size = 48) { this.each(c0, r0, c1, r1, k => { k.o = 'half'; k.om = mat; k.size = size; }); return this; }
  container(c0, r0, c1, r1, color = 'red') { this.each(c0, r0, c1, r1, k => { k.o = 'container'; k.om = 'container_' + color; k.size = 128; }); return this; }
  barrel(c, r) { const k = this.at(c, r); if (k) { k.o = 'barrel'; k.om = 'barrel'; } return this; }
  ceil(c0, r0, c1, r1, h) { this.each(c0, r0, c1, r1, k => { k.c = h; }); return this; }
  door(c0, r0, c1, r1, h = 176) { this.each(c0, r0, c1, r1, k => { k.o = null; k.c = h; }); return this; }
  tag(c0, r0, c1, r1, t) { this.each(c0, r0, c1, r1, k => k.tags.add(t)); return this; }
}

// ---------------------------------------------------------------- de_dust
function buildDust() {
  const g = new Grid(44, 44, 'sandstone', 288);
  // CT spawn
  g.floor(17, 1, 26, 6, 0, 'tiles').tag(18, 2, 25, 4, 'CT');
  // B site
  g.floor(1, 1, 13, 12, 0, 'sand');
  // CT -> B
  g.floor(14, 2, 16, 5, 0, 'sand'); g.wall(15, 2, 15, 2); g.wall(15, 5, 15, 5); g.door(15, 3, 15, 4, 176);
  // A site + platform
  g.floor(30, 1, 42, 12, 0, 'sand');
  g.raise(32, 2, 41, 9, 32, 'tiles'); g.raise(31, 2, 31, 9, 16, 'tiles'); g.raise(32, 10, 41, 10, 16, 'tiles');
  // CT -> A
  g.floor(27, 2, 29, 5, 0, 'sand'); g.wall(28, 2, 28, 2); g.wall(28, 5, 28, 5); g.door(28, 3, 28, 4, 176);
  // Mid
  g.floor(19, 7, 24, 35, 0, 'sand'); g.floor(21, 7, 22, 35, 0, 'tiles');
  g.wall(19, 14, 20, 14); g.wall(23, 14, 24, 14); g.door(21, 14, 22, 14, 192);
  // T spawn
  g.floor(12, 36, 31, 42, 0, 'sand').tag(15, 39, 28, 41, 'T');
  // Long A
  g.floor(36, 13, 42, 35, 0, 'sand');
  g.wall(36, 24, 38, 24); g.wall(41, 24, 42, 24); g.door(39, 24, 40, 24, 192);
  g.floor(32, 36, 42, 40, 0, 'sand');
  // the pit
  g.raise(40, 31, 42, 31, -16); g.raise(40, 32, 42, 35, -32);
  // Catwalk / short A
  g.floor(25, 16, 26, 18, 16, 'tiles'); g.floor(27, 16, 28, 18, 32, 'tiles'); g.floor(29, 16, 35, 18, 48, 'tiles');
  g.floor(33, 13, 35, 15, 48, 'tiles'); g.raise(33, 10, 35, 12, 32, 'tiles');
  g.floor(29, 15, 32, 15, 48, 'tiles'); g.half(29, 15, 32, 15, 'plaster', 40);
  // B tunnels (covered)
  g.floor(2, 13, 9, 35, 0, 'concrete'); g.ceil(2, 18, 9, 30, 144);
  g.wall(2, 13, 3, 13); g.wall(8, 13, 9, 13);
  g.floor(2, 36, 11, 40, 0, 'sand');
  // mid -> B lower tunnel
  g.floor(10, 22, 18, 24, 0, 'concrete'); g.ceil(10, 22, 18, 24, 144);
  // style: plaster buildings with varying heights
  g.wallStyle(14, 0, 30, 12, 'plaster', 352);
  g.wallStyle(10, 14, 18, 21, 'plaster', 320);
  g.wallStyle(25, 19, 35, 34, 'sandstone', 256);
  g.wallStyle(10, 25, 18, 35, 'plaster', 224);
  // crates / cover
  g.crate(3, 3, 128).crate(4, 3).crate(3, 4).crate(9, 5, 128).crate(10, 5).crate(6, 8).crate(7, 8).crate(11, 10, 128);
  g.half(2, 10, 4, 10, 'plaster'); g.barrel(12, 2); g.barrel(1, 12); g.barrel(12, 12);
  g.crate(35, 4).crate(36, 4, 128).crate(39, 7).crate(40, 7).crate(38, 2, 128).crate(41, 2);
  g.crate(21, 10, 128).crate(23, 27).crate(19, 29).crate(20, 29, 128);
  g.crate(37, 17, 128).crate(41, 20).crate(38, 29).barrel(42, 14).barrel(36, 34);
  g.crate(14, 37, 128).crate(29, 37).crate(30, 37, 128).crate(17, 1).crate(26, 1, 128);
  g.crate(4, 33).crate(5, 33, 128);
  return {
    id: 'de_dust', name: 'de_dust', desc: 'Пустынная классика: мид, лонг, катвок и туннели Б',
    grid: g, mode: 'tdm',
    sky: { top: 0x3b7dd8, bottom: 0xf3d9a8, sun: 0xfff1d6, sunI: 3.0, hemiI: 1.1, fog: 0xe9d3a6, fogFar: 9000 },
    sunDir: [0.45, 0.8, 0.3], accent: '#d8a657',
  };
}

// ---------------------------------------------------------------- de_warehouse
function buildWarehouse() {
  const g = new Grid(44, 44, 'concrete', 384);
  // yards
  g.floor(3, 1, 40, 8, 0, 'asphalt').tag(10, 2, 33, 4, 'CT');
  g.floor(3, 35, 40, 42, 0, 'asphalt').tag(8, 39, 35, 41, 'T');
  // side alleys
  g.floor(1, 6, 3, 37, 0, 'asphalt'); g.floor(40, 6, 42, 37, 0, 'asphalt');
  // main hall with roof & skylights
  g.floor(6, 12, 37, 31, 0, 'metalfloor'); g.ceil(6, 12, 37, 31, 320);
  g.ceil(11, 17, 13, 26, null); g.ceil(30, 17, 32, 26, null); g.ceil(20, 20, 23, 23, null);
  // hall doors
  for (const c of [9, 20, 32]) { g.floor(c, 32, c + 2, 34, 0, 'asphalt'); g.door(c, 32, c + 2, 34, 192); }
  for (const c of [6, 35]) { g.floor(c, 9, c + 2, 11, 0, 'asphalt'); g.door(c, 9, c + 2, 11, 192); }
  // side doors to alleys
  g.floor(4, 20, 5, 22, 0, 'asphalt'); g.door(4, 20, 5, 22, 176);
  g.floor(38, 20, 39, 22, 0, 'asphalt'); g.door(38, 20, 39, 22, 176);
  // catwalk (north side of hall) with stairs
  g.raise(14, 12, 29, 13, 96, 'metalfloor');
  [16, 32, 48, 64, 80].forEach((h, i) => g.raise(9 + i, 12, 9 + i, 13, h, 'metalfloor'));
  [80, 64, 48, 32, 16].forEach((h, i) => g.raise(30 + i, 12, 30 + i, 13, h, 'metalfloor'));
  // railing along the catwalk edge with a drop-down gap in the middle
  g.floor(14, 14, 29, 14, 96, 'metalfloor'); g.half(14, 14, 18, 14, 'concrete', 32); g.half(25, 14, 29, 14, 'concrete', 32);
  // containers in hall
  g.container(9, 16, 10, 20, 'red').container(15, 24, 19, 25, 'blue').container(24, 16, 25, 21, 'green');
  g.container(28, 27, 32, 28, 'red').container(17, 17, 18, 18, 'blue').container(34, 16, 35, 19, 'blue');
  g.crate(12, 28).crate(13, 28, 128).crate(26, 25).crate(27, 25, 128).crate(21, 28).crate(7, 26, 128);
  g.crate(35, 24).crate(35, 25, 128).crate(20, 16).crate(23, 16, 128);
  // yard cover
  g.container(14, 3, 18, 4, 'green').container(26, 5, 30, 6, 'red');
  g.crate(6, 5, 128).crate(7, 5).crate(36, 3, 128).crate(37, 3).crate(22, 6).barrel(4, 2).barrel(39, 7);
  g.container(13, 37, 17, 38, 'blue').container(27, 36, 31, 37, 'green');
  g.crate(6, 37).crate(22, 38, 128).crate(23, 38).crate(36, 37, 128).barrel(39, 41).barrel(4, 41);
  g.crate(2, 14, 128).crate(2, 26).crate(41, 16).crate(41, 28, 128).barrel(1, 30).barrel(42, 11);
  // building styles
  g.wallStyle(0, 0, 43, 0, 'brick', 448); g.wallStyle(0, 43, 43, 43, 'brick', 448);
  return {
    id: 'de_warehouse', name: 'de_warehouse', desc: 'Индустриальный склад: контейнеры, кэтвок, тёмный ангар',
    grid: g, mode: 'tdm',
    sky: { top: 0x5d7894, bottom: 0xc8cdd2, sun: 0xffe6c4, sunI: 2.6, hemiI: 1.0, fog: 0xb9c0c8, fogFar: 8000 },
    sunDir: [-0.4, 0.75, 0.5], accent: '#6fa8dc',
  };
}

// ---------------------------------------------------------------- aim_redline
function buildAim() {
  const g = new Grid(30, 38, 'dev_gray', 256);
  g.floor(1, 1, 28, 36, 0, 'dev_floor');
  g.tag(6, 2, 23, 3, 'CT').tag(6, 34, 23, 35, 'T');
  const side = (r, flip) => (rr) => flip ? 37 - rr : rr;
  for (const flip of [false, true]) {
    const R = side(0, flip);
    g.crate(4, R(7), 128, 'dev_orange').crate(5, R(7), 64, 'dev_orange').crate(24, R(7), 128, 'dev_orange').crate(25, R(7), 64, 'dev_orange');
    g.crate(13, R(9), 64, 'dev_orange').crate(14, R(9), 128, 'dev_orange').crate(15, R(9), 128, 'dev_orange').crate(16, R(9), 64, 'dev_orange');
    g.half(8, R(12), 11, R(12), 'dev_white', 48); g.half(18, R(12), 21, R(12), 'dev_white', 48);
    g.crate(2, R(14), 64, 'crate').crate(27, R(14), 64, 'crate');
    g.crate(6, R(15), 128, 'crate').crate(23, R(15), 128, 'crate');
  }
  // center platform
  g.raise(11, 16, 18, 21, 48, 'dev_white');
  g.raise(11, 15, 18, 15, 32, 'dev_white'); g.raise(11, 14, 18, 14, 16, 'dev_white');
  g.raise(11, 22, 18, 22, 32, 'dev_white'); g.raise(11, 23, 18, 23, 16, 'dev_white');
  g.crate(14, 18, 128, 'dev_orange').crate(15, 19, 128, 'dev_orange');
  g.half(3, 18, 6, 19, 'dev_white', 48); g.half(23, 18, 26, 19, 'dev_white', 48);
  g.wall(9, 18, 9, 19, 'dev_gray', 160); g.wall(20, 18, 20, 19, 'dev_gray', 160);
  return {
    id: 'aim_redline', name: 'aim_redline', desc: 'Аим-карта на dev-текстурах: быстро, симметрично, без укрытий от аимбота',
    grid: g, mode: 'tdm',
    sky: { top: 0x1e2a44, bottom: 0x6b7ea8, sun: 0xffffff, sunI: 2.4, hemiI: 1.3, fog: 0x55607a, fogFar: 7000 },
    sunDir: [0.3, 0.9, 0.2], accent: '#e0802a',
  };
}

// ---------------------------------------------------------------- bhop_lava (KZ)
function buildBhop() {
  const g = new Grid(18, 104, 'dev_gray', 512);
  g.lava(1, 1, 16, 102, -48);
  g.floor(1, 1, 16, 7, 0, 'dev_floor').tag(5, 2, 12, 4, 'start').tag(1, 1, 16, 7, 'startzone');
  // section 1: big blocks, 2-cell gaps (easy)
  const blk = (c, r, w, h, y = 0, m = 'dev_orange') => g.floor(c, r, c + w - 1, r + h - 1, y, m);
  blk(6, 10, 4, 3); blk(4, 15, 4, 3); blk(8, 20, 4, 3); blk(5, 25, 4, 3); blk(7, 30, 4, 3);
  // checkpoint 1
  g.floor(1, 35, 16, 38, 0, 'dev_floor').tag(1, 35, 16, 38, 'cp');
  // section 2: smaller, climbing
  blk(4, 41, 3, 3, 16); blk(6, 46, 3, 3, 32); blk(8, 51, 3, 3, 16); blk(10, 56, 3, 3, 0); blk(7, 61, 3, 3, 16); blk(5, 66, 3, 2, 32);
  g.floor(1, 70, 16, 73, 32, 'dev_floor').tag(1, 70, 16, 73, 'cp');
  // section 3: long gaps, needs speed (bhop + strafes)
  blk(6, 77, 5, 2, 32); blk(3, 82, 5, 2, 16); blk(7, 87, 5, 2, 16); blk(10, 92, 5, 2, 0);
  g.floor(1, 97, 16, 102, 0, 'dev_white').tag(1, 97, 16, 102, 'finish');
  return {
    id: 'bhop_lava', name: 'bhop_lava', desc: 'KZ/Бхоп-карта над лавой: таймер, чекпоинты, джамп-статы',
    grid: g, mode: 'kz', killY: -16,
    sky: { top: 0x220808, bottom: 0xff6a2a, sun: 0xffd2a0, sunI: 2.2, hemiI: 1.2, fog: 0x5a1a10, fogFar: 9000 },
    sunDir: [0.2, 0.9, -0.3], accent: '#ff6a2a',
  };
}

export const MAPS = [buildDust, buildWarehouse, buildAim, buildBhop];
export const MAP_INFO = MAPS.map(fn => { const m = fn(); return { id: m.id, name: m.name, desc: m.desc, mode: m.mode, accent: m.accent }; });
export function getMap(id) { const fn = MAPS.find(f => f().id === id) || MAPS[0]; return fn(); }

// ------------------------------------------------------------- compiler
function greedy(g, keyFn) {
  // returns rects {c0,r0,c1,r1,key,cell}
  const used = new Uint8Array(g.w * g.h);
  const rects = [];
  for (let r = 0; r < g.h; r++) for (let c = 0; c < g.w; c++) {
    if (used[r * g.w + c]) continue;
    const k = keyFn(g.at(c, r));
    if (k == null) continue;
    let c1 = c;
    while (c1 + 1 < g.w && !used[r * g.w + c1 + 1] && keyFn(g.at(c1 + 1, r)) === k) c1++;
    let r1 = r;
    outer: while (r1 + 1 < g.h) {
      for (let cc = c; cc <= c1; cc++) if (used[(r1 + 1) * g.w + cc] || keyFn(g.at(cc, r1 + 1)) !== k) break outer;
      r1++;
    }
    for (let rr = r; rr <= r1; rr++) for (let cc = c; cc <= c1; cc++) used[rr * g.w + cc] = 1;
    rects.push({ c0: c, r0: r, c1, r1, key: k, cell: g.at(c, r) });
  }
  return rects;
}

export function compileMap(map) {
  const g = map.grid;
  const ox = -g.w * CS / 2, oz = -g.h * CS / 2;
  const X = c => ox + c * CS, Z = r => oz + r * CS;
  const boxes = []; // {min,max,mat,kind}
  const push = (x0, y0, z0, x1, y1, z1, mat, kind, extra) => boxes.push({ min: [x0, y0, z0], max: [x1, y1, z1], mat, kind, ...extra });

  // floors (every cell has a floor slab, walls stand on them)
  for (const R of greedy(g, k => `${k.f}|${k.fm}`)) {
    push(X(R.c0), R.cell.lava ? R.cell.f - 64 : Math.min(R.cell.f - 64, -64), Z(R.r0), X(R.c1 + 1), R.cell.f, Z(R.r1 + 1), R.cell.fm, R.cell.lava ? 'lava' : 'floor');
  }
  for (const R of greedy(g, k => k.o === 'wall' ? `${k.om}|${k.f}|${k.wh}` : null))
    push(X(R.c0), R.cell.f, Z(R.r0), X(R.c1 + 1), R.cell.wh, Z(R.r1 + 1), R.cell.om, 'wall');
  for (const R of greedy(g, k => k.o === 'half' ? `${k.om}|${k.f}|${k.size}` : null))
    push(X(R.c0), R.cell.f, Z(R.r0), X(R.c1 + 1), R.cell.f + R.cell.size, Z(R.r1 + 1), R.cell.om, 'half');
  for (const R of greedy(g, k => k.o === 'container' ? `${k.om}|${k.f}` : null))
    push(X(R.c0) + 2, R.cell.f, Z(R.r0) + 2, X(R.c1 + 1) - 2, R.cell.f + 128, Z(R.r1 + 1) - 2, R.cell.om, 'container');
  for (const R of greedy(g, k => (k.c != null && k.o !== 'wall') ? `${k.c}` : null))
    push(X(R.c0), R.cell.c, Z(R.r0), X(R.c1 + 1), g.wallH, Z(R.r1 + 1), 'concrete', 'ceil');
  // crates & barrels individually
  for (let r = 0; r < g.h; r++) for (let c = 0; c < g.w; c++) {
    const k = g.at(c, r);
    if (k.o === 'crate') push(X(c), k.f, Z(r), X(c + 1), k.f + k.size, Z(r + 1), k.om, 'crate', { stack: k.size / 64 });
    if (k.o === 'barrel') push(X(c) + 10, k.f, Z(r) + 10, X(c + 1) - 10, k.f + 46, Z(r + 1) - 10, 'barrel', 'barrel');
  }
  // skybox clip: invisible ceiling high above
  push(ox, 2000, oz, ox + g.w * CS, 2064, oz + g.h * CS, null, 'clip');

  // nav graph: one node per walkable cell
  const nodes = [];
  const nodeAt = new Int32Array(g.w * g.h).fill(-1);
  for (let r = 0; r < g.h; r++) for (let c = 0; c < g.w; c++) {
    const k = g.at(c, r);
    if (k.o || k.lava) continue;
    if (k.c != null && k.c - k.f < 90) continue;
    nodeAt[r * g.w + c] = nodes.length;
    nodes.push({ id: nodes.length, c, r, x: X(c) + CS / 2, y: k.f, z: Z(r) + CS / 2, links: [] });
  }
  for (const n of nodes) {
    for (let dr = -1; dr <= 1; dr++) for (let dc = -1; dc <= 1; dc++) {
      if (!dr && !dc) continue;
      const id = nodeAt[(n.r + dr) * g.w + (n.c + dc)];
      if (n.r + dr < 0 || n.r + dr >= g.h || n.c + dc < 0 || n.c + dc >= g.w || id < 0) continue;
      const m = nodes[id];
      const dh = m.y - n.y;
      if (dr && dc) {
        const a = nodeAt[(n.r) * g.w + (n.c + dc)], b = nodeAt[(n.r + dr) * g.w + n.c];
        if (a < 0 || b < 0) continue;
        if (Math.abs(nodes[a].y - n.y) > 18 || Math.abs(nodes[b].y - n.y) > 18 || Math.abs(dh) > 18) continue;
      }
      if (dh <= 18) n.links.push({ to: id, jump: false, cost: Math.hypot(dr, dc) + (dh < -18 ? 0.5 : 0) });
      else if (dh <= 50 && !(dr && dc)) n.links.push({ to: id, jump: true, cost: 2.5 });
    }
  }
  const spawns = { T: [], CT: [], start: [] };
  const zones = [];
  for (let r = 0; r < g.h; r++) for (let c = 0; c < g.w; c++) {
    const k = g.at(c, r);
    for (const t of k.tags) {
      if (spawns[t] && !k.o) spawns[t].push({ x: X(c) + CS / 2, y: k.f + 1, z: Z(r) + CS / 2 });
    }
  }
  // zones as bounding rects of tags
  for (const t of ['startzone', 'cp', 'finish']) {
    for (const R of greedy(g, k => k.tags.has(t) ? t : null))
      zones.push({ type: t, min: [X(R.c0), R.cell.f - 10, Z(R.r0)], max: [X(R.c1 + 1), R.cell.f + 200, Z(R.r1 + 1)], spawn: { x: (X(R.c0) + X(R.c1 + 1)) / 2, y: R.cell.f + 1, z: (Z(R.r0) + Z(R.r1 + 1)) / 2 } });
  }
  return { boxes, nodes, nodeAt, spawns, zones, bounds: { x0: ox, z0: oz, x1: -ox, z1: -oz }, X, Z };
}
