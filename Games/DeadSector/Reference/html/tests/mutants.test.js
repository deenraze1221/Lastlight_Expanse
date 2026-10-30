import test from 'node:test';
import assert from 'node:assert/strict';
import * as T from 'three';
import {Enemies} from '../enemies.js';
import {poseMutant,ZOMBIE_SKINS,BOSS_SKINS,attackPose} from '../mutant-visuals.js';
import {generateTown} from '../town.js';
const audio={play(){}};

test('nine deterministic zombie outfits survive network replication',()=>{
 const host=new Enemies(new T.Scene(),[],audio),guest=new Enemies(new T.Scene(),[],audio);
 for(let i=0;i<9;i++)host.spawn('zombie',i*3,0);
 assert.equal(ZOMBIE_SKINS.length,9);assert.equal(new Set(host.items.map(e=>e.skin)).size,9);
 assert.equal(new Set(host.items.map(e=>e.rig.getObjectByName('chest').material.color.getHex())).size,9);
 guest.apply(host.snapshot());assert.deepEqual(guest.items.map(e=>e.skin),host.items.map(e=>e.skin));
 for(const name of ['hardhat','medical-patch','vest','hood'])assert.ok(host.items.some(e=>e.rig.getObjectByName(name)),name);
});

test('brute arms stay outside the belly during walking and attacking; feet contact the floor',()=>{
 const enemies=new Enemies(new T.Scene(),[],audio),brute=enemies.spawn('brute',0,0);
 const belly=brute.rig.getObjectByName('belly');
 for(let i=0;i<64;i++){
  brute.phase=i*Math.PI*2/64;brute.motion=1;brute.attackTime=i%2?.21:0;poseMutant(brute);brute.rig.updateMatrixWorld(true);
  const body=new T.Box3().setFromObject(belly);
  for(const a of brute.visual.arms){const bounds=new T.Box3().setFromObject(a.pivot);assert.ok(a.side<0?bounds.max.x<body.min.x:bounds.min.x>body.max.x,`arm ${a.side}, phase ${i}`);}
  const bottoms=brute.visual.legs.map(l=>new T.Box3().setFromObject(l.ankle).min.y);
  assert.ok(bottoms.every(y=>y>=-1e-5),'feet never penetrate the floor');assert.ok(Math.min(...bottoms)<1e-5,'a stance foot stays grounded');
 }
 const [left,right]=brute.visual.arms;brute.phase=0;brute.attackTime=0;poseMutant(brute);assert.ok(left.pivot.rotation.x*right.pivot.rotation.x<0,'opposite arm swings');
});

test('a kill hides the intact model without tipping it, emits red fragments once and expires the effect',()=>{
 const host=new Enemies(new T.Scene(),[],audio),guest=new Enemies(new T.Scene(),[],audio),e=host.spawn('zombie',0,0);
 guest.apply(host.snapshot());const position=e.rig.position.clone();
 assert.equal(host.shoot(new T.Vector3(0,1.2,5),new T.Vector3(0,0,-1),250),true);
 assert.equal(e.rig.visible,false);assert.equal(e.rig.rotation.x,0);assert.ok(e.rig.position.equals(position));assert.equal(host.alive,0);assert.equal(host.blood.active,48);
 guest.apply(host.snapshot());assert.equal(guest.items[0].rig.visible,false);assert.equal(guest.blood.active,48);const cursor=guest.blood.cursor;
 for(let i=0;i<5;i++)guest.apply(host.snapshot());assert.equal(guest.blood.cursor,cursor,'repeated death packets do not replay burst');
 host.updateEffects(2);guest.updateEffects(2,true);assert.equal(host.blood.active,0);assert.equal(guest.blood.active,0);
 host.step(2.1,{hp:100,position:{x:5,y:0,z:5}},()=>{});assert.equal(host.snapshot().length,0);
});

test('all four hive exits admit the enlarged brute on generated city navigation',()=>{
 for(const seed of [1,42,91]){
  const town=generateTown(seed),enemies=new Enemies(new T.Scene(),town.colliders,audio);
  for(const h of town.hives){const from={x:h.x-Math.sign(h.x)*1.5,z:h.z-Math.sign(h.z)*1.5};assert.ok(enemies.fatNav.path(from,town.spawn).length>0,`seed ${seed}, hive ${h.x}/${h.z}`);}
 }
});

test('five distinct boss costumes replicate and every encounter rotates its outfit',()=>{const a=new Enemies(new T.Scene(),[],audio),b=new Enemies(new T.Scene(),[],audio);for(let wave=5;wave<=25;wave+=5)a.spawn('boss',wave,0,undefined,undefined,{wave});assert.equal(BOSS_SKINS.length,5);assert.equal(new Set(a.items.map(e=>e.skin)).size,5);assert.equal(new Set(a.items.map(e=>e.rig.getObjectByName('chest').material.color.getHex())).size,5);b.apply(a.snapshot());assert.deepEqual(b.items.map(e=>e.skin),a.items.map(e=>e.skin));for(const name of ['plague-filter','iron-pauldron','charred-fissure','reclaimer-cowl'])assert.ok(a.items.some(e=>e.rig.getObjectByName(name)));});
test('attack preparation and recovery connect continuously at impact',()=>{const e={type:'zombie',pendingAttack:true,windupDuration:.18,windup:.18};assert.ok(Math.abs(attackPose(e))<1e-9);let last=0;for(let i=1;i<=120;i++){e.windup=.18*(1-i/120);const next=attackPose(e);assert.ok(Math.abs(next-last)<.1);last=next;}e.pendingAttack=false;e.attackTime=.21;assert.ok(Math.abs(attackPose(e)-last)<1e-8);e.attackTime=0;assert.ok(Math.abs(attackPose(e))<1e-9);});
