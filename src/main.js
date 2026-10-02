import * as THREE from 'three';
import { cfg, matchConfig, WEAPONS, WEAPON_ORDER } from './config.js';
import { soundEngine } from './audio.js';
import { MapBuilder } from './maps.js';
import { CharacterRig, FirstpersonViewmodel } from './models.js';
import { SourcePlayerPhysics } from './physics.js';
import { AntiAimController, RagebotController, simulateFireBullet, traceWorldRay, intersectRayAABB } from './hvh.js';
import { BotEntity } from './bots.js';
import { EffectsManager } from './effects.js';
import { EspHudRenderer } from './esp_hud.js';
import { CheatMenuUI } from './menu.js';
import { MainMenuLobby } from './main_menu.js';

// ============================================================================
// INITIALIZE 3D SCENE, CAMERA & RENDERER
// ============================================================================
const gameCanvas = document.getElementById('game-canvas');
const espCanvas = document.getElementById('esp-canvas');

const renderer = new THREE.WebGLRenderer({
  canvas: gameCanvas,
  antialias: true,
  powerPreference: 'high-performance'
});
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x87b8e8);
scene.fog = new THREE.FogExp2(0x87b8e8, 0.00022);

const camera = new THREE.PerspectiveCamera(90, window.innerWidth / window.innerHeight, 1.5, 6500);
scene.add(camera);

const hemiLight = new THREE.HemisphereLight(0xffffff, 0x475569, 0.85);
hemiLight.position.set(0, 600, 0);
scene.add(hemiLight);

const sunLight = new THREE.DirectionalLight(0xfffbeb, 1.15);
sunLight.position.set(550, 950, 480);
sunLight.castShadow = true;
sunLight.shadow.mapSize.width = 2048;
sunLight.shadow.mapSize.height = 2048;
sunLight.shadow.camera.near = 100;
sunLight.shadow.camera.far = 2800;
const d = 1350;
sunLight.shadow.camera.left = -d;
sunLight.shadow.camera.right = d;
sunLight.shadow.camera.top = d;
sunLight.shadow.camera.bottom = -d;
scene.add(sunLight);

// ============================================================================
// CORE SYSTEMS
// ============================================================================
const mapBuilder = new MapBuilder(scene);
const effects = new EffectsManager(scene);
const espHud = new EspHudRenderer(espCanvas);
espHud.resize(window.innerWidth, window.innerHeight);

const playerPhysics = new SourcePlayerPhysics();
const playerAA = new AntiAimController();
const ragebot = new RagebotController();

// Local Player High-Detail 3D Agent Models
const localRealRig = new CharacterRig({ isEnemy: false, isGhost: false });
scene.add(localRealRig.root);

const desyncGhostRig = new CharacterRig({
  isEnemy: false,
  isGhost: true,
  ghostColor: '#ff2a85',
  ghostOpacity: 0.42,
  wireframe: false
});
scene.add(desyncGhostRig.root);

const fakeLagGhostRig = new CharacterRig({
  isEnemy: false,
  isGhost: true,
  ghostColor: '#38bdf8',
  ghostOpacity: 0.35,
  wireframe: true
});
scene.add(fakeLagGhostRig.root);

const fpViewmodel = new FirstpersonViewmodel(camera);

// Game & Round State
let currentMapData = null;
let bots = [];
let viewYaw = 0;
let viewPitch = 0;
let isPointerLocked = false;

let currentWeaponId = 'ssg08';
let previousWeaponId = 'deagle';
let currentWeapon = WEAPONS[currentWeaponId];
let ammoInMag = {
  ssg08: 10,
  scar20: 20,
  awp: 5,
  ak47: 30,
  m4a1s: 20,
  deagle: 7,
  r8: 8,
  zeus: 1,
  knife: 1
};
let fireCooldown = 0;
let reloadTimer = 0;
let isScoped = false;
let pendingDoubleTapShot = null;
let autoPeekAnchorPos = null;
let autoPeekRetracting = false;

// Player & Round Stats
let playerHp = 100;
let playerAlive = true;
let playerKills = 0;
let playerHeadshots = 0;
let playerDeaths = 0;
let ctRoundsWon = 0;
let tRoundsWon = 0;
let roundTimeRemaining = 115;
let roundTransitionTimer = 0;
let freezeTimeRemaining = 0;

// Clan Tag Spammer Animation
const CLANTAG_FRAMES = [
  'g', 'ga', 'gam', 'game', 'games', 'gamese', 'gamesen', 'gamesens', 'gamesense',
  'gamesense ', 'gamesens', 'gamesen', 'gamese', 'games', 'game', 'gam', 'ga', 'g', ''
];
let currentClantag = '';

// Input State
const keys = {
  forward: false,
  backward: false,
  left: false,
  right: false,
  jump: false,
  jumpPressedOnce: false,
  crouch: false,
  fakeDuck: false,
  walk: false,
  mouse1: false,
  mouse1ClickedOnce: false,
  tab: false
};

// ============================================================================
// LOGS, KILLFEED, CHAT & ROUND ANNOUNCEMENTS
// ============================================================================
const TRASH_TALK_LINES = [
  '1',
  'sit dog (gamesense.pub)',
  'uid? config issue tbh',
  'missed my desync LMAO',
  'ez autowall + backtrack',
  'skeet.cc > all'
];

function addEventLog(htmlText) {
  const cLog = document.getElementById('dev-console-log');
  if (cLog) {
    const line = document.createElement('div');
    line.innerHTML = htmlText;
    cLog.appendChild(line);
    cLog.scrollTop = cLog.scrollHeight;
  }
  if (!cfg.misc.eventLogs) return;
  const container = document.getElementById('event-logs');
  const el = document.createElement('div');
  el.className = 'event-log-item';
  el.innerHTML = htmlText;
  container.appendChild(el);
  while (container.children.length > 7) {
    container.removeChild(container.firstChild);
  }
  setTimeout(() => {
    if (el.parentNode) el.remove();
  }, 4800);
}

function addKillfeed(killerName, victimName, weaponName, isHs, isWb) {
  const container = document.getElementById('killfeed');
  const el = document.createElement('div');
  const isLocalKiller = killerName.includes(cfg.misc.playerName);
  el.className = `killfeed-item ${isLocalKiller ? 'local-kill' : ''}`;
  el.innerHTML = `
    <span class="kf-killer ${isLocalKiller ? 'ct-color' : 't-color'}">${killerName}</span>
    <span class="kf-badge">${weaponName}${isWb ? ' <b class="kf-wb">穿透</b>' : ''}${isHs ? ' <b class="kf-hs">⌖</b>' : ''}</span>
    <span class="kf-victim ${isLocalKiller ? 't-color' : 'ct-color'}">${victimName}</span>
  `;
  container.appendChild(el);
  while (container.children.length > 5) {
    container.removeChild(container.firstChild);
  }
  setTimeout(() => {
    if (el.parentNode) el.remove();
  }, 5000);
}

