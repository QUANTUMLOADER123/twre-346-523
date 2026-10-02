// Complete CS:GO Weapons, Skins, Match Lobby Settings & Gamesense (Skeet.cc) Configuration

export const WEAPONS = {
  ssg08: {
    id: 'ssg08',
    slot: 1,
    category: 'Rifles / Snipers',
    name: 'SSG 08 (Scout)',
    shortName: 'SSG 08',
    damage: 88,
    headMultiplier: 4.0,
    chestMultiplier: 1.0,
    stomachMultiplier: 1.25,
    legMultiplier: 0.75,
    fireRate: 1.10,
    recoil: 0.032,
    baseSpread: 0.002,
    moveSpread: 0.032,
    airSpread: 0.010, // Jump-scout accurate at jump apex
    scopedSpread: 0.0004,
    maxSpeed: 230,
    scopedSpeed: 230,
    penetrationPower: 2.5,
    auto: false,
    hasScope: true,
    magSize: 10,
    reserveAmmo: 90,
    reloadTime: 1.9,
    canDoubleTap: false
  },
  scar20: {
    id: 'scar20',
    slot: 2,
    category: 'Rifles / Snipers',
    name: 'SCAR-20 (Auto-Sniper)',
    shortName: 'SCAR-20',
    damage: 80,
    headMultiplier: 4.0,
    chestMultiplier: 1.0,
    stomachMultiplier: 1.25,
    legMultiplier: 0.75,
    fireRate: 0.24,
    recoil: 0.024,
    baseSpread: 0.003,
    moveSpread: 0.058,
    airSpread: 0.11,
    scopedSpread: 0.0007,
    maxSpeed: 215,
    scopedSpeed: 120,
    penetrationPower: 2.5,
    auto: true,
    hasScope: true,
    magSize: 20,
    reserveAmmo: 90,
    reloadTime: 2.3,
    canDoubleTap: true
  },
  awp: {
    id: 'awp',
    slot: 3,
    category: 'Rifles / Snipers',
    name: 'AWP',
    shortName: 'AWP',
    damage: 115,
    headMultiplier: 4.0,
    chestMultiplier: 1.1,
    stomachMultiplier: 1.25,
    legMultiplier: 0.75,
    fireRate: 1.40,
    recoil: 0.058,
    baseSpread: 0.014,
    moveSpread: 0.088,
    airSpread: 0.16,
    scopedSpread: 0.0002,
    maxSpeed: 200,
    scopedSpeed: 100,
    penetrationPower: 3.0,
    auto: false,
    hasScope: true,
    magSize: 5,
    reserveAmmo: 30,
    reloadTime: 2.6,
    canDoubleTap: false
  },
  ak47: {
    id: 'ak47',
    slot: 4,
    category: 'Rifles / Snipers',
    name: 'AK-47',
    shortName: 'AK-47',
    damage: 36,
    headMultiplier: 4.0,
    chestMultiplier: 1.0,
    stomachMultiplier: 1.25,
    legMultiplier: 0.75,
    fireRate: 0.10,
    recoil: 0.022,
    baseSpread: 0.0032,
    moveSpread: 0.052,
    airSpread: 0.10,
    scopedSpread: 0.0032,
    maxSpeed: 215,
    scopedSpeed: 215,
    penetrationPower: 2.0,
    auto: true,
    hasScope: false,
    magSize: 30,
    reserveAmmo: 90,
    reloadTime: 2.0,
    canDoubleTap: true
  },
  m4a1s: {
    id: 'm4a1s',
    slot: 5,
    category: 'Rifles / Snipers',
    name: 'M4A1-S (Silenced)',
    shortName: 'M4A1-S',
    damage: 38,
    headMultiplier: 3.5,
    chestMultiplier: 1.0,
    stomachMultiplier: 1.25,
    legMultiplier: 0.75,
    fireRate: 0.095,
    recoil: 0.014,
    baseSpread: 0.0022,
    moveSpread: 0.042,
    airSpread: 0.085,
    scopedSpread: 0.0022,
    maxSpeed: 225,
    scopedSpeed: 225,
    penetrationPower: 1.8,
    auto: true,
    hasScope: false,
    magSize: 20,
    reserveAmmo: 80,
    reloadTime: 1.9,
    canDoubleTap: true
  },
  deagle: {
    id: 'deagle',
    slot: 6,
    category: 'Pistols',
    name: 'Desert Eagle',
    shortName: 'DEAGLE',
    damage: 63,
    headMultiplier: 3.9,
    chestMultiplier: 1.0,
    stomachMultiplier: 1.25,
    legMultiplier: 0.75,
    fireRate: 0.36,
    recoil: 0.038,
    baseSpread: 0.0038,
    moveSpread: 0.042,
    airSpread: 0.085,
    scopedSpread: 0.0038,
    maxSpeed: 230,
    scopedSpeed: 230,
    penetrationPower: 2.0,
    auto: false,
    hasScope: false,
    magSize: 7,
    reserveAmmo: 35,
    reloadTime: 1.8,
    canDoubleTap: true
  },
  r8: {
    id: 'r8',
    slot: 7,
    category: 'Pistols',
    name: 'R8 Revolver',
    shortName: 'R8 REV',
    damage: 86,
    headMultiplier: 3.2,
    chestMultiplier: 1.0,
    stomachMultiplier: 1.25,
    legMultiplier: 0.8,
    fireRate: 0.48,
    recoil: 0.04,
    baseSpread: 0.002,
    moveSpread: 0.018,
    airSpread: 0.035,
    scopedSpread: 0.002,
    maxSpeed: 220,
    scopedSpeed: 220,
    penetrationPower: 2.2,
    auto: false,
    hasScope: false,
    magSize: 8,
    reserveAmmo: 24,
    reloadTime: 2.0,
    canDoubleTap: false
  },
  zeus: {
    id: 'zeus',
    slot: 8,
    category: 'Equipment',
    name: 'Zeus x27 (Taser)',
    shortName: 'ZEUS x27',
    damage: 500,
    headMultiplier: 1.0,
    chestMultiplier: 1.0,
    stomachMultiplier: 1.0,
    legMultiplier: 1.0,
    fireRate: 1.2,
    recoil: 0.01,
    baseSpread: 0.001,
    moveSpread: 0.005,
    airSpread: 0.01,
    scopedSpread: 0.001,
    maxSpeed: 230,
    scopedSpeed: 230,
    penetrationPower: 0,
    auto: false,
    hasScope: false,
    isShortRange: true,
    range: 185,
    magSize: 1,
    reserveAmmo: 5,
    reloadTime: 2.5,
    canDoubleTap: false
  },
  knife: {
    id: 'knife',
    slot: 9,
    category: 'Melee',
    name: 'Karambit',
    shortName: 'KNIFE',
    damage: 65,
    backstabDamage: 180,
    headMultiplier: 1.4,
    chestMultiplier: 1.0,
    stomachMultiplier: 1.1,
    legMultiplier: 1.0,
    fireRate: 0.42,
    recoil: 0.0,
    baseSpread: 0.0,
    moveSpread: 0.0,
    airSpread: 0.0,
    scopedSpread: 0.0,
    maxSpeed: 250, // Full 250 u/s Source knife speed
    scopedSpeed: 250,
    penetrationPower: 0,
    auto: true,
    hasScope: false,
    isMelee: true,
    range: 96,
    magSize: 1,
    reserveAmmo: 0,
    reloadTime: 0,
    canDoubleTap: false
  }
};

