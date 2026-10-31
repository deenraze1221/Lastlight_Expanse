import test from 'node:test';
import {generateTown} from '../town.js';
import assert from 'node:assert/strict';
import { Player, PLAYER, FIXED_STEP, overlapsBody } from '../player.js';
import { getColliders, SPAWN } from '../map.js';

const emptyInput = { forward: false, backward: false, left: false, right: false, sprint: false, jump: false };
function advance(p, seconds, input = {}, colliders = []) {
  for (let i = 0; i < Math.round(seconds / FIXED_STEP); i++) p.step(FIXED_STEP, { ...emptyInput, ...input }, colliders);
}
const near = (a, b, epsilon = 1e-8) => assert.ok(Math.abs(a - b) < epsilon, `${a} ≠ ${b}`);
const wall = { minX: -10, maxX: 10, minY: 0, maxY: 4, minZ: -2, maxZ: -1 };

test('cardinal and diagonal walk cover the same distance; sprint is faster', () => {
  const a = new Player({ x: 0, y: 0, z: 0 }), b = new Player(a.spawn), c = new Player(a.spawn);
  advance(a, 1, { forward: true }); advance(b, 1, { forward: true, right: true }); advance(c, 1, { forward: true, sprint: true });
  assert.ok(Math.hypot(a.position.x,a.position.z)>4.1&&Math.hypot(a.position.x,a.position.z)<PLAYER.walk);
  near(Math.hypot(b.position.x,b.position.z),Math.hypot(a.position.x,a.position.z));
  assert.ok(-c.position.z>6.5&&-c.position.z<PLAYER.run);
});
test('walls block walking and running; diagonal movement slides along a wall', () => {
  const a = new Player({ x: 0, y: 0, z: 0 });
  advance(a, 1, { forward: true, right: true, sprint: true }, [wall]);
  near(a.position.z, wall.maxZ + PLAYER.radius); assert.ok(a.position.x > 4);
  assert.equal(overlapsBody(a.position, wall), false);
});
test('jump leaves ground, cannot double jump, then lands', () => {
  const a = new Player({ x: 0, y: 0, z: 0 }), b = new Player(a.spawn);
  advance(a, FIXED_STEP, { jump: true }); advance(b, FIXED_STEP, { jump: true });
  assert.ok(a.position.y > 0); assert.equal(a.grounded, false);
  advance(a, .2, { jump: true }); advance(b, .2);
  near(a.position.y, b.position.y);
  advance(a, 1); near(a.position.y, 0); assert.equal(a.grounded, true);
});
test('player lands on a low crate and falls off when walking away', () => {
  const box = { minX: -1, maxX: 1, minY: 0, maxY: .65, minZ: -1, maxZ: 1 };
  const a = new Player({ x: 0, y: 1.1, z: 0 }); a.grounded = false;
  advance(a, .5, {}, [box]); near(a.position.y, .65); assert.equal(a.grounded, true);
  advance(a, 1, { right: true }, [box]); near(a.position.y, 0); assert.equal(a.grounded, true);
});
test('low ceilings stop upward movement', () => {
  const ceiling = { minX: -2, maxX: 2, minY: 2.2, maxY: 2.5, minZ: -2, maxZ: 2 };
  const a = new Player({ x: 0, y: 0, z: 0 }); let highest = 0;
  for (let i = 0; i < 120; i++) { a.step(FIXED_STEP, { ...emptyInput, jump: i === 0 }, [ceiling]); highest = Math.max(highest, a.position.y); }
  assert.ok(highest <= 2.2 - PLAYER.height + 1e-8); near(a.position.y, 0);
});
test('yaw rotates forward motion and pitch is clamped', () => {
  const a = new Player({ x: 0, y: 0, z: 0 }); a.yaw = Math.PI / 2;
  advance(a, 1, { forward: true }); assert.ok(a.position.x < -4.1); near(a.position.z, 0);
  a.look(0, 1e6, 1); near(a.pitch,-Math.PI/3);a.look(0,1e6,2);near(a.pitch,-Math.PI/3);
  a.look(0, -1e6, 1); assert.ok(a.pitch < Math.PI / 2);
});
test('spawn is unobstructed; perimeter contains sprinting player', () => {
  const boxes = getColliders(), a = new Player(SPAWN);
  assert.ok(boxes.every(b => !overlapsBody(a.position, b)));
  a.position={x:0,y:0,z:85};advance(a, 12, { backward: true, sprint: true }, boxes);
  near(a.position.z, 124.5 - PLAYER.radius);
});
test('generated building doorway admits player and adjacent facade blocks movement',()=>{
 const layout=generateTown(1),b=layout.buildings[0],front=b.z+b.d/2;
 const a=new Player({x:b.x,y:0,z:front+2}),c=new Player({x:b.x+b.w*.35,y:0,z:front+2});
 advance(a,1,{forward:true},layout.colliders);advance(c,1,{forward:true},layout.colliders);
 assert.ok(a.position.z<front-1);assert.ok(c.position.z>front);
});
test('fixed step produces the same movement at 30, 60 and 144 rendered frames/sec', () => {
  const positions = [30, 60, 144].map(fps => {
    const a = new Player(SPAWN); let accumulator = 0;
    for (let frame = 0; frame < fps * 2; frame++) {
      accumulator += 1 / fps;
      while (accumulator + 1e-12 >= FIXED_STEP) {
        a.step(FIXED_STEP, { ...emptyInput, forward: true }, []); accumulator -= FIXED_STEP;
      }
    }
    return a.position.z;
  });
  for (const z of positions) near(z, positions[0]);assert.ok(positions[0]>SPAWN.z-PLAYER.walk*2);
});
test('reset restores position, orientation and vertical motion', () => {
  const a = new Player(SPAWN); advance(a, .1, { jump: true, forward: true }); a.look(100, 50, 1); a.reset();
  assert.deepEqual(a.position, SPAWN); assert.equal(a.yaw, 0); assert.equal(a.pitch, 0); assert.equal(a.velocityY, 0); assert.equal(a.grounded, true);
});

test('movement accelerates and brakes briefly; stationary shift does not sprint or drain stamina',()=>{const p=new Player({x:0,y:0,z:0});advance(p,.5,{sprint:true});assert.equal(p.speed,0);assert.equal(p.sprinting,false);near(p.stamina,5);p.step(FIXED_STEP,{...emptyInput,forward:true},[]);assert.ok(p.speed>0&&p.speed<PLAYER.walk);advance(p,.5,{forward:true});const before=p.position.z;advance(p,.05);assert.ok(p.position.z<before);advance(p,.2);near(p.speed,0);});
