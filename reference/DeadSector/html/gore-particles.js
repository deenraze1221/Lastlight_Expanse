import * as T from 'three';

// One bounded instanced pool for all deaths, including replicated co-op deaths.
export class GoreParticles {
 constructor(scene,capacity=3072){
  this.capacity=capacity;this.cursor=0;this.items=new Array(capacity);this.dummy=new T.Object3D();
  this.mesh=new T.InstancedMesh(new T.BoxGeometry(1,1,1),new T.MeshStandardMaterial({color:'#b41e2d',emissive:'#510913',emissiveIntensity:.35,roughness:1,flatShading:true}),capacity);
  this.mesh.name='red-death-particles';this.mesh.count=0;this.mesh.frustumCulled=false;this.mesh.instanceMatrix.setUsage(T.DynamicDrawUsage);scene.add(this.mesh);
 }
 burst(enemy,amount){
  let seed=(Math.imul(enemy.id+1,2654435761)^(enemy.skin+17))>>>0;
  const random=()=>{seed=(1664525*seed+1013904223)>>>0;return seed/4294967296;};
  const p=enemy.rig.position,scale=enemy.scale,count=amount??(enemy.type==='brute'?112:enemy.type==='eye'?64:48);
  for(let n=0;n<count;n++){
   const slot=this.cursor++%this.capacity,angle=random()*Math.PI*2,speed=(1.1+random()*2.9)*Math.sqrt(scale),size=(.025+random()*.045)*Math.sqrt(scale);
   const item={x:p.x+(random()-.5)*enemy.radius,y:p.y+(enemy.type==='eye'?(random()-.5)*.8:(.25+random()*.65)*enemy.height),z:p.z+(random()-.5)*enemy.radius,vx:Math.sin(angle)*speed,vy:1.4+random()*3.5,vz:Math.cos(angle)*speed,size,age:0,life:1+random()*.8,rx:random()*3,ry:random()*3,spin:random()*8-4};
   this.items[slot]=item;this.mesh.count=Math.min(this.capacity,Math.max(this.mesh.count,slot+1));
   this.mesh.setColorAt(slot,new T.Color().setRGB(.45+random()*.45,.08+random()*.08,.09+random()*.05));this.write(slot,item,1);
  }
  this.mesh.instanceMatrix.needsUpdate=true;if(this.mesh.instanceColor)this.mesh.instanceColor.needsUpdate=true;
 }
 write(slot,p,fade){this.dummy.position.set(p.x,p.y,p.z);this.dummy.rotation.set(p.rx,p.ry,p.rx*.6);this.dummy.scale.setScalar(p.size*fade);this.dummy.updateMatrix();this.mesh.setMatrixAt(slot,this.dummy.matrix);}
 step(dt){
  if(dt<=0)return;let dirty=false;
  for(let i=0;i<this.mesh.count;i++){
   const p=this.items[i];if(!p)continue;p.age+=dt;dirty=true;
   if(p.age>=p.life){this.write(i,p,0);this.items[i]=undefined;continue;}
   p.vy-=9.81*dt;p.x+=p.vx*dt;p.y+=p.vy*dt;p.z+=p.vz*dt;
   if(p.y<p.size/2){p.y=p.size/2;p.vy=0;p.vx*=Math.exp(-8*dt);p.vz*=Math.exp(-8*dt);}
   else{p.rx+=p.spin*dt;p.ry+=p.spin*.6*dt;}
   this.write(i,p,Math.min(1,(p.life-p.age)/.35));
  }
  if(dirty)this.mesh.instanceMatrix.needsUpdate=true;
 }
 clear(){this.items.fill(undefined);this.mesh.count=0;this.cursor=0;}
 get active(){return this.items.reduce((n,p)=>n+Boolean(p),0);}
}