function addChatMessage(author, text) {
  const container = document.getElementById('hvh-chat');
  const el = document.createElement('div');
  el.className = 'chat-msg';
  el.innerHTML = `<span class="chat-author">[ALL] ${author}:</span> ${text}`;
  container.appendChild(el);
  while (container.children.length > 5) {
    container.removeChild(container.firstChild);
  }
  setTimeout(() => {
    if (el.parentNode) el.remove();
  }, 7000);
}

function showRoundBanner(text, durationMs = 2600) {
  const banner = document.getElementById('round-banner');
  if (!banner) return;
  banner.textContent = text;
  banner.classList.remove('hidden');
  setTimeout(() => {
    banner.classList.add('hidden');
  }, durationMs);
}

// ============================================================================
// MAP, ROUND & BOT MANAGEMENT
// ============================================================================
function startNewRound() {
  const sp = currentMapData.playerSpawns[0] || { x: 0, y: 10, z: 800, yaw: 0 };
  playerPhysics.reset(sp);
  viewYaw = sp.yaw || 0;
  viewPitch = 0;
  playerHp = matchConfig.startingHp || 100;
  playerAlive = true;

  Object.keys(WEAPONS).forEach((wid) => {
    ammoInMag[wid] = WEAPONS[wid].magSize;
  });

  bots.forEach((b, idx) => {
    const bsp = currentMapData.botSpawns[idx % currentMapData.botSpawns.length];
    b.spawn(bsp);
  });

  roundTimeRemaining = matchConfig.roundTimeSec || 115;
  freezeTimeRemaining = matchConfig.gameMode === 'rounds' ? 1.5 : 0;
  roundTransitionTimer = 0;
  updateHudDOM();
}

function switchMap(mapId) {
  matchConfig.mapId = mapId;
  effects.clearAll();
  currentMapData = mapBuilder.loadMap(mapId);

  syncBotCount(matchConfig.botCount, true);
  startNewRound();

  document.querySelectorAll('.map-pill').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.map === mapId);
  });

  addEventLog(`<span class="log-tag">[server]</span> Loaded 3D map <b>${mapId}</b> (${matchConfig.tickrate} tick)`);
}

function syncBotCount(targetCount, forceRespawn = false) {
  while (bots.length > targetCount) {
    const b = bots.pop();
    b.destroy();
  }
  while (bots.length < targetCount) {
    const idx = bots.length;
    const b = new BotEntity(idx, scene);
    const sp = currentMapData.botSpawns[idx % currentMapData.botSpawns.length];
    b.spawn(sp);
    bots.push(b);
  }
  if (forceRespawn) {
    bots.forEach((b, idx) => {
      const sp = currentMapData.botSpawns[idx % currentMapData.botSpawns.length];
      b.spawn(sp);
    });
  }
}

function refreshWeaponModels(triggerInspect = false) {
  localRealRig.setWeapon(currentWeaponId, cfg.skins);
  fpViewmodel.setWeapon(currentWeaponId, cfg.skins);
  if (triggerInspect) {
    fpViewmodel.triggerInspect();
  }
}

function switchWeapon(weaponId) {
  if (!WEAPONS[weaponId] || weaponId === currentWeaponId) return;
  previousWeaponId = currentWeaponId;
  currentWeaponId = weaponId;
  currentWeapon = WEAPONS[weaponId];
  isScoped = false;
  reloadTimer = 0;
  fireCooldown = 0.16;

  refreshWeaponModels(false);

  document.querySelectorAll('.wep-slot').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.slot === weaponId);
  });
  updateHudDOM();
}

// ============================================================================
// SHOOTING & RAGEBOT EXECUTION
// ============================================================================
function executePlayerShot(rageTarget = null, isDoubleTapSecondShot = false) {
  if (!playerAlive || reloadTimer > 0 || freezeTimeRemaining > 0) return false;

  if (!currentWeapon.isMelee && ammoInMag[currentWeaponId] <= 0) {
    reloadTimer = currentWeapon.reloadTime;
    return false;
  }

  if (!matchConfig.infiniteAmmo && !currentWeapon.isMelee) {
    ammoInMag[currentWeaponId]--;
  }

  if (!isDoubleTapSecondShot) {
    fireCooldown = currentWeapon.fireRate;
  }

  soundEngine.playWeaponFire(currentWeaponId, false);
  fpViewmodel.triggerFire(currentWeapon.isMelee);

  const eyeOrigin = new THREE.Vector3(
    playerPhysics.pos.x,
    playerPhysics.getEyeY(),
    playerPhysics.pos.z
  );

  const tracerStart = eyeOrigin.clone();
  tracerStart.y -= 4;

  const myDisplayName = `${currentClantag ? `[${currentClantag}] ` : ''}${cfg.misc.playerName}`;

  if (rageTarget) {
    const { bot, point, hitboxType, damage, wallbanged, btTicks } = rageTarget;

    if (!cfg.rage.silentAim) {
      const dx = point.x - eyeOrigin.x;
      const dy = point.y - eyeOrigin.y;
      const dz = point.z - eyeOrigin.z;
      const horiz = Math.hypot(dx, dz);
      viewYaw = Math.atan2(-dx, -dz);
      viewPitch = Math.atan2(dy, horiz);
    }

    if (cfg.visuals.bulletTracers && !currentWeapon.isMelee) {
      effects.addTracer(tracerStart, point, cfg.visuals.tracerColor, 1.15);
    }
    if (cfg.visuals.showImpacts && !currentWeapon.isMelee) {
      effects.addImpactBox(point, false, 2.0);
      effects.addImpactBox(point.clone().add(new THREE.Vector3(0.8, 0.4, -0.8)), true, 2.0);
    }

    const isHs = hitboxType === 'head';
    const killed = bot.takeDamage(damage);

    soundEngine.playHitSound(cfg.misc.hitSound, isHs);
    espHud.triggerHitmarker(isHs);
    effects.addWorldHit(point, damage, isHs, wallbanged);
    effects.addHitSparks(point, isHs);
    if (cfg.visuals.shotChams) {
      effects.addShotGhost(bot, cfg.visuals.enemyVisColor, 1.8);
    }
    if (cfg.rage.autoPeek && autoPeekAnchorPos) {
      autoPeekRetracting = true;
    }

    const hc = ragebot.computeHitchance(currentWeapon, playerPhysics, isScoped);
    addEventLog(
      `<span class="log-tag">[gamesense]</span> Hit <b>${bot.name}</b> in the <span class="${isHs ? 'log-hs' : ''}">${hitboxType}</span> for <b>${damage}</b> damage (${hc}% hc, ${btTicks}t bt${wallbanged ? ', autowall' : ''}${isDoubleTapSecondShot ? ', DT' : ''})`
    );

    if (killed) {
      playerKills++;
      if (isHs) playerHeadshots++;
      addKillfeed(myDisplayName, bot.name, currentWeapon.shortName, isHs, wallbanged);
      if (cfg.misc.killSay) {
        const line = TRASH_TALK_LINES[(playerKills - 1) % TRASH_TALK_LINES.length];
        addChatMessage(myDisplayName, line);
      }
    }

    if (
      !isDoubleTapSecondShot &&
      cfg.rage.doubleTap &&
      currentWeapon.canDoubleTap &&
      ragebot.dtCharge >= 0.99
    ) {
      ragebot.dtCharge = 0;
      ragebot.dtCooldown = 0.85;
      pendingDoubleTapShot = { delay: 0.045 };
    }

    updateHudDOM();
    return true;
  }

  // Manual Crosshair Shot
  const lookDir = new THREE.Vector3(
    -Math.sin(viewYaw) * Math.cos(viewPitch),
    Math.sin(viewPitch),
    -Math.cos(viewYaw) * Math.cos(viewPitch)
  ).normalize();

  const maxRange = currentWeapon.isMelee
    ? currentWeapon.range
    : currentWeapon.isShortRange
    ? currentWeapon.range
    : 4000;
  const endPos = traceWorldRay(eyeOrigin, lookDir, currentMapData.colliders, maxRange);

  let hitBot = null;
  let hitType = 'chest';
  let hitDist = Infinity;
  let hitPos = endPos;

  const invDir = {
    x: Math.abs(lookDir.x) > 1e-6 ? 1 / lookDir.x : 1e6,
    y: Math.abs(lookDir.y) > 1e-6 ? 1 / lookDir.y : 1e6,
    z: Math.abs(lookDir.z) > 1e-6 ? 1 / lookDir.z : 1e6
  };

  for (const bot of bots) {
    if (!bot.alive) continue;
    const bp = bot.physics.pos;
    const bh = bot.physics.getCurrentHeight();
    const botBox = {
      minX: bp.x - 16,
      maxX: bp.x + 16,
      minY: bp.y,
      maxY: bp.y + bh,
      minZ: bp.z - 16,
      maxZ: bp.z + 16
    };
    const hit = intersectRayAABB(eyeOrigin, lookDir, invDir, botBox, maxRange);
    if (hit && hit.tMin < hitDist) {
      hitDist = hit.tMin;
      hitBot = bot;
      hitPos = new THREE.Vector3(
        eyeOrigin.x + lookDir.x * hit.tMin,
        eyeOrigin.y + lookDir.y * hit.tMin,
        eyeOrigin.z + lookDir.z * hit.tMin
      );
      hitType = hitPos.y > bp.y + bh - 16 ? 'head' : hitPos.y > bp.y + 32 ? 'chest' : 'legs';
    }
  }

  if (hitBot) {
    const sim = currentWeapon.isMelee
      ? { damage: currentWeapon.damage, wallbanged: false }
      : simulateFireBullet(eyeOrigin, hitPos, currentWeapon, hitType, currentMapData.colliders, true);

    if (sim.damage > 0) {
      const isHs = hitType === 'head';
      const killed = hitBot.takeDamage(sim.damage);
      soundEngine.playHitSound(cfg.misc.hitSound, isHs);
      espHud.triggerHitmarker(isHs);
      effects.addWorldHit(hitPos, sim.damage, isHs, sim.wallbanged);

      addEventLog(
        `<span class="log-tag">[shot]</span> Hit <b>${hitBot.name}</b> in the <b>${hitType}</b> for <b>${sim.damage}</b> damage`
      );
      if (killed) {
        playerKills++;
        if (isHs) playerHeadshots++;
        addKillfeed(myDisplayName, hitBot.name, currentWeapon.shortName, isHs, sim.wallbanged);
      }
    }
  }

  if (!currentWeapon.isMelee) {
    if (cfg.visuals.bulletTracers) {
      effects.addTracer(tracerStart, hitPos, cfg.visuals.tracerColor, 0.95);
    }
    if (cfg.visuals.showImpacts) {
      effects.addImpactBox(hitPos, false, 1.8);
    }
  }

  updateHudDOM();
  return true;
}

