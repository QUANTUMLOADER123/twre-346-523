import * as THREE from 'three';
import { CharacterRig } from './models.js';

export class EffectsManager {
  constructor(scene) {
    this.scene = scene;
    this.tracers = [];
    this.impacts = [];
    this.sparks = [];
    this.shotGhosts = [];
    this.smokes = [];
    this.worldHits = [];

    // 3D Auto-Peek Ground Circle Anchor
    const ringGeom = new THREE.RingGeometry(14, 18, 32);
    ringGeom.rotateX(-Math.PI / 2);
    this.autoPeekMat = new THREE.MeshBasicMaterial({
      color: 0xa8cf0a,
      side: THREE.DoubleSide,
      transparent: true,
      opacity: 0.85,
      depthWrite: false
    });
    this.autoPeekMesh = new THREE.Mesh(ringGeom, this.autoPeekMat);
    this.autoPeekMesh.visible = false;
    this.scene.add(this.autoPeekMesh);
  }

  updateAutoPeekRing(active, pos) {
    this.autoPeekMesh.visible = active && !!pos;
    if (this.autoPeekMesh.visible) {
      this.autoPeekMesh.position.set(pos.x, pos.y + 0.8, pos.z);
      const scale = 1 + Math.sin(performance.now() * 0.008) * 0.08;
      this.autoPeekMesh.scale.set(scale, 1, scale);
    }
  }

  addTracer(startPos, endPos, colorHex = '#95b806', duration = 1.1) {
    const dir = new THREE.Vector3().subVectors(endPos, startPos);
    const len = dir.length();
    if (len < 1) return;

    const geom = new THREE.CylinderGeometry(0.42, 0.42, len, 6, 1, false);
    geom.translate(0, len / 2, 0);
    geom.rotateX(Math.PI / 2);

    const mat = new THREE.MeshBasicMaterial({
      color: new THREE.Color(colorHex),
      transparent: true,
      opacity: 0.92,
      depthWrite: false
    });

    const mesh = new THREE.Mesh(geom, mat);
    mesh.position.copy(startPos);
    mesh.lookAt(endPos);
    this.scene.add(mesh);

    this.tracers.push({ mesh, geom, mat, age: 0, duration });
  }

  addImpactBox(pos, isServer = false, duration = 2.0) {
    const geom = new THREE.BoxGeometry(3.2, 3.2, 3.2);
    const mat = new THREE.MeshBasicMaterial({
      color: isServer ? 0x3b82f6 : 0xff2a55,
      transparent: true,
      opacity: 0.68,
      depthTest: false
    });
    const mesh = new THREE.Mesh(geom, mat);
    mesh.position.copy(pos);
    this.scene.add(mesh);

    this.impacts.push({ mesh, geom, mat, age: 0, duration });
  }

  addHitSparks(pos, isHeadshot = false) {
    const count = isHeadshot ? 12 : 6;
    const color = isHeadshot ? 0xff2a85 : 0xa8cf0a;
    for (let i = 0; i < count; i++) {
      const geom = new THREE.BoxGeometry(1.4, 1.4, 1.4);
      const mat = new THREE.MeshBasicMaterial({
        color,
        transparent: true,
        opacity: 0.95
      });
      const mesh = new THREE.Mesh(geom, mat);
      mesh.position.copy(pos);
      const vel = new THREE.Vector3(
        (Math.random() - 0.5) * 140,
        Math.random() * 110 + 20,
        (Math.random() - 0.5) * 140
      );
      this.scene.add(mesh);
      this.sparks.push({ mesh, geom, mat, vel, age: 0, duration: 0.45 });
    }
  }

  addShotGhost(bot, colorHex = '#a8cf0a', duration = 1.8) {
    const ghost = new CharacterRig({
      isEnemy: false,
      isGhost: true,
      ghostColor: colorHex,
      ghostOpacity: 0.5,
      wireframe: true
    });
    ghost.updatePose({
      pos: bot.physics.pos,
      realYaw: bot.realYaw,
      lowerYaw: bot.fakeYaw,
      pitch: bot.pitch,
      crouchAmount: bot.physics.crouchAmount,
      speed: 0,
      walkCycle: 0,
      onGround: true
    });
    this.scene.add(ghost.root);
    this.shotGhosts.push({ ghost, age: 0, duration });
  }

