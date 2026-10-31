import * as T from 'three';
// Local view only: the full soldier rig remains unchanged for other players.
export class FirstPersonBody {
 constructor(avatar){this.avatar=avatar;this.phase=0;this.weight=0;this.distance=0;this.root=avatar.root;this.root.scale.set(.72,1,.72);avatar.head.visible=false;avatar.upper.visible=false;for(const child of this.root.children)if(child.isMesh)child.visible=false;this.waist=new T.Group();this.waist.name='local-waist';this.root.add(this.waist);const fabric=avatar.legs[0].hip.children.find(c=>c.isMesh).material,kit=fabric.clone(),belt=new T.MeshStandardMaterial({color:'#303730',roughness:1});const add=(name,w,h,d,y,z,material)=>{const mesh=new T.Mesh(new T.CylinderGeometry(w,w,h,12),material);mesh.scale.z=d/w;mesh.position.set(0,y,z);mesh.name=name;this.waist.add(mesh);};add('local-pelvis',.215,.18,.145,.88,-.08,fabric);add('local-abdomen',.19,.20,.11,1.045,.10,kit);add('local-belt',.22,.055,.15,.96,-.08,belt);for(const leg of avatar.legs){leg.hip.position.y=.8575;leg.hip.position.z=-.10;leg.foot.position.z=0;}}
 update(player,dt){const travel=Math.max(0,(player.distance||0)-this.distance);this.distance=player.distance||0;this.phase+=travel*Math.PI*2/(player.sprinting?2.3:1.65);const moving=player.grounded&&player.speed>.08;this.weight=T.MathUtils.damp(this.weight,moving?Math.min(1,player.speed/4.5):0,12,dt);const yaw=player.yaw||0,forward=-(player.velocityX||0)*Math.sin(yaw)-(player.velocityZ||0)*Math.cos(yaw),direction=forward<-.1?-1:1;
  for(const [i,leg]of this.avatar.legs.entries()){const swing=Math.sin(this.phase+i*Math.PI)*.24*this.weight*direction,angle=.20+swing;leg.hip.rotation.set(angle,0,0);leg.hip.position.y=.1175+.74*Math.cos(angle);leg.knee.rotation.x=0;leg.foot.rotation.x=-angle;}

  this.root.position.set(player.position.x+Math.sin(yaw)*.18,player.position.y,player.position.z+Math.cos(yaw)*.18);this.root.rotation.set(0,yaw,0);
 }
}
