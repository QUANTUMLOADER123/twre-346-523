const ctxProxy = new Proxy({}, { get(t, k) { if (k==='createLinearGradient'||k==='createRadialGradient') return ()=>({addColorStop(){}}); if (k==='getImageData') return (x,y,w,h)=>({data:new Uint8ClampedArray(w*h*4)}); if (k==='measureText') return ()=>({width:10}); if (typeof k==='string'&&/^[a-z]/.test(k)) return ()=>{}; }, set(){return true;} });
globalThis.document = { createElement: () => ({ width: 0, height: 0, getContext: () => ctxProxy, style: {} }) };
const THREE = await import('three');
const M = await import('../src/models.js');
const c = M.createCharacter('T');
for (const pistol of [false, true]) {
  c.setGun(pistol ? 'glock' : 'ak47');
  c.setPose({ pistol, duck: 0, pitch: 0, aimYaw: 0, bodyYaw: 0, moveT: 0, speed: 0 });
  c.root.updateMatrixWorld(true);
  const hand = (n) => new THREE.Vector3(0, -11, 0).applyMatrix4(c.bones[n].matrixWorld);
  const gm = new THREE.Vector3().applyMatrix4(c.bones.gunMount.matrixWorld);
  const hg = new THREE.Vector3(0, 0, -11).applyMatrix4(c.bones.gunMount.matrixWorld);
  console.log(pistol ? 'pistol' : 'rifle', 'R hand', hand('foreArmR').toArray().map(v=>v.toFixed(1)), 'grip', gm.toArray().map(v=>v.toFixed(1)), 'L hand', hand('foreArmL').toArray().map(v=>v.toFixed(1)), 'handguard', hg.toArray().map(v=>v.toFixed(1)));
}
