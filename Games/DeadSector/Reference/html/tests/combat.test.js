import test from 'node:test';import assert from 'node:assert/strict';import * as T from 'three';import {Rifle,WEAPONS} from '../weapons.js';import {Enemies} from '../enemies.js';const audio={play(){},shot(){}};
test('all ten weapons use the requested capacities, damage, reloads and automatic modes',()=>{
 const expected={Pistol:[8,45,1.15,false],Helsing:[60,35,4,true],Tikhar:[1,130,3,false],Shambler:[5,65,3,false],Shotgun:[2,90,2,false],Kalash:[30,50,3,true],VSV:[12,80,6,true],Scorpion:[35,45,4,true],Vector:[20,40,3,true],G36:[35,40,4,true]};
 assert.equal(WEAPONS.length,10);
 for(const [i,spec] of WEAPONS.entries()){
  const g=new Rifle(audio,()=>(i+.1)/WEAPONS.length),[capacity,damage,reload,automatic]=expected[spec.id];g.reset(spec.id);g.step(1.5);
  assert.equal(g.config.id,spec.id);assert.deepEqual([g.ammo,g.config.damage,g.config.reload,g.config.automatic],[capacity,damage,reload,automatic]);
  assert.equal(g.gun.name,spec.id);assert.ok(g.gun.geometry.attributes.position.count>500);assert.ok(g.variants.every(v=>!['RifleGod','RifleBran','SMG'].includes(v.record.id)));
  if(Array.isArray(g.gun.material)){assert.ok(g.gun.geometry.groups.length>0);assert.ok(g.gun.geometry.groups.every(group=>group.materialIndex<g.gun.material.length));}
  for(let n=0;n<capacity;n++){assert.equal(g.fire(),true);assert.equal(g.fire(),false);g.step(spec.interval+.001);}
  assert.equal(g.fire(),false);g.reload();assert.equal(g.fire(),false);g.step(reload-.01);assert.equal(g.ammo,0);g.step(.011);assert.equal(g.ammo,capacity);assert.equal(g.reserve,spec.reserve-capacity);
 }
 assert.ok(WEAPONS.find(w=>w.id==='Helsing').interval<WEAPONS.find(w=>w.id==='Vector').interval);
 assert.ok(WEAPONS.find(w=>w.id==='Vector').interval<WEAPONS.find(w=>w.id==='Kalash').interval);
 assert.ok(WEAPONS.find(w=>w.id==='Kalash').interval<WEAPONS.find(w=>w.id==='VSV').interval);
});
test('walls block hits; enemy HP and size reflect four variants',()=>{const enemies=new Enemies(new T.Scene(),[],audio);for(const [i,type]of ['zombie','runner','eye','brute'].entries())enemies.spawn(type,i*10,0);assert.deepEqual(enemies.items.map(e=>e.hp),[250,125,200,800]);assert.equal(enemies.items[3].rig.scale.y,3);const origin=new T.Vector3(0,1.2,5),direction=new T.Vector3(0,0,-1);enemies.colliders=[{minX:-2,maxX:2,minY:0,maxY:5,minZ:2,maxZ:3}];assert.equal(enemies.shoot(origin,direction),false);enemies.colliders=[];for(let i=0;i<8;i++)enemies.shoot(origin,direction,35);assert.equal(enemies.items[0].hp,0);assert.equal(enemies.alive,3);});
test('aggressive melee and flying projectile damage require contact, and projectiles can miss',()=>{const player={hp:100,position:{x:0,y:0,z:0}},a=new Enemies(new T.Scene(),[],audio);a.spawn('zombie',0,1);let damage=0;for(let i=0;i<150;i++)a.step(1/120,player,n=>damage+=n);assert.equal(damage,25);const b=new Enemies(new T.Scene(),[],audio);b.spawn('eye',0,8);let hit=0;for(let i=0;i<360;i++)b.step(1/120,player,n=>hit+=n);assert.ok(hit>=30);const c=new Enemies(new T.Scene(),[],audio);c.spawn('eye',0,8);for(let i=0;i<130;i++)c.step(1/120,player,()=>assert.fail('Projectile arrives too early'));player.position.x=20;for(let i=0;i<120;i++)c.step(1/120,player,()=>assert.fail('Missed projectile must not deal damage'));});
