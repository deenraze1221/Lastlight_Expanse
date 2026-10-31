import test from 'node:test';import assert from 'node:assert/strict';import * as T from 'three';
import {createInventory,buy,equipWeapon,recordShot,totalAmmo,rewardForHP,useMedkit,syncCombat,SHOP_ITEMS} from '../economy.js';
import {Rifle} from '../weapons.js';import {MoneyDrops} from '../loot.js';import {Enemies} from '../enemies.js';import {Player,FIXED_STEP} from '../player.js';import {generateTown} from '../town.js';
const audio={play(){},shot(){}};
test('starter pistol only, two weapon slots, exact prices and replacement preserve universal ammunition',()=>{
 const a=createInventory();assert.deepEqual(Object.keys(a.weapons),['Pistol']);assert.equal(a.money,0);assert.equal(totalAmmo(a),92);
 const expected={Helsing:6500,Tikhar:4000,Shambler:3500,Shotgun:2000,Kalash:4500,VSV:6000,Scorpion:5000,Vector:3000,G36:5500};
 a.completedWave=25;a.money=100000;for(const [id,price]of Object.entries(expected)){assert.equal(SHOP_ITEMS.find(i=>i.id===id).price,price);const before=a.money;assert.equal(buy(a,id).ok,true);assert.equal(a.money,before-price);assert.deepEqual(Object.keys(a.weapons).sort(),['Pistol',id].sort());assert.equal(a.equipped,id);assert.equal(equipWeapon(a,'Pistol'),true);assert.equal(totalAmmo(a),92);}
 a.weapons.G36.ammo=10;a.reserve-=10;buy(a,'Vector');assert.equal(totalAmmo(a),92);assert.equal(a.weapons.G36,undefined);
});
test('full ammo packs respect 500 total including both magazines; medkits and repairs charge only valid purchases',()=>{
 const a=createInventory();a.money=10000;assert.equal(buy(a,'ammo').ok,true);assert.equal(totalAmmo(a),142);a.reserve=442;assert.equal(buy(a,'ammo').ok,true);assert.equal(totalAmmo(a),500);a.reserve=443;const cash=a.money;assert.equal(buy(a,'ammo').ok,false);assert.equal(a.money,cash);a.reserve=492;let before=a.money;assert.equal(buy(a,'ammo').ok,false);assert.equal(a.money,before);
 a.health=10;assert.equal(buy(a,'smallMedkit').ok,true);assert.equal(a.health,35);assert.equal(a.smallMedkit,0);assert.equal(buy(a,'largeMedkit').ok,true);assert.equal(a.health,100);assert.equal(a.largeMedkit,0);assert.equal(buy(a,'smallMedkit').ok,true);assert.equal(a.smallMedkit,1);assert.equal(useMedkit(a,'smallMedkit'),false);a.health=75;assert.equal(useMedkit(a,'smallMedkit'),true);assert.equal(a.health,100);assert.equal(a.smallMedkit+a.largeMedkit,0);
 before=a.money;assert.equal(buy(a,'repair').ok,false);assert.equal(a.money,before);a.weapons.Pistol.durability=70;assert.equal(buy(a,'repair').ok,true);assert.equal(a.weapons.Pistol.durability,100);assert.equal(a.money,before-500);
});
test('wear applies per actual 100 shots, broken guns cannot fire, repair restores firing and pistol stays separate',()=>{
 const a=createInventory();a.completedWave=25;a.money=10000;buy(a,'Helsing');const gun=new Rifle(audio);gun.bindInventory(a);gun.step(1.5);a.reserve=0;
 for(let shot=1;shot<=2000;shot++){a.weapons.Helsing.ammo=1;assert.equal(gun.fire(),true);gun.step(gun.config.interval+.001);if(shot===99)assert.equal(gun.durability,100);if(shot===100)assert.equal(gun.durability,95);}
 assert.equal(gun.durability,0);assert.equal(a.weapons.Pistol.durability,100);a.weapons.Helsing.ammo=1;assert.equal(gun.fire(),false);assert.equal(buy(a,'repair').ok,true);assert.equal(gun.durability,50);assert.equal(gun.fire(),true);
 const stale=structuredClone(a);a.revision++;a.weapons.Helsing.durability=100;syncCombat(a,stale);assert.equal(a.weapons.Helsing.durability,100);
});
test('zombies leave collectable cash, not free ammo or medicine, with monotonic HP rewards and one pickup',()=>{
 assert.deepEqual([125,250,300,600].map(rewardForHP),[15,30,40,100]);assert.equal(rewardForHP(0),15);assert.equal(rewardForHP(10000),100);
 const scene=new T.Scene(),loot=new MoneyDrops(scene),enemies=new Enemies(scene,[],audio),a=createInventory();enemies.onDeath=e=>loot.spawn(e);const e=enemies.spawn('zombie',0,0);enemies.die(e);assert.equal(loot.items.length,1);assert.equal(loot.nearby({x:0,y:0,z:0}).value,30);assert.equal(loot.collect('cash-'+e.id,a),true);assert.equal(loot.collect('cash-'+e.id,a),false);assert.equal(a.money,30);assert.deepEqual(generateTown(42).supplies,[]);
});
test('five seconds of sprint drain stamina, five seconds of recovery refill it, no stationary drain',()=>{
 const p=new Player({x:0,y:0,z:0}),input={forward:true,backward:false,left:false,right:false,sprint:true,jump:false};
 for(let i=0;i<600;i++)p.step(FIXED_STEP,input,[]);assert.equal(p.stamina,0);assert.ok(-p.position.z>36.5&&-p.position.z<37.5);
 const z=p.position.z;for(let i=0;i<600;i++)p.step(FIXED_STEP,input,[]);assert.equal(p.stamina,5);assert.ok(z-p.position.z>=22.5&&z-p.position.z<23);
 for(let i=0;i<600;i++)p.step(FIXED_STEP,{...input,forward:false},[]);assert.equal(p.stamina,5);
});
test('town adds exactly 15 percent street population, four architectural families, unique furnished market and varied plans',()=>{
 for(const seed of [1,42,91]){const t=generateTown(seed);assert.equal(t.streetDensity.actual,t.streetDensity.target);assert.equal(t.streetDensity.target,Math.ceil(t.streetDensity.base*1.15));for(const type of ['trafficCone','streetClock','newsstand','publicPhone','busMap'])assert.ok(t.props.some(p=>p.type===type));for(const type of ['cinema','motel','firestation','tower'])assert.ok(t.buildings.some(b=>b.type===type));assert.equal(t.buildings.find(b=>b.type==='tower').h,10);assert.equal(t.buildings.filter(b=>b.type==='grocery').length,1);const f=t.furniture.filter(f=>f.building===t.shop.building);assert.equal(new Set(f.map(f=>f.type)).size,10);assert.equal(f.filter(f=>f.type==='terminal').length,1);assert.ok(new Set(t.buildings.map(b=>b.floorplan)).size>=4);}
});

test('replicated deaths do not create another money drop during host restore',()=>{
 const enemies=new Enemies(new T.Scene(),[],audio);let drops=0;enemies.onDeath=()=>drops++;enemies.apply([{id:99,type:'zombie',skin:0,hp:0,x:0,y:0,z:0,corpse:0,rotation:[0,0,0,'XYZ']}]);assert.equal(drops,0);assert.equal(enemies.blood.active,48);
});