export const WEAPON_ORDER = ['ssg08', 'scar20', 'awp', 'ak47', 'm4a1s', 'deagle', 'r8', 'zeus', 'knife'];

export const SKIN_PALETTES = {
  default: { name: 'Default Tactical', primary: 0x2b3644, secondary: 0x1e242b, emissive: 0x000000, metalness: 0.65, roughness: 0.38 },
  sapphire: { name: 'Doppler Sapphire', primary: 0x1d4ed8, secondary: 0x0f172a, emissive: 0x2563eb, metalness: 0.92, roughness: 0.14 },
  ruby: { name: 'Doppler Ruby', primary: 0xbe123c, secondary: 0x1f0910, emissive: 0xe11d48, metalness: 0.92, roughness: 0.14 },
  emerald: { name: 'Gamma Emerald', primary: 0x059669, secondary: 0x062419, emissive: 0x10b981, metalness: 0.92, roughness: 0.14 },
  fade: { name: 'Marble Fade', primary: 0xd946ef, secondary: 0xf59e0b, emissive: 0x9333ea, metalness: 0.88, roughness: 0.18 },
  printstream: { name: 'Printstream Pearl', primary: 0xf8fafc, secondary: 0x111827, emissive: 0x38bdf8, metalness: 0.55, roughness: 0.22 },
  asiimov: { name: 'Asiimov Orange/White', primary: 0xf97316, secondary: 0xf8fafc, emissive: 0x000000, metalness: 0.35, roughness: 0.45 },
  gold: { name: 'Gold Arabesque', primary: 0xeab308, secondary: 0x422006, emissive: 0xca8a04, metalness: 0.95, roughness: 0.16 }
};

