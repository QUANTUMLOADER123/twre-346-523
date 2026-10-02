import * as THREE from 'three';

const textureCache = new Map();

function createCanvasTex(key, size, drawFn, repeatX = 1, repeatY = 1) {
  const fullKey = `${key}_${repeatX}_${repeatY}`;
  if (textureCache.has(fullKey)) return textureCache.get(fullKey);

  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext('2d');
  drawFn(ctx, size);

  const tex = new THREE.CanvasTexture(canvas);
  tex.wrapS = THREE.RepeatWrapping;
  tex.wrapT = THREE.RepeatWrapping;
  tex.repeat.set(repeatX, repeatY);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.needsUpdate = true;
  textureCache.set(fullKey, tex);
  return tex;
}

export function getSandstoneWallTex(rx = 4, ry = 2) {
  return createCanvasTex('sandstone_wall', 256, (ctx, s) => {
    ctx.fillStyle = '#d7be98';
    ctx.fillRect(0, 0, s, s);
    for (let i = 0; i < 1600; i++) {
      ctx.fillStyle = i % 2 === 0 ? 'rgba(255,255,255,0.06)' : 'rgba(90,65,40,0.06)';
      ctx.fillRect(Math.random() * s, Math.random() * s, 3, 3);
    }
    ctx.strokeStyle = 'rgba(110, 82, 52, 0.38)';
    ctx.lineWidth = 3;
    const rows = 4;
    const rowH = s / rows;
    for (let r = 0; r < rows; r++) {
      const y = r * rowH;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(s, y);
      ctx.stroke();
      const offset = (r % 2) * (s / 4);
      for (let c = 0; c < 2; c++) {
        const x = (c * (s / 2) + offset) % s;
        ctx.beginPath();
        ctx.moveTo(x, y);
        ctx.lineTo(x, y + rowH);
        ctx.stroke();
      }
    }
  }, rx, ry);
}

export function getSandFloorTex(rx = 16, ry = 16) {
  return createCanvasTex('sand_floor', 256, (ctx, s) => {
    ctx.fillStyle = '#c8af88';
    ctx.fillRect(0, 0, s, s);
    ctx.strokeStyle = 'rgba(85, 65, 42, 0.25)';
    ctx.lineWidth = 2;
    const step = s / 4;
    for (let i = 0; i <= 4; i++) {
      ctx.beginPath();
      ctx.moveTo(i * step, 0);
      ctx.lineTo(i * step, s);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(0, i * step);
      ctx.lineTo(s, i * step);
      ctx.stroke();
    }
    for (let i = 0; i < 2000; i++) {
      ctx.fillStyle = i % 2 === 0 ? 'rgba(255,245,220,0.07)' : 'rgba(75,55,35,0.07)';
      ctx.fillRect(Math.random() * s, Math.random() * s, 2, 2);
    }
  }, rx, ry);
}

export function getWoodCrateTex(rx = 1, ry = 1) {
  return createCanvasTex('wood_crate', 256, (ctx, s) => {
    ctx.fillStyle = '#8d6236';
    ctx.fillRect(0, 0, s, s);
    ctx.strokeStyle = '#533518';
    ctx.lineWidth = 3;
    for (let i = 1; i < 6; i++) {
      const y = (s / 6) * i;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(s, y);
      ctx.stroke();
    }
    ctx.strokeStyle = '#6a4622';
    ctx.lineWidth = 22;
    ctx.strokeRect(11, 11, s - 22, s - 22);
    ctx.beginPath();
    ctx.moveTo(16, 16);
    ctx.lineTo(s - 16, s - 16);
    ctx.stroke();
    ctx.fillStyle = 'rgba(30, 20, 10, 0.55)';
    ctx.font = 'bold 20px monospace';
    ctx.fillText('WB-CRATE', 72, s / 2 + 6);
  }, rx, ry);
}

export function getWoodDoorTex(rx = 1, ry = 1) {
  return createCanvasTex('wood_door', 256, (ctx, s) => {
    ctx.fillStyle = '#754d29';
    ctx.fillRect(0, 0, s, s);
    ctx.strokeStyle = '#3d2510';
    ctx.lineWidth = 4;
    for (let x = 32; x < s; x += 32) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, s);
      ctx.stroke();
    }
    ctx.fillStyle = '#383d42';
    ctx.fillRect(0, 42, s, 18);
    ctx.fillRect(0, s - 60, s, 18);
  }, rx, ry);
}

export function getDevOrangeTex(rx = 2, ry = 2) {
  return createCanvasTex('dev_orange', 256, (ctx, s) => {
    ctx.fillStyle = '#e06d1b';
    ctx.fillRect(0, 0, s, s);
    ctx.strokeStyle = 'rgba(255,255,255,0.42)';
    ctx.lineWidth = 3;
    ctx.strokeRect(2, 2, s - 4, s - 4);
    ctx.strokeStyle = 'rgba(255,255,255,0.18)';
    ctx.lineWidth = 1.5;
    const step = s / 4;
    for (let i = 1; i < 4; i++) {
      ctx.beginPath();
      ctx.moveTo(i * step, 0);
      ctx.lineTo(i * step, s);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(0, i * step);
      ctx.lineTo(s, i * step);
      ctx.stroke();
    }
    ctx.fillStyle = 'rgba(255,255,255,0.5)';
    ctx.font = 'bold 18px monospace';
    ctx.fillText('128 x 128', 14, 28);
  }, rx, ry);
}