// ============================================================================
// INITIALIZE SKEET CHEAT MENU & CS:GO MAIN MENU LOBBY
// ============================================================================
const cheatMenu = new CheatMenuUI({
  getBots: () => bots,
  onSkinChange: (inspect = false) => refreshWeaponModels(inspect),
  onStealName: () => {
    if (bots.length > 0) {
      const target = bots[Math.floor(Math.random() * bots.length)];
      cfg.misc.playerName = target.name;
      addEventLog(`<span class="log-tag">[gamesense]</span> Stole player name: <b>${target.name}</b>`);
    }
  },
  onDumpWins: () => {
    bots.forEach((b) => {
      addEventLog(`<span class="log-tag">[mm_dump]</span> ${b.name} — Rank: <b>${b.rank}</b> (${b.wins} wins)`);
    });
  },
  onLogEvent: (msg) => addEventLog(msg)
});

const mainLobby = new MainMenuLobby({
  onStartMatch: () => {
    soundEngine.init();
    switchMap(matchConfig.mapId);
    updateLockBannerVisibility();
    gameCanvas.requestPointerLock();
  },
  onResumeMatch: () => {
    soundEngine.init();
    updateLockBannerVisibility();
    gameCanvas.requestPointerLock();
  },
  onOpenCheatMenu: () => {
    cheatMenu.toggle(true);
    updateLockBannerVisibility();
  },
  onWeaponSelect: (wid) => {
    switchWeapon(wid);
  },
  onSkinUpdate: () => {
    refreshWeaponModels(false);
  }
});

// ============================================================================
// HUD, SCOREBOARD & WATERMARK UPDATES
// ============================================================================
function updateHudDOM() {
  const maxHp = matchConfig.startingHp || 100;
  const hpPct = Math.max(0, Math.min(100, (playerHp / maxHp) * 100));
  document.getElementById('hud-hp').textContent = `${playerHp}`;
  const hpFill = document.getElementById('hud-hp-fill');
  if (hpFill) hpFill.style.width = `${hpPct}%`;
  const miniCtHp = document.getElementById('hud-mini-ct-hp');
  if (miniCtHp) miniCtHp.style.width = `${hpPct}%`;
  const hpBlock = document.getElementById('csgo-hp-block');
  if (hpBlock) hpBlock.classList.toggle('low-hp', playerHp <= 25);

  const mapBadge = document.getElementById('hud-map-badge');
  if (mapBadge && mapBuilder.currentMap) {
    mapBadge.textContent = mapBuilder.currentMap.toUpperCase();
  }

  document.getElementById('hud-score').textContent = `${playerKills} K / ${playerHeadshots} HS / ${playerDeaths} D`;
  document.getElementById('hud-wep-name').textContent = currentWeapon.name.toUpperCase();
  const knifeSlotName = document.getElementById('hud-knife-slot-name');
  if (knifeSlotName) {
    knifeSlotName.textContent =
      cfg.skins.knifeModel === 'm9'
        ? '★ M9 Bayonet'
        : cfg.skins.knifeModel === 'butterfly'
        ? '★ Butterfly'
        : '★ Karambit';
  }

  if (currentWeapon.isMelee) {
    document.getElementById('hud-ammo').textContent = '★ MELEE';
  } else if (reloadTimer > 0) {
    document.getElementById('hud-ammo').textContent = 'RELOAD...';
  } else {
    document.getElementById('hud-ammo').textContent = matchConfig.infiniteAmmo
      ? `${currentWeapon.magSize} / ∞`
      : `${ammoInMag[currentWeaponId]} / ${currentWeapon.reserveAmmo}`;
  }
  document.getElementById('score-ct').textContent = `${ctRoundsWon}`;
  document.getElementById('score-t').textContent = `${tRoundsWon}`;

  // Update Top-Center CS:GO Alive Counts & Bot Avatars
  const aliveCtEl = document.getElementById('hud-alive-ct');
  if (aliveCtEl) aliveCtEl.textContent = playerAlive ? '1 ЖИВ' : '0 ЖИВ';
  const aliveBots = bots.filter((b) => b.alive).length;
  const aliveTEl = document.getElementById('hud-alive-t');
  if (aliveTEl) aliveTEl.textContent = `${aliveBots} ЖИВЫ`;

  const tAvatarsEl = document.getElementById('hud-t-avatars');
  if (tAvatarsEl) {
    tAvatarsEl.innerHTML = bots
      .slice(0, 5)
      .map((b) => {
        const bPct = b.alive ? Math.max(0, Math.min(100, (b.hp / (b.maxHp || 100)) * 100)) : 0;
        return `
          <div class="csgo-mini-avatar t ${b.alive ? 'alive' : 'dead'}" title="${b.name}">
            <span>${b.alive ? 'T' : '☠'}</span>
            <div class="mini-hp"><div style="width:${bPct}%"></div></div>
          </div>
        `;
      })
      .join('');
  }
}