  addSmokeCloud(pos, duration = 14.0) {
    const geom = new THREE.SphereGeometry(125, 18, 14);
    const mat = new THREE.MeshStandardMaterial({
      color: 0x94a3b8,
      roughness: 0.95,
      transparent: true,
      opacity: 0.88,
      depthWrite: false
    });
    const mesh = new THREE.Mesh(geom, mat);
    mesh.position.set(pos.x, pos.y + 48, pos.z);
    this.scene.add(mesh);
    this.smokes.push({ mesh, geom, mat, age: 0, duration });
  }

  addWorldHit(pos, damage, isHeadshot = false, isWallbang = false) {
    this.worldHits.push({
      pos: pos.clone(),
      damage,
      isHeadshot,
      isWallbang,
      age: 0,
      duration: 1.35
    });
  }

  update(dt, removeSmoke = false) {
    for (let i = this.tracers.length - 1; i >= 0; i--) {
      const t = this.tracers[i];
      t.age += dt;
      if (t.age >= t.duration) {
        this.scene.remove(t.mesh);
        t.geom.dispose();
        t.mat.dispose();
        this.tracers.splice(i, 1);
      } else {
        t.mat.opacity = 0.92 * (1 - t.age / t.duration);
      }
    }

    for (let i = this.impacts.length - 1; i >= 0; i--) {
      const imp = this.impacts[i];
      imp.age += dt;
      if (imp.age >= imp.duration) {
        this.scene.remove(imp.mesh);
        imp.geom.dispose();
        imp.mat.dispose();
        this.impacts.splice(i, 1);
      } else {
        imp.mat.opacity = 0.68 * (1 - imp.age / imp.duration);
      }
    }

    for (let i = this.sparks.length - 1; i >= 0; i--) {
      const s = this.sparks[i];
      s.age += dt;
      if (s.age >= s.duration) {
        this.scene.remove(s.mesh);
        s.geom.dispose();
        s.mat.dispose();
        this.sparks.splice(i, 1);
      } else {
        s.vel.y -= 420 * dt;
        s.mesh.position.addScaledVector(s.vel, dt);
        s.mat.opacity = 1 - s.age / s.duration;
      }
    }

    for (let i = this.shotGhosts.length - 1; i >= 0; i--) {
      const sg = this.shotGhosts[i];
      sg.age += dt;
      if (sg.age >= sg.duration) {
        this.scene.remove(sg.ghost.root);
        this.shotGhosts.splice(i, 1);
      } else {
        sg.ghost.ghostMat.opacity = 0.5 * (1 - sg.age / sg.duration);
      }
    }

    for (let i = this.smokes.length - 1; i >= 0; i--) {
      const sm = this.smokes[i];
      sm.age += dt;
      if (sm.age >= sm.duration) {
        this.scene.remove(sm.mesh);
        sm.geom.dispose();
        sm.mat.dispose();
        this.smokes.splice(i, 1);
      } else {
        // If "Remove smoke (Wireframe)" is enabled in Skeet Visuals, render smoke as faint wireframe!
        sm.mat.wireframe = removeSmoke;
        sm.mat.opacity = removeSmoke ? 0.22 : 0.88 * Math.min(1, (sm.duration - sm.age) / 2.0);
      }
    }

    for (let i = this.worldHits.length - 1; i >= 0; i--) {
      const wh = this.worldHits[i];
      wh.age += dt;
      if (wh.age >= wh.duration) {
        this.worldHits.splice(i, 1);
      }
    }
  }

  clearAll() {
    for (const t of this.tracers) {
      this.scene.remove(t.mesh);
      t.geom.dispose();
      t.mat.dispose();
    }
    this.tracers = [];
    for (const imp of this.impacts) {
      this.scene.remove(imp.mesh);
      imp.geom.dispose();
      imp.mat.dispose();
    }
    this.impacts = [];
    for (const s of this.sparks) {
      this.scene.remove(s.mesh);
      s.geom.dispose();
      s.mat.dispose();
    }
    this.sparks = [];
    for (const sg of this.shotGhosts) {
      this.scene.remove(sg.ghost.root);
    }
    this.shotGhosts = [];
    for (const sm of this.smokes) {
      this.scene.remove(sm.mesh);
      sm.geom.dispose();
      sm.mat.dispose();
    }
    this.smokes = [];
    this.worldHits = [];
    this.autoPeekMesh.visible = false;
  }
}