export function getDevGreyTex(rx = 8, ry = 8) {
  return createCanvasTex('dev_grey', 256, (ctx, s) => {
    ctx.fillStyle = '#343a42';
    ctx.fillRect(0, 0, s, s);
    ctx.strokeStyle = 'rgba(255,255,255,0.22)';
    ctx.lineWidth = 2;
    ctx.strokeRect(1, 1, s - 2, s - 2);
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    const step = s / 4;
    for (let i = 1; i < 4; i++) {
      ctx.beginPath();
      ctx.moveTo(i * step, 0);
      ctx.lineTo(i * step, s);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(0, i * step);
      ctx.lineTo(s, i * step);
      ctx.stroke();
    }
  }, rx, ry);
}

export function getCyberFloorTex(rx = 14, ry = 14) {
  return createCanvasTex('cyber_floor', 256, (ctx, s) => {
    ctx.fillStyle = '#121624';
    ctx.fillRect(0, 0, s, s);
    ctx.strokeStyle = 'rgba(0, 245, 212, 0.24)';
    ctx.lineWidth = 2;
    ctx.strokeRect(1, 1, s - 2, s - 2);
    ctx.strokeStyle = 'rgba(168, 85, 247, 0.12)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(s / 2, 0);
    ctx.lineTo(s / 2, s);
    ctx.moveTo(0, s / 2);
    ctx.lineTo(s, s / 2);
    ctx.stroke();
  }, rx, ry);
}

export function getCyberWallTex(rx = 4, ry = 2) {
  return createCanvasTex('cyber_wall', 256, (ctx, s) => {
    ctx.fillStyle = '#1b2034';
    ctx.fillRect(0, 0, s, s);
    ctx.strokeStyle = '#00f5d4';
    ctx.lineWidth = 3;
    ctx.strokeRect(2, 2, s - 4, s - 4);
    ctx.fillStyle = 'rgba(168, 85, 247, 0.25)';
    ctx.fillRect(0, s - 16, s, 8);
  }, rx, ry);
}

export function getInfernoBrickTex(rx = 5, ry = 2) {
  return createCanvasTex('inferno_brick', 256, (ctx, s) => {
    ctx.fillStyle = '#c88d68';
    ctx.fillRect(0, 0, s, s);
    // Warm Italian plaster + red brick patches
    ctx.fillStyle = 'rgba(165, 65, 42, 0.28)';
    ctx.fillRect(20, 40, 90, 60);
    ctx.fillRect(140, 130, 85, 70);
    ctx.strokeStyle = 'rgba(90, 45, 28, 0.35)';
    ctx.lineWidth = 2;
    for (let y = 0; y < s; y += 32) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(s, y);
      ctx.stroke();
    }
  }, rx, ry);
}

export function getCobbleFloorTex(rx = 16, ry = 16) {
  return createCanvasTex('cobble_floor', 256, (ctx, s) => {
    ctx.fillStyle = '#7e7872';
    ctx.fillRect(0, 0, s, s);
    ctx.strokeStyle = '#524d49';
    ctx.lineWidth = 2.5;
    const step = 32;
    for (let y = 0; y < s; y += step) {
      const off = (y / step) % 2 === 0 ? 0 : 16;
      for (let x = 0; x < s; x += step) {
        ctx.strokeRect((x + off) % s, y, step - 2, step - 2);
      }
    }
  }, rx, ry);
}

export function getNukeWallTex(rx = 4, ry = 2) {
  return createCanvasTex('nuke_wall', 256, (ctx, s) => {
    ctx.fillStyle = '#8fa1b3';
    ctx.fillRect(0, 0, s, s);
    // Corrugated industrial siding lines
    ctx.strokeStyle = 'rgba(30, 45, 62, 0.32)';
    ctx.lineWidth = 3;
    for (let x = 0; x < s; x += 16) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, s);
      ctx.stroke();
    }
    // Yellow/Black hazard band at bottom
    ctx.fillStyle = '#eab308';
    ctx.fillRect(0, s - 28, s, 28);
    ctx.fillStyle = '#111827';
    for (let x = -32; x < s + 32; x += 32) {
      ctx.beginPath();
      ctx.moveTo(x, s - 28);
      ctx.lineTo(x + 16, s - 28);
      ctx.lineTo(x, s);
      ctx.lineTo(x - 16, s);
      ctx.closePath();
      ctx.fill();
    }
  }, rx, ry);
}

export function getAsphaltFloorTex(rx = 18, ry = 18) {
  return createCanvasTex('asphalt_floor', 256, (ctx, s) => {
    ctx.fillStyle = '#47505c';
    ctx.fillRect(0, 0, s, s);
    for (let i = 0; i < 2400; i++) {
      ctx.fillStyle = i % 2 === 0 ? 'rgba(255,255,255,0.05)' : 'rgba(15,20,28,0.08)';
      ctx.fillRect(Math.random() * s, Math.random() * s, 2, 2);
    }
  }, rx, ry);
}
