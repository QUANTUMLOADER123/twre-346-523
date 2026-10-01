// HUD + ESP + radar, all drawn on a 2D canvas overlay
import * as THREE from 'three';
import { WEAPONS, weaponInaccuracy } from './weapons.js';
import { wrap180, D2R, R2D, angleVectors } from './util.js';
import { fmtTime } from './game.js';

const v3 = new THREE.Vector3();

export class HUD {
  constructor(canvas) {
    this.c = canvas; this.ctx = canvas.getContext('2d');
    this.fps = 0; this.fpsAcc = 0; this.fpsN = 0; this.lastSpeed = 0; this.speedColor = '#fff';
    this.spotted = new Map(); this.spotT = 0;
    this.resize();
  }
  resize() {
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    this.dpr = dpr;
    this.c.width = innerWidth * dpr; this.c.height = innerHeight * dpr;
    this.c.style.width = innerWidth + 'px'; this.c.style.height = innerHeight + 'px';
    this.W = innerWidth; this.H = innerHeight;
  }

  project(p, cam) {
    v3.copy(p).applyMatrix4(cam.matrixWorldInverse);
    if (v3.z > -1) return null; // behind camera
    v3.applyMatrix4(cam.projectionMatrix);
    return { x: (v3.x + 1) / 2 * this.W, y: (1 - v3.y) / 2 * this.H };
  }

  draw(game, app, dt) {
    const ctx = this.ctx, W = this.W, H = this.H, cfg = app.cfg, V = cfg.visuals;
    ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);
    this.fpsAcc += dt; this.fpsN++;
    if (this.fpsAcc > 0.5) { this.fps = Math.round(this.fpsN / this.fpsAcc); this.fpsAcc = 0; this.fpsN = 0; }
    if (!game) return;
    const p = game.player, cam = game.camera;
    const cx = W / 2, cy = H / 2;
    const accent = cfg.menu.accent;

    // spotted enemies (for radar)
    this.spotT -= dt;
    if (this.spotT <= 0) {
      this.spotT = 0.15;
      const eye = p.eye(new THREE.Vector3());
      for (const a of game.actors) {
        if (a === p || !a.alive) continue;
        let s = a.team === p.team || game.world.visible(eye, a.eye(new THREE.Vector3()));
        if (!s) for (const t of game.actors) if (t.isBot && t.team === p.team && t.alive && t.ai.target === a) { s = true; break; }
        this.spotted.set(a, s ? 1.5 : (this.spotted.get(a) || 0) - 0.15);
      }
    }

