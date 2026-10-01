// Core game: world, actors, fixed-tick simulation, shooting, damage, modes, rendering glue.
import * as THREE from 'three';
import { World } from './world.js';
import { getMap, compileMap } from './maps.js';
import { buildMapScene, buildRadarImage } from './mapmesh.js';
import { playerMove, MV, EYE_STAND, EYE_DUCK } from './movement.js';
import { Actor } from './entity.js';
import { Bot, botThink, botHearShot, botOnShotMissed } from './bot.js';
import { WEAPONS, HITGROUP_MULT, weaponInaccuracy } from './weapons.js';
import { createCharacter, applySkin } from './models.js';
import { Effects } from './effects.js';
import * as cheats from './cheats.js';
import { angleVectors, anglesTo, wrap180, clamp, D2R, R2D, rayCapsule, lerp } from './util.js';

export const TICK = 1 / 64;
const KILLSAYS = ['1', 'nice resolver bro', 'owned by strafe.zone', 'hs', 'sit', 'uff ya', 'buy better bot', 'tap'];

export class Game {
  constructor(app) {
    this.app = app; this.cfg = app.cfg; this.audio = app.audio; this.input = app.input;
    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(74, innerWidth / innerHeight, 4, 30000);
    this.camera.rotation.order = 'YXZ';
    this.effects = new Effects(this.scene);
    this.time = 0; this.tickCount = 0;
    this.actors = []; this.player = null;
    this.killfeed = []; this.chat = [];
    this.hitmarker = { t: -10, hs: false, kill: false };
    this.dmgNumbers = []; this.damageFlash = 0; this.killFlash = 0;
    this.speedHistory = [];
    this.jump = { active: false, start: null, pre: 0, max: 0, strafes: 0, lastSide: 0, sync: 0, airTicks: 0, last: null, bhops: 0, groundTicks: 0, takeoffSpeed: 0 };
    this.kz = { running: false, start: 0, time: 0, best: null, cp: null, finished: null, cps: 0, jumps: 0 };
    this.score = { CT: 0, T: 0 };
    this.net = { origin: new THREE.Vector3(), velocity: new THREE.Vector3(), choked: 0, chokeTime: 0, fake: 0, real: 0, pitch: 0, sent: true };
    this.aaInfo = { real: 0, fake: 0, pitch: 0, aa: false };
    this.killedBy = null;
    this.matCache = new Map();
    this.mouseDXTick = 0;
    this.dt = { charged: true, chargeAt: 0 };
  }

