// Procedural canvas textures (no external assets needed)
import * as THREE from 'three';

function rng(seed) {
  let s = seed >>> 0 || 1;
  return () => {
    s ^= s << 13; s >>>= 0;
    s ^= s >>> 17;
    s ^= s << 5; s >>>= 0;
    return (s >>> 0) / 4294967296;
  };
}

function canvas(size = 256) {
  const c = document.createElement('canvas');
  c.width = c.height = size;
  return c;
}

function noise(ctx, size, amount, r, alpha = 1) {
  const img = ctx.getImageData(0, 0, size, size);
  const d = img.data;
  for (let i = 0; i < d.length; i += 4) {
    const n = (r() - 0.5) * amount;
    d[i] = Math.max(0, Math.min(255, d[i] + n));
    d[i + 1] = Math.max(0, Math.min(255, d[i + 1] + n));
    d[i + 2] = Math.max(0, Math.min(255, d[i + 2] + n));
    d[i + 3] = d[i + 3] * alpha;
  }
  ctx.putImageData(img, 0, 0);
}

function blotches(ctx, size, r, color, count, minR, maxR, alpha) {
  for (let i = 0; i < count; i++) {
    const x = r() * size, y = r() * size, rad = minR + r() * (maxR - minR);
    const g = ctx.createRadialGradient(x, y, 0, x, y, rad);
    g.addColorStop(0, color.replace('A', alpha));
    g.addColorStop(1, color.replace('A', 0));
    ctx.fillStyle = g;
    // wrap
    for (const ox of [-size, 0, size]) for (const oy of [-size, 0, size]) {
      ctx.save(); ctx.translate(ox, oy);
      ctx.fillRect(x - rad, y - rad, rad * 2, rad * 2);
      ctx.restore();
    }
  }
}

function tex(c, srgb = true) {
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.anisotropy = 8;
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.needsUpdate = true;
  return t;
}