    // damage vignette & kill flash
    if (game.damageFlash > 0) {
      const g = ctx.createRadialGradient(cx, cy, Math.min(W, H) * 0.3, cx, cy, Math.max(W, H) * 0.7);
      g.addColorStop(0, 'rgba(255,0,0,0)'); g.addColorStop(1, `rgba(200,0,0,${0.45 * game.damageFlash})`);
      ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);
      game.damageFlash = Math.max(0, game.damageFlash - dt * 1.5);
    }
    if (game.killFlash > 0 && V.killEffect) {
      ctx.fillStyle = `rgba(120,180,255,${0.18 * game.killFlash})`; ctx.fillRect(0, 0, W, H);
      game.killFlash = Math.max(0, game.killFlash - dt * 2.5);
    }

    // ESP
    if (V.esp) this.drawESP(game, cfg, cam);
    // damage numbers
    game.dmgNumbers = game.dmgNumbers.filter(d => game.time - d.t < 1.2);
    for (const d of game.dmgNumbers) {
      const age = game.time - d.t;
      const s = this.project(v3.copy(d.pos).add({ x: 0, y: age * 30, z: 0 }), cam);
      if (!s) continue;
      ctx.font = `bold ${d.hs ? 18 : 15}px Verdana`; ctx.textAlign = 'center';
      ctx.fillStyle = `rgba(${d.hs ? '255,60,60' : '255,255,255'},${1 - age / 1.2})`;
      ctx.strokeStyle = 'rgba(0,0,0,0.7)'; ctx.lineWidth = 3;
      ctx.strokeText('-' + d.dmg, s.x, s.y); ctx.fillText('-' + d.dmg, s.x, s.y);
    }

    const w = p.weapon;
    const scoped = p.alive && w.sniper && p.ws.scoped > 0;
    // scope overlay
    if (scoped) {
      if (V.noScope) {
        ctx.strokeStyle = 'rgba(0,0,0,0.9)'; ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(0, cy); ctx.lineTo(W, cy); ctx.moveTo(cx, 0); ctx.lineTo(cx, H); ctx.stroke();
      } else {
        const r = H * 0.47;
        ctx.fillStyle = '#000';
        ctx.beginPath(); ctx.rect(0, 0, W, H); ctx.arc(cx, cy, r, 0, Math.PI * 2, true); ctx.fill();
        const g = ctx.createRadialGradient(cx, cy, r * 0.85, cx, cy, r);
        g.addColorStop(0, 'rgba(0,0,0,0)'); g.addColorStop(1, 'rgba(0,0,0,0.8)');
        ctx.fillStyle = g; ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.fill();
        ctx.strokeStyle = '#000'; ctx.lineWidth = 1.5;
        ctx.beginPath(); ctx.moveTo(cx - r, cy); ctx.lineTo(cx + r, cy); ctx.moveTo(cx, cy - r); ctx.lineTo(cx, cy + r); ctx.stroke();
        ctx.lineWidth = 4;
        ctx.beginPath(); ctx.moveTo(cx - r, cy); ctx.lineTo(cx - r * 0.3, cy); ctx.moveTo(cx + r * 0.3, cy); ctx.lineTo(cx + r, cy); ctx.moveTo(cx, cy + r * 0.3); ctx.lineTo(cx, cy + r); ctx.stroke();
      }
    }

    if (p.alive) this.drawCrosshair(game, cfg, cam, scoped);

    // hitmarker
    const hmAge = game.time - game.hitmarker.t;
    if (V.hitmarker && hmAge < 0.35) {
      const a = 1 - hmAge / 0.35;
      ctx.strokeStyle = game.hitmarker.hs ? `rgba(255,60,60,${a})` : `rgba(255,255,255,${a})`;
      ctx.lineWidth = game.hitmarker.kill ? 3 : 2;
      const s = 6, g = 4;
      ctx.beginPath();
      for (const [dx, dy] of [[1, 1], [1, -1], [-1, 1], [-1, -1]]) { ctx.moveTo(cx + dx * g, cy + dy * g); ctx.lineTo(cx + dx * (g + s), cy + dy * (g + s)); }
      ctx.stroke();
    }

    // legit aimbot fov circle
    if (cfg.legit.aimbot && p.alive) {
      const r = Math.tan(cfg.legit.fov * D2R) / Math.tan(cam.fov * D2R / 2) * H / 2;
      ctx.strokeStyle = 'rgba(255,255,255,0.25)'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.stroke();
    }
    // rage target marker
    if (game.rageTarget && cfg.rage.enabled) {
      const s = this.project(game.rageTarget.point, cam);
      if (s) { ctx.strokeStyle = accent; ctx.lineWidth = 1.5; ctx.strokeRect(s.x - 4, s.y - 4, 8, 8); }
    }

    // anti-aim arrows
    if (cfg.aa.enabled && p.alive) {
      const side = game.aaInfo.side || 1;
      const man = game.aaState?.manual || 0;
      const arrow = (x, dir, on) => {
        ctx.fillStyle = on ? accent : 'rgba(255,255,255,0.25)';
        ctx.beginPath(); ctx.moveTo(x + dir * 12, cy); ctx.lineTo(x, cy - 8); ctx.lineTo(x, cy + 8); ctx.fill();
      };
      arrow(cx - 50, -1, man === 1 || (man === 0 && side < 0));
      arrow(cx + 50, 1, man === 2 || (man === 0 && side > 0));
    }

    this.drawRadar(game, cfg);
    this.drawBottom(game, cfg);
    this.drawKillfeed(game, cfg);
    this.drawIndicators(game, cfg);
    this.drawChat(game);
    if (V.speedIndicator || game.mode === 'kz') this.drawSpeed(game, cfg);
    if (V.speedGraph) this.drawSpeedGraph(game);
    if (V.keystrokes) this.drawKeys(app);
    if (game.mode === 'kz') this.drawKZ(game);
    if (V.watermark) this.drawWatermark(game, cfg);
    if (!p.alive && game.mode === 'tdm') this.drawDeath(game);
    if (app.input.down('Tab') && !app.menuOpen) this.drawScoreboard(game);
    // damage direction
    if (game.damageDirT && game.time - game.damageDirT < 1) {
      const a = 1 - (game.time - game.damageDirT);
      const rel = wrap180(game.damageDir - p.yaw) * D2R;
      ctx.save(); ctx.translate(cx, cy); ctx.rotate(-rel);
      ctx.fillStyle = `rgba(255,40,40,${0.7 * a})`;
      ctx.beginPath(); ctx.moveTo(0, -110); ctx.lineTo(-18, -88); ctx.lineTo(18, -88); ctx.fill();
      ctx.restore();
    }
  }

  // ------------------------------------------------------------ crosshair
  drawCrosshair(game, cfg, cam, scoped) {
    const ctx = this.ctx, V = cfg.visuals, p = game.player, w = p.weapon;
    let cx = this.W / 2, cy = this.H / 2;
    const pxPerDeg = (this.H / 2) / Math.tan(cam.fov * D2R / 2);
    if (V.recoilCrosshair && !cfg.misc.norecoil) {
      // show where bullets go (punch*2 minus visual punch)
      const vis = V.noVisRecoil ? 0 : 0.9;
      cx += Math.tan(-p.ws.punchY * (2 - vis) * D2R) * pxPerDeg;
      cy -= Math.tan(p.ws.punchP * (2 - vis) * D2R) * pxPerDeg;
    }
    const spread = cfg.misc.nospread ? 0 : weaponInaccuracy(w, p.ws, p);
    if (V.spreadCircle && !w.melee) {
      const r = Math.tan(spread * D2R) * pxPerDeg;
      ctx.fillStyle = 'rgba(255,255,255,0.06)'; ctx.strokeStyle = 'rgba(255,255,255,0.3)';
      ctx.beginPath(); ctx.arc(cx, cy, Math.max(2, r), 0, Math.PI * 2); ctx.fill(); ctx.stroke();
    }
    if (scoped) return;
    if (w.sniper && !V.sniperCrosshair) return;
    const gap = V.crosshairGap + (V.crosshairDynamic ? Math.min(30, Math.tan(spread * D2R) * pxPerDeg * 0.5) : 0);
    const len = V.crosshairSize;
    ctx.fillStyle = 'rgba(0,0,0,0.8)';
    const bars = [[cx - gap - len, cy - 1, len, 2], [cx + gap, cy - 1, len, 2], [cx - 1, cy - gap - len, 2, len], [cx - 1, cy + gap, 2, len]];
    for (const [x, y, ww, hh] of bars) ctx.fillRect(x - 1, y - 1, ww + 2, hh + 2);
    ctx.fillStyle = V.crosshairColor;
    for (const [x, y, ww, hh] of bars) ctx.fillRect(x, y, ww, hh);
  }

  // ------------------------------------------------------------ ESP
  drawESP(game, cfg, cam) {
    const ctx = this.ctx, V = cfg.visuals, p = game.player;
    const eye = cam.position;
    for (const a of game.actors) {
      if (a === p || !a.alive) continue;
      const team = a.team === p.team;
      if (team && !V.teammates) continue;
      const head = a.model.hitboxes[0].wa;
      const vis = game.world.visible(eye, head) || game.world.visible(eye, v3.set(a.origin.x, a.origin.y + 36, a.origin.z).clone());
      const col = team ? V.teamColor : vis ? V.espVisColor : V.espColor;
      const pos = a.model.root.position;
      const top = this.project(new THREE.Vector3(pos.x, pos.y + (a.ducked ? 58 : 76), pos.z), cam);
      const bot = this.project(new THREE.Vector3(pos.x, pos.y - 2, pos.z), cam);
      if (!top || !bot || top.x < -200 || top.x > this.W + 200) {
        if (V.offscreen && !team) this.offscreenArrow(game, a, col);
        continue;
      }
      const h = bot.y - top.y, wdt = h * 0.45;
      const x = top.x - wdt / 2, y = top.y;
      ctx.lineWidth = 1;
      if (V.box !== 'Off') {
        ctx.strokeStyle = 'rgba(0,0,0,0.75)';
        if (V.box === 'Corner') {
          const L = Math.min(wdt, h) * 0.28;
          const corners = (o) => {
            ctx.beginPath();
            ctx.moveTo(x - o, y + L); ctx.lineTo(x - o, y - o); ctx.lineTo(x + L, y - o);
            ctx.moveTo(x + wdt - L, y - o); ctx.lineTo(x + wdt + o, y - o); ctx.lineTo(x + wdt + o, y + L);
            ctx.moveTo(x - o, y + h - L); ctx.lineTo(x - o, y + h + o); ctx.lineTo(x + L, y + h + o);
            ctx.moveTo(x + wdt - L, y + h + o); ctx.lineTo(x + wdt + o, y + h + o); ctx.lineTo(x + wdt + o, y + h - L);
            ctx.stroke();
          };
          ctx.lineWidth = 3; corners(0); ctx.strokeStyle = col; ctx.lineWidth = 1; corners(0);
        } else {
          ctx.strokeRect(x - 1, y - 1, wdt + 2, h + 2); ctx.strokeRect(x + 1, y + 1, wdt - 2, h - 2);
          ctx.strokeStyle = col; ctx.strokeRect(x, y, wdt, h);
        }
      }
      if (V.health) {
        const hp = Math.max(0, a.health) / 100;
        ctx.fillStyle = 'rgba(0,0,0,0.7)'; ctx.fillRect(x - 6, y - 1, 4, h + 2);
        ctx.fillStyle = `hsl(${hp * 120},90%,50%)`; ctx.fillRect(x - 5, y + h * (1 - hp), 2, h * hp);
        if (V.healthText && a.health < 100) { ctx.font = '9px Verdana'; ctx.fillStyle = '#fff'; ctx.textAlign = 'right'; ctx.fillText(a.health, x - 7, y + h * (1 - hp) + 3); }
      }
      ctx.textAlign = 'center';
      if (V.name) { ctx.font = '11px Verdana'; this.shadowText(a.name, top.x, y - 5, '#fff'); }
      let by = y + h + 11;
      if (V.weapon) { ctx.font = '9px Verdana'; this.shadowText(a.weapon.name.toUpperCase(), top.x, by, '#ddd'); by += 11; }
      if (V.distance) { ctx.font = '9px Verdana'; this.shadowText(Math.round(a.origin.distanceTo(p.origin) / 52.5) + 'm', top.x, by, '#bbb'); }
      if (V.skeleton) {
        ctx.strokeStyle = col; ctx.lineWidth = 1.2; ctx.beginPath();
        for (const [b1, b2] of a.model.skeletonPairs) {
          const s1 = this.project(a.model.bonePos(b1, new THREE.Vector3()), cam), s2 = this.project(a.model.bonePos(b2, new THREE.Vector3()), cam);
          if (s1 && s2) { ctx.moveTo(s1.x, s1.y); ctx.lineTo(s2.x, s2.y); }
        }
        ctx.stroke();
      }
      if (V.headDot) {
        const s = this.project(head, cam);
        if (s) { ctx.fillStyle = col; ctx.beginPath(); ctx.arc(s.x, s.y, Math.max(2, h * 0.04), 0, Math.PI * 2); ctx.fill(); }
      }
      if (V.snaplines) { ctx.strokeStyle = col; ctx.globalAlpha = 0.6; ctx.beginPath(); ctx.moveTo(this.W / 2, this.H); ctx.lineTo(top.x, y + h); ctx.stroke(); ctx.globalAlpha = 1; }
    }
  }
  offscreenArrow(game, a, col) {
    const ctx = this.ctx, p = game.player;
    const ang = Math.atan2(-(a.origin.x - p.origin.x), -(a.origin.z - p.origin.z)) * R2D;
    const rel = wrap180(ang - p.yaw) * D2R;
    const r = Math.min(this.W, this.H) * 0.28;
    const x = this.W / 2 - Math.sin(rel) * r, y = this.H / 2 - Math.cos(rel) * r;
    ctx.save(); ctx.translate(x, y); ctx.rotate(-rel);
    ctx.fillStyle = col; ctx.globalAlpha = 0.5 + 0.4 * Math.sin(performance.now() / 150);
    ctx.beginPath(); ctx.moveTo(0, -12); ctx.lineTo(-9, 6); ctx.lineTo(9, 6); ctx.fill();
    ctx.restore(); ctx.globalAlpha = 1;
  }
  shadowText(t, x, y, col) {
    const ctx = this.ctx;
    ctx.fillStyle = 'rgba(0,0,0,0.85)'; ctx.fillText(t, x + 1, y + 1);
    ctx.fillStyle = col; ctx.fillText(t, x, y);
  }

  // ------------------------------------------------------------ radar
  drawRadar(game, cfg) {
    const ctx = this.ctx, p = game.player, R = game.radar;
    if (!R) return;
    const size = 190, x0 = 14, y0 = 14, cxr = x0 + size / 2, cyr = y0 + size / 2;
    const scale = 0.065; // px per unit
    ctx.save();
    ctx.beginPath(); ctx.arc(cxr, cyr, size / 2, 0, Math.PI * 2);
    ctx.fillStyle = 'rgba(10,12,16,0.65)'; ctx.fill();
    ctx.clip();
    ctx.translate(cxr, cyr); ctx.rotate(p.yaw * D2R);
    const k = scale * R.cs / R.px; // image px -> radar px
    const px = (p.origin.x - R.x0) / R.cs * R.px, pz = (p.origin.z - R.z0) / R.cs * R.px;
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(R.canvas, -px * k, -pz * k, R.canvas.width * k, R.canvas.height * k);
    for (const a of game.actors) {
      if (a === p || !a.alive) continue;
      const team = a.team === p.team;
      const seen = cfg.visuals.radar || team || (this.spotted.get(a) || 0) > 0;
      if (!seen) continue;
      const dx = (a.origin.x - p.origin.x) * scale, dz = (a.origin.z - p.origin.z) * scale;
      ctx.fillStyle = team ? '#4ca3ff' : '#ff4040';
      ctx.beginPath(); ctx.arc(dx, dz, 4, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = '#000'; ctx.lineWidth = 1; ctx.stroke();
    }
    ctx.restore();
    // player arrow
    ctx.fillStyle = '#fff';
    ctx.beginPath(); ctx.moveTo(cxr, cyr - 7); ctx.lineTo(cxr - 5, cyr + 5); ctx.lineTo(cxr + 5, cyr + 5); ctx.fill();
    ctx.strokeStyle = 'rgba(255,255,255,0.25)'; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(cxr, cyr, size / 2, 0, Math.PI * 2); ctx.stroke();
    ctx.font = '10px Verdana'; ctx.textAlign = 'center'; ctx.fillStyle = 'rgba(255,255,255,0.6)';
    ctx.fillText(game.map.name, cxr, y0 + size + 14);
  }

  // ------------------------------------------------------------ bottom hud
  drawBottom(game, cfg) {
    const ctx = this.ctx, p = game.player, W = this.W, H = this.H;
    ctx.textAlign = 'left';
    const panel = (x, y, w, h) => { const g = ctx.createLinearGradient(x, 0, x + w, 0); g.addColorStop(0, 'rgba(0,0,0,0.55)'); g.addColorStop(1, 'rgba(0,0,0,0)'); ctx.fillStyle = g; ctx.fillRect(x, y, w, h); };
    panel(0, H - 58, 340, 58);
    ctx.font = 'bold 30px "Trebuchet MS", Verdana'; ctx.fillStyle = p.health > 25 ? '#e8e8e8' : '#ff4848';
    ctx.fillText('✚', 18, H - 18);
    ctx.fillText(Math.max(0, p.health), 52, H - 18);
    ctx.fillStyle = '#e8e8e8'; ctx.fillText('⛨', 150, H - 18); ctx.fillText(p.armor, 184, H - 18);
    // hp bar
    ctx.fillStyle = 'rgba(255,255,255,0.15)'; ctx.fillRect(52, H - 12, 80, 3);
    ctx.fillStyle = p.health > 25 ? '#e8e8e8' : '#ff4848'; ctx.fillRect(52, H - 12, 80 * Math.max(0, p.health) / 100, 3);
    // ammo
    const w = p.weapon;
    const g = ctx.createLinearGradient(W - 340, 0, W, 0); g.addColorStop(0, 'rgba(0,0,0,0)'); g.addColorStop(1, 'rgba(0,0,0,0.55)');
    ctx.fillStyle = g; ctx.fillRect(W - 340, H - 58, 340, 58);
    ctx.textAlign = 'right';
    if (!w.melee) {
      const am = p.ammo[w.id];
      ctx.font = 'bold 30px "Trebuchet MS", Verdana'; ctx.fillStyle = am.clip === 0 ? '#ff4848' : '#e8e8e8';
      ctx.fillText(am.clip, W - 90, H - 18);
      ctx.font = 'bold 18px "Trebuchet MS", Verdana'; ctx.fillStyle = '#bdbdbd';
      ctx.fillText('/ ' + (cfg.misc.infiniteAmmo ? '∞' : am.reserve), W - 22, H - 19);
      if (p.ws.reloading) { ctx.font = '12px Verdana'; ctx.fillStyle = '#ffd04a'; ctx.fillText('ПЕРЕЗАРЯДКА…', W - 22, H - 64); }
    }
    ctx.font = '12px Verdana'; ctx.fillStyle = '#cfcfcf';
    ctx.fillText(w.name.toUpperCase() + (cfg.skins[w.id] && cfg.skins[w.id] !== 'Default' ? ' | ' + cfg.skins[w.id] : ''), W - 22, H - 48);
    // weapon slots
    ctx.font = '11px Verdana';
    p.inventory.forEach((id, i) => {
      ctx.fillStyle = i === p.slot ? '#fff' : 'rgba(255,255,255,0.35)';
      ctx.fillText(`${i + 1}  ${WEAPONS[id].name}`, W - 22, H - 150 + i * 16);
    });
    // team score
    if (game.mode === 'tdm') {
      ctx.textAlign = 'center'; ctx.font = 'bold 16px Verdana';
      ctx.fillStyle = 'rgba(0,0,0,0.5)'; ctx.fillRect(W / 2 - 70, 8, 140, 28);
      ctx.fillStyle = '#6fb5ff'; ctx.fillText(game.score.CT, W / 2 - 35, 28);
      ctx.fillStyle = '#ddd'; ctx.font = '11px Verdana'; ctx.fillText('TDM', W / 2, 26);
      ctx.fillStyle = '#ffb35c'; ctx.font = 'bold 16px Verdana'; ctx.fillText(game.score.T, W / 2 + 35, 28);
    }
  }

  drawKillfeed(game, cfg) {
    const ctx = this.ctx; let y = cfg.visuals.watermark ? 44 : 14;
    ctx.font = '12px Verdana';
    for (const k of game.killfeed) {
      const wTxt = `  [${k.weapon}]${k.hs ? ' ⌖' : ''}  `;
      const tw = ctx.measureText(k.killer).width + ctx.measureText(wTxt).width + ctx.measureText(k.victim).width + 16;
      const x = this.W - 14 - tw;
      ctx.fillStyle = k.local ? 'rgba(120,20,20,0.6)' : 'rgba(0,0,0,0.5)';
      ctx.fillRect(x, y, tw, 22);
      if (k.local) { ctx.strokeStyle = 'rgba(255,60,60,0.9)'; ctx.strokeRect(x + 0.5, y + 0.5, tw - 1, 21); }
      ctx.textAlign = 'left';
      let cx = x + 8;
      ctx.fillStyle = k.kt === 'CT' ? '#7fbfff' : '#ffbf6f'; ctx.fillText(k.killer, cx, y + 15); cx += ctx.measureText(k.killer).width;
      ctx.fillStyle = '#eee'; ctx.fillText(wTxt, cx, y + 15); cx += ctx.measureText(wTxt).width;
      ctx.fillStyle = k.vt === 'CT' ? '#7fbfff' : '#ffbf6f'; ctx.fillText(k.victim, cx, y + 15);
      y += 26;
    }
  }

  drawIndicators(game, cfg) {
    const ctx = this.ctx; let y = this.H / 2 + 40;
    const x = 18;
    ctx.font = 'bold 20px Verdana'; ctx.textAlign = 'left';
    const ind = (txt, col) => { ctx.fillStyle = 'rgba(0,0,0,0.6)'; ctx.fillText(txt, x + 1, y + 1); ctx.fillStyle = col; ctx.fillText(txt, x, y); y += 26; };
    if (cfg.rage.enabled && cfg.rage.doubletap) ind('DT', game.dt.charged ? '#9ee35d' : '#ff4848');
    if (cfg.fakelag.enabled) ind('FL ' + game.net.choked, '#d7e3ff');
    if (cfg.aa.enabled) ind(game.aaInfo.aa ? 'AA ' + (game.aaInfo.side > 0 ? '▶' : '◀') : 'AA', '#b88cff');
    if (cfg.misc.bhop) ind('BHOP', '#ffd04a');
    if (cfg.misc.autostrafe !== 'Off') ind('STRAFE', '#7fd8ff');
    if (cfg.misc.edgejump && game.app.input.down('ShiftRight')) ind('EJ', '#fff');
    if (cfg.rage.enabled) ind('RAGE', cfg.menu.accent);
    if (cfg.legit.trigger) ind('TRIG', '#ff9a4a');
  }

  drawChat(game) {
    const ctx = this.ctx; let y = this.H - 150;
    ctx.font = '13px Verdana'; ctx.textAlign = 'left';
    for (const c of game.chat.slice(-6)) {
      const a = Math.min(1, 10 - (game.time - c.t));
      ctx.globalAlpha = a;
      ctx.fillStyle = c.team === 'CT' ? '#7fbfff' : c.team === 'KZ' ? '#9ee35d' : '#ffbf6f';
      const name = (c.team === 'KZ' ? '[KZ] ' : '') + c.name + ': ';
      this.shadowText(name, 18, y, ctx.fillStyle);
      ctx.fillStyle = '#fff'; this.shadowText(c.text, 18 + ctx.measureText(name).width, y, '#fff');
      y += 18;
    }
    ctx.globalAlpha = 1;
  }

  drawSpeed(game, cfg) {
    const ctx = this.ctx, p = game.player;
    const s = Math.round(p.speed2d());
    if (s > this.lastSpeed + 0.5) this.speedColor = '#7dff7d'; else if (s < this.lastSpeed - 0.5) this.speedColor = '#ff8a6a'; else if (s < 1) this.speedColor = '#ffffff';
    this.lastSpeed = s;
    ctx.textAlign = 'center'; ctx.font = 'bold 22px Verdana';
    const J = game.jump;
    const txt = `${s}` + (J.active && !p.onGround ? `  (${Math.round(J.pre)})` : '');
    this.shadowText(txt, this.W / 2, this.H - 92, this.speedColor);
    if (cfg.visuals.jumpstats && J.last && game.time - J.last.t < 4) {
      const L = J.last;
      ctx.font = '12px Verdana';
      const good = L.dist > 240 ? '#ff4bd1' : L.dist > 230 ? '#ffd04a' : L.dist > 215 ? '#7dff7d' : '#ffffff';
      this.shadowText(`${L.type}${L.bhops ? ' x' + L.bhops : ''}: ${L.dist.toFixed(2)}u  |  pre ${Math.round(L.pre)}  max ${Math.round(L.max)}  |  ${L.strafes} strafes  sync ${Math.round(L.sync)}%  ${Math.abs(L.dh) > 2 ? '| Δh ' + Math.round(L.dh) : ''}`, this.W / 2, this.H - 72, good);
    }
  }
  drawSpeedGraph(game) {
    const ctx = this.ctx, hist = game.speedHistory;
    const w = 300, h = 60, x0 = this.W / 2 - w / 2, y0 = this.H - 180;
    ctx.fillStyle = 'rgba(0,0,0,0.25)'; ctx.fillRect(x0, y0, w, h);
    ctx.strokeStyle = '#7dff7d'; ctx.lineWidth = 1.5; ctx.beginPath();
    hist.forEach((e, i) => { const x = x0 + i / 300 * w, y = y0 + h - Math.min(1, e.s / 600) * h; if (i) ctx.lineTo(x, y); else ctx.moveTo(x, y); });
    ctx.stroke();
    ctx.fillStyle = 'rgba(255,208,74,0.7)';
    hist.forEach((e, i) => { if (e.g) ctx.fillRect(x0 + i / 300 * w, y0 + h - 3, 1, 3); });
  }
  drawKeys(app) {
    const ctx = this.ctx, inp = app.input;
    const x0 = this.W / 2 - 60, y0 = this.H - 260, s = 30, g = 4;
    const key = (lbl, code, x, y, w = s) => {
      const on = Array.isArray(code) ? code.some(c => inp.down(c)) : inp.down(code);
      ctx.fillStyle = on ? 'rgba(255,255,255,0.85)' : 'rgba(0,0,0,0.4)'; ctx.fillRect(x, y, w, s);
      ctx.fillStyle = on ? '#000' : '#fff'; ctx.font = 'bold 12px Verdana'; ctx.textAlign = 'center'; ctx.fillText(lbl, x + w / 2, y + 19);
    };
    key('W', 'KeyW', x0 + s + g, y0);
    key('A', 'KeyA', x0, y0 + s + g); key('S', 'KeyS', x0 + s + g, y0 + s + g); key('D', 'KeyD', x0 + 2 * (s + g), y0 + s + g);
    key('DUCK', ['ControlLeft', 'KeyC'], x0 - 2, y0 + 2 * (s + g), 50); key('JUMP', 'Space', x0 + 52, y0 + 2 * (s + g), 50);
  }
  drawKZ(game) {
    const ctx = this.ctx, K = game.kz;
    ctx.textAlign = 'center';
    ctx.fillStyle = 'rgba(0,0,0,0.5)'; ctx.fillRect(this.W / 2 - 120, 8, 240, 52);
    ctx.font = 'bold 22px Verdana';
    const t = K.running ? K.time : K.finished ? K.finished.time : 0;
    this.shadowText(fmtTime(t), this.W / 2, 34, K.running ? '#fff' : K.finished ? '#9ee35d' : '#aaa');
    ctx.font = '11px Verdana';
    this.shadowText(`CP ${K.cps}  |  прыжков ${K.jumps}  |  рекорд ${K.best ? fmtTime(K.best) : '—'}`, this.W / 2, 52, '#ccc');
    ctx.font = '11px Verdana'; ctx.fillStyle = 'rgba(255,255,255,0.5)';
    ctx.fillText('T — рестарт  ·  упал в лаву → к чекпоинту', this.W / 2, 76);
  }
  drawWatermark(game, cfg) {
    const ctx = this.ctx;
    const txt = `strafe.zone  |  ${this.fps} fps  |  64 tick  |  ${game.map.name}  |  ${new Date().toLocaleTimeString()}`;
    ctx.font = '11px Verdana';
    const w = ctx.measureText(txt).width + 20;
    const x = this.W - w - 14, y = 10;
    ctx.fillStyle = 'rgba(16,16,20,0.85)'; ctx.fillRect(x, y, w, 24);
    const g = ctx.createLinearGradient(x, 0, x + w, 0);
    g.addColorStop(0, '#3ad6ff'); g.addColorStop(0.5, '#ff3fa4'); g.addColorStop(1, '#ffe14d');
    ctx.fillStyle = g; ctx.fillRect(x, y, w, 2);
    ctx.textAlign = 'left'; ctx.fillStyle = '#eee'; ctx.fillText(txt, x + 10, y + 16);
    ctx.fillStyle = cfg.menu.accent; ctx.fillText('strafe', x + 10, y + 16);
  }
  drawDeath(game) {
    const ctx = this.ctx, p = game.player, k = game.killedBy;
    ctx.textAlign = 'center';
    ctx.fillStyle = 'rgba(0,0,0,0.55)'; ctx.fillRect(this.W / 2 - 200, this.H * 0.68, 400, 70);
    ctx.font = 'bold 18px Verdana'; ctx.fillStyle = '#ff5a5a';
    ctx.fillText(k ? `Убит игроком ${k.name}` : 'Вы погибли', this.W / 2, this.H * 0.68 + 26);
    ctx.font = '13px Verdana'; ctx.fillStyle = '#ddd';
    if (k) ctx.fillText(`${k.weapon}${k.hs ? ' · в голову' : ''} · у него осталось ${Math.max(0, k.health)} HP`, this.W / 2, this.H * 0.68 + 46);
    ctx.fillText(`Респавн через ${Math.max(0, p.respawnAt - game.time).toFixed(1)}с`, this.W / 2, this.H * 0.68 + 63);
  }
  drawScoreboard(game) {
    const ctx = this.ctx, W = this.W, H = this.H;
    const w = Math.min(760, W - 40), x0 = (W - w) / 2, y0 = H * 0.15;
    const teams = ['CT', 'T'];
    ctx.fillStyle = 'rgba(12,14,18,0.88)';
    const rows = game.actors.length + 4;
    ctx.fillRect(x0, y0, w, rows * 24 + 30);
    ctx.textAlign = 'left'; ctx.font = 'bold 14px Verdana'; ctx.fillStyle = '#fff';
    ctx.fillText(`${game.map.name} — Team Deathmatch`, x0 + 14, y0 + 22);
    let y = y0 + 48;
    for (const t of teams) {
      const list = game.actors.filter(a => a.team === t).sort((a, b) => b.kills - a.kills);
      if (!list.length) continue;
      ctx.fillStyle = t === 'CT' ? '#6fb5ff' : '#ffb35c'; ctx.font = 'bold 12px Verdana';
      ctx.fillText(t === 'CT' ? `Спецназ — ${game.score.CT}` : `Террористы — ${game.score.T}`, x0 + 14, y);
      ctx.fillStyle = '#999'; ctx.font = '11px Verdana';
      ['K', 'D', 'HS%', 'HP'].forEach((h, i) => ctx.fillText(h, x0 + w - 260 + i * 60, y));
      y += 20;
      for (const a of list) {
        if (a === game.player) { ctx.fillStyle = 'rgba(255,255,255,0.08)'; ctx.fillRect(x0 + 6, y - 14, w - 12, 20); }
        ctx.fillStyle = a.alive ? '#eee' : '#777'; ctx.font = '12px Verdana';
        ctx.fillText(a.name + (a.alive ? '' : '  ☠'), x0 + 20, y);
        [a.kills, a.deaths, a.kills ? Math.round(a.hs / a.kills * 100) : 0, a.alive ? a.health : 0].forEach((v, i) => ctx.fillText(String(v), x0 + w - 260 + i * 60, y));
        y += 22;
      }
      y += 8;
    }
  }
}