  // --------------------------------------------------------------- loading
  load(mapId, opts) {
    const cfg = this.cfg;
    const map = getMap(mapId);
    this.map = map;
    const comp = compileMap(map);
    this.comp = comp;
    this.world = new World();
    for (const b of comp.boxes) this.world.addBox(b.min, b.max, { kind: b.kind, mat: b.mat });
    this.nav = { nodes: comp.nodes, nodeAt: comp.nodeAt, grid: map.grid, bounds: comp.bounds };
    this.mapScene = buildMapScene(comp, map, this.scene);
    this.radar = buildRadarImage(map, comp);
    this.mode = map.mode;
    this.center = new THREE.Vector3((comp.bounds.x0 + comp.bounds.x1) / 2, 0, (comp.bounds.z0 + comp.bounds.z1) / 2);

    const p = new Actor({ name: opts.name || 'You', team: 'CT', isBot: false, primary: cfg.game.primary });
    this.player = p;
    this.actors.push(p);
    this.scene.add(p.model.root);
    // networked "ghost" model: what bots see (fake lag / desync)
    this.netModel = createCharacter('CT');
    this.ghostMat = new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.35, depthWrite: false });
    this.netModel.setGun(p.weaponId);
    this.netModel.setGhost(this.ghostMat);
    this.netModel.root.visible = false;
    this.scene.add(this.netModel.root);

    if (this.mode === 'tdm') {
      for (let i = 0; i < opts.teammates; i++) this.addBot('CT', opts.difficulty, i === 1 ? 'awp' : 'm4a4');
      for (let i = 0; i < opts.bots; i++) this.addBot('T', opts.difficulty, i === 2 ? 'awp' : (i === 4 ? 'ssg08' : 'ak47'));
    } else {
      p.inventory = ['ssg08', 'deagle', 'knife']; p.slot = 2; p.model.setGun('knife'); p.pose.pistol = true;
      this.kz.best = Number(localStorage.getItem('kz_best_' + map.id)) || null;
    }
    for (const a of this.actors) this.spawn(a);
    this.applySkins();
  }

  addBot(team, diff, primary) {
    const b = new Bot(team, diff, primary);
    this.actors.push(b);
    this.scene.add(b.model.root);
    return b;
  }

  dispose() {
    this.scene.traverse(o => { if (o.geometry) o.geometry.dispose(); });
  }

  applySkins() {
    const s = this.cfg.skins;
    this.app.viewmodel?.setWeapon(this.player.weaponId, true);
    for (const id of Object.keys(WEAPONS)) this.app.viewmodel?.applySkin(id, s[id], s.color);
    applySkin(this.player.model.gun, s[this.player.weaponId], s.color);
  }

  spawnPoint(team) {
    if (this.mode === 'kz') {
      const s = this.comp.spawns.start;
      return s[Math.floor(Math.random() * s.length)] || { x: 0, y: 1, z: 0 };
    }
    const list = this.comp.spawns[team];
    let best = list[0], bd = -1;
    for (let i = 0; i < 10; i++) {
      const c = list[Math.floor(Math.random() * list.length)];
      let md = 1e9;
      for (const a of this.actors) {
        if (!a.alive) continue;
        const d = Math.hypot(a.origin.x - c.x, a.origin.z - c.z);
        if (a.team !== team) md = Math.min(md, d); else if (d < 40) md = Math.min(md, 10);
      }
      if (md > bd) { bd = md; best = c; }
    }
    return best;
  }

  spawn(a) {
    const sp = this.spawnPoint(a.team);
    const yaw = this.mode === 'kz' ? 180 : anglesTo(new THREE.Vector3(sp.x, 0, sp.z), this.center).yaw;
    a.resetForSpawn(sp, yaw + (Math.random() - 0.5) * 30);
    a.pose.bodyYaw = a.yaw; a.pose.aimYaw = a.yaw;
    a.protectedUntil = this.time + (this.mode === 'tdm' ? 1.5 : 0);
    if (a.isBot) { a.ai.aimYaw = a.yaw; a.ai.aimPitch = 0; a.ai.path = []; a.ai.target = null; a.ai.lastKnown = null; a.switchTo(0, this.time); }
    else if (this.mode !== 'kz') {
      a.inventory[0] = this.cfg.game.primary;
      a.slot = -1; a.switchTo(0, this.time); a.prevSlot = 1;
      this.app.viewmodel?.setWeapon(a.weaponId);
      applySkin(a.model.gun, this.cfg.skins[a.weaponId], this.cfg.skins.color);
      this.killedBy = null;
    }
    if (a === this.player) {
      this.net.origin.copy(a.origin); this.net.choked = 0;
      this.netModel.root.position.copy(a.origin);
    }
    this.updatePose(a, a.yaw, 0, a.yaw);
  }

  // --------------------------------------------------------------- tick
  tick() {
    const dt = TICK;
    this.time += dt; this.tickCount++;
    MV.enableBunnyhopping = this.cfg.misc.svBunnyhop;
    MV.airaccelerate = this.cfg.misc.airaccelerate;
    this.playerTick(dt);
    for (const a of this.actors) if (a.isBot) this.botTick(a, dt);
    if (this.mode === 'tdm') {
      for (const a of this.actors) if (!a.alive && this.time >= a.respawnAt && a.respawnAt > 0) {
        if (a === this.player && this.app.menuOpen && false) continue;
        this.spawn(a);
      }
    }
    // chat expiry
    this.killfeed = this.killfeed.filter(k => this.time - k.t < 7);
    this.chat = this.chat.filter(c => this.time - c.t < 10);
    this.mouseDXTick = 0;
  }

  playerTick(dt) {
    const p = this.player, cfg = this.cfg, inp = this.input;
    p.prevOrigin.copy(p.origin);
    if (!p.alive) {
      this.updateDeadPose(p);
      return;
    }
    const canAct = !this.app.chatOpen && !this.app.buyOpen;
    const k = c => canAct && inp.down(c);
    let fm = (k('KeyW') ? 450 : 0) - (k('KeyS') ? 450 : 0);
    let sm = (k('KeyD') ? 450 : 0) - (k('KeyA') ? 450 : 0);
    const spaceHeld = k('Space');
    // scroll-wheel jump: alternates press/release while scrolling
    const wheelActive = performance.now() - inp.wheelTime < 90;
    let jump = spaceHeld;
    if (wheelActive) { this.wheelToggle = !this.wheelToggle; if (this.wheelToggle) jump = true; }
    const w = p.weapon;
    const cmd = {
      forwardmove: fm, sidemove: sm, yaw: p.yaw, jump, duck: k('ControlLeft') || k('KeyC') || k('ControlRight'), walk: k('ShiftLeft'),
      maxspeed: p.ws.scoped && w.scopedSpeed ? w.scopedSpeed : w.maxspeed, autoJump: false,
    };
    const mouseOk = this.app.pointerActive && !this.app.menuOpen;
    let wantFire = mouseOk && inp.mouse1;
    const attack2 = mouseOk && inp.consumeMouse2Press();
    let aimYaw = p.yaw, aimPitch = p.pitch;

    // weapon switching
    if (canAct) {
      if (inp.pressed('Digit1')) this.playerSwitch(0);
      if (inp.pressed('Digit2')) this.playerSwitch(1);
      if (inp.pressed('Digit3')) this.playerSwitch(2);
      if (inp.pressed('KeyQ')) this.playerSwitch(p.prevSlot);
      if (inp.wheelSwitch) { this.playerSwitch((p.slot + inp.wheelSwitch + 3) % 3); inp.wheelSwitch = 0; }
      if (inp.pressed('KeyF')) { p.ws.inspect = this.time; }
    }

    // ---- aim assistance
    const rage = cheats.rageAimbot(this, cfg);
    this.rageTarget = rage;
    let forceFire = false;
    if (rage) {
      const rw = p.weapon;
      if (cfg.rage.autoscope && rw.sniper && !p.ws.scoped) { p.ws.scoped = 1; }
      const ready = this.time >= p.ws.nextAttack && !p.ws.reloading;
      if (cfg.rage.autostop && p.onGround && !cmd.jump && (ready || this.time >= p.ws.nextAttack - 0.15)) cheats.quickStop(p, cmd);
      const hcOk = rage.hitchance >= cfg.rage.hitchance;
      if ((cfg.rage.autofire || wantFire) && hcOk && (!rw.sniper || p.ws.scoped)) forceFire = true;
      if (!hcOk && !cfg.rage.autofire) wantFire = false;
      const nr = cfg.misc.norecoil;
      aimYaw = rage.angles.yaw - (nr ? 0 : p.ws.punchY * 2);
      aimPitch = rage.angles.pitch + (nr ? 0 : p.ws.punchP * 2);
      if (!cfg.rage.silent) { p.yaw = aimYaw; p.pitch = aimPitch; }
    } else {
      cheats.legitAimbot(this, cfg, wantFire);
      cheats.rcs(this, cfg);
      aimYaw = p.yaw; aimPitch = p.pitch;
      if (cheats.triggerbot(this, cfg)) forceFire = true;
    }

    const jumpHeld = spaceHeld || wheelActive;
    cheats.movementCheats(this, cfg, cmd, { jumpHeld, mouseDX: this.mouseDXTick });

    // ---- movement
    const wasGround = p.onGround;
    const preSpeed = p.speed2d();
    playerMove(this.world, p, cmd, dt);
    this.handleMoveEvents(p, wasGround, preSpeed, cmd);

    // ---- weapon
    const fired = this.weaponTick(p, { attack: wantFire || forceFire, attack2, reload: canAct && inp.pressed('KeyR') }, aimYaw, aimPitch);

    // ---- anti-aim & networking
    const aa = cheats.computeAntiAim(this, cfg, fired);
    if (fired) { aa.real = aimYaw; aa.fake = aimYaw; aa.pitch = aimPitch; }
    this.aaInfo = aa;
    let bodyYaw;
    if (aa.aa) bodyYaw = aa.real - (aa.side || 1) * (aa.body || 0);
    else bodyYaw = this.naturalBody(p, aa.real, dt);
    this.updatePose(p, aa.real, aa.pitch, bodyYaw);
    const choke = cheats.fakeLagShouldChoke(this, cfg, fired);
    if (!choke) this.sendNet(p, aa); else { this.net.choked++; }
    this.net.chokeTime = this.net.choked * TICK;

    // ---- KZ logic
    if (this.mode === 'kz') this.kzTick(p);

    this.speedHistory.push({ s: p.speed2d(), g: p.onGround });
    if (this.speedHistory.length > 300) this.speedHistory.shift();
  }

  playerSwitch(slot) {
    const p = this.player;
    if (slot === p.slot || !p.inventory[slot]) return;
    p.switchTo(slot, this.time);
    this.app.viewmodel?.setWeapon(p.weaponId);
    applySkin(p.model.gun, this.cfg.skins[p.weaponId], this.cfg.skins.color);
    this.netModel.setGun(p.weaponId); this.netModel.setGhost(this.ghostMat);
  }

  naturalBody(a, aimYaw, dt) {
    let body = a.pose.bodyYaw;
    const diff = wrap180(aimYaw - body);
    if (a.speed2d() > 5) body += diff * Math.min(1, dt * 12);
    else if (Math.abs(diff) > 50) body = aimYaw - Math.sign(diff) * 50;
    return wrap180(body);
  }

  sendNet(p, aa) {
    const n = this.net;
    n.origin.copy(p.origin); n.velocity.copy(p.velocity); n.choked = 0;
    n.fake = aa.fake; n.real = aa.real; n.pitch = aa.pitch;
    const pose = { ...p.pose, aimYaw: aa.fake, bodyYaw: aa.aa ? aa.fake : p.pose.bodyYaw, pitch: aa.pitch };
    this.netModel.root.position.copy(p.origin);
    this.netModel.setPose(pose);
    this.netModel.updateHitboxes();
  }

  handleMoveEvents(p, wasGround, preSpeed, cmd) {
    const J = this.jump;
    for (const ev of p.events) {
      if (ev === 'jump') {
        if (J.active && J.groundTicks <= 2) J.bhops++; else J.bhops = 0;
        J.active = true; J.start = p.origin.clone(); J.pre = preSpeed; J.max = preSpeed; J.strafes = 0; J.lastSide = 0; J.sync = 0; J.airTicks = 0;
        this.kz.jumps++;
      } else if (ev === 'land') {
        if (J.active && J.start) {
          const dist = Math.hypot(p.origin.x - J.start.x, p.origin.z - J.start.z) + 32;
          const dh = p.origin.y - J.start.y;
          J.last = { dist, pre: J.pre, max: J.max, strafes: J.strafes, sync: J.airTicks ? J.sync / J.airTicks * 100 : 0, dh, bhops: J.bhops, t: this.time, type: J.bhops > 0 ? 'BH' : (J.pre > 280 ? 'LJ+' : 'LJ') };
        }
        J.groundTicks = 0;
        this.audio.land(true);
      }
    }
    p.events.length = 0;
    if (!p.onGround && J.active) {
      J.airTicks++;
      const s = p.speed2d();
      if (s > preSpeed + 0.01) J.sync++;
      if (s > J.max) J.max = s;
      const side = Math.sign(cmd.sidemove);
      if (side !== 0 && side !== J.lastSide) { J.strafes++; J.lastSide = side; }
    } else if (p.onGround) {
      J.groundTicks++;
      if (J.groundTicks > 3) J.bhops = 0;
    }
    this.footsteps(p, true);
  }

  footsteps(a, local) {
    if (!a.onGround || !a.alive) { a.stepAccum = 0; return; }
    const s = a.speed2d();
    if (s < 140) return;
    a.stepAccum += s * TICK;
    if (a.stepAccum > 85) { a.stepAccum = 0; this.audio.footstep(a.origin, local); }
  }

  botTick(b, dt) {
    b.prevOrigin.copy(b.origin);
    if (!b.alive) { this.updateDeadPose(b); return; }
    const o = botThink(this, b, dt);
    const w = b.weapon;
    const cmd = { forwardmove: o.forwardmove, sidemove: o.sidemove, yaw: b.yaw, jump: o.jump, duck: o.duck, walk: o.walk, maxspeed: b.ws.scoped && w.scopedSpeed ? w.scopedSpeed : w.maxspeed, autoJump: false };
    playerMove(this.world, b, cmd, dt);
    b.events.length = 0;
    this.weaponTick(b, { attack: o.attack, attack2: o.attack2, reload: o.reload }, b.yaw, b.pitch);
    this.updatePose(b, b.yaw, b.pitch, this.naturalBody(b, b.yaw, dt));
    this.footsteps(b, false);
  }

  updatePose(a, aimYaw, pitch, bodyYaw) {
    const P = a.pose;
    const by = bodyYaw * D2R;
    const fx = -Math.sin(by), fz = -Math.cos(by), rx = Math.cos(by), rz = -Math.sin(by);
    P.speedFwd = a.velocity.x * fx + a.velocity.z * fz;
    P.speedSide = a.velocity.x * rx + a.velocity.z * rz;
    P.phase += a.speed2d() * TICK * 0.05;
    P.duck += ((a.ducked ? 1 : 0) - P.duck) * 0.35;
    P.air = !a.onGround;
    P.bodyYaw = bodyYaw; P.aimYaw = aimYaw; P.pitch = pitch;
    P.pistol = a.weapon.slot === 1 || !!a.weapon.melee;
    a.model.root.position.copy(a.origin);
    a.model.setPose(P);
    a.model.updateHitboxes();
  }

  updateDeadPose(a) {
    a.pose.dead = Math.min(1, (this.time - a.deathTime) / 0.45);
    a.model.root.position.copy(a.origin);
    a.model.setPose(a.pose);
    // let the corpse drop to the ground
    if (!a.onGround) {
      a.velocity.x *= 0.9; a.velocity.z *= 0.9;
      playerMove(this.world, a, { forwardmove: 0, sidemove: 0, yaw: a.yaw, jump: false, duck: false, walk: false, maxspeed: 250 }, TICK);
      a.events.length = 0;
    }
    if (this.time - a.deathTime > 5) a.model.root.visible = false;
  }

  // --------------------------------------------------------------- weapons
  weaponTick(a, inp, yaw, pitch) {
    const now = this.time, w = a.weapon, ws = a.ws, am = a.ammo[w.id];
    const isP = a === this.player;
    const infinite = isP && this.cfg.misc.infiniteAmmo;
    ws.fireInacc *= Math.exp(-TICK * 5);
    if (now - ws.lastShot > Math.max(0.12, Math.min(w.cycle * 1.1, 0.4))) {
      const k = Math.exp(-TICK * 9);
      ws.punchP *= k; ws.punchY *= k;
      if (Math.abs(ws.punchP) < 0.01) ws.punchP = 0;
      ws.recoilIdx = Math.max(0, ws.recoilIdx - TICK * 16);
    }
    if (ws.reloading && now >= ws.reloadEnd) {
      const need = w.clip - am.clip;
      const take = infinite ? need : Math.min(need, am.reserve);
      am.clip += take; if (!infinite) am.reserve -= take;
      ws.reloading = false;
    }
    const startReload = () => {
      if (ws.reloading || w.melee || am.clip >= w.clip || (am.reserve <= 0 && !infinite)) return;
      ws.reloading = true; ws.reloadEnd = now + w.reload; ws.scoped = 0; ws.inspect = 0;
      this.audio.reload(a.origin, isP);
      if (isP) this.app.viewmodel?.reload(w.reload);
    };
    if (inp.reload) startReload();
    if (w.sniper && inp.attack2 && !ws.reloading) {
      ws.scoped = (ws.scoped + 1) % 3;
      if (isP) this.audio.ui('click');
    }
    let fired = false;
    if (!ws.reloading && now >= ws.nextAttack) {
      if (w.melee) {
        if (inp.attack || (inp.attack2 && isP) || (inp.attack2 && !isP)) {
          this.knifeAttack(a, yaw, pitch, !inp.attack && inp.attack2);
          fired = true;
        } else if (isP && this.input.mouse2 && this.app.pointerActive && !this.app.menuOpen) {
          this.knifeAttack(a, yaw, pitch, true); fired = true;
        }
      } else if (inp.attack && (w.auto || !ws.attackHeldPrev)) {
        if (am.clip <= 0) {
          if (isP && !ws.attackHeldPrev) this.audio.empty();
          ws.nextAttack = now + 0.2;
          startReload();
        } else {
          this.fireWeapon(a, yaw, pitch);
          fired = true;
          // double tap: instantly fire a second shot, then recharge
          if (isP && this.cfg.rage.enabled && this.cfg.rage.doubletap && this.dt.charged && am.clip > 0) {
            this.fireWeapon(a, yaw, pitch);
            this.dt.charged = false; this.dt.chargeAt = now + 0.45;
          }
        }
      }
    }
    if (isP && !this.dt.charged && now >= this.dt.chargeAt && now - ws.lastShot > 0.2) this.dt.charged = true;
    ws.attackHeldPrev = inp.attack;
    return fired;
  }

  fireWeapon(a, yaw, pitch) {
    const now = this.time, w = a.weapon, ws = a.ws, am = a.ammo[w.id];
    const isP = a === this.player;
    if (!(isP && this.cfg.misc.infiniteAmmo)) am.clip--;
    else if (am.clip > 1) am.clip--;
    ws.nextAttack = now + w.cycle;
    const noRecoil = isP && this.cfg.misc.norecoil;
    const noSpread = isP && this.cfg.misc.nospread;
    let by = yaw + (noRecoil ? 0 : ws.punchY * 2);
    let bp = pitch - (noRecoil ? 0 : ws.punchP * 2);
    const spread = noSpread ? 0 : weaponInaccuracy(w, ws, a);
    const ang = Math.random() * Math.PI * 2, rad = Math.random() * spread;
    by += Math.cos(ang) * rad; bp += Math.sin(ang) * rad;
    const dir = angleVectors(by, bp);
    const eye = a.eye(new THREE.Vector3());
    const hit = this.traceBullet(eye, dir, a, w.range);
    // effects
    const muzzle = this.muzzlePos(a, dir);
    const showTracer = isP ? this.cfg.visuals.tracers : Math.random() < 0.35;
    if (showTracer) {
      if (isP) this.effects.tracer(muzzle, hit.point, new THREE.Color(this.cfg.visuals.tracerColor).getHex(), 0.6, 1.2);
      else this.effects.tracer(muzzle, hit.point, 0xffd27a, 0.35, 0.08);
    }
    if (isP && this.cfg.visuals.impacts) this.effects.impactBox(hit.point, hit.actor ? 0xff2040 : 0x2060ff);
    if (!isP || this.cfg.visuals.thirdperson) this.effects.muzzleFlash(muzzle, w.sniper ? 14 : 9);
    if (hit.actor) {
      this.applyDamage(hit.actor, a, w.damage * HITGROUP_MULT[hit.group] * Math.pow(w.rangeMod, hit.dist / 500), hit.group, w, hit.point, dir);
    } else {
      if (hit.hitWorld) {
        this.effects.decal(hit.point, hit.normal);
        this.effects.burst(hit.point, hit.normal, 'spark', 4);
        this.effects.burst(hit.point, hit.normal, 'dust', 2);
      }
      if (a.isBot && a.ai.target === this.player) botOnShotMissed(a);
    }
    // recoil
    if (w.pattern) {
      const inc = w.pattern[Math.min(Math.floor(ws.recoilIdx), w.pattern.length - 1)];
      ws.punchP += inc.p * 0.5 * w.recoilScale;
      ws.punchY += inc.y * 0.5 * w.recoilScale + (Math.random() - 0.5) * 0.08;
    } else if (w.punch) {
      ws.punchP += w.punch; ws.punchY += (Math.random() - 0.5) * w.punch * 0.3;
    }
    ws.recoilIdx += 1;
    ws.fireInacc = Math.min(w.spreadFireMax, ws.fireInacc + w.spreadFire);
    ws.lastShot = now;
    if (w.sniper) ws.scopedBeforeShot = ws.scoped;
    if (w.sniper && w.id === 'awp') ws.scoped = 0;
    this.audio.shot(w.sound, eye, isP);
    if (isP) this.app.viewmodel?.fire(w);
    for (const b of this.actors) if (b.isBot && b !== a) botHearShot(this, b, a);
    a.ws.inspect = 0;
  }

  muzzlePos(a, dir) {
    if (a === this.player && !this.cfg.visuals.thirdperson) {
      const eye = a.eye(new THREE.Vector3());
      const r = new THREE.Vector3(Math.cos(a.yaw * D2R), 0, -Math.sin(a.yaw * D2R));
      return eye.addScaledVector(dir, 22).addScaledVector(r, 5).add(new THREE.Vector3(0, -5, 0));
    }
    const m = a.model.gun?.userData.muzzle;
    if (m) { a.model.root.updateMatrixWorld(true); return new THREE.Vector3().setFromMatrixPosition(m.matrixWorld); }
    return a.eye(new THREE.Vector3());
  }

  traceBullet(eye, dir, shooter, range, extraR = 0) {
    const end = eye.clone().addScaledVector(dir, range);
    const tr = this.world.traceRay(eye, end);
    const worldDist = tr.hitFraction * range;
    let best = null, bt = worldDist;
    const v = new THREE.Vector3(), c = new THREE.Vector3();
    for (const e of this.actors) {
      if (!e.alive || e === shooter || e.team === shooter.team) continue;
      e.center(c); v.subVectors(c, eye);
      const t = v.dot(dir);
      if (t < -60 || t > bt + 60) continue;
      const perp = v.addScaledVector(dir, -t).length();
      if (perp > 70) continue;
      for (const h of e.model.hitboxes) {
        const tt = rayCapsule(eye, dir, h.wa, h.wb, h.r + extraR);
        if (tt >= 0 && tt < bt) { bt = tt; best = { actor: e, group: h.group }; }
      }
    }
    if (best) return { ...best, dist: bt, point: eye.clone().addScaledVector(dir, bt) };
    return { actor: null, dist: worldDist, point: eye.clone().addScaledVector(dir, worldDist), normal: tr.normal.clone(), hitWorld: tr.hitFraction < 1 };
  }

  knifeAttack(a, yaw, pitch, stab) {
    const w = a.weapon;
    a.ws.nextAttack = this.time + (stab ? w.cycle2 : w.cycle);
    const dir = angleVectors(yaw, pitch);
    const eye = a.eye(new THREE.Vector3());
    const hit = this.traceBullet(eye, dir, a, w.range, 5);
    this.audio.shot('knife', eye, a === this.player);
    if (a === this.player) this.app.viewmodel?.fire(w, stab);
    if (hit.actor) {
      const v = hit.actor;
      const toV = Math.atan2(-(v.origin.x - a.origin.x), -(v.origin.z - a.origin.z)) * R2D;
      const back = Math.abs(wrap180(toV - v.pose.bodyYaw)) < 70;
      const dmg = back ? (stab ? 180 : 90) : (stab ? w.damage2 : w.damage);
      this.audio.knifeHit(hit.point);
      this.applyDamage(v, a, dmg, back ? 'chest' : hit.group === 'head' ? 'chest' : hit.group, w, hit.point, dir, true);
    } else if (hit.hitWorld && hit.dist < w.range) {
      this.effects.burst(hit.point, hit.normal, 'spark', 3);
    }
  }

  applyDamage(victim, attacker, rawDmg, group, w, point, dir, melee = false) {
    if (!victim.alive) return;
    const isPV = victim === this.player, isPA = attacker === this.player;
    if ((isPV && this.cfg.misc.godmode) || this.time < (victim.protectedUntil || 0)) { this.effects.burst(point, null, 'spark', 3); return; }
    let dmg = rawDmg;
    if (!melee && victim.armor > 0 && group !== 'leg') {
      const before = dmg;
      dmg *= w.armorPen;
      victim.armor = Math.max(0, victim.armor - Math.round((before - dmg) * 0.5));
    }
    dmg = Math.max(1, Math.round(dmg));
    victim.health -= dmg;
    victim.lastAttacker = attacker; victim.lastDamageTime = this.time;
    this.effects.burst(point, dir.clone().negate(), 'blood', group === 'head' ? 10 : 5);
    if (group === 'head') this.audio.headshot(isPA);
    if (isPA) {
      this.hitmarker = { t: this.time, hs: group === 'head', kill: victim.health <= 0 };
      if (this.cfg.misc.hitsound !== 'Off') this.audio.hitsound(this.cfg.misc.hitsound);
      if (this.cfg.visuals.damageIndicator) this.dmgNumbers.push({ pos: point.clone(), dmg, hs: group === 'head', t: this.time });
    }
    if (isPV) {
      this.damageFlash = 1; this.audio.hurt();
      this.damageDir = anglesTo(victim.origin, attacker.origin).yaw;
      this.damageDirT = this.time;
    }
    if (victim.isBot && !victim.ai.target && attacker.alive) {
      victim.ai.lastKnown = attacker.origin.clone(); victim.ai.lastKnownTime = this.time; victim.ai.path = [];
      victim.ai.aimYaw = anglesTo(victim.eye(new THREE.Vector3()), attacker.eye(new THREE.Vector3())).yaw + (Math.random() - 0.5) * 40;
    }
    if (victim.health <= 0) this.kill(victim, attacker, w, group === 'head');
  }

  kill(victim, killer, w, hs) {
    victim.health = 0; victim.alive = false; victim.deaths++;
    victim.deathTime = this.time; victim.respawnAt = this.time + (victim === this.player ? 3 : 4);
    victim.pose.deathDir = Math.random() < 0.5 ? 1 : -1;
    victim.velocity.multiplyScalar(0.3);
    if (killer && killer !== victim) {
      killer.kills++; if (hs) killer.hs++;
      this.score[killer.team] = (this.score[killer.team] || 0) + 1;
    }
    this.killfeed.push({ killer: killer.name, kt: killer.team, victim: victim.name, vt: victim.team, weapon: w.killIcon, hs, t: this.time, local: killer === this.player || victim === this.player });
    if (this.killfeed.length > 6) this.killfeed.shift();
    if (killer === this.player) {
      this.killFlash = 1;
      if (this.cfg.misc.killsay) this.say(this.player.name, KILLSAYS[Math.floor(Math.random() * KILLSAYS.length)], this.player.team);
    }
    if (victim === this.player) {
      this.killedBy = { name: killer.name, weapon: w.name, hs, health: killer.health };
      this.player.ws.scoped = 0;
    }
  }

  say(name, text, team) { this.chat.push({ name, text, team, t: this.time }); }

  // --------------------------------------------------------------- KZ
  kzTick(p) {
    const inZone = (z) => p.origin.x > z.min[0] && p.origin.x < z.max[0] && p.origin.z > z.min[2] && p.origin.z < z.max[2] && p.origin.y > z.min[1] && p.origin.y < z.max[1];
    const K = this.kz;
    let inStart = false;
    this.comp.zones.forEach((z, i) => {
      if (!inZone(z)) return;
      if (z.type === 'startzone') inStart = true;
      if (z.type === 'cp' && K.cp !== i) { K.cp = i; K.cps++; this.audio.ui('cp'); this.say('KZ', `Чекпоинт #${K.cps} сохранён`, 'KZ'); }
      if (z.type === 'finish' && K.running) {
        K.running = false; K.finished = { time: this.time - K.start, t: this.time };
        this.audio.ui('finish');
        const best = K.best;
        if (!best || K.finished.time < best) { K.best = K.finished.time; localStorage.setItem('kz_best_' + this.map.id, String(K.best)); }
        this.say('KZ', `Финиш! Время ${fmtTime(K.finished.time)}${!best || K.finished.time <= K.best ? ' — новый рекорд!' : ''}`, 'KZ');
      }
    });
    if (inStart) { K.running = false; K.cp = null; K.cps = 0; K.jumps = 0; this.wasInStart = true; }
    else if (this.wasInStart) { this.wasInStart = false; K.running = true; K.start = this.time; K.finished = null; }
    if (K.running) K.time = this.time - K.start;
    if (p.origin.y < (this.map.killY ?? -9999)) this.kzTeleport(false);
    if (this.input.pressed('KeyT')) this.kzTeleport(true);
  }

  kzTeleport(restart) {
    const p = this.player, K = this.kz;
    let sp;
    if (!restart && K.cp != null) sp = this.comp.zones[K.cp].spawn;
    else { sp = this.spawnPoint('start'); K.running = false; K.cp = null; }
    p.origin.set(sp.x, sp.y, sp.z); p.prevOrigin.copy(p.origin); p.velocity.set(0, 0, 0);
    this.jump.active = false;
  }

  // --------------------------------------------------------------- rendering helpers
  getChamsMat(type, color, hidden) {
    const key = `${type}|${color}|${hidden}`;
    if (this.matCache.has(key)) return this.matCache.get(key);
    const c = new THREE.Color(color);
    let m;
    const depth = hidden ? { depthFunc: THREE.GreaterDepth, depthWrite: false, transparent: true } : {};
    switch (type) {
      case 'Flat': m = new THREE.MeshBasicMaterial({ color: c, ...depth }); break;
      case 'Shaded': m = new THREE.MeshLambertMaterial({ color: c, emissive: c.clone().multiplyScalar(0.25), ...depth }); break;
      case 'Metallic': m = new THREE.MeshStandardMaterial({ color: c, metalness: 1, roughness: 0.2, emissive: c.clone().multiplyScalar(0.15), ...depth }); break;
      case 'Wireframe': m = new THREE.MeshBasicMaterial({ color: c, wireframe: true, ...depth }); break;
      case 'Glow': default:
        m = new THREE.ShaderMaterial({
          uniforms: { color: { value: c } }, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending,
          depthFunc: hidden ? THREE.GreaterDepth : THREE.LessEqualDepth,
          vertexShader: `varying vec3 vN; varying vec3 vV; void main(){ vec4 mv = modelViewMatrix*vec4(position,1.0); vN = normalize(normalMatrix*normal); vV = normalize(-mv.xyz); gl_Position = projectionMatrix*mv; }`,
          fragmentShader: `uniform vec3 color; varying vec3 vN; varying vec3 vV; void main(){ float f = pow(1.0 - abs(dot(vN, vV)), 1.6); gl_FragColor = vec4(color*(0.25+f*1.6), 0.35+f); }`,
        });
    }
    this.matCache.set(key, m);
    return m;
  }
}

export function fmtTime(t) {
  const m = Math.floor(t / 60), s = t - m * 60;
  return `${m}:${s < 10 ? '0' : ''}${s.toFixed(2)}`;
}