const painters = {
  sandstone(ctx, s, r) {
    ctx.fillStyle = '#c9a66b'; ctx.fillRect(0, 0, s, s);
    blotches(ctx, s, r, 'rgba(120,85,40,A)', 20, 10, 60, 0.18);
    const rows = 6, bh = s / rows;
    for (let y = 0; y < rows; y++) {
      const off = (y % 2) * (s / 4);
      const bw = s / 2;
      for (let x = -1; x < 3; x++) {
        const bx = x * bw + off;
        const shade = 180 + Math.floor(r() * 40);
        ctx.fillStyle = `rgba(${shade + 20},${shade - 10},${shade - 70},0.35)`;
        ctx.fillRect(bx + 2, y * bh + 2, bw - 4, bh - 4);
      }
      ctx.fillStyle = 'rgba(90,65,35,0.7)';
      ctx.fillRect(0, y * bh, s, 2);
      for (let x = -1; x < 3; x++) ctx.fillRect(x * bw + off, y * bh, 2, bh);
    }
    noise(ctx, s, 34, r);
  },
  plaster(ctx, s, r) {
    ctx.fillStyle = '#d8c7a3'; ctx.fillRect(0, 0, s, s);
    blotches(ctx, s, r, 'rgba(150,120,80,A)', 25, 8, 70, 0.22);
    blotches(ctx, s, r, 'rgba(255,245,220,A)', 15, 8, 50, 0.25);
    // cracks
    ctx.strokeStyle = 'rgba(90,70,50,0.35)'; ctx.lineWidth = 1;
    for (let i = 0; i < 6; i++) {
      ctx.beginPath(); let x = r() * s, y = r() * s; ctx.moveTo(x, y);
      for (let k = 0; k < 6; k++) { x += (r() - 0.5) * 30; y += r() * 20; ctx.lineTo(x, y); }
      ctx.stroke();
    }
    noise(ctx, s, 22, r);
  },
  sand(ctx, s, r) {
    ctx.fillStyle = '#c2a273'; ctx.fillRect(0, 0, s, s);
    blotches(ctx, s, r, 'rgba(160,125,80,A)', 40, 10, 50, 0.3);
    blotches(ctx, s, r, 'rgba(225,200,150,A)', 30, 10, 40, 0.3);
    for (let i = 0; i < 400; i++) {
      ctx.fillStyle = `rgba(${90 + r() * 60},${70 + r() * 40},40,${0.3 + r() * 0.4})`;
      ctx.fillRect(r() * s, r() * s, 1 + r() * 2, 1 + r() * 2);
    }
    noise(ctx, s, 30, r);
  },
  tiles(ctx, s, r) {
    ctx.fillStyle = '#a89878'; ctx.fillRect(0, 0, s, s);
    const n = 4, ts = s / n;
    for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
      const v = 150 + r() * 50;
      ctx.fillStyle = `rgb(${v + 10},${v},${v - 25})`;
      ctx.fillRect(x * ts + 2, y * ts + 2, ts - 4, ts - 4);
    }
    blotches(ctx, s, r, 'rgba(80,60,40,A)', 20, 10, 40, 0.15);
    noise(ctx, s, 26, r);
  },
  concrete(ctx, s, r) {
    ctx.fillStyle = '#8d8d88'; ctx.fillRect(0, 0, s, s);
    blotches(ctx, s, r, 'rgba(60,60,60,A)', 30, 10, 60, 0.15);
    blotches(ctx, s, r, 'rgba(200,200,190,A)', 20, 10, 50, 0.12);
    ctx.fillStyle = 'rgba(40,40,40,0.5)';
    ctx.fillRect(0, 0, s, 2); ctx.fillRect(0, 0, 2, s);
    ctx.fillStyle = 'rgba(30,30,30,0.6)';
    for (const [x, y] of [[20, 20], [s - 20, 20], [20, s - 20], [s - 20, s - 20]]) { ctx.beginPath(); ctx.arc(x, y, 3, 0, 7); ctx.fill(); }
    noise(ctx, s, 28, r);
  },
  asphalt(ctx, s, r) {
    ctx.fillStyle = '#4a4a4c'; ctx.fillRect(0, 0, s, s);
    for (let i = 0; i < 1500; i++) {
      const v = 40 + r() * 70;
      ctx.fillStyle = `rgba(${v},${v},${v},0.6)`;
      ctx.fillRect(r() * s, r() * s, 1 + r() * 2, 1 + r() * 2);
    }
    blotches(ctx, s, r, 'rgba(20,20,20,A)', 10, 10, 50, 0.25);
    noise(ctx, s, 20, r);
  },
  metalfloor(ctx, s, r) {
    ctx.fillStyle = '#5b6066'; ctx.fillRect(0, 0, s, s);
    // diamond plate pattern
    for (let y = 0; y < s; y += 16) for (let x = 0; x < s; x += 16) {
      const ox = ((y / 16) % 2) * 8;
      ctx.save(); ctx.translate(x + ox + 4, y + 8); ctx.rotate(((x + y) / 16) % 2 ? 0.7 : -0.7);
      ctx.fillStyle = 'rgba(200,205,210,0.35)'; ctx.fillRect(-5, -1.5, 10, 3);
      ctx.fillStyle = 'rgba(20,20,20,0.35)'; ctx.fillRect(-5, 1.5, 10, 1);
      ctx.restore();
    }
    ctx.fillStyle = 'rgba(20,20,20,0.7)'; ctx.fillRect(0, 0, s, 3); ctx.fillRect(0, 0, 3, s);
    blotches(ctx, s, r, 'rgba(90,60,30,A)', 12, 8, 40, 0.25);
    noise(ctx, s, 24, r);
  },
  crate(ctx, s, r) {
    ctx.fillStyle = '#8a6236'; ctx.fillRect(0, 0, s, s);
    const planks = 5, ph = s / planks;
    for (let i = 0; i < planks; i++) {
      const v = r() * 30 - 15;
      ctx.fillStyle = `rgb(${150 + v},${108 + v},${62 + v})`;
      ctx.fillRect(0, i * ph + 1, s, ph - 2);
      ctx.strokeStyle = 'rgba(80,50,20,0.35)';
      for (let k = 0; k < 6; k++) { ctx.beginPath(); const yy = i * ph + r() * ph; ctx.moveTo(0, yy); ctx.bezierCurveTo(s * 0.3, yy + (r() - 0.5) * 8, s * 0.6, yy + (r() - 0.5) * 8, s, yy); ctx.stroke(); }
    }
    // frame + cross
    ctx.fillStyle = '#6b4622';
    const fw = s * 0.11;
    ctx.fillRect(0, 0, s, fw); ctx.fillRect(0, s - fw, s, fw); ctx.fillRect(0, 0, fw, s); ctx.fillRect(s - fw, 0, fw, s);
    ctx.save(); ctx.translate(s / 2, s / 2); ctx.rotate(Math.PI / 4); ctx.fillRect(-s * 0.68, -fw / 2, s * 1.36, fw); ctx.restore();
    ctx.strokeStyle = 'rgba(40,25,10,0.8)'; ctx.lineWidth = 2;
    ctx.strokeRect(fw, fw, s - fw * 2, s - fw * 2); ctx.strokeRect(1, 1, s - 2, s - 2);
    ctx.fillStyle = '#333';
    for (const [x, y] of [[fw / 2, fw / 2], [s - fw / 2, fw / 2], [fw / 2, s - fw / 2], [s - fw / 2, s - fw / 2]]) { ctx.beginPath(); ctx.arc(x, y, 3, 0, 7); ctx.fill(); }
    noise(ctx, s, 26, r);
  },
  container_red(ctx, s, r) { painters._container(ctx, s, r, [150, 45, 35]); },
  container_blue(ctx, s, r) { painters._container(ctx, s, r, [40, 75, 135]); },
  container_green(ctx, s, r) { painters._container(ctx, s, r, [55, 110, 60]); },
  _container(ctx, s, r, [cr, cg, cb]) {
    ctx.fillStyle = `rgb(${cr},${cg},${cb})`; ctx.fillRect(0, 0, s, s);
    for (let x = 0; x < s; x += 16) {
      const g = ctx.createLinearGradient(x, 0, x + 16, 0);
      g.addColorStop(0, 'rgba(0,0,0,0.35)'); g.addColorStop(0.5, 'rgba(255,255,255,0.15)'); g.addColorStop(1, 'rgba(0,0,0,0.35)');
      ctx.fillStyle = g; ctx.fillRect(x, 0, 16, s);
    }
    blotches(ctx, s, r, 'rgba(110,60,25,A)', 18, 4, 30, 0.45);
    ctx.fillStyle = 'rgba(0,0,0,0.5)'; ctx.fillRect(0, 0, s, 6); ctx.fillRect(0, s - 6, s, 6);
    noise(ctx, s, 20, r);
  },
  barrel(ctx, s, r) {
    ctx.fillStyle = '#2f5d8a'; ctx.fillRect(0, 0, s, s);
    ctx.fillStyle = 'rgba(0,0,0,0.35)';
    for (const y of [0.15, 0.5, 0.85]) ctx.fillRect(0, y * s - 5, s, 10);
    blotches(ctx, s, r, 'rgba(120,70,30,A)', 20, 4, 25, 0.5);
    ctx.fillStyle = 'rgba(240,200,40,0.9)'; ctx.font = 'bold 30px sans-serif'; ctx.fillText('☢', s * 0.42, s * 0.62);
    noise(ctx, s, 22, r);
  },
  brick(ctx, s, r) {
    ctx.fillStyle = '#6e4a3a'; ctx.fillRect(0, 0, s, s);
    const rows = 8, bh = s / rows, bw = s / 4;
    for (let y = 0; y < rows; y++) {
      const off = (y % 2) * bw / 2;
      for (let x = -1; x < 5; x++) {
        const v = r() * 40 - 20;
        ctx.fillStyle = `rgb(${150 + v},${70 + v / 2},${50 + v / 2})`;
        ctx.fillRect(x * bw + off + 2, y * bh + 2, bw - 4, bh - 4);
      }
    }
    noise(ctx, s, 30, r);
  },
  dev_gray(ctx, s, r) { painters._dev(ctx, s, '#8a8a8a', '#7a7a7a', '#5c5c5c'); },
  dev_orange(ctx, s, r) { painters._dev(ctx, s, '#e0802a', '#cc7020', '#8a4410'); },
  dev_floor(ctx, s, r) { painters._dev(ctx, s, '#5d6a74', '#53606a', '#3a444c'); },
  dev_white(ctx, s, r) { painters._dev(ctx, s, '#d8d8d8', '#c8c8c8', '#909090'); },
  _dev(ctx, s, a, b, line) {
    ctx.fillStyle = a; ctx.fillRect(0, 0, s, s);
    ctx.fillStyle = b; ctx.fillRect(0, 0, s / 2, s / 2); ctx.fillRect(s / 2, s / 2, s / 2, s / 2);
    ctx.strokeStyle = line; ctx.lineWidth = 2;
    for (let i = 0; i <= 8; i++) {
      ctx.beginPath(); ctx.moveTo(i * s / 8, 0); ctx.lineTo(i * s / 8, s); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(0, i * s / 8); ctx.lineTo(s, i * s / 8); ctx.stroke();
    }
    ctx.lineWidth = 4; ctx.strokeRect(0, 0, s, s);
    ctx.fillStyle = line; ctx.font = 'bold 18px monospace'; ctx.fillText('128', 6, 22);
  },
  lava(ctx, s, r) {
    ctx.fillStyle = '#a01a00'; ctx.fillRect(0, 0, s, s);
    blotches(ctx, s, r, 'rgba(255,160,20,A)', 40, 10, 50, 0.6);
    blotches(ctx, s, r, 'rgba(255,240,120,A)', 20, 5, 25, 0.7);
    blotches(ctx, s, r, 'rgba(40,0,0,A)', 20, 10, 40, 0.5);
    noise(ctx, s, 30, r);
  },
  wood_dark(ctx, s, r) {
    ctx.fillStyle = '#5a3a1e'; ctx.fillRect(0, 0, s, s);
    ctx.strokeStyle = 'rgba(30,15,5,0.4)';
    for (let i = 0; i < 40; i++) { ctx.beginPath(); const y = r() * s; ctx.moveTo(0, y); ctx.bezierCurveTo(s / 3, y + (r() - .5) * 10, 2 * s / 3, y + (r() - .5) * 10, s, y); ctx.stroke(); }
    noise(ctx, s, 20, r);
  },
};

