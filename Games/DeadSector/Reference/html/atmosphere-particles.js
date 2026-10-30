import * as T from 'three';
// Fixed buffers: decorative ash and electrical sparks never affect collisions.
export class AtmosphereParticles{
 constructor(scene,buildings,lamps){this.buildings=buildings;this.lamps=lamps;this.time=0;this.worldPositions=new Float32Array(750*3);this.initialized=false;this.ash=this.make(scene,'drifting-ash',750,'#a1aaa0',.045,.48);this.sparks=this.make(scene,'lamp-sparks',64,'#ffc875',.055,.95);this.sparks.material.blending=T.AdditiveBlending;}
 make(scene,name,count,color,size,opacity){const geometry=new T.BufferGeometry();geometry.setAttribute('position',new T.BufferAttribute(new Float32Array(count*3),3).setUsage(T.DynamicDrawUsage));const p=new T.Points(geometry,new T.PointsMaterial({color,size,transparent:true,opacity,depthWrite:false,sizeAttenuation:true}));p.name=name;p.frustumCulled=false;scene.add(p);return p;}
 update(dt,position,quality,powered){this.time+=Math.max(0,dt);const time=this.time,count={low:180,medium:420,high:750}[quality]||420,a=this.ash.geometry.attributes.position;this.ash.geometry.setDrawRange(0,count);
 if(!this.initialized){for(let i=0;i<750;i++){this.worldPositions[i*3]=position.x+(i*7.719)%36-18;this.worldPositions[i*3+1]=(i*.731)%9;this.worldPositions[i*3+2]=position.z+(i*11.127)%36-18;}this.initialized=true;}
 for(let i=0;i<750;i++){const n=i*3;let x=this.worldPositions[n]+dt*(.10+Math.sin(time*.4+i)*.025),y=this.worldPositions[n+1]-dt*.10,z=this.worldPositions[n+2]+dt*.045;x=position.x+((x-position.x+18)%36+36)%36-18;z=position.z+((z-position.z+18)%36+36)%36-18;if(y<0)y+=9;this.worldPositions.set([x,y,z],n);if(i<count){const roof=this.buildings.some(b=>Math.abs(x-b.x)<b.w/2&&Math.abs(z-b.z)<b.d/2&&y<b.h+.15);a.setXYZ(i,x,roof?-100:y,z);}}
 a.needsUpdate=true;
 const lamp=this.lamps[0],burst=time%3.7,active=lamp?.visible&&!powered&&burst<.65;this.sparks.visible=!!active;if(active){const s=this.sparks.geometry.attributes.position,n=quality==='low'?12:quality==='high'?64:32;this.sparks.geometry.setDrawRange(0,n);for(let i=0;i<n;i++){const age=(burst+i*.013)%.65,angle=i*2.399;s.setXYZ(i,lamp.position.x+Math.cos(angle)*age*.9,lamp.position.y-.06-age*age*3,lamp.position.z+Math.sin(angle)*age*.9);}s.needsUpdate=true;}
 }
}