// Match & Server Lobby Configuration (Configurable in the Main Menu & In-Game)
export const matchConfig = {
  mapId: 'de_mirage_mid',
  gameMode: 'dm', // 'dm' (Deathmatch) | 'rounds' (5v5 / 1v1 Rounds) | 'hvh_arena' | 'bhop_surf'
  botCount: 5,
  botBehavior: 'legit', // 'legit' | 'hvh' | 'moving' | 'static'
  botDifficulty: 55, // 10 - 100%
  botWeapons: 'all', // 'all' | 'snipers' | 'pistols' | 'knives'
  tickrate: 64, // 64 | 128
  roundTimeSec: 115,
  playerTeam: 'ct', // 'ct' | 't'
  playerModelStyle: 'sas', // 'sas' | 'seal' | 'phoenix' | 'elite'
  startingHp: 100,
  infiniteAmmo: true,
  freeArmor: true,
  friendlyCrosshairStyle: 'classic',
  mouseSensitivity: 2.2
};

// Gamesense.pub / Skeet.cc Cheat Configuration
// Starts CLEAN / OFF by default so the user configures everything themselves in [INSERT]!
export function createCleanConfig() {
  return {
    rage: {
      enabled: false,
      silentAim: true,
      autoFire: false,
      autoWall: false,
      autoScope: false,
      autoStop: false,
      fov: 180,
      minDamage: 50,
      minDamageOverride: 15,
      minDamageOverrideActive: false,
      hitchance: 65,
      preferBaim: false,
      forceBaimActive: false,
      multipoint: false,
      multipointScale: 70,
      hitboxes: {
        head: true,
        chest: true,
        stomach: true,
        arms: false,
        legs: false
      },
      resolver: false,
      doubleTap: false,
      hideShots: false,
      backtrack: false,
      backtrackTicks: 12,
      autoPeek: false,
      autoPeekActive: false
    },

    antiaim: {
      enabled: false,
      pitch: 'none', // 'none' | 'down' (89°) | 'up' (-89°) | 'zero' | 'jitter'
      yawBase: 'at_targets', // 'local' | 'at_targets'
      yawMode: 'backward', // 'backward' | 'spin' (Крутилка) | 'jitter' | 'sideways' | 'random'
      spinSpeed: 22,
      yawOffset: 180,
      jitterMode: 'off', // 'off' | 'offset' | 'center' | '3way' | 'random'
      jitterRange: 60,
      bodyYaw: false,
      bodyYawMode: 'static', // 'static' | 'jitter' | 'sway' | 'opposite'
      desyncLimit: 58,
      inverted: false,
      freestanding: false,
      fakeLag: false,
      fakeLagMode: 'off', // 'off' | 'max' | 'dynamic' | 'fluctuate' | 'break_lc'
      fakeLagLimit: 14,
      fakeLagWhileShooting: false,
      slowWalk: false,
      slowWalkSpeed: 68,
      fakeDuck: false,
      slideWalk: false
    },

    movement: {
      bhop: false,
      autoStrafe: false,
      autoStrafeMode: 'rage', // 'view' | 'directional' | 'rage'
      strafeSmooth: 85,
      zHop: false,
      preSpeed: false,
      duckInAir: false,
      fastStop: false,
      edgeJump: false,
      airStuckMode: 'off', // 'off' | 'hold' | 'toggle'
      airStuckActive: false,
      infiniteStamina: false,
      // Authentic Source Engine Cvars:
      sv_friction: 5.2,
      sv_accelerate: 5.5,
      sv_airaccelerate: 12, // Standard CS:GO 12
      sv_stopspeed: 80,
      sv_gravity: 800
    },

    visuals: {
      thirdperson: false,
      thirdpersonDist: 120,
      cameraFov: 90,
      viewmodelFov: 75,
      // Enemy ESP
      espEnabled: false,
      espBox: false,
      espBoxStyle: 'full', // 'full' | 'corner'
      espBoxColor: '#ffffff',
      espName: false,
      espHealth: false,
      espWeapon: false,
      espSkeleton: false,
      espFlags: false,
      espSnaplines: false,
      espOofArrows: false,
      espMultipointDot: false,
      // 3D Chams
      enemyChams: false,
      enemyChamsXqz: false,
      enemyChamsStyle: 'metallic', // 'flat' | 'metallic' | 'wireframe' | 'textured'
      enemyVisColor: '#95b806', // Skeet green
      enemyXqzColor: '#38bdf8',
      localChams: false,
      localChamsStyle: 'textured',
      localRealColor: '#95b806',
      desyncGhostChams: false,
      desyncGhostColor: '#ff2a85',
      fakeLagGhostChams: false,
      fakeLagGhostColor: '#38bdf8',
      backtrackChams: false,
      shotChams: false,
      removeSmoke: false,
      // World & Effects
      bulletTracers: false,
      tracerColor: '#95b806',
      enemyTracerColor: '#ff4757',
      showImpacts: false,
      hitmarkers: true,
      damageNumbers: true,
      nightmode: false,
      nightmodeIntensity: 65,
      asusWalls: 100,
      removeScopeOverlay: false,
      aaArrows: false,
      velocityGraph: true
    },

    misc: {
      knifebot: false,
      autoWeapons: false, // Hold mouse1 on semi-auto guns
      revealRanks: false,
      autoAccept: true,
      clantagSpammer: false,
      antiUntrusted: true,
      hideFromObs: false,
      hitSound: 'skeet', // 'skeet' | 'neverlose' | 'bell' | 'cod' | 'off'
      eventLogs: true,
      killSay: false,
      watermark: true,
      keybindList: true,
      godmode: false,
      selectedPreset: 'legit',
      playerName: 'YOU'
    },

    skins: {
      knifeModel: 'karambit', // 'karambit' | 'm9' | 'butterfly'
      knifeSkin: 'fade',
      weaponSkin: 'default',
      gloveColor: '#1e293b',
      agentSkin: 'default'
    },

    // Customizable Keybinds for Skeet [KEY] badges
    binds: {
      thirdperson: 'KeyV',
      invertDesync: 'KeyF',
      minDamageOverride: 'KeyZ',
      forceBaim: 'KeyB',
      doubleTap: 'KeyX',
      fakeDuck: 'KeyC',
      zHop: 'KeyT',
      preSpeed: 'KeyG',
      edgeJump: 'KeyE',
      airStuck: 'KeyN',
      autoPeek: 'KeyH'
    }
  };
}

