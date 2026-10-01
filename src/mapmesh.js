// Turns compiled map boxes into merged, textured meshes + lighting + sky
import * as THREE from 'three';
import { mergeGeometries } from 'three/examples/jsm/utils/BufferGeometryUtils.js';
import { getTexture } from './textures.js';

const TEX_SCALE = { crate: 64, dev_orange: 128, dev_gray: 128, dev_floor: 128, dev_white: 128, lava: 192, sandstone: 160, plaster: 192, sand: 160, tiles: 128, concrete: 128, asphalt: 192, metalfloor: 96, brick: 128, barrel: 64 };

function worldUVBox(x0, y0, z0, x1, y1, z1, scale, localCrate = false) {
  const geo = new THREE.BoxGeometry(x1 - x0, y1 - y0, z1 - z0);
  geo.translate((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
  const pos = geo.attributes.position, nrm = geo.attributes.normal, uv = geo.attributes.uv;
  for (let i = 0; i < pos.count; i++) {
    const x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i);
    const nx = Math.abs(nrm.getX(i)), ny = Math.abs(nrm.getY(i));
    let u, v;
    if (localCrate) {
      // each face shows the texture exactly once
      const lx = (x - x0) / (x1 - x0), ly = (y - y0) / (y1 - y0), lz = (z - z0) / (z1 - z0);
      if (nx > 0.5) { u = lz; v = ly; } else if (ny > 0.5) { u = lx; v = lz; } else { u = lx; v = ly; }
    } else {
      if (nx > 0.5) { u = z / scale; v = y / scale; } else if (ny > 0.5) { u = x / scale; v = z / scale; } else { u = x / scale; v = y / scale; }
    }
    uv.setXY(i, u, v);
  }
  return geo;
}

export function buildMapScene(compiled, map, scene) {
  const root = new THREE.Group(); root.name = 'map';
  const byMat = new Map();
  const add = (mat, geo) => { if (!byMat.has(mat)) byMat.set(mat, []); byMat.get(mat).push(geo); };
  const barrels = [];
  for (const b of compiled.boxes) {
    if (!b.mat) continue;
    const [x0, y0, z0] = b.min, [x1, y1, z1] = b.max;
    if (b.kind === 'crate') {
      const n = b.stack || 1;
      for (let i = 0; i < n; i++) {
        const inset = i > 0 ? 3 : 0;
        add(b.mat + '#crate', worldUVBox(x0 + inset, y0 + i * 64, z0 + inset, x1 - inset, y0 + (i + 1) * 64, z1 - inset, 64, true));
      }
    } else if (b.kind === 'barrel') {
      barrels.push(b);
    } else {
      add(b.mat, worldUVBox(x0, y0, z0, x1, y1, z1, TEX_SCALE[b.mat] || 128));
    }
  }
  const materials = [];
  for (const [key, geos] of byMat) {
    const name = key.split('#')[0];
    const tex = getTexture(name);
    const merged = mergeGeometries(geos, false);
    geos.forEach(g => g.dispose());
    const isLava = name === 'lava';
    const mat = new THREE.MeshStandardMaterial({
      map: tex, roughness: name.startsWith('container') || name === 'metalfloor' ? 0.55 : 0.92, metalness: name.startsWith('container') || name === 'metalfloor' ? 0.35 : 0.0,
      emissive: isLava ? 0xff4400 : 0x000000, emissiveMap: isLava ? tex : null, emissiveIntensity: isLava ? 1.4 : 0,
    });
    mat.userData.baseColor = mat.color.clone();
    materials.push(mat);
    const mesh = new THREE.Mesh(merged, mat);
    mesh.castShadow = !isLava; mesh.receiveShadow = true;
    mesh.userData.lava = isLava;
    root.add(mesh);
  }
  if (barrels.length) {
    const geos = barrels.map(b => {
      const g = new THREE.CylinderGeometry(21, 21, 46, 16);
      g.translate((b.min[0] + b.max[0]) / 2, b.min[1] + 23, (b.min[2] + b.max[2]) / 2);
      return g;
    });
    const mat = new THREE.MeshStandardMaterial({ map: getTexture('barrel'), roughness: 0.6, metalness: 0.3 });
    mat.userData.baseColor = mat.color.clone();
    materials.push(mat);
    const mesh = new THREE.Mesh(mergeGeometries(geos), mat); mesh.castShadow = mesh.receiveShadow = true; root.add(mesh);
  }

  // sky dome
  const skyMat = new THREE.ShaderMaterial({
    side: THREE.BackSide, depthWrite: false, fog: false,
    uniforms: { top: { value: new THREE.Color(map.sky.top) }, bottom: { value: new THREE.Color(map.sky.bottom) }, sunDir: { value: new THREE.Vector3(...map.sunDir).normalize() }, night: { value: 0 } },
    vertexShader: `varying vec3 vDir; void main(){ vDir = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); gl_Position.z = gl_Position.w; }`,
    fragmentShader: `uniform vec3 top; uniform vec3 bottom; uniform vec3 sunDir; uniform float night; varying vec3 vDir;
      void main(){ float h = clamp(vDir.y*1.4+0.15,0.0,1.0); vec3 c = mix(bottom, top, pow(h,0.8));
        float s = max(dot(normalize(vDir), sunDir),0.0); c += vec3(1.0,0.9,0.7)*pow(s,350.0)*2.0 + vec3(1.0,0.8,0.5)*pow(s,8.0)*0.25;
        c = mix(c, c*vec3(0.12,0.14,0.25), night);
        gl_FragColor = vec4(c,1.0); }`,
  });
  const sky = new THREE.Mesh(new THREE.SphereGeometry(20000, 32, 16), skyMat);
  sky.frustumCulled = false; sky.renderOrder = -10;
  root.add(sky);

  // lights
  const hemi = new THREE.HemisphereLight(map.sky.top, 0x6b5a45, map.sky.hemiI);
  root.add(hemi);
  const amb = new THREE.AmbientLight(0xffffff, 0.25); root.add(amb);
  const sun = new THREE.DirectionalLight(map.sky.sun, map.sky.sunI);
  const b = compiled.bounds;
  const cx = (b.x0 + b.x1) / 2, cz = (b.z0 + b.z1) / 2;
  const ext = Math.max(b.x1 - b.x0, b.z1 - b.z0) / 2 + 200;
  const sd = new THREE.Vector3(...map.sunDir).normalize();
  sun.position.set(cx + sd.x * 4000, sd.y * 4000, cz + sd.z * 4000);
  sun.target.position.set(cx, 0, cz);
  sun.castShadow = true;
  sun.shadow.mapSize.set(4096, 4096);
  const sc = sun.shadow.camera;
  sc.left = -ext; sc.right = ext; sc.top = ext; sc.bottom = -ext; sc.near = 100; sc.far = 9000;
  sun.shadow.bias = -0.0004; sun.shadow.normalBias = 1.5;
  root.add(sun); root.add(sun.target);
  scene.fog = new THREE.Fog(map.sky.fog, 1500, map.sky.fogFar);
  scene.add(root);
  return { root, materials, sky, skyMat, hemi, sun, amb, base: { hemi: map.sky.hemiI, sun: map.sky.sunI, amb: 0.25, fog: new THREE.Color(map.sky.fog) } };
}

// top-down radar image
export function buildRadarImage(map, compiled) {
  const g = map.grid;
  const px = 4;
  const c = document.createElement('canvas'); c.width = g.w * px; c.height = g.h * px;
  const ctx = c.getContext('2d');
  for (let r = 0; r < g.h; r++) for (let col = 0; col < g.w; col++) {
    const k = g.at(col, r);
    let color;
    if (k.o === 'wall') color = 'rgba(20,22,26,0.95)';
    else if (k.lava) color = 'rgba(200,60,10,0.8)';
    else if (k.o === 'crate' || k.o === 'container' || k.o === 'half' || k.o === 'barrel') color = 'rgba(120,110,95,0.95)';
    else { const v = Math.max(0, Math.min(255, 140 + k.f)); color = `rgba(${v},${v},${v + 8},0.85)`; }
    if (k.c != null && !k.o) { const v = 105 + k.f / 2; color = `rgba(${v},${v},${v},0.85)`; }
    ctx.fillStyle = color; ctx.fillRect(col * px, r * px, px, px);
  }
  return { canvas: c, px, cs: 64, x0: compiled.bounds.x0, z0: compiled.bounds.z0 };
}