function updateScoreboardDOM() {
  const sb = document.getElementById('scoreboard-overlay');
  if (!keys.tab || mainLobby.isVisible || cheatMenu.isOpen) {
    sb.classList.add('hidden');
    return;
  }
  sb.classList.remove('hidden');

  document.getElementById('sb-server-title').textContent =
    `GAMESENSE HVH / TACTICAL SERVER — ${mapBuilder.currentMap.toUpperCase()} [${matchConfig.tickrate} TICK]`;

  const tbody = document.getElementById('sb-tbody');
  const myRank = cfg.misc.revealRanks ? 'The Global Elite (884 wins)' : 'Hidden';
  let rowsHtml = `
    <tr style="background: rgba(149, 184, 6, 0.14);">
      <td class="sb-clan">${currentClantag ? `[${currentClantag}]` : ''}</td>
      <td><b>${cfg.misc.playerName} (YOU)</b></td>
      <td>${currentWeapon.shortName}</td>
      <td>${playerKills}</td>
      <td>${playerDeaths}</td>
      <td>${myRank}</td>
    </tr>
  `;

  bots.forEach((b) => {
    const rnk = cfg.misc.revealRanks ? `${b.rank} (${b.wins} wins)` : 'Hidden (Enable Reveal Ranks)';
    rowsHtml += `
      <tr>
        <td class="sb-clan">${matchConfig.botBehavior === 'hvh' ? '[HVH]' : '[BOT]'}</td>
        <td>${b.name}</td>
        <td>${b.weapon.shortName}</td>
        <td>${b.kills}</td>
        <td>${b.deaths}</td>
        <td>${rnk}</td>
      </tr>
    `;
  });

  tbody.innerHTML = rowsHtml;
}

function updateKeybindsWidget(fps) {
  if (cfg.misc.clantagSpammer) {
    const idx = Math.floor(performance.now() / 280) % CLANTAG_FRAMES.length;
    currentClantag = CLANTAG_FRAMES[idx];
  } else {
    currentClantag = '';
  }

  const wmBox = document.getElementById('watermark');
  wmBox.style.display = cfg.misc.watermark && !cfg.misc.hideFromObs ? 'block' : 'none';
  if (cfg.misc.watermark) {
    const tagPart = currentClantag ? `[${currentClantag}] ` : '';
    document.getElementById('watermark-text').textContent =
      `${tagPart}gamesense.pub | ${mapBuilder.currentMap} | ${matchConfig.tickrate}tick | fps: ${fps} | [INSERT] menu`;
  }

  const kbWidget = document.getElementById('keybinds-widget');
  kbWidget.style.display = cfg.misc.keybindList && !cfg.misc.hideFromObs ? 'block' : 'none';
  if (!cfg.misc.keybindList) return;

  const activeBinds = [];
  if (cfg.rage.enabled) activeBinds.push(['Ragebot', '[enabled]']);
  if (cfg.antiaim.enabled && cfg.antiaim.yawMode === 'spin') {
    activeBinds.push([`Spinbot (${cfg.antiaim.spinSpeed}°/t)`, '[active]']);
  } else if (cfg.antiaim.enabled) {
    activeBinds.push([`Anti-aim (${cfg.antiaim.yawMode})`, '[active]']);
  }
  if (cfg.antiaim.bodyYaw) {
    activeBinds.push([`Body yaw (${cfg.antiaim.inverted ? 'R' : 'L'})`, `[${cfg.antiaim.desyncLimit}°]`]);
  }
  if (cfg.antiaim.fakeLag && cfg.antiaim.fakeLagMode !== 'off') {
    activeBinds.push(['Fake lag', `[${cfg.antiaim.fakeLagLimit}t]`]);
  }
  if (cfg.rage.doubleTap) {
    activeBinds.push(['Double tap', ragebot.dtCharge >= 0.99 ? '[ready]' : '[charge]']);
  }
  if (cfg.rage.minDamageOverrideActive) {
    activeBinds.push(['Damage override', `[${cfg.rage.minDamageOverride}]`]);
  }
  if (cfg.rage.forceBaimActive) {
    activeBinds.push(['Force body aim', '[active]']);
  }
  if (cfg.visuals.thirdperson) {
    activeBinds.push(['Third person', '[active]']);
  }
  if (keys.fakeDuck) {
    activeBinds.push(['Fake duck', '[active]']);
  }
  if (cfg.movement.airStuckMode !== 'off' && cfg.movement.airStuckActive) {
    activeBinds.push(['Air stuck', '[active]']);
  }
  if (cfg.rage.autoPeek && autoPeekAnchorPos) {
    activeBinds.push(['Quick peek', autoPeekRetracting ? '[retract]' : '[ready]']);
  }

  const listEl = document.getElementById('keybinds-list');
  listEl.innerHTML =
    activeBinds.length === 0
      ? '<div class="kb-row"><span style="color:#777">No active binds ([INS])</span></div>'
      : activeBinds
          .map(([name, state]) => `<div class="kb-row"><span>${name}</span><span class="kb-state">${state}</span></div>`)
          .join('');
}

// ============================================================================
// INPUT & POINTER LOCK HANDLERS
// ============================================================================
const lockBanner = document.getElementById('lock-banner');

function updateLockBannerVisibility() {
  if (isPointerLocked || cheatMenu.isOpen || mainLobby.isVisible) {
    lockBanner.classList.add('hidden');
  } else {
    lockBanner.classList.remove('hidden');
  }
}

document.addEventListener('pointerlockchange', () => {
  isPointerLocked = document.pointerLockElement === gameCanvas;
  updateLockBannerVisibility();
});

document.getElementById('start-play-btn').addEventListener('click', () => {
  soundEngine.init();
  cheatMenu.toggle(false);
  mainLobby.hide();
  gameCanvas.requestPointerLock();
});

document.getElementById('banner-menu-btn').addEventListener('click', () => {
  soundEngine.init();
  cheatMenu.toggle(true);
  updateLockBannerVisibility();
});

