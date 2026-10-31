import {installPowerMaterial,POWER_SHADER} from './power-grid.js';
import {DISTRICT_STYLES} from './districts.js';
import {MAP_HALF,BACKDROP_DEPTH} from './world-config.js';
import * as T from 'three';
import {installSurfaceDetail} from './surface-detail.js';
import {generateSkyline} from './skyline.js';
// Instanced construction keeps detailed models practical in a 64-building city.
export function createCity(scene,layout){
 const palettes={concrete:'#8b8e7e',brick:'#8d6652',plaster:'#a6a58c',dark:'#303c35',wood:'#806448',steel:'#627269',rust:'#865539',glass:'#3c5654',window:'#354d50',rubber:'#202924',fabric:'#77816a',white:'#c4c6b3',leaf:'#526a46',bark:'#5d4d3b',light:'#e1d79f',road:'#343c37',pavement:'#808574',yard:'#727e62',floor:'#9a9380',paint:'#b7b79b'};
 for(const style of Object.values(DISTRICT_STYLES))palettes[style.wall]=style.color;
 const geometries={dome:new T.SphereGeometry(1,16,8,0,Math.PI*2,0,Math.PI/2),box:new T.BoxGeometry(1,1,1),cylinder:new T.CylinderGeometry(1,1,1,10),sphere:new T.IcosahedronGeometry(1,1),torus:new T.TorusGeometry(1,.1,5,12),cone:new T.ConeGeometry(1,1,8),plane:new T.PlaneGeometry(1,1)},materials={},batches=new Map();scene.userData.powerCut={value:-1};
 for(const [name,color]of Object.entries(palettes)){const m=name==='light'?new T.MeshBasicMaterial({color}):new T.MeshStandardMaterial({color,roughness:name==='glass'?.45:.92,metalness:name==='steel'?.35:0});
 if(name==='paint'){m.polygonOffset=true;m.polygonOffsetFactor=-2;m.polygonOffsetUnits=-2;}
 if(!['light','glass','paint','leaf'].includes(name))installSurfaceDetail(m,Object.values(DISTRICT_STYLES).find(s=>s.wall===name)?.surface||name);
 if(name==='light')installPowerMaterial(m,scene.userData.powerCut);
 if(name==='window'){m.customProgramCacheKey=()=> 'city-window-v3';m.emissive.set('#c7a976');m.emissiveIntensity=.02;const compile=m.onBeforeCompile;m.onBeforeCompile=shader=>{compile(shader);shader.uniforms.powerCut=scene.userData.powerCut;shader.fragmentShader=shader.fragmentShader.replace('#include <common>',`#include <common>\nuniform float powerCut;${POWER_SHADER}`);shader.fragmentShader=shader.fragmentShader.replace('#include <emissivemap_fragment>','#include <emissivemap_fragment>\n totalEmissiveRadiance*=gridPower(surfacePosition)*step(.78,cityHash(floor(vec2(surfacePosition.x+surfacePosition.z,surfacePosition.y)/vec2(1.9,2.8))));');};scene.userData.windowMaterial=m;}
 materials[name]=m;}
 materials.road.dispose();materials.road=new T.MeshPhysicalMaterial({color:palettes.road,roughness:.92,clearcoat:.0001,clearcoatRoughness:.08});installSurfaceDetail(materials.road,'road');scene.userData.roadMaterial=materials.road;
 for(const type of new Set(layout.buildings.map(b=>b.type))){const canvas=document.createElement('canvas');canvas.width=256;canvas.height=48;const ctx=canvas.getContext('2d');ctx.fillStyle='#ded9ba';ctx.textAlign='center';ctx.textBaseline='middle';ctx.font='bold 28px monospace';ctx.fillText(type==='grocery'?'SECTOR MART':({residentialHighrise:'NORTH RESIDENCES',corporateTower:'CIVIC OFFICES',hospitalTower:'MEDICAL TOWER',communicationsTower:'RELAY STATION'}[type]||type.toUpperCase()),128,25,242);const texture=new T.CanvasTexture(canvas);texture.colorSpace=T.SRGBColorSpace;texture.magFilter=T.NearestFilter;materials['label-'+type]=new T.MeshBasicMaterial({map:texture,alphaTest:.4,polygonOffset:true,polygonOffsetFactor:-2,polygonOffsetUnits:-2});if(type!=='grocery')installPowerMaterial(materials['label-'+type],scene.userData.powerCut);}
 const screen=document.createElement('canvas');screen.width=256;screen.height=160;const sc=screen.getContext('2d');sc.fillStyle='#0a1d17';sc.fillRect(0,0,256,160);sc.fillStyle='#c7e1a0';sc.font='bold 24px monospace';sc.fillText('SECTOR MART',12,32);sc.font='15px monospace';sc.fillText('> WEAPONS',12,70);sc.fillText('> SUPPLIES',12,98);sc.fillStyle='#6eae91';sc.fillRect(12,119,232,2);sc.fillText('ACCESS TERMINAL',12,145);const sm=new T.CanvasTexture(screen);sm.colorSpace=T.SRGBColorSpace;sm.magFilter=T.NearestFilter;materials['terminal-screen']=new T.MeshBasicMaterial({map:sm,polygonOffset:true,polygonOffsetFactor:-2,polygonOffsetUnits:-2});
 const anchor=new T.Matrix4(),local=new T.Matrix4(),q=new T.Quaternion(),scale=new T.Vector3(),position=new T.Vector3();let model=null,exportRole="model";const actualBounds=new Map(),primitiveKeys=new Set(),geometryKeys=new Map(),coplanarDuplicates=[];let duplicateParts=0;for(const geo of Object.values(geometries))geo.computeBoundingBox();
 function part(shape,mat,x,y,z,w,h,d,rx=0,ry=0,rz=0,tint=0){const batchKey=shape+'/'+mat;if(!batches.has(batchKey))batches.set(batchKey,[]);q.setFromEuler(new T.Euler(rx,ry,rz));local.compose(position.set(x,y,z),q,scale.set(w,h,d));const matrix=anchor.clone().multiply(local),primitiveKey=shape+'/'+mat+'/'+matrix.elements.map(v=>Math.round(v*1e5)).join(',');if(primitiveKeys.has(primitiveKey)){duplicateParts++;return;}primitiveKeys.add(primitiveKey);const geometryKey=shape+'/'+matrix.elements.map(v=>Math.round(v*1e5)).join(',');if(geometryKeys.has(geometryKey)&&geometryKeys.get(geometryKey)!==mat&&!materials[mat].polygonOffset&&!materials[geometryKeys.get(geometryKey)].polygonOffset)coplanarDuplicates.push({model:model?.id,first:geometryKeys.get(geometryKey),second:mat});geometryKeys.set(geometryKey,mat);batches.get(batchKey).push({matrix,tint:model?.variant??0,owner:model?.id??null,role:exportRole});if(model?.id!==undefined){const key=String(model.id);if(!actualBounds.has(key))actualBounds.set(key,new T.Box3());actualBounds.get(key).union(geometries[shape].boundingBox.clone().applyMatrix4(matrix));}}
 const box=(mat,w,h,d,x,y,z,rx=0,ry=0,rz=0)=>part('box',mat,x,y,z,w,h,d,rx,ry,rz);
 const cyl=(mat,r,h,x,y,z,rx=0,rz=0)=>part('cylinder',mat,x,y,z,r,h,r,rx,0,rz);
 function begin(item){model=item;anchor.makeRotationY(item.yaw||0);anchor.setPosition(item.x,item.y||0,item.z);}
 function legs(w,d,height=.7,mat='steel'){for(const x of [-w/2+.07,w/2-.07])for(const z of [-d/2+.07,d/2-.07])box(mat,.07,height,.07,x,height/2,z);}
 function furnishing(f){begin(f);const {w,h,d,type,variant:v}=f;
  if(type==='terminal'){
   box('steel',w,.12,d,0,.06,0);box('dark',.56,1.20,.46,0,.72,-.04);box('steel',.62,.07,.50,0,1.35,-.01);box('steel',w,.72,.22,0,1.84,-.02);box('dark',w-.07,.62,.025,0,1.84,.102);part('plane','terminal-screen',0,1.86,.119,.75,.47,1);for(const x of [-.43,.43])box('light',.018,.55,.015,x,1.85,.108);box('dark',.58,.04,.29,0,1.45,.16);for(let n=0;n<6;n++)box('steel',.058,.012,.04,-.20+n*.08,1.478,.18);box('steel',.34,.20,.035,0,1.03,.205);box('dark',.23,.025,.012,0,1.05,.229);box('light',.05,.025,.012,.12,.99,.23);
  }else if(type==='groceryShelf'||type==='displayRack'){
   for(const x of [-w/2+.035,w/2-.035])box('steel',.07,h,.08,x,h/2,0);
   for(let level=0;level<4;level++){const y=.12+level*(h-.25)/4;box('steel',w,.06,d,0,y,0);for(let n=0;n<5;n++)box((n+v+level)%3===0?'rust':(n+v)%2?'white':'leaf',w*.13,.18,d*.65,-w*.36+n*w*.18,y+.12,0);}
   box('dark',w,.12,d,0,h-.06,0);
  }else if(type==='checkout'){
   box('white',w,.95,d,0,.475,0);box('dark',w*.62,.05,d-.06,-w*.15,.975,0);box('steel',.42,.1,.35,w*.30,1.05,0);box('dark',.32,.20,.13,w*.30,1.15,-.09);box('glass',.25,.12,.012,w*.30,1.18,-.018);
  }else if(type==='produceStand'){
   box('wood',w,.66,d,0,.33,0);box('dark',w,.05,d,0,.685,0);for(let n=0;n<5;n++)for(const z of [-.2,.1])part('sphere',(n+v)%2?'leaf':'rust',-w*.38+n*w*.19,.82,z,.11,.13,.11);
  }else if(type==='refrigeratedCase'){
   box('white',w-.18,h-.34,d*.45,0,h/2,-d*.275);box('steel',w,.16,d,0,.08,0);box('steel',w,.16,d,0,h-.08,0);for(const x of [-w/2+.04,w/2-.04])box('steel',.08,h-.32,d,x,h/2,0);for(let n=0;n<4;n++){box('white',w-.16,.045,d*.8,0,.3+n*.38,0);for(let k=0;k<5;k++)box('rust',.15,.23,.15,-w*.35+k*w*.175,.44+n*.38,0);}
  }else if(type==='freezer'){
   box('white',w,h-.08,d,0,(h-.08)/2,0);box('glass',w-.1,.08,d-.1,0,h-.04,0);for(const x of [-w*.23,w*.23])box('steel',.32,.03,.04,x,h-.095,d*.25);
  }else if(type==='shoppingCart'){
   for(const x of [-w*.35,w*.35])for(const z of [-d*.32,d*.32])cyl('rubber',.08,.06,x,.08,z,0,Math.PI/2);
   for(const x of [-w/2+.04,w/2-.04]){box('steel',.04,.65,.04,x,.51,-d*.35);box('steel',.04,.45,d*.7,x,.60,.03);}box('steel',w,.04,d*.7,0,.40,.03);box('steel',w,.45,.04,0,.60,d*.38);box('dark',w,.05,.05,0,.925,-d*.38);
  }else if(type==='basketStand'){
   box('steel',w,.15,d,0,.075,0);for(let n=0;n<5;n++){box('leaf',w-.06,.10,d-.05,0,.25+n*.15,0);box('dark',w-.15,.02,d-.14,0,.31+n*.15,0);}
  }else if(type==='stockBoxes'){
   for(const x of [-w*.25,w*.25])for(const z of [-d*.25,d*.25])box('wood',w*.47,h*.55,d*.47,x,h*.275,z);box('wood',w*.65,h*.45-.012,d*.65,0,h*.775-.006,0);box('white',.10,.006,d*.65,0,h-.006,0);for(const side of [-1,1])box('white',.10,h*.45-.018,.006,0,h*.775-.006,side*(d*.325+.004));
  }else if(['desk','table','workbench'].includes(type)){const top=.76;legs(w,d,top-.06);box('wood',w,.06,d,0,top-.03,0);if(type==='desk'){box('steel',.3,.025,.2,0,top+.0125,-.08);box('steel',.05,.19,.05,0,top+.12,-.12);box('dark',.5,.32,.055,0,top+.36,-.15);box('glass',.44,.26,.01,0,top+.36,-.116);box('dark',.4,.025,.14,0,top+.0125,.2);}if(type==='workbench'){box('steel',w,.28,.05,0,1.1,-d/2+.025);for(let i=0;i<5;i++)box('dark',.045,.2,.045,-w*.35+i*w*.17,1.08,-d/2+.07);}}
  else if(type==='chair'||type==='stool'){legs(w,d,.44);box('wood',w,.06,d,0,.47,0);if(type==='chair'){for(const x of [-w/2+.035,w/2-.035])box('steel',.07,.45,.07,x,.725,-d/2+.035);box('fabric',w-.1,.26,.07,0,.81,-d/2+.035);}}
  else if(type==='bed'||type==='examBed'){legs(w,d,.22);box('steel',w,.12,d,0,.28,0);box('white',w-.04,.18,d-.05,0,.43,0);box('fabric',w-.08,.04,d*.58,0,.54,d*.14);box('white',w*.7,.12,.35,0,.58,-d*.32);if(type==='bed')box('wood',w,.6,.08,0,.65,-d/2+.04);}
  else if(type==='sofa'){legs(w,d,.12,'wood');box('fabric',w,.3,d,0,.27,0);box('fabric',w,.48,.18,0,.66,-d/2+.09);for(const x of [-w/2+.08,w/2-.08])box('wood',.16,.35,d,x,.58,0);}
  else if(type==='sink'){legs(w,d,.82);box('white',w,.08,d,0,.86,0);box('steel',.04,.15,.04,0,.975,-d*.25);box('steel',.04,.04,.16,0,1.03,-d*.1);}
  else if(type==='toilet'){part('sphere','white',0,.37,.05,.21,.2,.3);box('white',.22,.22,.3,0,.11,.08);box('white',w,.42,.2,0,.61,-d/2+.1);part('sphere','dark',0,.54,.12,.16,.015,.22);}
  else if(type==='tvstand'){legs(w,d,.34,'wood');box('wood',w,.08,d,0,.38,0);box('steel',.25,.06,.15,0,.45,0);box('dark',w*.9,.65,.07,0,.82,-.03);box('glass',w*.8,.54,.012,0,.82,.012);}
  else if(type==='shelf'){for(const x of [-w/2+.03,w/2-.03])box('steel',.06,h,d,x,h/2,0);box('wood',w-.12,h,.04,0,h/2,-d/2+.02);for(let level=0;level<4;level++){const y=.1+level*.43;box('wood',w-.12,.06,d-.04,0,y,0);for(let n=0;n<3;n++)box(n%2?'white':'rust',.14,.25,d*.6,-w*.28+n*w*.28,y+.155,0);}}
  else{const bodyHeight=type==='counter'?1:h;box(type==='counter'?'wood':'steel',w,bodyHeight,d-.08,0,bodyHeight/2,-.04);const count=type==='fridge'?2:3;for(let i=0;i<count;i++){const y=bodyHeight*(i+.5)/count;box('dark',w-.1,.018,.012,0,bodyHeight*i/count+.015,d/2-.03);box('wood',.045,.12,.02,w*.3,y,d/2-.01);}if(type==='washer'||type==='oven')part('sphere','glass',0,h*.5,d/2-.012,w*.32,h*.3,.012);if(type==='counter')box('dark',.3,.25,.1,0,1.125,-.1);}

 }
 // Every visible structural piece has one owner; wall/ceiling joins meet without duplicate faces.
 for(const b of layout.buildings){begin(b);const {w,d,h,variant:v}=b,t=.22,door=b.door,wall=DISTRICT_STYLES[b.district]?.wall||'concrete';
  box(wall,w,3.12,t,0,1.56,-d/2+t/2);for(const side of [-1,1])box(wall,t,3.12,d-2*t,side*(w/2-t/2),1.56,0);for(const side of [-1,1])box(wall,(w-door)/2,3,t,side*(w/4+door/4),1.5,d/2-t/2);box(wall,w,.12,t,0,3.06,d/2-t/2);box('floor',w,.18,d,0,3.21,0);
  exportRole='partition';for(const p of b.partitions)box('plaster',p.maxX-p.minX,p.maxY-p.minY,p.maxZ-p.minZ,(p.minX+p.maxX)/2-b.x,(p.maxY+p.minY)/2,(p.minZ+p.maxZ)/2-b.z);
  exportRole='model';for(const side of [-1,1])box('wood',.08,3,.08,side*(door/2+.04),1.5,d/2+.04);box('wood',door+.16,.08,.08,0,3.04,d/2+.04);part('plane','label-'+b.type,0,3.04,d/2+.081,door+.08,.065,1);
  const stepped=v%3===1,levels=Math.ceil((h-3.3)/2.8);let roof={x:0,z:0,w,d};
  for(let level=0;level<levels;level++){
    const bottom=3.3+level*2.8,top=Math.min(h,bottom+2.8),uw=w-(stepped?level*.45:0),ud=d-(stepped?level*.35:0),ux=stepped?level*.08:0,uz=0;
    box(wall,uw,top-bottom,ud,ux,(top+bottom)/2,uz);roof={x:ux,z:uz,w:uw,d:ud};
    const wy=bottom+(top-bottom)*.53,wh=Math.min(1.35,top-bottom-.6),spacing=1.8+(v%4)*.15;
    if(wh>.5)for(let x=-uw/2+1.2;x<uw/2-1;x+=spacing){
      box('window',.94,wh,.06,ux+x,wy,uz+ud/2+.04);box('concrete',1.1,.08,.22,ux+x,wy-wh/2-.06,uz+ud/2+.11);
      if(v%2){for(const side of [-1,1])box('dark',.05,wh+.1,.07,ux+x+side*.51,wy,uz+ud/2+.08);}
    }
    if(wh>.5)for(const side of [-1,1])for(let z=-ud/2+1.4;z<ud/2-1;z+=2.3)box('window',.06,wh,.85,ux+side*(uw/2+.04),wy,uz+z);
    if(v%2){for(const side of [-1,1]){box('dark',uw+.12,.07,.06,ux,top-.035,uz+side*(ud/2+.03));box('dark',.06,.07,ud,ux+side*(uw/2+.03),top-.035,uz);}}
    if(['apartment','hotel','motel'].includes(b.type)&&level===0){
      const face=uz+ud/2,by=bottom+.28;box('steel',2,.08,.8,ux-uw*.2,by,face+.4);for(const side of [-1,1])box('steel',.05,.7,.05,ux-uw*.2+side*.95,by+.39,face+.75);box('steel',2,.05,.05,ux-uw*.2,by+.765,face+.75);
      for(const x of [-.8,.8])box('steel',.05,Math.hypot(.5,.6),.05,ux-uw*.2+x,by-.25,face+.3,Math.atan2(.6,.5));
    }
  }
  box('dark',roof.w+.12,.16,roof.d+.12,roof.x,h+.08,roof.z);const ry=h+.16;
  box('steel',1.3,.7,1,roof.x+roof.w*.22,ry+.35,-roof.d*.15);for(let i=0;i<4;i++)box('dark',1.15,.035,.04,roof.x+roof.w*.22,ry+.25+i*.1,-roof.d*.15+.51);
  if(v%4===0){cyl('steel',.65,.9,roof.x-roof.w*.23,ry+.45,-roof.d*.18);box('steel',1.45,.1,1.45,roof.x-roof.w*.23,ry+.95,-roof.d*.18);}else if(v%4===1)cyl('rust',.1,1.8,roof.x-roof.w*.3,ry+.9,0);else if(v%4===2){box('steel',roof.w*.4,.16,roof.d*.25,roof.x,ry+.08,roof.d*.15);box('glass',roof.w*.38,.03,roof.d*.23,roof.x,ry+.175,roof.d*.15);}
  if(['shop','cafe','bakery','postoffice','clinic','grocery','cinema'].includes(b.type)){
    for(const side of [-1,1])box('steel',.06,1.105,.06,side*w*.3,3.6725,d/2+.06);box('steel',w*.65,.05,.06,0,4.25,d/2+.06);
    box('rust',w*.7,.15,1,0,3.55,d/2+.5);box('dark',w*.5,.45,.08,0,4,d/2+.05);part('plane','label-'+b.type,b.type==='clinic'?w*.06:0,4,d/2+.091,w*(b.type==='clinic'?.31:.47),.38,1);
    if(b.type==='clinic'){box('light',.14,.32,.03,-w*.18,4,d/2+.105);for(const side of [-1,1])box('light',.12,.1,.03,-w*.18+side*.13,4,d/2+.105);}
  }
  if(b.type==='grocery'){box('leaf',w*.88,.72,.12,0,3.9,d/2+.13);part('plane','label-grocery',0,3.9,d/2+.198,w*.80,.52,1);for(const side of [-1,1]){box('glass',(w-door)/2-.3,1.7,.06,side*(door/2+(w-door)/4),1.65,d/2+.06);box('steel',.06,1.8,.08,side*(w/2-.2),1.65,d/2+.08);}box('light',door-.2,.04,.18,0,2.88,d/2-.1);}
  if(b.type==='cinema'){box('rust',w*.9,.8,1.2,0,3.9,d/2+.6);part('plane','label-cinema',0,3.95,d/2+1.207,w*.7,.5,1);for(let n=0;n<10;n++)box('light',.06,.06,.02,-w*.4+n*w*.8/9,3.59,d/2+1.218);}
  if(b.type==='grocery'){
   const base=h+.16;box('steel',2.5,.14,2.5,0,base+.07,0);
   for(const x of [-1,1])for(const z of [-1,1])box('steel',.12,9.65,.12,x,base+4.925,z);
   for(let level=0;level<4;level++){const y=base+1.3+level*2.35;for(const side of [-1,1]){box('steel',.07,Math.hypot(2,2.2),.07,0,y,side,0,0,Math.atan2(2,2.2));box('steel',.07,Math.hypot(2,2.2),.07,side,y,0,Math.atan2(2,2.2));}}
   box('steel',2.4,.15,2.4,0,base+9.8,0);for(const side of [-1,1]){box('leaf',2.3,.9,.07,0,base+8.4,side*1.10);part('plane','label-grocery',0,base+8.4,side*1.14,2.15,.7,1,0,side<0?Math.PI:0);box('leaf',.07,.9,2.3,side*1.10,base+8.4,0);part('plane','label-grocery',side*1.14,base+8.4,0,2.15,.7,1,0,side*Math.PI/2);}box('light',.28,.12,.28,0,base+9.935,0);
  }
  if(b.type==='firestation'){box('rust',w,.40,.07,0,3.6,d/2+.05);for(const side of [-1,1])box('white',.36,2.8,.06,side*(door/2+.3),1.4,d/2+.07);}
  if(b.type==='residentialHighrise'){for(let y=5;y<h-1;y+=2.8)for(const side of [-1,1]){box('concrete',2,.12,.7,side*w*.27,y,d/2+.28);box('steel',2,.5,.05,side*w*.27,y+.3,d/2+.6);}}
  if(b.type==='corporateTower'){for(let x=-w/2+.5;x<w/2;x+=1.5)box('steel',.12,h-3.4,.12,x,(h+3.4)/2,d/2+.12);for(let y=4;y<h;y+=2.8)box('glass',w-.7,.65,.025,0,y,d/2+.09);box('steel',w*.65,1.4,d*.65,0,h+.7,0);}
  if(b.type==='hospitalTower'){for(const side of [-1,1])box('white',.4,h-3.3,.12,side*(w/2-.4),(h+3.3)/2,d/2+.1);box('light',1.1,.26,.04,0,h-1,d/2+.16);box('light',.26,1.1,.04,0,h-1,d/2+.17);box('paint',2,.035,.25,0,h+.18,0);for(const x of [-.8,.8])box('paint',.25,.035,1.6,x,h+.18,0);}
  if(b.type==='communicationsTower'){for(const side of [-1,1])box('dark',.28,h,.18,side*w*.35,h/2,d/2+.09);box('steel',.14,4,.14,0,h+2,0);for(let n=0;n<4;n++){box('steel',1.8-n*.3,.06,.06,0,h+.8+n*.8,0);box('light',.15,.15,.15,0,h+4,0);}}
  // District silhouettes differ without blocking the existing doors or sidewalks.
  if(b.district==='industrial'){for(const side of [-1,1]){box('rust',.12,h,.12,side*(w/2-.2),h/2,d/2+.07);box('steel',.8,.6,.12,side*w*.33,3.8,d/2+.08);for(let n=0;n<4;n++)box('dark',.65,.035,.025,side*w*.33,3.6+n*.1,d/2+.15);}}
  if(b.district==='residential'){for(const side of [-1,1]){box('wood',.18,1.4,.10,side*w*.35,4.8,d/2+.09);box('wood',.18,1.4,.10,side*w*.35+.85,4.8,d/2+.09);}}
  if(b.district==='medical'){for(const side of [-1,1])box('white',.18,h-3.4,.10,side*(w/2-.3),(h+3.4)/2,d/2+.06);}
  if(b.district==='market'&&b.type!=='grocery'){box('rust',w*.65,.10,.65,0,3.32,d/2+.25);for(let n=0;n<5;n++)box('fabric',w*.11,.08,.65,-w*.26+n*w*.13,3.41,d/2+.25);}
  if(b.type==='courthouse'){for(const side of [-1,1]){cyl('concrete',.2,3,side*w*.35,1.5,d/2+.35);box('concrete',.55,.12,.55,side*w*.35,.06,d/2+.35);}box('white',w,.35,.9,0,3.28,d/2+.25);box('concrete',w*.8,.3,d*.8,0,h+.3,0);for(let x=-w*.32;x<w*.4;x+=1.3)box('white',.2,h-4,.13,x,(h+4)/2,d/2+.13);}
  if(b.type==='artDecoTower'){for(const side of [-1,1]){box('white',.22,h-3.4,.16,side*w*.35,(h+3.4)/2,d/2+.1);box('steel',.15,3,.15,side*.6,h+1.5,0);}box('concrete',2,1.8,2,0,h+.9,0);box('steel',.14,4,.14,0,h+3,0);box('light',.18,.18,.18,0,h+5,0);}
  if(b.type==='freightDepot'){for(let n=0;n<5;n++){const x=-w*.4+n*w*.2;box('steel',w*.19,.14,d*.85,x,h+.4,0,0,0,.18);box('glass',w*.16,.25,.8,x,h+.25,d*.28);}for(const side of [-1,1]){box('rust',.3,2.8,.12,side*(door/2+.25),1.4,d/2+.12);box('dark',1.2,.65,.06,side*w*.33,1.8,d/2+.06);}}
  if(b.type==='observatory'){cyl('concrete',3,.6,0,h+.46,0);part('dome','steel',0,h+.76,0,3,2.6,3);box('dark',.24,2.2,.08,0,h+1.8,2.1);cyl('steel',.25,2.4,0,h+2.2,1.2,Math.PI/3);for(const side of [-1,1])box('white',.18,h-3.4,.12,side*w*.35,(h+3.4)/2,d/2+.08);}
  if(b.type==='tower'){for(const side of [-1,1])box('dark',.18,h,.12,side*(w/2-.5),h/2,d/2+.08);box('steel',1,.3,1,0,h+.31,0);}
 }
 for(const f of layout.furniture)furnishing(f);
 const posts=new Set();
 for(const p of layout.props){begin(p);const {type,w,h,d,variant:v}=p;
  if(type==='recyclingStation'){for(const side of [-1,1]){box(side<0?'leaf':'steel',.6,1.1,d-.08,side*.33,.55,-.04);box('dark',.35,.12,.02,side*.33,.88,d/2-.06);box('white',.18,.2,.025,side*.33,.6,d/2-.05);}box('dark',w,.08,d,0,1.16,0);}
  else if(type==='parcelLocker'){box('steel',w,h,d-.08,0,h/2,-.04);for(let row=0;row<4;row++)for(let col=0;col<3;col++){const x=-.51+col*.51,y=.25+row*.45;box('dark',.48,.42,.015,x,y,d/2-.06);box('rust',.43,.37,.018,x,y,d/2-.045);box('white',.06,.02,.02,x+.12,y,d/2-.03);}}
  else if(type==='busShelterSign'){cyl('steel',.035,2.1,0,1.05,0);box('leaf',w,.65,.12,0,2.075,0);box('white',w*.7,.08,.015,0,2.2,.07);box('white',w*.5,.2,.015,0,1.96,.07);}
  else if(type==='roadBlock'){box('concrete',w,.64,d,0,.32,0);box('rust',w,.16,d,0,.72,0);for(let x=-.5;x<.7;x+=.35)box('white',.14,.16,.012,x,.72,d/2-.06);}
  else if(type==='cableReel'){cyl('wood',.45,.07,0,.5,-.36,Math.PI/2);cyl('wood',.45,.07,0,.5,.36,Math.PI/2);cyl('rubber',.32,.65,0,.5,0,Math.PI/2);cyl('dark',.08,.8,0,.5,0,Math.PI/2);box('wood',.7,.1,.7,0,.05,0);}
  else if(type==='utilityCabinet'){box('steel',w,h,d-.10,0,h/2,-.05);for(const side of [-1,1]){box('dark',.48,1.3,.018,side*.27,.72,d/2-.06);for(let n=0;n<4;n++)box('steel',.35,.025,.02,side*.27,.3+n*.1,d/2-.044);box('white',.06,.18,.022,side*.12,.9,d/2-.04);}}
  else if(type==='flowerBox'){box('concrete',w,.35,d,0,.175,0);box('yard',w-.1,.02,d-.1,0,.36,0);for(let n=0;n<5;n++){part('sphere','leaf',-.4+n*.2,.47,0,.13,.17,.20);part('sphere',n%2?'rust':'white',-.4+n*.2,.62,0,.05,.03,.05);}}
  else if(type==='streetKiosk'){box('steel',w,h-.12,d-.10,0,(h-.12)/2,-.05);box('rust',w,.12,d,0,h-.06,0);box('dark',w-.1,1.3,.04,0,1.1,d/2-.06);box('white',w-.18,.6,.015,0,1.3,d/2-.03);for(let n=0;n<4;n++)box('leaf',w-.3,.04,.018,0,1.1+n*.12,d/2-.02);}
  else if(type==='deliveryTrolley'){box('steel',w,.07,d,0,.18,0);for(const x of [-w*.4,w*.4]){cyl('steel',.025,1,x,.63,-d*.4);for(const z of [-d*.35,d*.35])cyl('rubber',.10,.07,x,.1,z,0,Math.PI/2);}box('steel',w,.05,.05,0,1.12,-d*.4);box('wood',w-.08,.48,d-.1,0,.455,0);}
  else if(type==='solarBollard'){cyl('steel',.10,.9,0,.45,0);cyl('light',.12,.13,0,.965,0);box('glass',.26,.06,.26,0,1.06,0);}
  else if(type==='atm'){box('steel',w,h,d-.08,0,h/2,-.04);box('dark',w*.8,.65,.035,0,1.25,d/2-.035);box('glass',w*.6,.34,.012,0,1.35,d/2-.012);box('dark',.42,.035,.03,0,.77,d/2-.0125);for(let i=0;i<3;i++)box('light',.08,.04,.012,-.12+i*.12,.98,d/2-.012);}
  else if(type==='vendingMachine'){box('rust',w,h,d-.08,0,h/2,-.04);box('glass',w*.70,h*.65,.025,-w*.08,h*.58,d/2-.035);for(let row=0;row<4;row++)for(let col=0;col<3;col++)box((row+col)%2?'white':'leaf',.14,.19,.012,-.33+col*.24,.6+row*.28,d/2-.012);box('dark',.65,.12,.035,0,.24,d/2-.02);}
  else if(type==='bikeRack'){for(const x of [-.65,0,.65]){cyl('steel',.025,.70,x,.35,-.18);cyl('steel',.025,.70,x,.35,.18);box('steel',.05,.05,.41,x,.72,0);}box('steel',w,.06,.50,0,.03,0);}
  else if(type==='fireExtinguisher'){cyl('rust',.13,.63,0,.315,0);cyl('steel',.06,.12,0,.69,0);box('dark',.27,.035,.08,0,.77,0);box('white',.12,.18,.025,0,.40,.125);}
  else if(type==='sandbagStack'){for(let row=0;row<3;row++)for(let col=0;col<3;col++)part('sphere','fabric',-.51+col*.51+(row%2)*.04,.13+row*.24,0,.24,.12,.28);}
  else if(type==='surveyTripod'||type==='constructionLight'){
   const hub=h*.64;for(let n=0;n<3;n++){const angle=n*Math.PI*2/3,foot=new T.Vector3(Math.sin(angle)*(w*.43),.035,Math.cos(angle)*(d*.43)),top=new T.Vector3(0,hub,0),v=top.clone().sub(foot),rotation=new T.Euler().setFromQuaternion(new T.Quaternion().setFromUnitVectors(new T.Vector3(0,1,0),v.clone().normalize())),mid=top.clone().add(foot).multiplyScalar(.5);box('steel',.035,v.length(),.035,mid.x,mid.y,mid.z,rotation.x,rotation.y,rotation.z);box('rubber',.09,.055,.09,foot.x,.0275,foot.z);}cyl('steel',.035,h*.30,0,hub+h*.15,0);box('rust',w*.7,.25,d*.65,0,h-.15,0);box(type==='constructionLight'?'light':'glass',w*.5,.14,.025,0,h-.15,d*.34);
  }
  else if(type==='ticketMachine'){box('steel',w,h,d-.06,0,h/2,-.03);box('glass',w*.7,.3,.02,0,1.1,d/2-.015);box('dark',.3,.04,.025,0,.75,d/2-.01);for(let n=0;n<3;n++)box('white',.05,.05,.025,-.1+n*.1,.9,d/2-.01);}
  else if(type==='drinkingFountain'){box('concrete',.35,.8,.30,0,.4,0);box('steel',w,.12,d,0,.86,0);cyl('steel',.025,.08,.15,.96,0);box('dark',.25,.01,.18,0,.925,0);}
  else if(type==='ventStack'){box('concrete',w,.15,d,0,.075,0);cyl('rust',.18,1.35,0,.825,0);cyl('steel',.3,.12,0,1.56,0);cyl('dark',.2,.06,0,1.65,0);}
  else if(type==='fireHoseCabinet'){box('steel',.08,.4,.12,-w*.35,.2,0);box('steel',.08,.4,.12,w*.35,.2,0);box('rust',w,1.2,d-.08,0,1,-.04);box('dark',w-.08,1.05,.025,0,1,d/2-.015);part('torus','rust',0,1.05,d/2-.025,.24,.24,.24);box('white',.3,.08,.02,0,.65,d/2-.01);}
  else if(type==='foldingBarricade'){for(const side of [-1,1])for(const z of [-.15,.15])box('steel',.06,.9,.06,side*.56,.45,z,z<0?-.2:.2);box('rust',w,.26,.10,0,.84,0);for(let n=0;n<5;n++)box('white',.12,.24,.012,-.56+n*.28,.84,.056);}
  else if(type==='noticeBoard'){for(const x of [-w*.4,w*.4])box('wood',.08,h,.08,x,h/2,0);box('wood',w,1,.12,0,h-.55,0);for(let n=0;n<4;n++)box('white',.27,.55,.015,-.52+n*.34,h-.55,.07);}
  else if(type==='waterTank'){for(const x of [-.45,.45])for(const z of [-.45,.45])box('steel',.1,.4,.1,x,.2,z);cyl('steel',.60,1.25,0,1.0,0);cyl('dark',.62,.07,0,1.65,0);cyl('steel',.12,.1,0,1.74,0);}
  else if(type==='concretePipe'){cyl('concrete',.40,1.5,0,.41,0,0,Math.PI/2);for(const x of [-.76,.76]){cyl('dark',.31,.014,x,.41,0,0,Math.PI/2);part('torus','concrete',x,.41,0,.36,.36,.36,0,Math.PI/2);}}
  else if(type==='trafficCone'){box('rubber',w,.06,d,0,.03,0);part('cone','rust',0,.36,0,.16,.62,.16);part('cone','white',0,.38,0,.11,.15,.11);}
  else if(type==='streetClock'){cyl('steel',.06,2.8,0,1.4,0);cyl('dark',.3,.10,0,3,0,Math.PI/2);cyl('white',.25,.012,0,3,.06,Math.PI/2);box('dark',.025,.17,.012,0,3.06,.073);box('dark',.14,.025,.012,.055,3,.073);}
  else if(type==='newsstand'){box('steel',w,h-.1,d,0,(h-.1)/2,0);box('rust',w,.1,d,0,h-.05,0);for(let n=0;n<4;n++){box('dark',w-.12,.24,.02,0,.3+n*.36,d/2-.02);for(const x of [-w*.28,0,w*.28])box('white',.30,.19,.02,x,.33+n*.36,d/2-.007);}}
  else if(type==='publicPhone'){box('steel',w,.08,d,0,.04,0);box('steel',w*.75,1.88,d*.5,0,1.02,-d*.25);box('dark',w*.50,.6,.08,0,1.30,.035);box('glass',w*.30,.13,.015,0,1.48,.085);box('dark',.08,.40,.10,-w*.19,1.2,.13);}
  else if(type==='busMap'){for(const x of [-w*.43,w*.43])box('steel',.06,h,.06,x,h/2,0);box('dark',w,1.1,d,0,1.5,0);box('white',w-.12,.9,.02,0,1.5,d/2-.025);for(let n=0;n<4;n++)box('leaf',w-.25,.025,.012,0,1.2+n*.18,d/2-.01);}
  else if(type==='bench'||type==='busstop'){for(let i=0;i<4;i++){box('wood',type==='bench'?w-.1:w-.3,.055,.12,0,.475,-.21+i*.14);box('wood',w-.15,.09,.06,0,.65+i*.09,-.24);}for(const x of [-w*.38,w*.38]){box('steel',.09,.45,.48,x,.225,0);box('steel',.09,.45,.08,x,.725,-.24);}if(type==='busstop'){for(const x of [-w/2+.06,w/2-.06])box('steel',.12,h-.1,.12,x,(h-.1)/2,-d/2+.06);box('steel',w,.1,d,0,h-.05,0);}}
  else if(type==='fence'){const length=p.length,segments=Math.ceil(length/2.4);for(let i=0;i<=segments;i++){const x=-length/2+i*length/segments,point=new T.Vector3(x,0,0).applyMatrix4(anchor),key=`${point.x.toFixed(3)}/${point.z.toFixed(3)}`;if(!posts.has(key)){posts.add(key);box('steel',.1,h,.1,x,h/2,0);}}
   for(const y of [.2,h-.2])box('steel',length-.1,.06,.06,0,y,0);for(let x=-length/2+.2;x<length/2-.15;x+=.24)box('steel',.025,h-.4,.025,x,h/2,0);
  }
  else if(type==='car'||type==='van'||type==='hatchback'){const color=v%3?'steel':'rust';box(color,w-.4,.5,d-.12,0,.65,0);const cabin=type==='van'?d*(.6+(v%3)*.02):type==='hatchback'?d*(.49+(v%3)*.02):d*(.43+(v%3)*.02);box('glass',w-.42,h-.98,cabin,0,(h+.82)/2,-.08);box(color,w-.32,.08,cabin+.1,0,h-.04,-.08);
   for(const z of [-d*.32,d*.32]){cyl('steel',.05,w-.36,0,.32,z,0,Math.PI/2);for(const side of [-1,1]){cyl('rubber',.32,.18,side*(w/2-.09),.32,z,0,Math.PI/2);box('steel',.06,.03,.06,side*w*.3,.385,z);}}for(const x of [-w*.32,w*.32])box('light',.3,.13,.06,x,.72,-d/2+.03);box('dark',w-.2,.13,.09,0,.4,-d/2+.045);box('dark',w-.2,.13,.09,0,.4,d/2-.045);
  }
  else if(type==='lamp'){cyl('steel',.09,h-.2,0,(h-.2)/2,0);box('steel',.24,.12,.24,0,h-.14,0);box('light',.25,.16,.25,0,h-.08,0);}
  else if(['tree','birch','pine'].includes(type)){cyl(type==='birch'?'white':'bark',.11,h*.65,0,h*.325,0);if(type==='pine'){for(let n=0;n<3;n++)part('cone','leaf',0,h*.45+n*h*.16,0,w*.5-n*.15,h*.45,d*.5-n*.15);}else{part('sphere','leaf',0,h*.72,0,w*.5,h*.28,d*.5);}}
  else if(type==='bush'){cyl('bark',.04,h*.25,0,h*.125,0);for(const x of [-w*.2,w*.2])part('sphere','leaf',x,h*.55,0,w*.3,h*.4,d*.5);}
  else if(type==='planter'){box('concrete',w,.35,d,0,.175,0);box('yard',w-.1,.03,d-.1,0,.365,0);for(const x of [-w*.3,0,w*.3]){cyl('bark',.025,.15,x,.45,0);part('sphere','leaf',x,.535,0,.18,.115,.16);}}
  else if(type==='parkingmeter'){cyl('steel',.045,.95,0,.475,0);box('steel',.25,.35,.22,0,1.125,0);box('glass',.16,.12,.01,0,1.16,.115);}
  else if(type==='mailbox'){cyl('steel',.065,.75,0,.375,0);box('steel',w,.55,d,0,1.025,0);box('dark',w*.75,.04,.012,0,1.12,d/2-.006);}
  else if(type==='bike'){
    for(const x of [-.5,.5])part('torus','rubber',x,.286,0,.26,.26,.26);
    const rod=(a,b,r=.025)=>{const delta=new T.Vector3(...b).sub(new T.Vector3(...a)),euler=new T.Euler().setFromQuaternion(new T.Quaternion().setFromUnitVectors(new T.Vector3(0,1,0),delta.clone().normalize()));part('cylinder','steel',(a[0]+b[0])/2,(a[1]+b[1])/2,(a[2]+b[2])/2,r,delta.length(),r,euler.x,euler.y,euler.z);};
    for(const [a,b]of [[[0,.32,0],[-.5,.286,0]],[[0,.32,0],[-.15,.7,0]],[[-.5,.286,0],[-.15,.7,0]],[[-.15,.7,0],[.35,.7,0]],[[0,.32,0],[.35,.7,0]],[[.35,.7,0],[.5,.286,0]],[[.35,.7,0],[.4,.94,0]],[[.4,.94,-.2],[.4,.94,.2]]])rod(a,b);box('dark',.28,.06,.18,-.15,.76,0);
  }
  else if(type==='barrel'||type==='bin'){cyl(type==='barrel'?'rust':'steel',w*.45,h-.03,0,(h-.03)/2,0);for(const y of [.04,h*.28,h*.72,h-.04])cyl('dark',w*.48,.035,0,y,0);cyl('steel',w*.46,.03,0,h-.015,0);}
  else if(type==='crate'||type==='pallet'){const height=type==='pallet'?.2:h;for(let i=0;i<5;i++)box('wood',w/5-.012,.05,d,-w/2+(i+.5)*w/5,height-.025,0);for(const x of [-w/2+.06,w/2-.06])box('wood',.12,height-.05,d,x,(height-.05)/2,0);if(type==='crate')for(let i=0;i<5;i++)for(const z of [-d/2+.025,d/2-.025])box('wood',w/5-.012,height-.05,.05,-w/2+(i+.5)*w/5,(height-.05)/2,z);}
  else if(type==='dumpster'){box('steel',w-.08,h-.2,d-.08,0,(h-.2)/2+.1,0);box('dark',w,.1,d,0,h-.05,0);for(const x of [-w*.35,w*.35])for(const z of [-d*.3,d*.3])cyl('rubber',.08,.2,x,.1,z);}
  else if(type==='barrier'){box('concrete',w,.22,d,0,.11,0);box('concrete',w-.12,h-.22,d*.55,0,.22+(h-.22)/2,0);for(let i=0;i<5;i++)box('dark',.16,.15,.02,-w*.35+i*w*.175,h-.15,-d*.28);}
  else if(type==='hydrant'){cyl('rust',.14,.7,0,.35,0);cyl('rust',.18,.1,0,.75,0);box('steel',.5,.14,.15,0,.5,0);}
  else if(type==='bollard')cyl('steel',.12,h,0,h/2,0);
  else if(type==='sign'){cyl('steel',.035,h-.1,0,(h-.1)/2,0);box('dark',w,.4,.08,0,h-.2,0);}
  else{box('steel',w,h,d,0,h/2,0);for(let n=0;n<6;n++)box('dark',w*.7,.03,.015,0,h*.2+n*h*.1,-d/2+.008);}
 }
 // One exact terrain surface per point. Interior finishes replace, rather than overlay, yard tiles.
 anchor.identity();model=null;
 function surface(x,z,w,d,type){part('plane',type,x,0,z,w,d,1,-Math.PI/2);}
 for(const s of layout.surfaces){const x=(s.minX+s.maxX)/2,z=(s.minZ+s.maxZ)/2;if(s.type!=='yard'){surface(x,z,s.maxX-s.minX,s.maxZ-s.minZ,s.type);continue;}const b=layout.buildings.find(b=>x>b.cx-8&&x<b.cx+8&&z>b.cz-8&&z<b.cz+8);if(!b){surface(x,z,s.maxX-s.minX,s.maxZ-s.minZ,'yard');continue;}
  const xs=[s.minX,...[b.x-b.w/2,b.x+b.w/2].filter(x=>x>s.minX&&x<s.maxX),s.maxX].sort((a,b)=>a-b),zs=[s.minZ,...[b.z-b.d/2,b.z+b.d/2].filter(z=>z>s.minZ&&z<s.maxZ),s.maxZ].sort((a,b)=>a-b);for(let i=0;i<xs.length-1;i++)for(let j=0;j<zs.length-1;j++){const px=(xs[i]+xs[i+1])/2,pz=(zs[j]+zs[j+1])/2,inside=px>b.x-b.w/2&&px<b.x+b.w/2&&pz>b.z-b.d/2&&pz<b.z+b.d/2;surface(px,pz,xs[i+1]-xs[i],zs[j+1]-zs[j],inside?'floor':pz>b.z+b.d/2?'pavement':'yard');}
 }
 for(const m of layout.markings)part('plane','paint',m.x,.002,m.z,m.w,m.d,1,-Math.PI/2);
 for(const bay of layout.parking){for(const side of [-1,1])part('plane','paint',bay.x+side*bay.w/2,.002,bay.z,.055,bay.d,1,-Math.PI/2);part('plane','paint',bay.x,.002,bay.z-bay.d/2,bay.w,.055,1,-Math.PI/2);}
 // Background shares instanced materials, without interiors or gameplay colliders.
 const skyline=generateSkyline(layout.seed??1);anchor.identity();model=null;
 for(const b of skyline){anchor.makeTranslation(b.x,0,b.z);const wall=['concrete','brick','plaster'][b.variant%3];box(wall,b.w,b.h,b.d,0,b.h/2,0);box('dark',b.w+.25,.3,b.d+.25,0,b.h+.15,0);box('steel',2,1.2,2,0,b.h+.9,0);for(let y=2.5;y<b.h-1;y+=3.2){for(let x=-b.w/2+1.6;x<b.w/2-1;x+=3.3)for(const side of [-1,1])box('glass',1.15,1.6,.06,x,y,side*(b.d/2+.04));for(let z=-b.d/2+1.6;z<b.d/2-1;z+=3.3)for(const side of [-1,1])box('glass',.06,1.6,1.15,side*(b.w/2+.04),y,z);}}
 anchor.identity();for(const [x,z,w,d]of [[0,-(MAP_HALF+50),450,100],[0,MAP_HALF+50,450,100],[-(MAP_HALF+50),0,100,250],[MAP_HALF+50,0,100,250]])surface(x,z,w,d,'road');
 scene.userData.skyline={count:skyline.length,depth:BACKDROP_DEPTH,extent:MAP_HALF+BACKDROP_DEPTH};
 // Instanced materials support geometry reuse without thousands of draw calls.
 for(const [key,records]of batches){const [shape,mat]=key.split('/'),mesh=new T.InstancedMesh(geometries[shape],materials[mat],records.length);records.forEach((record,i)=>{mesh.setMatrixAt(i,record.matrix);const v=record.tint%16,shade=.9+(v%4)*.035;mesh.setColorAt(i,new T.Color(shade,shade-(v%3)*.015,shade-(v%5)*.012));});mesh.instanceMatrix.needsUpdate=true;mesh.castShadow=shape!=='plane'&&mat!=='glass'&&mat!=='light';// Ground receives cast shadows; solid instanced facades avoid self-shadow acne.
 mesh.receiveShadow=shape==='plane'&&mat!=='light';mesh.computeBoundingBox();mesh.computeBoundingSphere();mesh.name='city/'+key;if(scene.userData.exportForGodot)mesh.userData.sourceOwners=records.map(r=>r.owner);if(scene.userData.exportForGodot)mesh.userData.sourceRoles=records.map(r=>r.role);scene.add(mesh);}
 scene.userData.city={removedDuplicateParts:duplicateParts,coplanarDuplicates,instances:[...batches.values()].reduce((n,v)=>n+v.length,0),drawBatches:batches.size,assetCount:layout.props.length+layout.furniture.length+layout.buildings.length,bounds:Object.fromEntries([...actualBounds].map(([key,b])=>[key,{min:b.min.toArray(),max:b.max.toArray()}]))};
}
