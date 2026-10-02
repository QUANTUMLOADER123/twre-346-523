import * as THREE from 'three';

export class EspHudRenderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.radarCanvas = document.getElementById('csgo-radar-canvas');
    this.radarCtx = this.radarCanvas ? this.radarCanvas.getContext('2d') : null;
    this.hitmarkerTimer = 0;
    this.hitmarkerHeadshot = false;
    this.speedHistory = new Array(90).fill(0);
  }

  resize(w = window.innerWidth, h = window.innerHeight) {
    this.canvas.width = w;
    this.canvas.height = h;
    if (!this.radarCanvas) {
      this.radarCanvas = document.getElementById('csgo-radar-canvas');
      if (this.radarCanvas) this.radarCtx = this.radarCanvas.getContext('2d');
    }
  }

  triggerHitmarker(headshot = false) {
    this.hitmarkerTimer = 0.26;
    this.hitmarkerHeadshot = headshot;
  }

  projectToScreen(worldPos, camera) {
    const v = worldPos.clone().project(camera);
    const x = (v.x * 0.5 + 0.5) * this.canvas.width;
    const y = (-v.y * 0.5 + 0.5) * this.canvas.height;
    return { x, y, behind: v.z > 1 };
  }

  renderRadar(playerPhysics, viewYaw, bots, colliders = [], smokeClouds = []) {
    if (!this.radarCtx || !this.radarCanvas) return;
    const rctx = this.radarCtx;
    const w = this.radarCanvas.width;
    const h = this.radarCanvas.height;
    const cx = w * 0.5;
    const cy = h * 0.5;
    const radius = w * 0.5 - 2;
    const scale = 0.052;

    rctx.clearRect(0, 0, w, h);
    rctx.save();

    // Circular radar clip
    rctx.beginPath();
    rctx.arc(cx, cy, radius, 0, Math.PI * 2);
    rctx.clip();

    // Dark CS:GO radar background
    const bgGrad = rctx.createRadialGradient(cx, cy, 8, cx, cy, radius);
    bgGrad.addColorStop(0, 'rgba(22, 30, 38, 0.92)');
    bgGrad.addColorStop(1, 'rgba(10, 14, 18, 0.96)');
    rctx.fillStyle = bgGrad;
    rctx.fillRect(0, 0, w, h);

    // Subtle concentric radar rings
    rctx.strokeStyle = 'rgba(255, 255, 255, 0.07)';
    rctx.lineWidth = 1;
    [radius * 0.35, radius * 0.7].forEach((r) => {
      rctx.beginPath();
      rctx.arc(cx, cy, r, 0, Math.PI * 2);
      rctx.stroke();
    });

    const px = playerPhysics.pos.x;
    const pz = playerPhysics.pos.z;
    const rot = -viewYaw;

    const worldToRadar = (wx, wz) => {
      const dx = wx - px;
      const dz = wz - pz;
      const rx = dx * Math.cos(rot) - dz * Math.sin(rot);
      const rz = dx * Math.sin(rot) + dz * Math.cos(rot);
      return {
        x: cx + rx * scale,
        y: cy + rz * scale
      };
    };

    // Draw map architecture blocks on radar
    rctx.fillStyle = 'rgba(155, 175, 195, 0.22)';
    rctx.strokeStyle = 'rgba(205, 220, 238, 0.38)';
    rctx.lineWidth = 1;

    for (const c of colliders) {
      if (c.maxY - c.minY < 18 || c.maxX - c.minX > 2100 || c.maxZ - c.minZ > 2100) continue;
      const corners = [
        worldToRadar(c.minX, c.minZ),
        worldToRadar(c.maxX, c.minZ),
        worldToRadar(c.maxX, c.maxZ),
        worldToRadar(c.minX, c.maxZ)
      ];
      rctx.beginPath();
      rctx.moveTo(corners[0].x, corners[0].y);
      for (let i = 1; i < 4; i++) rctx.lineTo(corners[i].x, corners[i].y);
      rctx.closePath();
      rctx.fill();
      rctx.stroke();
    }

    // Draw active 3D smoke clouds on radar
    for (const sm of smokeClouds) {
      if (!sm.pos) continue;
      const p = worldToRadar(sm.pos.x, sm.pos.z);
      rctx.fillStyle = 'rgba(215, 225, 235, 0.48)';
      rctx.beginPath();
      rctx.arc(p.x, p.y, 145 * scale, 0, Math.PI * 2);
      rctx.fill();
    }

    // Player FOV view cone pointing straight up
    const coneGrad = rctx.createRadialGradient(cx, cy, 2, cx, cy, radius * 0.8);
    coneGrad.addColorStop(0, 'rgba(255, 255, 255, 0.24)');
    coneGrad.addColorStop(1, 'rgba(255, 255, 255, 0.0)');
    rctx.fillStyle = coneGrad;
    rctx.beginPath();
    rctx.moveTo(cx, cy);
    rctx.arc(cx, cy, radius * 0.8, -Math.PI * 0.5 - 0.52, -Math.PI * 0.5 + 0.52);
    rctx.closePath();
    rctx.fill();

    // Draw bots on radar
    for (const bot of bots) {
      const bp = worldToRadar(bot.physics.pos.x, bot.physics.pos.z);
      const dx = bp.x - cx;
      const dy = bp.y - cy;
      const dist = Math.hypot(dx, dy);
      const maxR = radius - 6;
      const rx = dist > maxR ? cx + (dx / dist) * maxR : bp.x;
      const ry = dist > maxR ? cy + (dy / dist) * maxR : bp.y;

      if (!bot.alive) {
        rctx.strokeStyle = 'rgba(255, 70, 70, 0.65)';
        rctx.lineWidth = 1.6;
        rctx.beginPath();
        rctx.moveTo(rx - 3, ry - 3);
        rctx.lineTo(rx + 3, ry + 3);
        rctx.moveTo(rx + 3, ry - 3);
        rctx.lineTo(rx - 3, ry + 3);
        rctx.stroke();
        continue;
      }

      // Red enemy dot with white border (authentic CS:GO radar blip)
      rctx.fillStyle = '#ff2a2a';
      rctx.strokeStyle = '#ffffff';
      rctx.lineWidth = 1.2;
      rctx.beginPath();
      rctx.arc(rx, ry, 4.2, 0, Math.PI * 2);
      rctx.fill();
      rctx.stroke();
    }

    // Local player dot in center
    rctx.fillStyle = '#78ff5a';
    rctx.strokeStyle = '#0a0e12';
    rctx.lineWidth = 1.5;
    rctx.beginPath();
    rctx.arc(cx, cy, 4.2, 0, Math.PI * 2);
    rctx.fill();
    rctx.stroke();

    rctx.restore();
  }

  render({
    dt,
    camera,
    cfg,
    playerPhysics,
    playerAA,
    ragebot,
    bots,
    effects,
    isScoped,
    viewYaw,
    colliders = []
  }) {
    // 1. Update CS:GO Top-Left Circular Radar
    this.renderRadar(
      playerPhysics,
      viewYaw,
      bots,
      colliders,
      effects ? effects.smokeClouds : []
    );

    const ctx = this.ctx;
    ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

    // 2. Render 2D Enemy ESP
    if (cfg.visuals.espEnabled) {
      for (const bot of bots) {
        if (!bot.alive) continue;

        const feetPos = new THREE.Vector3(bot.physics.pos.x, bot.physics.pos.y, bot.physics.pos.z);
        const headPos = new THREE.Vector3(
          bot.physics.pos.x,
          bot.physics.pos.y + bot.physics.getCurrentHeight() + 4,
          bot.physics.pos.z
        );

        const sFeet = this.projectToScreen(feetPos, camera);
        const sHead = this.projectToScreen(headPos, camera);

        if (sFeet.behind || sHead.behind) {
          if (cfg.visuals.espOofArrows) {
            this.drawOutOfFovArrow(ctx, bot.physics.pos, playerPhysics, viewYaw);
          }
          continue;
        }

        const boxH = Math.abs(sFeet.y - sHead.y);
        const boxW = boxH * 0.56;
        const boxX = sHead.x - boxW * 0.5;
        const boxY = Math.min(sHead.y, sFeet.y);

        if (boxH < 6 || boxH > this.canvas.height * 1.5) continue;

        // Bounding Box
        if (cfg.visuals.espBox) {
          ctx.strokeStyle = 'rgba(0, 0, 0, 0.88)';
          ctx.lineWidth = 3;
          ctx.strokeRect(Math.round(boxX), Math.round(boxY), Math.round(boxW), Math.round(boxH));

          ctx.strokeStyle = cfg.visuals.espBoxColor || '#ffffff';
          ctx.lineWidth = 1;
          ctx.strokeRect(Math.round(boxX), Math.round(boxY), Math.round(boxW), Math.round(boxH));
        }

        // Health Bar (Left side, 1px inner bar with black outline like Skeet.cc)
        if (cfg.visuals.espHealth) {
          const maxHp = bot.maxHp || 100;
          const hpFrac = Math.max(0, Math.min(1, bot.hp / maxHp));
          const barH = boxH * hpFrac;
          const barX = Math.round(boxX - 6);
          const barY = Math.round(boxY);

          ctx.fillStyle = 'rgba(0, 0, 0, 0.88)';
          ctx.fillRect(barX - 1, barY - 1, 4, Math.round(boxH) + 2);

          const r = Math.floor((1 - hpFrac) * 255);
          const g = Math.floor(hpFrac * 235);
          ctx.fillStyle = `rgb(${r}, ${g}, 45)`;
          ctx.fillRect(barX, Math.round(barY + (boxH - barH)), 2, Math.round(barH));

          if (bot.hp < maxHp) {
            ctx.font = 'bold 10px Tahoma, sans-serif';
            ctx.fillStyle = '#ffffff';
            ctx.textAlign = 'center';
            ctx.strokeStyle = '#000';
            ctx.lineWidth = 2;
            const hpY = barY + (boxH - barH) + 4;
            ctx.strokeText(`${bot.hp}`, barX, hpY);
            ctx.fillText(`${bot.hp}`, barX, hpY);
          }
        }

        // Name ESP
        if (cfg.visuals.espName) {
          ctx.font = '11px Tahoma, Verdana, sans-serif';
          ctx.textAlign = 'center';
          ctx.strokeStyle = 'rgba(0, 0, 0, 0.9)';
          ctx.lineWidth = 2.5;
          ctx.strokeText(bot.name, boxX + boxW * 0.5, boxY - 4);
          ctx.fillStyle = '#ffffff';
          ctx.fillText(bot.name, boxX + boxW * 0.5, boxY - 4);
        }

        // Weapon ESP
        if (cfg.visuals.espWeapon) {
          ctx.font = 'bold 9px Tahoma, sans-serif';
          ctx.textAlign = 'center';
          const wText = bot.weapon.shortName.toUpperCase();
          ctx.strokeStyle = 'rgba(0, 0, 0, 0.9)';
          ctx.lineWidth = 2.5;
          ctx.strokeText(wText, boxX + boxW * 0.5, boxY + boxH + 11);
          ctx.fillStyle = '#e0e0e0';
          ctx.fillText(wText, boxX + boxW * 0.5, boxY + boxH + 11);
        }

        // Flags ESP (HK, FAKE, LC, PRIO)
        if (cfg.visuals.espFlags) {
          const flags = [{ text: 'HK', color: '#ffffff' }];
          if (bot.desyncDelta && Math.abs(bot.desyncDelta) > 10) {
            flags.push({ text: 'FAKE', color: '#95b806' });
          }
          if (bot.chokedTicks > 1) {
            flags.push({ text: `LC [${bot.chokedTicks}]`, color: '#38bdf8' });
          }
          if (bot.highPriority) {
            flags.push({ text: 'PRIO', color: '#ff4242' });
          }

          ctx.font = 'bold 9px Tahoma, sans-serif';
          ctx.textAlign = 'left';
          flags.forEach((f, idx) => {
            const fy = boxY + 8 + idx * 10;
            const fx = boxX + boxW + 4;
            ctx.strokeStyle = 'rgba(0,0,0,0.9)';
            ctx.lineWidth = 2;
            ctx.strokeText(f.text, fx, fy);
            ctx.fillStyle = f.color;
            ctx.fillText(f.text, fx, fy);
          });
        }

        // Skeleton ESP
        if (cfg.visuals.espSkeleton && bot.rig && bot.rig.getHitboxesWorld) {
          const pts = bot.rig.getHitboxesWorld();
          const pHead = this.projectToScreen(pts.head, camera);
          const pChest = this.projectToScreen(pts.chest, camera);
          const pStom = this.projectToScreen(pts.stomach, camera);
          const pPelv = this.projectToScreen(pts.pelvis, camera);
          const pFeet = this.projectToScreen(pts.feet, camera);

          ctx.strokeStyle = 'rgba(255, 255, 255, 0.85)';
          ctx.lineWidth = 1.2;
          ctx.beginPath();
          ctx.moveTo(pHead.x, pHead.y);
          ctx.lineTo(pChest.x, pChest.y);
          ctx.lineTo(pStom.x, pStom.y);
          ctx.lineTo(pPelv.x, pPelv.y);
          ctx.lineTo(pFeet.x, pFeet.y);
          ctx.stroke();
        }
      }
    }

    // 3. Floating World Hit Damage Numbers
    if (cfg.visuals.damageNumbers && effects && effects.worldHits) {
      for (const h of effects.worldHits) {
        const scr = this.projectToScreen(h.pos, camera);
        if (scr.behind) continue;
        const alpha = Math.max(0, 1 - h.age / h.maxAge);
        ctx.font = h.isHs ? 'bold 15px Tahoma, sans-serif' : 'bold 12px Tahoma, sans-serif';
        ctx.textAlign = 'center';
        ctx.strokeStyle = `rgba(0,0,0,${alpha * 0.9})`;
        ctx.lineWidth = 3;
        ctx.fillStyle = h.isHs
          ? `rgba(149, 184, 6, ${alpha})`
          : `rgba(255, 255, 255, ${alpha})`;
        const txt = `-${h.damage}`;
        ctx.strokeText(txt, scr.x, scr.y);
        ctx.fillText(txt, scr.x, scr.y);
      }
    }

    // 4. Sniper Scope Overlay or Classic CS:GO Crosshair + Hitmarker
    const scopeEl = document.getElementById('scope-overlay');
    if (scopeEl) {
      if (isScoped && !cfg.visuals.removeScopeOverlay) {
        scopeEl.classList.remove('hidden');
      } else {
        scopeEl.classList.add('hidden');
      }
    }
    this.drawCrosshairAndHitmarker(ctx, dt, cfg);

    // 5. Bottom-Left Skeet Indicators (DT, ONSHOT, DMG, BAIM, PEEK, FAKE, LC)
    this.drawSkeetIndicators(ctx, cfg, playerPhysics, playerAA, ragebot);
  }

  drawOutOfFovArrow(ctx, targetPos, playerPhysics, viewYaw) {
    const cx = this.canvas.width * 0.5;
    const cy = this.canvas.height * 0.5;
    const dx = targetPos.x - playerPhysics.pos.x;
    const dz = targetPos.z - playerPhysics.pos.z;
    const worldAngle = Math.atan2(-dx, -dz);
    const relAngle = worldAngle - viewYaw;

    const radius = 150;
    const ax = cx - Math.sin(relAngle) * radius;
    const ay = cy - Math.cos(relAngle) * radius;

    ctx.save();
    ctx.translate(ax, ay);
    ctx.rotate(-relAngle);
    ctx.beginPath();
    ctx.moveTo(0, -9);
    ctx.lineTo(-7, 7);
    ctx.lineTo(7, 7);
    ctx.closePath();
    ctx.fillStyle = 'rgba(255, 65, 65, 0.75)';
    ctx.fill();
    ctx.strokeStyle = 'rgba(0,0,0,0.85)';
    ctx.lineWidth = 1.2;
    ctx.stroke();
    ctx.restore();
  }

  drawCrosshairAndHitmarker(ctx, dt, cfg) {
    const cx = this.canvas.width * 0.5;
    const cy = this.canvas.height * 0.5;

    ctx.strokeStyle = 'rgba(0, 0, 0, 0.88)';
    ctx.lineWidth = 3;
    const gap = 3;
    const len = 6;

    ctx.beginPath();
    ctx.moveTo(cx - gap - len, cy);
    ctx.lineTo(cx - gap, cy);
    ctx.moveTo(cx + gap, cy);
    ctx.lineTo(cx + gap + len, cy);
    ctx.moveTo(cx, cy - gap - len);
    ctx.lineTo(cx, cy - gap);
    ctx.moveTo(cx, cy + gap);
    ctx.lineTo(cx, cy + gap + len);
    ctx.stroke();

    ctx.strokeStyle = '#68ff38';
    ctx.lineWidth = 1.4;
    ctx.stroke();

    if (this.hitmarkerTimer > 0) {
      this.hitmarkerTimer -= dt;
      if (cfg.visuals.hitmarkers) {
        const alpha = Math.min(1, this.hitmarkerTimer / 0.25);
        ctx.strokeStyle = this.hitmarkerHeadshot
          ? `rgba(149, 184, 6, ${alpha})`
          : `rgba(255, 255, 255, ${alpha})`;
        ctx.lineWidth = 1.8;
        const s = 4;
        const e = 10;
        ctx.beginPath();
        ctx.moveTo(cx - e, cy - e);
        ctx.lineTo(cx - s, cy - s);
        ctx.moveTo(cx + e, cy - e);
        ctx.lineTo(cx + s, cy - s);
        ctx.moveTo(cx - e, cy + e);
        ctx.lineTo(cx - s, cy + s);
        ctx.moveTo(cx + e, cy + e);
        ctx.lineTo(cx + s, cy + s);
        ctx.stroke();
      }
    }
  }

  drawSkeetIndicators(ctx, cfg, playerPhysics, playerAA, ragebot) {
    const items = [];

    if (cfg.rage.enabled && cfg.rage.doubleTap) {
      items.push({
        text: 'DT',
        color: ragebot && ragebot.dtCharge >= 0.99 ? '#95b806' : '#ff4242'
      });
    }
    if (cfg.rage.enabled && cfg.rage.hideShots) {
      items.push({ text: 'ONSHOT', color: '#95b806' });
    }
    if (cfg.rage.minDamageOverrideActive) {
      items.push({
        text: `DMG: ${cfg.rage.minDamageOverride}`,
        color: '#ffffff'
      });
    }
    if (cfg.rage.forceBaimActive) {
      items.push({ text: 'BAIM', color: '#ff4242' });
    }
    if (cfg.rage.autoPeek) {
      items.push({ text: 'PEEK', color: '#95b806' });
    }
    if (cfg.antiaim.enabled && cfg.antiaim.bodyYaw) {
      items.push({ text: 'FAKE', color: '#95b806' });
    }
    if (playerPhysics.getSpeed2D() > 255) {
      items.push({ text: 'LC', color: '#95b806' });
    }

    let y = this.canvas.height - 135;
    ctx.font = 'bold 22px Verdana, Tahoma, sans-serif';
    ctx.textAlign = 'left';

    for (const item of items) {
      ctx.fillStyle = 'rgba(0, 0, 0, 0.78)';
      ctx.fillText(item.text, 19, y + 2);
      ctx.fillStyle = item.color;
      ctx.fillText(item.text, 17, y);
      y -= 28;
    }
  }
}