const cache = new Map();
export function getTexture(name) {
  if (cache.has(name)) return cache.get(name);
  const c = canvas(256);
  const ctx = c.getContext('2d', { willReadFrequently: true });
  const r = rng(name.split('').reduce((a, ch) => a * 31 + ch.charCodeAt(0), 7));
  (painters[name] || painters.dev_gray)(ctx, 256, r);
  const t = tex(c);
  cache.set(name, t);
  return t;
}

// small helper textures
export function makeDecalTexture() {
  const c = canvas(64); const ctx = c.getContext('2d');
  const g = ctx.createRadialGradient(32, 32, 0, 32, 32, 30);
  g.addColorStop(0, 'rgba(10,10,10,1)'); g.addColorStop(0.35, 'rgba(25,22,20,0.95)');
  g.addColorStop(0.6, 'rgba(40,35,30,0.5)'); g.addColorStop(1, 'rgba(0,0,0,0)');
  ctx.fillStyle = g; ctx.fillRect(0, 0, 64, 64);
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; return t;
}
export function makeGlowTexture(color = '255,220,150') {
  const c = canvas(64); const ctx = c.getContext('2d');
  const g = ctx.createRadialGradient(32, 32, 0, 32, 32, 32);
  g.addColorStop(0, `rgba(255,255,255,1)`); g.addColorStop(0.25, `rgba(${color},0.9)`); g.addColorStop(1, `rgba(${color},0)`);
  ctx.fillStyle = g; ctx.fillRect(0, 0, 64, 64);
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; return t;
}
export function makeFlashTexture() {
  const c = canvas(128); const ctx = c.getContext('2d');
  ctx.translate(64, 64);
  for (let i = 0; i < 7; i++) {
    ctx.rotate(Math.PI * 2 / 7 + Math.random() * 0.3);
    const g = ctx.createLinearGradient(0, 0, 60, 0);
    g.addColorStop(0, 'rgba(255,255,220,1)'); g.addColorStop(0.4, 'rgba(255,190,80,0.8)'); g.addColorStop(1, 'rgba(255,120,20,0)');
    ctx.fillStyle = g;
    ctx.beginPath(); ctx.moveTo(0, -7); ctx.lineTo(60, 0); ctx.lineTo(0, 7); ctx.fill();
  }
  const g2 = ctx.createRadialGradient(0, 0, 0, 0, 0, 30);
  g2.addColorStop(0, 'rgba(255,255,240,1)'); g2.addColorStop(1, 'rgba(255,180,60,0)');
  ctx.fillStyle = g2; ctx.beginPath(); ctx.arc(0, 0, 30, 0, 7); ctx.fill();
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; return t;
}
export function makeFadeTexture() {
  const c = canvas(128); const ctx = c.getContext('2d');
  const g = ctx.createLinearGradient(0, 0, 128, 128);
  g.addColorStop(0, '#ffd700'); g.addColorStop(0.35, '#ff3fa4'); g.addColorStop(0.7, '#7a3cff'); g.addColorStop(1, '#2bd2ff');
  ctx.fillStyle = g; ctx.fillRect(0, 0, 128, 128);
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; return t;
}
export function makeCamoTexture(cols) {
  const c = canvas(128); const ctx = c.getContext('2d');
  ctx.fillStyle = cols[0]; ctx.fillRect(0, 0, 128, 128);
  const r = rng(1234);
  for (let i = 0; i < 60; i++) {
    ctx.fillStyle = cols[1 + Math.floor(r() * (cols.length - 1))];
    ctx.beginPath();
    const x = r() * 128, y = r() * 128;
    ctx.ellipse(x, y, 6 + r() * 18, 4 + r() * 10, r() * 3, 0, 7); ctx.fill();
  }
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.wrapS = t.wrapT = THREE.RepeatWrapping; return t;
}
