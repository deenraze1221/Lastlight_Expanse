import test from 'node:test';import assert from 'node:assert/strict';import * as T from 'three';import {Enemies} from '../enemies.js';import {Waves,ENEMY_TYPES,chooseEnemy} from '../waves.js';import {PoisonEffect} from '../poison.js';
const audio={play(){},creature(){}};
test('eye and brute balance and wave gates are exact; vomiters have a hard15% per-wave cap',()=>{assert.equal(ENEMY_TYPES.eye.hp,200);assert.equal(ENEMY_TYPES.brute.hp,800);assert.equal(ENEMY_TYPES.vomiter.speed,ENEMY_TYPES.zombie.speed*.85);for(let wave=1;wave<=30;wave++)for(let seed=1;seed<=30;seed++){const w=new Waves(seed);w.number=wave;w.warmup=false;const out=[];w.step(120,t=>out.push(t),()=>1);const count=out.filter(t=>t==='vomiter').length;assert.ok(count<=Math.floor(out.length*.15));if(wave<6){assert.equal(count,0);assert.ok(!out.includes('eye'));}if(wave<=20)assert.ok(!out.includes('brute'));const copy=new Waves();copy.restore(w.snapshot());assert.equal(copy.vomiters,w.vomiters);}assert.equal(chooseEnemy(()=>.999,20),'eye');assert.equal(chooseEnemy(()=>.999,21),'brute');});
test('vomit prepares0.25s, reaches8m at5m/s, deals35HP with poison and reloads in1–2s',()=>{
 const a=new Enemies(new T.Scene(),[],audio),e=a.spawn('vomiter',0,0),p={hp:100,position:{x:0,y:0,z:7.8}},poison=new PoisonEffect();let damage=0;e.attack=0;
 for(let i=0;i<29;i++)a.step(1/120,p,()=>assert.fail('Impact before preparation'));
 assert.equal(a.projectiles.length,0);
 for(let i=0;i<3;i++)a.step(1/120,p,()=>{});
 assert.equal(a.projectiles.length,1);assert.ok(e.attack>=1&&e.attack<=2);assert.ok(Math.abs(a.projectiles[0].velocity.length()-5)<1e-8);
 // Freeze further attacks to measure a single hit and poison sequence.
 e.attack=100;
 for(let i=0;i<200;i++)a.step(1/120,p,(amount,effect)=>{damage+=amount;if(effect?.poison)poison.apply(effect.id);});
 assert.equal(damage,10);poison.step(5,n=>damage+=n);assert.equal(damage,35);
 assert.equal(a.fireVomit(e,{x:e.rig.position.x,y:0,z:e.rig.position.z+8.1}),false);
 assert.ok(e.attack>=1&&e.attack<=2);
});
test('poison has five one-second ticks, duplicate packets do not reapply, and refreshing does not stack',()=>{const p=new PoisonEffect();let damage=0;assert.ok(p.apply('hit-1'));assert.equal(p.apply('hit-1'),false);p.step(.99,n=>damage+=n);assert.equal(damage,0);p.step(.01,n=>damage+=n);assert.equal(damage,5);p.apply('hit-2');p.step(5,n=>damage+=n);assert.equal(damage,30);assert.equal(p.active,false);p.step(10,n=>damage+=n);assert.equal(damage,30);p.reset();assert.equal(p.remaining,0);});
test('vomiter deaths release harmless green spores once, no red burst, replicated effects expire in2.5s',()=>{const a=new Enemies(new T.Scene(),[],audio),b=new Enemies(new T.Scene(),[],audio),e=a.spawn('vomiter',0,0);b.apply(a.snapshot());a.die(e);assert.equal(a.blood.active,0);assert.equal(a.spores.active,8);b.apply(a.snapshot());b.apply(a.snapshot());assert.equal(b.spores.active,8);a.updateEffects(2.6);b.updateEffects(2.6,true);assert.equal(a.spores.active,0);assert.equal(b.spores.active,0);assert.ok(e.rig.getObjectByName('vomiter-belly'));assert.ok(e.rig.getObjectByName('fungal-cap'));assert.equal(e.scale,1.25);});

test('walls block vomit and a missed projectile expires after1.6 seconds without damage',()=>{const wall={minX:-3,maxX:3,minY:0,maxY:4,minZ:2,maxZ:2.1},a=new Enemies(new T.Scene(),[wall],audio),z=a.spawn('vomiter',0,0);assert.equal(a.fireVomit(z,{x:0,y:0,z:4}),false);const b=new Enemies(new T.Scene(),[],audio),v=b.spawn('vomiter',0,0);assert.ok(b.fireVomit(v,{x:0,y:0,z:4}));const p={hp:100,position:{x:20,y:0,z:4}};for(let i=0;i<193;i++)b.step(1/120,p,()=>assert.fail('Miss caused damage'));assert.equal(b.projectiles.length,0);});
test('mid-wave restoration preserves vomiter cap and seeded spawn sequence',()=>{const a=new Waves(44);a.number=6;a.warmup=false;a.step(40,()=>{},()=>1);const b=new Waves();b.restore(a.snapshot());const aa=[],bb=[];a.step(80,(...v)=>aa.push(v),()=>1);b.step(80,(...v)=>bb.push(v),()=>1);assert.deepEqual(aa,bb);assert.equal(a.vomiters,b.vomiters);assert.ok(a.vomiters<=Math.floor(a.total*.15));});
