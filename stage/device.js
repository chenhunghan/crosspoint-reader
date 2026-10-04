// Metalio E-Ink 4: a portrait slab (≈72 × 125 × 9 mm, 1 unit = 1 cm) with a
// matte white body, light grey front bezel, recessed 480x800 e-ink panel, an
// orange HOME disc and two grey pills under the screen, and three orange keys
// on the right edge (BOOT, volume rocker, POWER). Buttons are separate meshes
// so the stage can raycast them and animate presses.
import * as THREE from 'three';
import {RoundedBoxGeometry} from 'three/addons/geometries/RoundedBoxGeometry.js';
import {KEY} from './sim-host.js';

export const DEV = {w: 7.2, h: 12.5, d: 0.9, r: 0.8};

function rrectPath(path, w, h, r, cy = 0) {
  const x = -w / 2, y = cy - h / 2;
  path.moveTo(x + r, y);
  path.lineTo(x + w - r, y);
  path.quadraticCurveTo(x + w, y, x + w, y + r);
  path.lineTo(x + w, y + h - r);
  path.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  path.lineTo(x + r, y + h);
  path.quadraticCurveTo(x, y + h, x, y + h - r);
  path.lineTo(x, y + r);
  path.quadraticCurveTo(x, y, x + r, y);
  return path;
}
const rrect = (w, h, r) => rrectPath(new THREE.Shape(), w, h, r);

// Extruded rounded rectangle centred on z = 0, total thickness d (incl. bevel b).
function slabGeo(w, h, r, d, b, holes = []) {
  const shape = rrect(w - 2 * b, h - 2 * b, Math.max(0.01, r - b));
  for (const hole of holes) shape.holes.push(hole);
  const g = new THREE.ExtrudeGeometry(shape, {depth: Math.max(0.001, d - 2 * b), bevelEnabled: b > 0, bevelThickness: b, bevelSize: b, bevelSegments: 5, curveSegments: 28});
  g.translate(0, 0, -(d - 2 * b) / 2);
  return g;
}

