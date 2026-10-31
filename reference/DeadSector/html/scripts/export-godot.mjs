// Reusable bridge: preserve the browser city's instancing and original weapon geometry.
import {createServer} from 'vite';
import {weaponSilhouette} from '../weapon-icons.js';
import {chromium} from 'playwright';
import {writeFile,mkdir} from 'node:fs/promises';
const vite=await createServer({server:{host:'127.0.0.1',port:0}});await vite.listen();
const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox','--enable-unsafe-swiftshader']});
try{
const page=await browser.newPage();await page.route('**/export-fixture',r=>r.fulfill({contentType:'text/html',body:'<body></body>'}));await page.goto(vite.resolvedUrls.local[0]+'export-fixture');
const data=await page.evaluate(async()=>{
 const T=await import('/node_modules/.vite/deps/three.js');const {createMap}=await import('/map.js');const {PeerAvatar}=await import('/peer-avatar.js');
 const scene=new T.Scene();scene.userData.exportForGodot=true;const layout=createMap(scene,42),geometries=[],materials=[],gids=new Map(),mids=new Map(),textures=[];
 function material(m){if(mids.has(m.uuid))return mids.get(m.uuid);const id=materials.length;mids.set(m.uuid,id);let texture=-1;if(m.map?.isDataTexture){const img=m.map.image,canvas=document.createElement('canvas');canvas.width=img.width;canvas.height=img.height;const ctx=canvas.getContext('2d');ctx.putImageData(new ImageData(new Uint8ClampedArray(img.data),img.width,img.height),0,0);texture=textures.length;textures.push(canvas.toDataURL('image/png'));}else if(m.map?.image instanceof HTMLCanvasElement){texture=textures.length;textures.push(m.map.image.toDataURL('image/png'));}materials.push({opacity:m.opacity??1,transparent:!!m.transparent,side:m.side,color:'#'+m.color.getHexString(),emissive:m.emissive?'#'+m.emissive.getHexString():'#000000',energy:m.emissiveIntensity||0,roughness:m.roughness??1,metalness:m.metalness||0,unlit:m.isMeshBasicMaterial||false,texture,surface:m.customProgramCacheKey?.().startsWith('city-surfaces')?Number(m.customProgramCacheKey().split('-').at(-1)):0});return id;}
 function geometry(g){if(gids.has(g.uuid))return gids.get(g.uuid);const id=geometries.length;gids.set(g.uuid,id);const a=g.attributes;geometries.push({position:Array.from(a.position.array),normal:a.normal?Array.from(a.normal.array):[],uv:a.uv?Array.from(a.uv.array):[],color:a.color?Array.from(a.color.array):[],index:g.index?Array.from(g.index.array):[],groups:g.groups});return id;}
 const batches=[];scene.updateMatrixWorld(true);scene.traverse(o=>{if(!o.isMesh||(o.material?.transparent&&o.material?.depthWrite===false))return;const transforms=[];if(o.isInstancedMesh){const m=new T.Matrix4();for(let i=0;i<o.count;i++){o.getMatrixAt(i,m);transforms.push(o.matrixWorld.clone().multiply(m).elements);}}else transforms.push(o.matrixWorld.elements);batches.push({roles:o.userData.sourceRoles||[],owners:o.userData.sourceOwners||[],name:o.name,geometry:geometry(o.geometry),materials:(Array.isArray(o.material)?o.material:[o.material]).map(material),transforms,colors:o.instanceColor?Array.from(o.instanceColor.array):[]});});
 const {WEAPONS}=await import('/weapon-config.js');const avatar=new PeerAvatar(),weapons={};
 for(const config of WEAPONS){avatar.setWeapon(config.id);avatar.body.updateMatrixWorld(true);const parts=[];avatar.body.traverse(o=>{if(o.isMesh)parts.push({geometry:geometry(o.geometry),materials:(Array.isArray(o.material)?o.material:[o.material]).map(material),transform:o.matrix.elements});});weapons[config.id]={...config,parts,grips:{support:avatar.arms[0].hand.position.toArray(),trigger:avatar.arms[1].hand.position.toArray()}};}
 const {Enemies}=await import('/enemies.js'),{ENEMY_TYPES}=await import('/waves.js'),{SHOP_ITEMS}=await import('/economy.js');
 const factory=new Enemies(new T.Scene(),[],{play(){},creature(){}});
 function node(o){o.updateMatrix();return {name:o.name,transform:o.matrix.elements,...(o.isMesh?{geometry:geometry(o.geometry),materials:(Array.isArray(o.material)?o.material:[o.material]).map(material)}:{}),children:o.children.filter(c=>c.visible).map(node)};}
 const creatures={};
 for(const type of Object.keys(ENEMY_TYPES)){creatures[type]=[];for(let skin=0;skin<(type==='zombie'?9:type==='boss'?5:1);skin++){const e=factory.spawn(type,0,0,skin,skin);creatures[type].push(node(e.visual.root));}}
 avatar.head.name='head';avatar.upper.name='upper';avatar.gun.name='weapon-mount';avatar.body.name='held-weapon';avatar.arms.forEach((a,i)=>{a.a.name=i?'right-upper-arm':'left-upper-arm';a.b.name=i?'right-forearm':'left-forearm';a.hand.name=i?'right-hand':'left-hand';});
 avatar.legs.forEach((leg,i)=>{leg.hip.name=i?'right-hip':'left-hip';leg.knee.name='knee';leg.foot.name='foot';});
 const {Operations,makeTrapModel}=await import('/operations.js'),{createGenerator,createGrowth}=await import('/mission-models.js');
 const ops=new Operations(new T.Scene(),layout,42);
 const growth=createGrowth();growth.userData.core.name='growth-core';ops.plane.userData.rotors.forEach((rotor,i)=>rotor.name='propeller-'+i);
 const mission_models={generator:node(createGenerator()),growth:node(growth),package:node(ops.makeMission('package')),zone:node(ops.makeMission('zone')),crate:node(ops.makeCrate()),plane:node(ops.plane),trap:node(makeTrapModel())};
 const soldier=node(avatar.root);
 const {FirstPersonBody}=await import('/first-person-body.js');new FirstPersonBody(avatar);
 const local_soldier=node(avatar.root);
 return {mission_models,soldier,local_soldier,buildings:layout.buildings.map(b=>({id:b.id,type:b.type,x:b.x,z:b.z,w:b.w,d:b.d,h:b.h,cx:b.cx,cz:b.cz,district:b.district})),source_bounds:scene.userData.city.bounds,source_assets:[...layout.buildings,...layout.props,...layout.furniture].map(p=>({id:p.id,type:p.type,x:p.x,y:p.y||0,z:p.z,yaw:p.yaw||0,w:p.w,h:p.h,d:p.d,building:p.building})),creatures,enemy_types:ENEMY_TYPES,shop_items:SHOP_ITEMS,hives:layout.hives,version:1,seed:42,spawn:layout.spawn,terminal:layout.shop.terminal,colliders:layout.colliders,batches,geometries,materials,textures,weapons};
});
await mkdir('godot/assets',{recursive:true});
await mkdir('godot/assets/icons',{recursive:true});
for(const id of Object.keys(data.weapons))await writeFile('godot/assets/icons/'+id+'.svg',weaponSilhouette(id).replace('<svg ','<svg xmlns="http://www.w3.org/2000/svg" width="180" height="88" ').replaceAll('currentColor','#b2c19c'));
for(let i=0;i<data.textures.length;i++)await writeFile(`godot/assets/label-${i}.png`,Buffer.from(data.textures[i].split(',')[1],'base64'));delete data.textures;await writeFile('godot/assets/city.json',JSON.stringify(data));console.log(`Godot bridge: ${data.batches.length} batches, ${data.colliders.length} colliders, all ten original weapons, seed ${data.seed}`);
}finally{await browser.close();await vite.close();}