document.getElementById('banner-lobby-btn').addEventListener('click', () => {
  soundEngine.init();
  cheatMenu.toggle(false);
  mainLobby.show(true);
  updateLockBannerVisibility();
});

document.getElementById('open-menu-btn').addEventListener('click', () => {
  soundEngine.init();
  cheatMenu.toggle();
  updateLockBannerVisibility();
});

document.getElementById('open-lobby-btn').addEventListener('click', () => {
  soundEngine.init();
  cheatMenu.toggle(false);
  mainLobby.show(true);
  updateLockBannerVisibility();
});

gameCanvas.addEventListener('mousedown', (e) => {
  soundEngine.init();
  if (mainLobby.isVisible || cheatMenu.isOpen) return;
  if (!isPointerLocked) {
    gameCanvas.requestPointerLock();
    return;
  }

  if (e.button === 0) {
    keys.mouse1 = true;
    keys.mouse1ClickedOnce = true;
    if (fireCooldown <= 0) {
      executePlayerShot(null, false);
      keys.mouse1ClickedOnce = false;
    }
  } else if (e.button === 2) {
    if (currentWeapon.hasScope) {
      isScoped = !isScoped;
    }
  }
});

window.addEventListener('mouseup', (e) => {
  if (e.button === 0) keys.mouse1 = false;
});

window.addEventListener('contextmenu', (e) => e.preventDefault());

window.addEventListener('mousemove', (e) => {
  if (!isPointerLocked || cheatMenu.isOpen || mainLobby.isVisible) return;
  const baseSens = (matchConfig.mouseSensitivity || 2.2) * 0.001;
  const sens = isScoped ? baseSens * 0.55 : baseSens;
  viewYaw -= e.movementX * sens;
  viewPitch -= e.movementY * sens;
  const maxPitch = 1.52;
  viewPitch = Math.max(-maxPitch, Math.min(maxPitch, viewPitch));
});

window.addEventListener('wheel', (e) => {
  if (!isPointerLocked || cheatMenu.isOpen || mainLobby.isVisible) return;
  const idx = WEAPON_ORDER.indexOf(currentWeaponId);
  const nextIdx = (idx + (e.deltaY > 0 ? 1 : -1) + WEAPON_ORDER.length) % WEAPON_ORDER.length;
  switchWeapon(WEAPON_ORDER[nextIdx]);
});

// Source Engine Developer Console Handler
const devConsole = document.getElementById('dev-console');
const devConsoleInput = document.getElementById('dev-console-input');
document.getElementById('dev-console-close').addEventListener('click', () => {
  devConsole.classList.add('hidden');
});
document.getElementById('dev-console-form').addEventListener('submit', (e) => {
  e.preventDefault();
  const raw = devConsoleInput.value.trim();
  if (!raw) return;
  devConsoleInput.value = '';
  addEventLog(`<span class="log-tag">]</span> ${raw}`);
  const [cmd, arg] = raw.split(/\s+/);
  const c = cmd.toLowerCase();
  if (c === 'help') {
    addEventLog(`Available: sv_airaccelerate <N>, sv_friction <N>, sv_gravity <N>, bot_add, bot_kick, god, thirdperson, firstperson, smoke, map <name>`);
  } else if (c === 'sv_airaccelerate' && arg) {
    cfg.movement.sv_airaccelerate = Number(arg);
    addEventLog(`"sv_airaccelerate" = "${cfg.movement.sv_airaccelerate}"`);
  } else if (c === 'sv_friction' && arg) {
    cfg.movement.sv_friction = Number(arg);
    addEventLog(`"sv_friction" = "${cfg.movement.sv_friction}"`);
  } else if (c === 'sv_gravity' && arg) {
    cfg.movement.sv_gravity = Number(arg);
    addEventLog(`"sv_gravity" = "${cfg.movement.sv_gravity}"`);
  } else if (c === 'bot_add') {
    matchConfig.botCount = Math.min(10, matchConfig.botCount + 1);
    syncBotCount(matchConfig.botCount, false);
    addEventLog(`Added bot (total: ${matchConfig.botCount})`);
  } else if (c === 'bot_kick') {
    matchConfig.botCount = 0;
    syncBotCount(0, false);
    addEventLog(`Kicked all bots.`);
  } else if (c === 'god') {
    cfg.misc.godmode = !cfg.misc.godmode;
    addEventLog(`godmode ${cfg.misc.godmode ? 'ON' : 'OFF'}`);
  } else if (c === 'thirdperson') {
    cfg.visuals.thirdperson = true;
  } else if (c === 'firstperson') {
    cfg.visuals.thirdperson = false;
  } else if (c === 'smoke') {
    effects.addSmokeCloud(playerPhysics.pos, 14.0);
  } else if (c === 'map' && arg) {
    switchMap(arg);
  } else {
    addEventLog(`Unknown command: ${cmd} (type "help")`);
  }
});