export const cfg = createCleanConfig();

export function resetConfigToClean() {
  const clean = createCleanConfig();
  Object.keys(clean).forEach((k) => {
    Object.assign(cfg[k], clean[k]);
  });
  Object.assign(cfg.rage.hitboxes, clean.rage.hitboxes);
}

export function applyPreset(presetName) {
  if (presetName === 'clean' || presetName === 'legit') {
    resetConfigToClean();
    if (presetName === 'legit') {
      cfg.visuals.espEnabled = true;
      cfg.visuals.espBox = true;
      cfg.visuals.espName = true;
      cfg.visuals.espHealth = true;
      cfg.movement.bhop = true;
    }
    return;
  }

  if (presetName === 'rage_hvh') {
    cfg.rage.enabled = true;
    cfg.rage.silentAim = true;
    cfg.rage.autoFire = true;
    cfg.rage.autoWall = true;
    cfg.rage.autoScope = true;
    cfg.rage.autoStop = true;
    cfg.rage.minDamage = 45;
    cfg.rage.hitchance = 68;
    cfg.rage.multipoint = true;
    cfg.rage.multipointScale = 78;
    cfg.rage.hitboxes = { head: true, chest: true, stomach: true, arms: false, legs: true };
    cfg.rage.preferBaim = true;
    cfg.rage.resolver = true;
    cfg.rage.doubleTap = true;
    cfg.rage.backtrack = true;
    cfg.rage.backtrackTicks = 12;
    cfg.rage.autoPeek = true;

    cfg.antiaim.enabled = true;
    cfg.antiaim.pitch = 'down';
    cfg.antiaim.yawBase = 'at_targets';
    cfg.antiaim.yawMode = 'spin';
    cfg.antiaim.spinSpeed = 24;
    cfg.antiaim.bodyYaw = true;
    cfg.antiaim.bodyYawMode = 'jitter';
    cfg.antiaim.desyncLimit = 58;
    cfg.antiaim.freestanding = true;
    cfg.antiaim.fakeLag = true;
    cfg.antiaim.fakeLagMode = 'dynamic';
    cfg.antiaim.fakeLagLimit = 14;
    cfg.antiaim.slideWalk = true;

    cfg.movement.bhop = true;
    cfg.movement.autoStrafe = true;
    cfg.movement.autoStrafeMode = 'rage';
    cfg.movement.fastStop = true;
    cfg.movement.infiniteStamina = true;
    cfg.movement.sv_airaccelerate = 28;

    cfg.visuals.thirdperson = true;
    cfg.visuals.cameraFov = 98;
    cfg.visuals.espEnabled = true;
    cfg.visuals.espBox = true;
    cfg.visuals.espBoxStyle = 'corner';
    cfg.visuals.espName = true;
    cfg.visuals.espHealth = true;
    cfg.visuals.espWeapon = true;
    cfg.visuals.espSkeleton = true;
    cfg.visuals.espFlags = true;
    cfg.visuals.espOofArrows = true;
    cfg.visuals.espMultipointDot = true;
    cfg.visuals.enemyChams = true;
    cfg.visuals.enemyChamsXqz = true;
    cfg.visuals.desyncGhostChams = true;
    cfg.visuals.fakeLagGhostChams = true;
    cfg.visuals.backtrackChams = true;
    cfg.visuals.shotChams = true;
    cfg.visuals.removeSmoke = true;
    cfg.visuals.bulletTracers = true;
    cfg.visuals.showImpacts = true;
    cfg.visuals.removeScopeOverlay = true;
    cfg.visuals.aaArrows = true;

    cfg.misc.knifebot = true;
    cfg.misc.autoWeapons = true;
    cfg.misc.clantagSpammer = true;
    cfg.misc.killSay = true;
  } else if (presetName === 'scout_jumpshot') {
    applyPreset('rage_hvh');
    cfg.rage.autoStop = false;
    cfg.rage.minDamage = 75;
    cfg.rage.hitchance = 55;
    cfg.rage.doubleTap = false;
    cfg.antiaim.yawMode = 'jitter';
    cfg.antiaim.jitterMode = 'center';
    cfg.antiaim.jitterRange = 68;
    cfg.movement.sv_airaccelerate = 45;
  } else if (presetName === 'bhop_god') {
    resetConfigToClean();
    cfg.movement.bhop = true;
    cfg.movement.autoStrafe = true;
    cfg.movement.autoStrafeMode = 'rage';
    cfg.movement.infiniteStamina = true;
    cfg.movement.sv_airaccelerate = 100;
    cfg.visuals.cameraFov = 105;
  }
}