export function buildDevice(screenCanvas) {
  const group = new THREE.Group();      // the device; origin = body centre
  const {w: W, h: H, d: D, r: R} = DEV;
  const FRONT = D / 2;

  const bodyMat = new THREE.MeshPhysicalMaterial({color: '#f4f4f1', roughness: 0.62, metalness: 0, clearcoat: 0.15, clearcoatRoughness: 0.6, sheen: 0.2});
  const bezelMat = new THREE.MeshPhysicalMaterial({color: '#dedfdc', roughness: 0.7, metalness: 0});
  const orange = new THREE.MeshPhysicalMaterial({color: '#f2762e', roughness: 0.42, clearcoat: 0.35, clearcoatRoughness: 0.35});
  const pillMat = new THREE.MeshPhysicalMaterial({color: '#4b4e54', roughness: 0.5, clearcoat: 0.2});
  const darkMat = new THREE.MeshStandardMaterial({color: '#202226', roughness: 0.8});

  const body = new THREE.Mesh(slabGeo(W, H, R, D, 0.16), bodyMat);
  body.castShadow = body.receiveShadow = true;
  group.add(body);

  // Screen: active area 480:800, upper part of the face.
  const SCR_W = 5.3, SCR_H = SCR_W * 800 / 480, SCR_Y = H / 2 - 0.88 - SCR_H / 2;
  const HOLE_W = SCR_W + 0.26, HOLE_H = SCR_H + 0.26;

  // Bezel: light grey panel inset from the white rim, with a window for the panel.
  const hole = rrectPath(new THREE.Path(), HOLE_W, HOLE_H, 0.1, SCR_Y);
  const bezel = new THREE.Mesh(slabGeo(W - 0.34, H - 0.34, R - 0.17, 0.06, 0.025, [hole]), bezelMat);
  bezel.position.z = FRONT - 0.005;
  bezel.receiveShadow = true;
  group.add(bezel);

  // Panel: inactive border, then the active matte paper area with the simulator canvas.
  const border = new THREE.Mesh(new THREE.PlaneGeometry(HOLE_W, HOLE_H), new THREE.MeshStandardMaterial({color: '#c9c7c0', roughness: 0.95}));
  border.position.set(0, SCR_Y, FRONT + 0.004);
  group.add(border);
  const tex = new THREE.CanvasTexture(screenCanvas);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 8;
  tex.minFilter = THREE.LinearMipmapLinearFilter;
  // E-ink is a hard pixel grid: magnify with nearest-neighbour, not blur.
  tex.magFilter = THREE.NearestFilter;
  const screenMat = new THREE.MeshBasicMaterial({map: tex, toneMapped: false});
  const screen = new THREE.Mesh(new THREE.PlaneGeometry(SCR_W, SCR_H), screenMat);
  screen.position.set(0, SCR_Y, FRONT + 0.014);
  screen.name = 'screen';
  group.add(screen);

  const buttons = [];
  // press: inward travel (group-space vector)
  const addButton = (name, key, mesh, press, title) => {
    mesh.name = name;
    mesh.castShadow = true;
    mesh.userData = {key, press, title, home: mesh.position.clone(), depth: 0, down: false};
    buttons.push(mesh);
    group.add(mesh);
    return mesh;
  };

  // Front keys on the bottom bezel.
  const KEY_Y = SCR_Y - SCR_H / 2 - 1.28;
  const ring = new THREE.Mesh(new THREE.CylinderGeometry(0.52, 0.52, 0.02, 48), darkMat);
  ring.rotation.x = Math.PI / 2;
  ring.position.set(-2.3, KEY_Y, FRONT + 0.06);
  ring.material = new THREE.MeshStandardMaterial({color: '#b9bab6', roughness: 0.8});
  group.add(ring);
  const home = new THREE.Mesh(new THREE.CylinderGeometry(0.45, 0.45, 0.12, 48), orange);
  home.rotation.x = Math.PI / 2;
  home.position.set(-2.3, KEY_Y, FRONT + 0.1);
  addButton('home', KEY.Home, home, new THREE.Vector3(0, 0, -0.06), 'HOME');

  const pillGeo = slabGeo(1.4, 0.58, 0.29, 0.1, 0.04);
  const pillL = new THREE.Mesh(pillGeo, pillMat);
  pillL.position.set(0.42, KEY_Y, FRONT + 0.07);
  addButton('pillL', KEY.Down, pillL, new THREE.Vector3(0, 0, -0.05), 'Left pill (BTN_DOWN)');
  const pillR = new THREE.Mesh(pillGeo, pillMat);
  pillR.position.set(2.05, KEY_Y, FRONT + 0.07);
  addButton('pillR', KEY.Up, pillR, new THREE.Vector3(0, 0, -0.05), 'Right pill (BTN_UP)');

  // Right edge keys.
  const side = (len, y, name, key, title) => {
    const m = new THREE.Mesh(new RoundedBoxGeometry(0.2, len, 0.42, 3, 0.08), orange);
    m.position.set(W / 2 + 0.03, y, 0);
    return addButton(name, key, m, new THREE.Vector3(-0.07, 0, 0), title);
  };
  side(0.95, 4.35, 'boot', KEY.Confirm, 'BOOT (Confirm)');
  side(1.04, 2.5, 'volUp', KEY.Up, 'Volume + (Up)');
  side(1.04, 1.44, 'volDown', KEY.Down, 'Volume − (Down)');
  side(0.62, -0.35, 'power', KEY.Power, 'POWER');

  // USB-C on the bottom edge.
  const usb = new THREE.Mesh(new RoundedBoxGeometry(0.92, 0.06, 0.3, 2, 0.028), darkMat);
  usb.position.set(0, -H / 2 + 0.015, 0);
  group.add(usb);

  // ---- press animation ---------------------------------------------------------------
  const byName = Object.fromEntries(buttons.map((b) => [b.name, b]));
  function setDown(name, down) {
    const b = byName[name];
    if (b) b.userData.down = down;
  }
  function update(dt) {
    for (const b of buttons) {
      const u = b.userData;
      const target = u.down ? 1 : 0;
      u.depth += (target - u.depth) * Math.min(1, dt * 28);
      b.position.copy(u.home).addScaledVector(u.press, u.depth);
    }
  }

  // Screen flash for FULL (0) / HALF (1) refreshes: briefly darken, then settle.
  let flashT = 1, flashMode = -1;
  function flash(mode) { flashMode = mode; flashT = 0; }
  function updateFlash(dt) {
    if (flashT >= 1) return;
    flashT = Math.min(1, flashT + dt / (flashMode === 0 ? 0.42 : 0.26));
    const k = flashMode === 0 ? (flashT < 0.35 ? 0.12 : flashT < 0.7 ? 0.6 : 1) : (flashT < 0.6 ? 0.6 : 1);
    screenMat.color.setScalar(k);
  }

  return {
    group, screen, screenMat, texture: tex, buttons, byName, setDown,
    update(dt) { update(dt); updateFlash(dt); },
    flash,
    screenSize: {w: SCR_W, h: SCR_H, y: SCR_Y, z: FRONT + 0.014},
  };
}
