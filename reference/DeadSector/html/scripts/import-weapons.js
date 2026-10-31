import {readFile,writeFile} from 'node:fs/promises';
import * as THREE from 'three';
import {FBXLoader} from 'three/addons/loaders/FBXLoader.js';
import {mergeGroups} from 'three/addons/utils/BufferGeometryUtils.js';
import {WEAPONS} from '../weapon-config.js';
import {paintWeapon,GRIPS} from './weapon-materials.js';

async function load(file){
  const bytes=await readFile(file);
  const root=new FBXLoader().parse(bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength),'');
  root.updateMatrixWorld(true);return root;
}
const old=await load('assets/imported/Guns/SciFi_gun_2-2.fbx');
const current=await load('assets/imported/Guns/gunsv2.fbx');
const lengths={Pistol:.48,Helsing:.93,Tikhar:1.12,Shambler:.92,Shotgun:.88,Kalash:.96,VSV:1,Scorpion:.63,Vector:.68,G36:.88};
const records=[];
for(const {id,label} of WEAPONS){
  const body=(id==='Pistol'?old:current).getObjectByName(id);
  if(!body?.isMesh)throw Error('Missing named weapon: '+id);
  const mag=id==='Pistol'?old.getObjectByName(id+'Magazine'):null;
  const bounds=new THREE.Box3().setFromObject(body),center=bounds.getCenter(new THREE.Vector3()),length=lengths[id];
  // Original pistol points +Z; the new FBX's barrels point +X. Both become -Z.
  const scale=length/(id==='Pistol'?bounds.max.z-bounds.min.z:bounds.max.x-bounds.min.x);
  function convert(mesh){
    const geometry=mesh.geometry.clone().applyMatrix4(mesh.matrixWorld);
    geometry.translate(-center.x,-center.y,-center.z);geometry.scale(scale,scale,scale);
    geometry.rotateY(id==='Pistol'?Math.PI:Math.PI/2);
    if(!geometry.groups.length)geometry.addGroup(0,geometry.index?.count??geometry.attributes.position.count,0);
    if(geometry.groups.length>1)mergeGroups(geometry);
    const materials=id==='Pistol'?(Array.isArray(mesh.material)?mesh.material:[mesh.material]).map(m=>({name:m.name,color:m.color.getHex()})):paintWeapon(geometry,id,length);
    for(const attribute of Object.values(geometry.attributes))for(let i=0;i<attribute.array.length;i++)attribute.array[i]=Math.round(attribute.array[i]*1e5)/1e5;
    return {geometry:geometry.toJSON(),materials};
  }
  const bodyPart=convert(body),positions=bodyPart.geometry.data.attributes.position.array;
  let lo=Infinity,hi=-Infinity;
  for(let i=0;i<positions.length;i+=3)if(positions[i+2]<-length*.49){lo=Math.min(lo,positions[i+1]);hi=Math.max(hi,positions[i+1]);}
  records.push({id,label,length,grips:GRIPS[id],muzzleY:(lo+hi)/2,source:id==='Pistol'?'SciFi_gun_2-2.fbx':'gunsv2.fbx',body:bodyPart,magazine:mag?convert(mag):null});
}
await writeFile('assets/models/weapons.json',JSON.stringify(records));
console.log('Converted retained pistol and all 9 gunsv2 weapons. New weapons use mechanical steel, brown and blue vertex-color finishes.');
