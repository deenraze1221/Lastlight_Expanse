import test from 'node:test';import assert from 'node:assert/strict';
import {generateTown,ROADS} from '../town.js';import {validateTown} from '../city-validation.js';import {CATALOG,bounds} from '../asset-catalog.js';import {Player,FIXED_STEP,overlapsBody} from '../player.js';
test('semantic placements across 24 seeds have no unsupported props, collisions or unfurnished rooms',()=>{for(let seed=1;seed<=24;seed++){const town=generateTown(seed);assert.deepEqual(validateTown(town),[],`seed ${seed}`);assert.equal(town.rooms.length,298);assert.ok(town.furniture.length>=500);assert.equal(town.props.filter(p=>p.type==='lamp').length,106);assert.ok(town.props.filter(p=>p.context==='traffic').length>=15);for(const lamp of town.props.filter(p=>p.type==='lamp')){assert.ok(ROADS.some(x=>Math.abs(Math.abs(lamp.x-x)-3.65)<1e-6));}}});
test('every varied room entrance and shop terminal admit the actual player body',()=>{
 for(const seed of [1,42,91]){const town=generateTown(seed);
  const paths=town.rooms.flatMap(r=>r.route?[{id:r.id,points:r.route}]:[]);
  paths.push({id:'terminal',points:[town.spawn,{x:10.35,z:12.2},{x:8.35,z:12.2},{x:8.35,z:10.9}]});
  for(const route of paths){const p=new Player({...route.points[0],y:0});for(const target of route.points.slice(1)){
   let reached=false;for(let step=0;step<600;step++){const dx=target.x-p.position.x,dz=target.z-p.position.z;if(Math.hypot(dx,dz)<.07){reached=true;break;}p.yaw=Math.atan2(-dx,-dz);p.step(FIXED_STEP,{forward:true,backward:false,left:false,right:false,sprint:false,jump:false},town.colliders);}
   assert.ok(reached,`seed ${seed}, route ${route.id}, target ${JSON.stringify(target)}, stopped ${JSON.stringify(p.position)}`);
  }}
 }
});
test('terrain is an exact 250m tiling, every parking vehicle fits its bay and catalog variants are unique',()=>{const t=generateTown(42),area=t.surfaces.reduce((n,s)=>n+(s.maxX-s.minX)*(s.maxZ-s.minZ),0);assert.ok(Math.abs(area-62500)<1e-6);for(let i=0;i<t.surfaces.length;i++)for(let j=i+1;j<t.surfaces.length;j++){const a=t.surfaces[i],b=t.surfaces[j];assert.ok(!(a.minX<b.maxX-1e-8&&a.maxX>b.minX+1e-8&&a.minZ<b.maxZ-1e-8&&a.maxZ>b.minZ+1e-8));}assert.ok(CATALOG.length>=1000);assert.equal(new Set(CATALOG.map(v=>v.id)).size,CATALOG.length);});

test('feet land on seats and tabletops rather than floating above whole-asset bounding boxes',()=>{const town=generateTown(42);for(const [type,front,top]of [['desk',.35,.76],['table',.3,.76],['sofa',.25,.42],['chair',.22,.5],['bed',.5,.56],['counter',.29,1],['bench',.2,.505],['car',-2,.9]]){const item=[...town.furniture,...town.props].find(f=>f.type===type);assert.ok(item,type);const p=new Player({x:item.x+Math.sin(item.yaw)*front,y:1.15,z:item.z+Math.cos(item.yaw)*front});p.grounded=false;const input={forward:false,backward:false,left:false,right:false,sprint:false,jump:false};for(let i=0;i<180;i++)p.step(FIXED_STEP,input,town.colliders.filter(c=>c.owner===item.id));assert.ok(Math.abs(p.position.y-top)<.001,`${type}: ${p.position.y} vs ${top}`);}});

test('navigation uses body height so flying eyes and brutes avoid upper building volumes',async()=>{const {createNavigation}=await import('../navigation.js');const roof=[{minX:-2,maxX:2,minY:3.12,maxY:6,minZ:-2,maxZ:2}],from={x:0,z:5},to={x:0,z:-5};const walk=createNavigation(roof).path(from,to),air=createNavigation(roof,.8,2,3.5).path(from,to),brute=createNavigation(roof,1.2,6).path(from,to);assert.ok(walk.every(p=>p.x===0));assert.ok(air.some(p=>Math.abs(p.x)>=3));assert.ok(brute.some(p=>Math.abs(p.x)>=4));});
