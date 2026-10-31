import {DISTRICT_STYLES} from './districts.js';
import {BUILDINGS,DIMENSIONS,bounds,intersects} from './asset-catalog.js';
import {createInteriors} from './interiors.js';
import {MAP_HALF,BLOCK_CENTERS,ROAD_CENTERS,INNER_ROADS} from './world-config.js';
import {STORE} from './economy.js';
export {BUILDINGS};export const PROP_TYPES=['bench','bin','dumpster','lamp','car','barrier','fence','barrel','crate','hydrant','powerbox','busstop','pallet','sign','bollard'];
export const SPAWN={...STORE.spawn},EXIT={x:0,z:-(MAP_HALF-5)},HIVES=[{x:-122,z:-122},{x:122,z:-122},{x:-122,z:122},{x:122,z:122}],ROADS=ROAD_CENTERS;
const ROAD_HALF=2.75,PAVEMENT=4.35;
export function generateTown(seed=1){let state=seed>>>0;const random=()=>{state=(state*1664525+1013904223)>>>0;return state/4294967296;};const buildings=[],props=[],furniture=[],colliders=[],supplies=[],districts=[],walls=[],parking=[],rooms=[],surfaces=[],markings=[];let next=0;const usedTypes=new Set();
const solid=(x,y,z,w,h,d,kind='structure',owner)=>{const b={minX:x-w/2,maxX:x+w/2,minY:y,maxY:y+h,minZ:z-d/2,maxZ:z+d/2,kind,owner};colliders.push(b);return b;};
function furnitureSolids(f){const {w,h,d,type}=f;const c=Math.cos(f.yaw),s=Math.sin(f.yaw),swap=Math.abs(s)>.5;
 const part=(x,y,z,width,height,depth)=>solid(f.x+c*x+s*z,y,f.z-s*x+c*z,swap?depth:width,height,swap?width:depth,'furniture',f.id);
 if(['desk','table','workbench'].includes(type)){part(0,0,0,w,.76,d);if(type==='desk'){part(0,.76,-.08,.3,.025,.2);part(0,.785,-.12,.05,.19,.05);part(0,.96,-.15,.5,.32,.055);}if(type==='workbench')part(0,.96,-d/2+.05,w,.28,.1);}
 else if(type==='chair'){part(0,0,0,w,.5,d);part(0,.5,-d/2+.035,w,.45,.07);}
 else if(type==='sofa'){part(0,0,0,w,.42,d);part(0,.42,-d/2+.09,w,.48,.18);for(const side of [-1,1])part(side*(w/2-.08),.405,0,.16,.35,d);}
 else if(type==='bed'||type==='examBed'){part(0,0,0,w,.52,d);part(0,.52,d*.14,w-.08,.04,d*.58);part(0,.52,-d*.32,w*.7,.12,.35);if(type==='bed')part(0,.35,-d/2+.04,w,.6,.08);}
 else if(type==='sink'){part(0,0,0,w,.9,d);part(0,.9,-d*.1-.0075,.04,.15,.175);}
 else if(type==='toilet'){part(0,0,0,w,.57,d);part(0,.4,-d/2+.1,w,.42,.2);}
 else if(type==='tvstand'){part(0,0,0,w,.42,d);part(0,.42,0,.25,.06,.15);part(0,.495,-.03,w*.9,.65,.07);}
 else if(type==='counter'){part(0,0,0,w,1,d);part(0,1,-.1,.3,.25,.1);}
 else if(type==='shelf'){for(const side of [-1,1])part(side*(w/2-.03),0,0,.06,h,d);part(0,0,-d/2+.02,w-.12,h,.04);for(let n=0;n<4;n++){const y=.1+n*.43;part(0,y-.03,0,w-.12,.06,d-.04);for(let k=0;k<3;k++)part(-w*.28+k*w*.28,y+.03,0,.14,.25,d*.6);}}
 else part(0,0,0,w,h,d);
}
const occupied=[];
function add(type,x,z,options={}){const item={id:'asset-'+next++,type,x,z,y:0,yaw:0,variant:Math.floor(random()*16),...options};[item.w,item.h,item.d]=DIMENSIONS[type];if(type==='fence')item.w=item.length;const a=bounds(item);if(occupied.some(b=>intersects(a,b,.08)&&!(type==='fence'&&b.type==='fence'&&b.building===item.building)))return null;props.push(item);occupied.push({...a,id:item.id,type:item.type,building:item.building});
 function local(lx,lz,w,h,d,y=0){const c=Math.cos(item.yaw),s=Math.sin(item.yaw),swap=Math.abs(s)>.5;solid(item.x+c*lx+s*lz,(item.y||0)+y,item.z-s*lx+c*lz,swap?d:w,h,swap?w:d,'prop',item.id);}
 if(['tree','pine','birch'].includes(type)){local(0,0,.24,item.h*.65,.24);const crownBottom=type==='pine'?item.h*.225:item.h*.44;local(0,0,item.w,item.h-crownBottom,item.d,crownBottom);}
 else if(type==='busstop'||type==='bench'){local(0,0,item.w-(type==='bench'?.1:.3),.505,.62);local(0,-.24,item.w-.15,.46,.08,.505);if(type==='busstop'){for(const side of [-1,1])local(side*(item.w/2-.06),-item.d/2+.06,.12,item.h-.1,.12);local(0,0,item.w,.1,item.d,item.h-.1);}}
 else if(['car','van','hatchback'].includes(type)){const cabin=type==='van'?item.d*(.6+(item.variant%3)*.02):type==='hatchback'?item.d*(.49+(item.variant%3)*.02):item.d*(.43+(item.variant%3)*.02);local(0,0,item.w-.4,.9,item.d-.12);local(0,-.08,item.w-.42,item.h-.98,cabin,.9);local(0,-.08,item.w-.32,.08,cabin+.1,item.h-.08);for(const side of [-1,1])for(const z of [-item.d*.32,item.d*.32])local(side*(item.w/2-.09),z,.18,.64,.64);}
 else if(type==='sign'){local(0,0,.07,item.h-.1,.07);local(0,0,item.w,.4,.08,item.h-.4);}
 else local(0,0,item.w,item.h,item.d);
 return item;}
for(const [x,z,w,d]of [[0,-MAP_HALF,251,1],[0,MAP_HALF,251,1],[-MAP_HALF,0,1,249],[MAP_HALF,0,1,249]]){solid(x,0,z,w,5.8,d);walls.push({x,z,w,h:5.8,d});}
for(const cx of BLOCK_CENTERS)for(const cz of BLOCK_CENTERS){
 const id=buildings.length,district=cx<0?(cz<0?'industrial':'residential'):(cz<0?'medical':'market'),type=cx===12&&cz===12?'grocery':(DISTRICT_STYLES[district].types.find(t=>!usedTypes.has(t))||DISTRICT_STYLES[district].types[Math.floor(random()*DISTRICT_STYLES[district].types.length)]),variant=(id+Math.floor(random()*32))%32;
 const w=type==='grocery'?STORE.w:8.8+Math.floor(random()*4)*.3,d=type==='grocery'?STORE.d:8.8+Math.floor(random()*3)*.35,h=({residentialHighrise:24,corporateTower:32,hospitalTower:21,communicationsTower:28,courthouse:16,artDecoTower:38,freightDepot:7,observatory:14}[type])??(type==='tower'?10:type==='grocery'?4.5:['garage','workshop','utility'].includes(type)?4.5:['apartment','hotel','office'].includes(type)?10+Math.floor(random()*3)*2.8:6+Math.floor(random()*2)*2.7);
 usedTypes.add(type);const b={id,type,variant,x:cx-1.65,z:cz-2.05,w,d,h,district,cx,cz,accessibleFloors:1,door:['garage','warehouse','workshop','firestation','grocery','cinema','freightDepot'].includes(type)?3.4:2.2};buildings.push(b);districts.push({name:district,x:cx,z:cz});occupied.push({minX:b.x-w/2,maxX:b.x+w/2,minY:0,maxY:h+.2,minZ:b.z-d/2,maxZ:b.z+d/2,id:'building-'+id});
 const t=.22,door=b.door;solid(b.x,0,b.z-d/2+t/2,w,3.12,t,'wall',id);for(const side of [-1,1])solid(b.x+side*(w/2-t/2),0,b.z,t,3.12,d-2*t,'wall',id);for(const side of [-1,1])solid(b.x+side*(w/4+door/4),0,b.z+d/2-t/2,(w-door)/2,3,t,'wall',id);solid(b.x,3,b.z+d/2-t/2,w,.12,t,'lintel',id);solid(b.x,3.12,b.z,w,.18,d,'ceiling',id);solid(b.x,3.3,b.z,w,h-3.3,d,'upper',id);
 if(type==='courthouse')for(const side of [-1,1])solid(b.x+side*w*.35,0,b.z+d/2+.35,.4,3,.4,'wall',id);
 b.structure=colliders.filter(c=>c.owner===id);
 rooms.push(...createInteriors(b,solid,f=>{furniture.push(f);furnitureSolids(f);},random));
 b.partitions=colliders.filter(c=>c.owner===id&&c.kind==='partition');
 // Facilities face a shared forecourt. Every bin is linked to an actual facility.
 const plaza={x:b.x,z:cz+5.6};b.plaza=plaza;
 for(const [side,x]of [[-1,cx-5.5],[1,cx+2.5]]){const stop=id%8===0&&side===1,facility=add(stop?'busstop':'bench',x,cz+5.5,{yaw:stop?0:side===-1?Math.PI/2:-Math.PI/2,building:id,context:stop?'transit':'forecourt',faces:stop?{x,z:cz+8.5}:plaza});if(facility)add('bin',x,cz+3.65,{building:id,context:'paired',pairedWith:facility.id});}
 if(id%3!==0){const bay={id:'bay-'+id,x:cx+5.65,z:cz-2.8,w:2.45,d:5.1,yaw:Math.PI,building:id};parking.push(bay);add(id%4===0?'van':id%4===1?'hatchback':'car',bay.x,bay.z,{yaw:bay.yaw,context:'parking',parkingId:bay.id,building:id});add('parkingmeter',bay.x+bay.w/2+.25,cz,{context:'parking-control',parkingId:bay.id,building:id});}
 else{const pallet=add('pallet',cx+5.6,cz-3,{context:'service',building:id});if(pallet)add('crate',cx+5.6,cz-3,{y:.2,supportId:pallet.id,context:'stack',building:id});add('barrel',cx+5.6,cz-.4,{context:'service',building:id});}
 add(id%2?'dumpster':'powerbox',cx+5.6,cz-6.1,{context:'service',building:id});
 if(id%3===0)add('barrier',cx+5.6,cz+1.8,{yaw:Math.PI/2,context:'service',building:id});
 if(id%3===0)add('planter',cx+.9,cz+6.5,{context:'forecourt',building:id});
 if(['postoffice','library','school'].includes(type))add('mailbox',b.x-1.8,b.z+d/2+.7,{context:'entrance',building:id});
 if(id%5===0)add('bike',cx+.8,cz+3.5,{context:'entrance',building:id});
 add('bollard',b.x+3,cz+3.4,{context:'pedestrian-entry',building:id});
 add(DISTRICT_STYLES[district].tree,cx+6.25,cz+6.3,{context:'planted',building:id});
 if(id%4===0){add('fence',cx-7.3,cz,{yaw:Math.PI/2,length:14.6,context:'boundary',building:id,lineId:`${id}/west`});add('fence',cx,cz-7.3,{length:14.6,context:'boundary',building:id,lineId:`${id}/back`});}

}
// Uniform lights follow the pavement line; intersections remain unobstructed.
for(const road of ROADS)for(const along of BLOCK_CENTERS){if(Math.abs(road)===120&&Math.abs(along)>96)continue;add('lamp',road+(road===120?-3.65:3.65),along,{yaw:Math.PI,context:'streetlight',road});}
for(const x of [-96,-48,0,48,96])for(const z of [-96,-48,0,48,96]){add('hydrant',x+3.65,z+5.4,{context:'intersection',road:x});add('sign',x+3.65,z+7.1,{context:'intersection',road:x});}
// Parallel traffic occupies one lane per segment and never parks on a crossing.
for(const road of INNER_ROADS)for(const z of BLOCK_CENTERS)if(random()<.6){const north=random()<.5;add(random()<.3?'van':'car',road+(north?1.35:-1.35),z,{yaw:north?0:Math.PI,context:'traffic',road,direction:north?'north':'south'});}
// Increase the existing outdoor population by 15%, with collision-checked semantic pockets.
const baseStreetCount=props.length,targetStreetCount=Math.ceil(baseStreetCount*1.15),newTypes=['trafficCone','streetClock','newsstand','publicPhone','busMap','atm','vendingMachine','bikeRack','fireExtinguisher','sandbagStack','surveyTripod','constructionLight','noticeBoard','waterTank','concretePipe','recyclingStation','parcelLocker','busShelterSign','roadBlock','cableReel','utilityCabinet','flowerBox','streetKiosk','deliveryTrolley','solarBollard','ticketMachine','drinkingFountain','ventStack','fireHoseCabinet','foldingBarricade'];
for(let round=0;props.length<targetStreetCount&&round<6;round++)for(const b of buildings){
 if(props.length>=targetStreetCount)break;
 const pool=round?DISTRICT_STYLES[b.district].props:newTypes,type=pool[(b.id+round)%pool.length],candidates=[[b.cx+4.05,b.cz+1.0],[b.cx-5.5,b.cz+2.2],[b.cx+.7,b.cz+6.8],[b.cx+4.1,b.cz+5.8],[b.cx-5.5,b.cz+6.9],[b.cx+6.6,b.cz+1.7]];
 for(const [x,z]of candidates){if(z>b.z+b.d/2-.3&&Math.abs(x-b.x)<b.door/2+.7)continue;if(add(type,x,z,{context:type==='trafficCone'?'parking-control':'pedestrian-service',building:b.id,yaw:0}))break;}
}
const streetDensity={base:baseStreetCount,target:targetStreetCount,actual:props.length};
// Non-overlapping terrain patches: road/sidewalk crossings share one surface, not stacked planes.
const cuts=[-MAP_HALF,MAP_HALF];for(const c of ROADS)for(const e of [-PAVEMENT,-ROAD_HALF,ROAD_HALF,PAVEMENT])if(c+e>-MAP_HALF&&c+e<MAP_HALF)cuts.push(c+e);cuts.sort((a,b)=>a-b);
for(let ix=0;ix<cuts.length-1;ix++)for(let iz=0;iz<cuts.length-1;iz++){const minX=cuts[ix],maxX=cuts[ix+1],minZ=cuts[iz],maxZ=cuts[iz+1],x=(minX+maxX)/2,z=(minZ+maxZ)/2,road=ROADS.some(c=>Math.abs(x-c)<ROAD_HALF||Math.abs(z-c)<ROAD_HALF),walk=ROADS.some(c=>Math.abs(x-c)<PAVEMENT||Math.abs(z-c)<PAVEMENT);surfaces.push({minX,maxX,minZ,maxZ,type:road?'road':walk?'pavement':'yard'});}
for(const c of ROADS)for(let n=-116;n<120;n+=4)if(!ROADS.some(r=>Math.abs(n-r)<4.5))for(const axis of ['x','z'])markings.push(axis==='x'?{x:n,z:c,w:1.8,d:.09}:{x:c,z:n,w:.09,d:1.8});
return {size:MAP_HALF*2,seed:seed>>>0,buildings,props,furniture,rooms,colliders,walls,hives:HIVES,spawn:{...SPAWN},exit:{...EXIT},surfaces,markings,parking,supplies:[],shop:{building:buildings.find(b=>b.type==='grocery').id,terminal:{...STORE.terminal}},streetDensity,districts,enemySpawns:[]};}