window.addEventListener('keydown', (e) => {
  if (document.activeElement === devConsoleInput) {
    if (e.code === 'Backquote' || e.code === 'Escape') {
      e.preventDefault();
      devConsole.classList.add('hidden');
      devConsoleInput.blur();
    }
    return;
  }

  if (cheatMenu.waitingForBindKey) return;

  if (e.code === 'Backquote') {
    e.preventDefault();
    devConsole.classList.toggle('hidden');
    if (!devConsole.classList.contains('hidden')) {
      if (document.pointerLockElement) document.exitPointerLock();
      devConsoleInput.focus();
    }
    return;
  }

  // INSERT or M or RightShift toggles the Skeet Cheat Menu anytime!
  if (e.code === 'Insert' || e.code === 'KeyM' || (e.code === 'ShiftRight' && !isPointerLocked)) {
    e.preventDefault();
    cheatMenu.toggle();
    updateLockBannerVisibility();
    return;
  }

  if (e.code === 'Tab') {
    e.preventDefault();
    keys.tab = true;
    updateScoreboardDOM();
    return;
  }

  // Map hotkeys F1 - F8
  if (e.code === 'F1') { e.preventDefault(); switchMap('de_mirage_mid'); return; }
  if (e.code === 'F2') { e.preventDefault(); switchMap('de_dust2_long'); return; }
  if (e.code === 'F3') { e.preventDefault(); switchMap('de_inferno_banana'); return; }
  if (e.code === 'F4') { e.preventDefault(); switchMap('de_nuke_outside'); return; }
  if (e.code === 'F5') { e.preventDefault(); switchMap('de_cache_mid'); return; }
  if (e.code === 'F6') { e.preventDefault(); switchMap('de_overpass_park'); return; }
  if (e.code === 'F7') { e.preventDefault(); switchMap('aim_redline_hvh'); return; }
  if (e.code === 'F8') { e.preventDefault(); switchMap('cs_office_cyber'); return; }

  if (cheatMenu.isOpen || mainLobby.isVisible) return;

  // Check customizable Skeet Keybinds
  if (e.code === cfg.binds.thirdperson) cfg.visuals.thirdperson = !cfg.visuals.thirdperson;
  if (e.code === cfg.binds.invertDesync) cfg.antiaim.inverted = !cfg.antiaim.inverted;
  if (e.code === cfg.binds.minDamageOverride) cfg.rage.minDamageOverrideActive = !cfg.rage.minDamageOverrideActive;
  if (e.code === cfg.binds.forceBaim) cfg.rage.forceBaimActive = !cfg.rage.forceBaimActive;
  if (e.code === cfg.binds.doubleTap) cfg.rage.doubleTap = !cfg.rage.doubleTap;
  if (e.code === cfg.binds.zHop) cfg.movement.zHop = !cfg.movement.zHop;
  if (e.code === cfg.binds.preSpeed) cfg.movement.preSpeed = !cfg.movement.preSpeed;
  if (e.code === cfg.binds.edgeJump) cfg.movement.edgeJump = !cfg.movement.edgeJump;
  if (e.code === cfg.binds.autoPeek) {
    if (autoPeekAnchorPos) {
      autoPeekAnchorPos = null;
      autoPeekRetracting = false;
    } else {
      cfg.rage.autoPeek = true;
      autoPeekAnchorPos = { ...playerPhysics.pos };
      autoPeekRetracting = false;
    }
  }
  if (e.code === cfg.binds.airStuck) {
    if (cfg.movement.airStuckMode === 'toggle') {
      cfg.movement.airStuckActive = !cfg.movement.airStuckActive;
    } else if (cfg.movement.airStuckMode === 'hold') {
      cfg.movement.airStuckActive = true;
    }
  }
  if (e.code === cfg.binds.fakeDuck) {
    keys.fakeDuck = true;
    keys.crouch = true;
  }

  switch (e.code) {
    case 'KeyW': keys.forward = true; break;
    case 'KeyS': keys.backward = true; break;
    case 'KeyA': keys.left = true; break;
    case 'KeyD': keys.right = true; break;
    case 'Space':
      e.preventDefault();
      if (!keys.jump) keys.jumpPressedOnce = true;
      keys.jump = true;
      break;
    case 'ControlLeft':
    case 'ControlRight':
      e.preventDefault();
      keys.crouch = true;
      break;
    case 'ShiftLeft':
    case 'AltLeft':
      keys.walk = true;
      break;
    case 'KeyR':
      if (!currentWeapon.isMelee && reloadTimer <= 0) {
        reloadTimer = currentWeapon.reloadTime;
      }
      break;
    case 'KeyG':
      fpViewmodel.triggerInspect();
      break;
    case 'KeyQ':
      switchWeapon(previousWeaponId);
      break;
    case 'Digit1': switchWeapon('ssg08'); break;
    case 'Digit2': switchWeapon('scar20'); break;
    case 'Digit3': switchWeapon('awp'); break;
    case 'Digit4': switchWeapon('ak47'); break;
    case 'Digit5': switchWeapon('m4a1s'); break;
    case 'Digit6': switchWeapon('deagle'); break;
    case 'Digit7': switchWeapon('r8'); break;
    case 'Digit8': switchWeapon('zeus'); break;
    case 'Digit9': switchWeapon('knife'); break;
    case 'Digit0':
    case 'KeyK': {
      // Throw 3D Volumetric Smoke Grenade 220 units ahead!
      const smokePos = {
        x: playerPhysics.pos.x - Math.sin(viewYaw) * 220,
        y: playerPhysics.pos.y,
        z: playerPhysics.pos.z - Math.cos(viewYaw) * 220
      };
      effects.addSmokeCloud(smokePos, 14.0);
      addEventLog(`<span class="log-tag">[grenade]</span> Deployed 3D Smoke Cloud (Use 'Remove smoke' in Visuals to see through!)`);
      break;
    }
  }
});

window.addEventListener('keyup', (e) => {
  if (e.code === 'Tab') {
    keys.tab = false;
    updateScoreboardDOM();
  }
  if (e.code === cfg.binds.fakeDuck) {
    keys.fakeDuck = false;
    keys.crouch = false;
  }
  if (e.code === cfg.binds.airStuck && cfg.movement.airStuckMode === 'hold') {
    cfg.movement.airStuckActive = false;
  }

  switch (e.code) {
    case 'KeyW': keys.forward = false; break;
    case 'KeyS': keys.backward = false; break;
    case 'KeyA': keys.left = false; break;
    case 'KeyD': keys.right = false; break;
    case 'Space': keys.jump = false; break;
    case 'ControlLeft':
    case 'ControlRight':
      keys.crouch = false;
      break;
    case 'ShiftLeft':
    case 'AltLeft':
      keys.walk = false;
      break;
  }
});

document.querySelectorAll('.map-pill').forEach((btn) => {
  btn.addEventListener('click', () => switchMap(btn.dataset.map));
});
document.querySelectorAll('.wep-slot').forEach((btn) => {
  btn.addEventListener('click', () => switchWeapon(btn.dataset.slot));
});

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
  espHud.resize(window.innerWidth, window.innerHeight);
});

// ============================================================================
// MAIN SIMULATION & RENDER LOOP
// ============================================================================
switchMap('de_mirage_mid');
updateHudDOM();

let lastTime = performance.now();
let fpsCounter = 0;
let fpsTimer = 0;
let displayedFps = 144;

