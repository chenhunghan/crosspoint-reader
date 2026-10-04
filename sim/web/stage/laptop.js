// The laptop: a big open space-grey notebook whose display is a canvas texture
// (the terminal). Geometry adapted from lns-lab (world.js, "the host: a giant
// open laptop"); the lid is taller here for a 16:10 terminal.
import * as THREE from 'three';
import {RoundedBoxGeometry} from 'three/addons/geometries/RoundedBoxGeometry.js';

const std = (color, o = {}) => new THREE.MeshStandardMaterial({color, roughness: 0.55, metalness: 0.15, ...o});

export function buildLaptop(screenCanvas) {
  const group = new THREE.Group();
  const alu = std('#3a414d', {roughness: 0.36, metalness: 0.6});
  const slab = new THREE.Mesh(new RoundedBoxGeometry(29, 1, 19, 4, 0.45), alu);
  slab.castShadow = slab.receiveShadow = true;
  const inset = new THREE.Mesh(new RoundedBoxGeometry(28.2, 0.04, 18.2, 2, 0.02), std('#2b313c', {roughness: 0.6, metalness: 0.25}));
  inset.position.set(0, 0.51, 0);
  inset.receiveShadow = true;
  const lip = new THREE.Mesh(new THREE.BoxGeometry(4.2, 0.08, 0.3), std('#1b2029', {metalness: 0.4}));
  lip.position.set(0, 0.2, 9.5);
  const hinge = new THREE.Mesh(new THREE.CylinderGeometry(0.34, 0.34, 26, 24), std('#20252e', {metalness: 0.7, roughness: 0.3}));
  hinge.rotation.z = Math.PI / 2;
  hinge.position.set(0, 0.35, -9.35);

  // Lid: 16:10 display (the terminal canvas is 1600x1000).
  const tilt = 0.2, SW = 28.8, DW = SW - 1.3, DH = DW * screenCanvas.height / screenCanvas.width, SH = DH + 1.5;
  const screen = new THREE.Group();
  screen.position.set(0, 0.35, -9.45);
  screen.rotation.x = -tilt;
  const lid = new THREE.Mesh(new RoundedBoxGeometry(SW, SH, 0.36, 3, 0.4), alu);
  lid.position.y = SH / 2;
  lid.castShadow = true;
  const glass = new THREE.Mesh(new RoundedBoxGeometry(SW - 0.5, SH - 0.5, 0.02, 2, 0.3), std('#07090f', {roughness: 0.12, metalness: 0.2}));
  glass.position.set(0, SH / 2, 0.19);
  const tex = new THREE.CanvasTexture(screenCanvas);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 8;
  tex.generateMipmaps = true;
  tex.minFilter = THREE.LinearMipmapLinearFilter;
  const display = new THREE.Mesh(new THREE.PlaneGeometry(DW, DH), new THREE.MeshBasicMaterial({map: tex, toneMapped: false}));
  display.position.set(0, SH / 2 + 0.08, 0.225);
  const cam = new THREE.Mesh(new THREE.CircleGeometry(0.1, 16), new THREE.MeshBasicMaterial({color: '#2a3a55'}));
  cam.position.set(0, SH - 0.32, 0.225);
  screen.add(lid, glass, display, cam);

  // Keyboard: black keys on the deck.
  const keyMat = std('#1b1f27', {roughness: 0.75, metalness: 0.1});
  const keyGeo = new RoundedBoxGeometry(1, 0.08, 1, 1, 0.03);
  const cols = 14, pitch = 1.72, kx0 = -(cols - 1) * pitch / 2, kz0 = -8.1;
  const keys = [];
  for (let r = 0; r < 6; r++) {
    const depth = r === 0 ? 0.75 : 1.42, z = kz0 + (r === 0 ? 0 : 0.95 + (r - 1) * pitch);
    for (let c = 0; c < cols; c++) {
      if (r === 5 && c > 3 && c < 10) { if (c === 4) keys.push([kx0 + 6.5 * pitch, z, pitch * 5 + 1.42, depth]); continue; }
      keys.push([kx0 + c * pitch, z, 1.42, depth]);
    }
  }
  const keyboard = new THREE.InstancedMesh(keyGeo, keyMat, keys.length);
  const m4 = new THREE.Matrix4();
  keys.forEach(([x, z, sx, sz], i) => keyboard.setMatrixAt(i, m4.compose(new THREE.Vector3(x, 0.57, z), new THREE.Quaternion(), new THREE.Vector3(sx, 1, sz))));
  keyboard.receiveShadow = true;
  const well = new THREE.Mesh(new RoundedBoxGeometry(cols * pitch + 0.5, 0.02, 10.9, 2, 0.2), std('#20252e', {roughness: 0.8}));
  well.position.set(0, 0.525, kz0 + 4.95);
  well.receiveShadow = true;
  const pad = new THREE.Mesh(new RoundedBoxGeometry(9.4, 0.03, 5.2, 2, 0.35), std('#444b58', {roughness: 0.22, metalness: 0.5}));
  pad.position.set(0, 0.53, 6.2);
  pad.receiveShadow = true;
  group.add(slab, inset, lip, hinge, screen, well, keyboard, pad);

  // Display centre and normal in group space, for camera framing.
  const centre = new THREE.Vector3(0, SH / 2, 0.3).applyEuler(screen.rotation).add(screen.position);
  const normal = new THREE.Vector3(0, 0, 1).applyEuler(screen.rotation);
  return {group, texture: tex, display, centre, normal, size: {w: DW, h: DH}};
}