function animate(now) {
  requestAnimationFrame(animate);

  const rawDt = Math.min(0.05, (now - lastTime) / 1000);
  lastTime = now;

  fpsCounter++;
  fpsTimer += rawDt;
  if (fpsTimer >= 0.4) {
    displayedFps = Math.round(fpsCounter / fpsTimer);
    fpsCounter = 0;
    fpsTimer = 0;
    const fpsPill = document.getElementById('hud-fps-pill');
    if (fpsPill) {
      fpsPill.textContent = `${displayedFps} FPS • ${matchConfig.tickrate} TICK`;
    }
    updateHudDOM();
    updateKeybindsWidget(displayedFps);
    if (keys.tab) updateScoreboardDOM();
  }

  // Round Clock & Round-Based Mode Logic
  const isRoundMode = matchConfig.gameMode === 'rounds';
  if (freezeTimeRemaining > 0) {
    freezeTimeRemaining = Math.max(0, freezeTimeRemaining - rawDt);
  } else if (!mainLobby.isVisible) {
    roundTimeRemaining = Math.max(0, roundTimeRemaining - rawDt);
  }

  const mins = Math.floor(roundTimeRemaining / 60);
  const secs = Math.floor(roundTimeRemaining % 60).toString().padStart(2, '0');
  document.getElementById('round-clock').textContent =
    freezeTimeRemaining > 0 ? `FREEZE ${Math.ceil(freezeTimeRemaining)}s` : `${mins}:${secs}`;

  if (isRoundMode && roundTransitionTimer <= 0 && bots.length > 0) {
    const anyBotAlive = bots.some((b) => b.alive);
    if (!anyBotAlive) {
      ctRoundsWon++;
      updateHudDOM();
      showRoundBanner('🏆 РАУНД ВЫИГРАН! ВСЕ ВРАГИ УНИЧТОЖЕНЫ');
      roundTransitionTimer = 3.0;
    } else if (!playerAlive || roundTimeRemaining <= 0) {
      tRoundsWon++;
      updateHudDOM();
      showRoundBanner('💀 РАУНД ПРОИГРАН! НОВЫЙ РАУНД ЧЕРЕЗ 3 СЕК');
      roundTransitionTimer = 3.0;
    }
  }

  if (roundTransitionTimer > 0) {
    roundTransitionTimer -= rawDt;
    if (roundTransitionTimer <= 0) {
      startNewRound();
    }
  }

  // Cooldowns & Reload
  if (fireCooldown > 0) fireCooldown = Math.max(0, fireCooldown - rawDt);
  if (reloadTimer > 0) {
    reloadTimer = Math.max(0, reloadTimer - rawDt);
    if (reloadTimer === 0) {
      ammoInMag[currentWeaponId] = currentWeapon.magSize;
      updateHudDOM();
    }
  }
  ragebot.updateRecharge(rawDt, cfg.rage);

  // 1. Scan Ragebot / Knifebot Targets
  const eyeOriginPre = new THREE.Vector3(
    playerPhysics.pos.x,
    playerPhysics.getEyeY(),
    playerPhysics.pos.z
  );
  const rageCandidate =
    !mainLobby.isVisible && playerAlive && freezeTimeRemaining <= 0
      ? ragebot.scanTargets({
          eyeOrigin: eyeOriginPre,
          viewYaw,
          viewPitch,
          weapon: currentWeapon,
          playerPhysics,
          isScoped,
          bots,
          colliders: currentMapData.colliders,
          cfg
        })
      : null;

  let shouldAutoStop = false;
  if (rageCandidate && cfg.rage.enabled && !currentWeapon.isMelee) {
    if (cfg.rage.autoScope && currentWeapon.hasScope && !isScoped) {
      isScoped = true;
    }
    if (cfg.rage.autoStop && playerPhysics.onGround) {
      shouldAutoStop = true;
    }
  }

  // 2. Step Authentic Source Engine Physics
  const subSteps = matchConfig.tickrate === 128 ? 3 : 2;
  const subDt = rawDt / subSteps;
  const maxWepSpeed = isScoped ? currentWeapon.scopedSpeed : currentWeapon.maxSpeed;

  if (!mainLobby.isVisible && playerAlive && freezeTimeRemaining <= 0) {
    // Quick Peek Assist (Auto-Peek) Retraction to Anchor Ring
    if (cfg.rage.autoPeek && autoPeekAnchorPos && autoPeekRetracting) {
      const dx = autoPeekAnchorPos.x - playerPhysics.pos.x;
      const dz = autoPeekAnchorPos.z - playerPhysics.pos.z;
      const dist = Math.hypot(dx, dz);
      if (dist < 14) {
        autoPeekRetracting = false;
        playerPhysics.vel.x = 0;
        playerPhysics.vel.z = 0;
      } else {
        playerPhysics.vel.x = (dx / dist) * 265;
        playerPhysics.vel.z = (dz / dist) * 265;
      }
    }

    for (let s = 0; s < subSteps; s++) {
      playerPhysics.stepSimulation({
        dt: subDt,
        keys,
        viewYaw,
        maxWeaponSpeed: maxWepSpeed,
        cfg,
        colliders: currentMapData.colliders,
        forceAutoStop: shouldAutoStop,
        onJumpCallback: () => soundEngine.playJump(),
        onFootstepCallback: () => soundEngine.playFootstep()
      });
    }
  }
  effects.updateAutoPeekRing(cfg.rage.autoPeek && !!autoPeekAnchorPos, autoPeekAnchorPos);

  // 3. Fire Ragebot Auto-Fire, Knifebot, Manual Fire, or Double-Tap 2nd Shot
  let didShootThisTick = false;

  if (pendingDoubleTapShot) {
    pendingDoubleTapShot.delay -= rawDt;
    if (pendingDoubleTapShot.delay <= 0) {
      pendingDoubleTapShot = null;
      const dtCandidate = ragebot.scanTargets({
        eyeOrigin: new THREE.Vector3(playerPhysics.pos.x, playerPhysics.getEyeY(), playerPhysics.pos.z),
        viewYaw,
        viewPitch,
        weapon: currentWeapon,
        playerPhysics,
        isScoped,
        bots,
        colliders: currentMapData.colliders,
        cfg
      });
      if (dtCandidate) {
        executePlayerShot(dtCandidate, true);
        didShootThisTick = true;
      }
    }
  }

  if (fireCooldown <= 0 && reloadTimer <= 0 && !mainLobby.isVisible && playerAlive && freezeTimeRemaining <= 0) {
    if (rageCandidate && ((cfg.rage.enabled && cfg.rage.autoFire) || (currentWeapon.isMelee && cfg.misc.knifebot))) {
      const currentHc = ragebot.computeHitchance(currentWeapon, playerPhysics, isScoped);
      if (currentHc >= cfg.rage.hitchance || !playerPhysics.onGround || currentWeapon.isMelee) {
        didShootThisTick = executePlayerShot(rageCandidate, false);
      }
    } else if (keys.mouse1 && (currentWeapon.auto || cfg.misc.autoWeapons)) {
      didShootThisTick = executePlayerShot(rageCandidate, false);
    }
  }

  // 4. Update Anti-Aim (Spinbot / Pitch 89° / Body Yaw Desync / Fake-Lag)
  playerAA.update({
    cfg,
    viewYaw,
    viewPitch,
    playerPos: playerPhysics.pos,
    playerSpeed: playerPhysics.getSpeed2D(),
    onGround: playerPhysics.onGround,
    crouchAmount: playerPhysics.crouchAmount,
    bots,
    colliders: currentMapData.colliders,
    didShootThisTick
  });

  // 5. Update Local Player 3D Models (Real Model + Desync Ghost + Fake-Lag Ghost)
  const showThirdperson = cfg.visuals.thirdperson || mainLobby.isVisible;
  localRealRig.root.visible = showThirdperson && playerAlive;

  if (showThirdperson && playerAlive) {
    localRealRig.updatePose({
      pos: playerPhysics.pos,
      realYaw: playerAA.realYaw,
      lowerYaw: playerAA.fakeYaw,
      pitch: playerAA.cmdPitch,
      crouchAmount: playerPhysics.crouchAmount,
      speed: playerPhysics.getSpeed2D(),
      walkCycle: playerPhysics.walkCycle,
      slideWalk: cfg.antiaim.enabled && cfg.antiaim.slideWalk,
      onGround: playerPhysics.onGround
    });
    localRealRig.updateChams(cfg.visuals);

    // Desync Fake Ghost Chams Model
    if (cfg.antiaim.enabled && cfg.antiaim.bodyYaw && cfg.visuals.desyncGhostChams) {
      desyncGhostRig.root.visible = true;
      desyncGhostRig.ghostMat.color.set(cfg.visuals.desyncGhostColor);
      desyncGhostRig.ghostMat.emissive.set(cfg.visuals.desyncGhostColor);
      const offsetPos = {
        x: playerPhysics.pos.x + Math.sin(playerAA.fakeYaw) * 8,
        y: playerPhysics.pos.y,
        z: playerPhysics.pos.z + Math.cos(playerAA.fakeYaw) * 8
      };
      desyncGhostRig.updatePose({
        pos: offsetPos,
        realYaw: playerAA.fakeYaw,
        lowerYaw: playerAA.fakeYaw,
        pitch: playerAA.cmdPitch,
        crouchAmount: playerPhysics.crouchAmount,
        speed: 0,
        walkCycle: 0,
        onGround: playerPhysics.onGround
      });
    } else {
      desyncGhostRig.root.visible = false;
    }

    // Fake-Lag Choked Packet Ghost Chams Model
    const sent = playerAA.lastSentState;
    const lagDist = Math.hypot(
      sent.pos.x - playerPhysics.pos.x,
      sent.pos.z - playerPhysics.pos.z
    );
    if (
      cfg.antiaim.enabled &&
      cfg.antiaim.fakeLag &&
      cfg.antiaim.fakeLagMode !== 'off' &&
      cfg.visuals.fakeLagGhostChams &&
      lagDist > 6
    ) {
      fakeLagGhostRig.root.visible = true;
      fakeLagGhostRig.ghostMat.color.set(cfg.visuals.fakeLagGhostColor);
      fakeLagGhostRig.ghostMat.emissive.set(cfg.visuals.fakeLagGhostColor);
      fakeLagGhostRig.updatePose({
        pos: sent.pos,
        realYaw: sent.realYaw,
        lowerYaw: sent.lowerYaw,
        pitch: sent.pitch,
        crouchAmount: sent.crouchAmount,
        speed: 0,
        walkCycle: 0,
        onGround: true
      });
    } else {
      fakeLagGhostRig.root.visible = false;
    }
  } else {
    desyncGhostRig.root.visible = false;
    fakeLagGhostRig.root.visible = false;
  }

  // 6. Update Bots
  for (const bot of bots) {
    bot.update({
      dt: rawDt,
      cfg,
      playerPhysics,
      playerAA,
      colliders: currentMapData.colliders,
      botSpawns: currentMapData.botSpawns,
      botWaypoints: currentMapData.botWaypoints,
      allowRespawn: matchConfig.gameMode !== 'rounds',
      isFreezeTime: freezeTimeRemaining > 0 || mainLobby.isVisible,
      onBotShoot: (shooterBot, botEye, targetAim, sim) => {
        soundEngine.playWeaponFire(shooterBot.weaponId, true);
        if (cfg.visuals.bulletTracers) {
          effects.addTracer(botEye, targetAim, cfg.visuals.enemyTracerColor, 0.85);
        }

        const distToReal = Math.hypot(
          targetAim.x - playerPhysics.pos.x,
          targetAim.z - playerPhysics.pos.z
        );
        const hitReal = distToReal < 13.5;

        if (hitReal && !cfg.misc.godmode && playerAlive) {
          const dmg = Math.max(4, Math.round(sim.damage * 0.22));
          playerHp -= dmg;
          if (playerHp <= 0) {
            playerDeaths++;
            shooterBot.kills++;
            addKillfeed(shooterBot.name, cfg.misc.playerName, shooterBot.weapon.shortName, false, sim.wallbanged);
            if (matchConfig.gameMode === 'rounds') {
              playerAlive = false;
              playerHp = 0;
            } else {
              playerHp = matchConfig.startingHp || 100;
              const sp = currentMapData.playerSpawns[playerDeaths % currentMapData.playerSpawns.length];
              playerPhysics.reset(sp);
            }
          }
          updateHudDOM();
        } else if (!hitReal && cfg.antiaim.enabled && Math.random() < 0.35) {
          addEventLog(
            `<span class="log-tag">[gamesense]</span> Missed shot by <b>${shooterBot.name}</b> due to your anti-aim (${playerAA.desyncDeltaDeg}° desync)`
          );
        }
      }
    });
  }

  // 7. Update Camera Position
  const eyePos = new THREE.Vector3(
    playerPhysics.pos.x,
    playerPhysics.getEyeY(),
    playerPhysics.pos.z
  );

  const targetFov = isScoped ? 42 : cfg.visuals.cameraFov;
  if (Math.abs(camera.fov - targetFov) > 0.2) {
    camera.fov += (targetFov - camera.fov) * 0.25;
    camera.updateProjectionMatrix();
  }

  if (mainLobby.isVisible) {
    // Cinematic Orbit Camera around our 3D Agent Model in the Main Menu!
    const orbitAngle = now * 0.00045;
    camera.position.set(
      playerPhysics.pos.x + Math.sin(orbitAngle) * 115,
      playerPhysics.pos.y + 58,
      playerPhysics.pos.z + Math.cos(orbitAngle) * 115
    );
    camera.lookAt(playerPhysics.pos.x, playerPhysics.pos.y + 42, playerPhysics.pos.z);
  } else {
    const lookVector = new THREE.Vector3(
      -Math.sin(viewYaw) * Math.cos(viewPitch),
      Math.sin(viewPitch),
      -Math.cos(viewYaw) * Math.cos(viewPitch)
    ).normalize();

    if (cfg.visuals.thirdperson) {
      const tpDist = cfg.visuals.thirdpersonDist;
      const backDir = lookVector.clone().negate();
      const tracedBack = traceWorldRay(eyePos, backDir, currentMapData.colliders, tpDist);
      const actualDist = Math.max(18, eyePos.distanceTo(tracedBack) - 8);

      camera.position.copy(eyePos).addScaledVector(backDir, actualDist);
      camera.position.y += 8;
      camera.lookAt(eyePos.clone().addScaledVector(lookVector, 120));
    } else {
      camera.position.copy(eyePos);
      camera.lookAt(eyePos.clone().addScaledVector(lookVector, 100));
    }
  }

  fpViewmodel.update(
    rawDt,
    playerPhysics.getSpeed2D(),
    playerPhysics.onGround,
    isScoped,
    cfg.visuals.thirdperson || mainLobby.isVisible
  );

  // 8. World Visual Modulations (Nightmode & ASUS Transparent Walls)
  const night = cfg.visuals.nightmode || mapBuilder.currentMap === 'cs_office_cyber';
  const targetSky = night ? new THREE.Color(0x070b16) : mapBuilder.skyColor;
  scene.background.lerp(targetSky, 0.1);
  scene.fog.color.copy(scene.background);
  hemiLight.intensity = night ? 0.38 : 0.85;
  sunLight.intensity = night ? 0.45 : 1.15;

  const wallOpacity = cfg.visuals.asusWalls / 100;
  for (const wm of mapBuilder.wallMeshes) {
    if (wm.material) {
      wm.material.opacity = wallOpacity;
    }
  }

  // 9. Render 3D Scene & 2D ESP HUD
  effects.update(rawDt, cfg.visuals.removeSmoke);
  renderer.render(scene, camera);

  if (!mainLobby.isVisible && !cfg.misc.hideFromObs) {
    espHud.render({
      dt: rawDt,
      camera,
      cfg,
      playerPhysics,
      playerAA,
      ragebot,
      bots,
      effects,
      isScoped,
      viewYaw,
      colliders: currentMapData ? currentMapData.colliders : []
    });
  } else {
    espHud.ctx.clearRect(0, 0, espHud.canvas.width, espHud.canvas.height);
  }
}

requestAnimationFrame(animate);
