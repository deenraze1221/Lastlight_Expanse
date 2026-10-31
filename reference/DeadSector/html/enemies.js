import {SporeClouds} from './spore-cloud.js';
import {SAFE_EDGE} from './world-config.js';
import * as T from 'three';
import {EnemyMotion} from './enemy-motion.js';
import {createNavigation} from './navigation.js';
import {ENEMY_TYPES,bossHealth} from './waves.js';
import {createMutantVisuals,poseMutant,ZOMBIE_SKINS,BOSS_SKINS} from './mutant-visuals.js';
import {GoreParticles} from './gore-particles.js';
export const ENEMY_DETECTION=180;
const collisionBoxes=new WeakMap();
export function obstacleDistance(origin,direction,colliders,max=100){
 let cached=collisionBoxes.get(colliders);if(!cached||cached.count!==colliders.length){cached={count:colliders.length,boxes:colliders.map(b=>new T.Box3(new T.Vector3(b.minX,b.minY,b.minZ),new T.Vector3(b.maxX,b.maxY,b.maxZ)))};collisionBoxes.set(colliders,cached);}
 const ray=new T.Ray(origin,direction),point=new T.Vector3(),ex=origin.x+direction.x*max,ey=origin.y+direction.y*max,ez=origin.z+direction.z*max,minX=Math.min(origin.x,ex),maxX=Math.max(origin.x,ex),minY=Math.min(origin.y,ey),maxY=Math.max(origin.y,ey),minZ=Math.min(origin.z,ez),maxZ=Math.max(origin.z,ez);let nearest=max;
 for(const b of cached.boxes){if(b.max.x<minX||b.min.x>maxX||b.max.y<minY||b.min.y>maxY||b.max.z<minZ||b.min.z>maxZ)continue;if(ray.intersectBox(b,point))nearest=Math.min(nearest,point.distanceTo(origin));}return nearest;
}
export class Enemies{
 constructor(scene,colliders,audio,spawns=[]){this.scene=scene;this.colliders=colliders;this.base={x:0,y:0,z:0};this.physics=new EnemyMotion(colliders);this.audio=audio;this.items=[];this.projectiles=[];this.seenVomitShots=new Set();this.nextShotId=0;this.nextId=0;this.clock=0;this.nav=createNavigation(colliders,.5);this.vomitNav=createNavigation(colliders,.625,2.5);this.bossNav=createNavigation(colliders,2.1,6.6);this.fatNav=createNavigation(colliders,2.15,6);this.airNav=createNavigation(colliders,.8,2,3.5);this.geometry=new Map();this.skinMaterials=new Map();this.blood=new GoreParticles(scene);this.spores=new SporeClouds(scene);this.materials={flesh:new T.MeshStandardMaterial({color:'#8f7060',roughness:1,flatShading:true}),uniform:new T.MeshStandardMaterial({color:'#454f36',roughness:1}),wound:new T.MeshStandardMaterial({color:'#773d37',roughness:1}),bone:new T.MeshStandardMaterial({color:'#b5b49b'}),iris:new T.MeshBasicMaterial({color:'#cb9d35'}),black:new T.MeshBasicMaterial({color:'#17201c'}),bullet:new T.MeshBasicMaterial({color:'#ee9360'}),vomit:new T.MeshStandardMaterial({color:'#9ab944',emissive:'#26390b',roughness:.25})};for(const [x,z]of spawns)this.spawn('zombie',x,z);}
 material(color,basic=false){const key=color+basic;if(!this.skinMaterials.has(key))this.skinMaterials.set(key,basic?new T.MeshBasicMaterial({color}):new T.MeshStandardMaterial({color,roughness:.94,flatShading:true}));return this.skinMaterials.get(key);}
 mesh(parent,kind,size,material,x,y,z){
  const key=kind+size.join(',');
  if(!this.geometry.has(key))this.geometry.set(key,kind==='box'?new T.BoxGeometry(...size):kind==='sphere'?new T.IcosahedronGeometry(...size):kind==='capsule'?new T.CapsuleGeometry(...size):kind==='cylinder'?new T.CylinderGeometry(...size):new T.ConeGeometry(...size));
  const m=new T.Mesh(this.geometry.get(key),typeof material==='string'?this.materials[material]:material);m.position.set(x,y,z);parent.add(m);return m;
 }
 spawn(type,x,z,id=this.nextId++,skin,options={}){
  const stats=ENEMY_TYPES[type],rig=new T.Group();rig.position.set(x,type==='eye'?4.5:0,z);rig.scale.setScalar(stats.scale);rig.name='mutant-'+type;this.scene.add(rig);
  const skins=type==='boss'?BOSS_SKINS:ZOMBIE_SKINS;skin=Number.isFinite(skin)?skin:type==='boss'&&Number.isFinite(options.wave)?Math.floor(options.wave/5)-1:id;skin=Math.max(0,Math.floor(skin))%skins.length;
  const visual=createMutantVisuals(this,type,skin);rig.add(visual.root);
  const e={id,type,skin,...stats,maxHp:stats.hp,rig,visual,limbs:visual.limbs,phase:id*1.37,motion:0,attackTime:0,attack:1,repath:0,path:[],aggro:true,stagger:0,windup:0,pendingAttack:false,reaction:0,sound:2+id%3,corpse:0,dead:false};
  if(options.boss||type==='boss'){e.boss=true;e.hp=e.maxHp=options.maxHp||bossHealth(options.wave);e.damage=55;e.speed=1.3;for(const side of [-1,1])this.mesh(visual.torso,'box',[.10,.32,.05],this.material('#d34d33',true),side*.18,.38,.34).name='boss-mark';visual.root.traverse(m=>{if(m.isMesh&&m.material?.color&&m.visible)m.userData.boss=true;});}
  if(e.boss){e.warning=new T.Mesh(new T.RingGeometry(.95,1,40),new T.MeshBasicMaterial({color:'#e67c47',side:T.DoubleSide,transparent:true,opacity:.45,depthWrite:false}));e.warning.rotation.x=-Math.PI/2;e.warning.position.y=.03;e.warning.visible=false;e.rig.add(e.warning);}
  e.memory={...this.base};e.sense=0;this.items.push(e);this.nextId=Math.max(this.nextId,id+1);poseMutant(e);return e;
 }
 die(enemy,emit=true,notify=true){if(enemy.dead)return;enemy.dead=true;enemy.hp=0;enemy.corpse=0;enemy.rig.visible=false;if(emit){if(enemy.type==='vomiter')this.spores.burst(enemy);else this.blood.burst(enemy);this.voice(enemy,'death');if(notify)this.onDeath?.(enemy);}}
 voice(e,kind){if(!this.listener)return;const p=e.rig.position,d=Math.hypot(p.x-this.listener.x,p.z-this.listener.z),range=kind==='death'?30:25;if(d>=range)return;this.audio.creature?.(kind,e.type,(1-d/range)*.85,e.id);}
 updateEffects(dt,remote=false){
  this.blood.step(dt);this.spores.step(dt);
  if(remote)for(const e of this.items){if(e.hp<=0)continue;e.idleTime=(e.idleTime||0)+dt;e.windup=Math.max(0,e.windup-dt);e.sound-=dt;if(e.sound<=0){this.voice(e,'living');e.sound=3+e.id%3;}e.phase+=dt*e.speed/(e.scale*.9)*Math.PI*2*e.motion;e.attackTime=Math.max(0,e.attackTime-dt);e.attack-=dt;if(e.type==='eye')e.visual.root.scale.setScalar(e.attack<.55?1+Math.sin(e.attack*35)*.06:1);poseMutant(e);}
 }
 get alive(){return this.items.reduce((n,e)=>n+(e.hp>0),0);}
 reset(){this.blood.clear();this.spores.clear();for(const e of this.items){e.hp=e.maxHp;e.stagger=0;e.corpse=0;e.dead=false;e.motion=0;e.attackTime=0;e.rig.visible=true;e.rig.rotation.set(0,0,0);poseMutant(e);}}
 hear(position,radius=90){for(const e of this.items)if(e.hp>0&&Math.hypot(e.rig.position.x-position.x,e.rig.position.y-(position.y||0),e.rig.position.z-position.z)<radius){if(!e.hasSight&&Math.hypot((e.memory?.x||0)-position.x,(e.memory?.z||0)-position.z)>3)e.repath=0;e.memory={x:position.x,y:position.y||0,z:position.z};e.heardUntil=this.clock+12;}}
 step(dt,player,hurt,other=null,hurtOther=()=>{}){this.clock+=dt;
 const living=[{p:player,hurt},...(Array.isArray(other)?other:other?[{p:other,hurt:hurtOther}]:[])].filter(v=>v.p.hp!==0);
 for(const e of this.items){if(e.hp<=0){this.die(e);e.corpse+=dt;continue;}e.idleTime=(e.idleTime||0)+dt;e.attack-=dt;e.repath-=dt;e.stagger=Math.max(0,e.stagger-dt);e.sound-=dt;e.attackTime=Math.max(0,e.attackTime-dt);e.reaction=Math.max(0,e.reaction-dt);if(!living.length)continue;
 const p=e.rig.position;let target=living.find(v=>v.p.position===e.targetPosition)||living.reduce((a,b)=>Math.hypot(a.p.position.x-p.x,a.p.position.z-p.z)<Math.hypot(b.p.position.x-p.x,b.p.position.z-p.z)?a:b),goal=target.p.position,distance=Math.hypot(goal.x-p.x,goal.z-p.z);
 e.sense-=dt;if(e.sense<=0){const origin=p.clone().add(new T.Vector3(0,e.type==='eye'?0:1.4,0));let visible=null,nearest=ENEMY_DETECTION+.001;for(const candidate of living){const g=candidate.p.position,d=Math.hypot(g.x-p.x,g.z-p.z);if(d>ENEMY_DETECTION||d>=nearest)continue;visible=candidate;nearest=d;}if(visible&&(!e.hasSight||Math.hypot(e.memory.x-visible.p.position.x,e.memory.z-visible.p.position.z)>2))e.repath=0;e.hasSight=!!visible;e.sense=.35+(e.id%4)*.05;if(visible){target=visible;goal=target.p.position;distance=nearest;const sightRay=new T.Vector3(goal.x,goal.y+1,goal.z).sub(origin),sightLength=sightRay.length();e.lineOfSight=obstacleDistance(origin,sightRay.normalize(),this.colliders,sightLength)>=sightLength-.1;e.targetPosition=goal;e.lastSeen=this.clock;e.memory={x:goal.x,y:goal.y,z:goal.z};}}

 if(e.boss){e.bossPhase=e.hp>e.maxHp*2/3?1:e.hp>e.maxHp/3?2:3;e.speed=1.3*(1+(e.bossPhase-1)*.2);e.warning.visible=!!e.pendingAttack&&e.attackKind==='slam';if(e.warning.visible)e.warning.scale.setScalar((e.bossPhase===3?4.5:3)/e.scale);}
 const chasing=e.hasSight||(this.clock-(e.lastSeen??-Infinity)<3&&distance<ENEMY_DETECTION+5);
 if(!chasing&&this.clock>(e.heardUntil||0))e.memory={...this.base};
 e.aggro=chasing;let pursuit=chasing?goal:e.memory;
 if(e.type!=='eye'&&!e.boss&&e.hasSight&&distance>6&&distance<25&&e.id%3!==0){const angle=e.id*2.399963;pursuit={x:goal.x+Math.cos(angle)*3,y:goal.y,z:goal.z+Math.sin(angle)*3};}
 if(e.type==='eye'&&chasing&&(!e.lineOfSight||distance<12||distance>24||e.attack>1.5)){if(!e.firingGoal||e.repath<=0){let best=null;for(let n=0;n<6;n++){const angle=(n+(e.id%6))*Math.PI/3,candidate={x:goal.x+Math.cos(angle)*18,y:4.5,z:goal.z+Math.sin(angle)*18},from=new T.Vector3(candidate.x,4.5,candidate.z),ray=new T.Vector3(goal.x,goal.y+1,goal.z).sub(from),len=ray.length();if(Math.abs(candidate.x)<SAFE_EDGE&&Math.abs(candidate.z)<SAFE_EDGE&&obstacleDistance(from,ray.normalize(),this.colliders,len)>=len-.1){best=candidate;break;}}e.firingGoal=best||pursuit;}pursuit=e.firingGoal;}
 if(e.sound<=0){this.voice(e,'living');e.sound=3+e.id%3;}const beforeX=p.x,beforeZ=p.z,yaw=Math.atan2(goal.x-p.x,goal.z-p.z),turn=Math.atan2(Math.sin(yaw-e.rig.rotation.y),Math.cos(yaw-e.rig.rotation.y));e.rig.rotation.y+=T.MathUtils.clamp(turn,-dt*3,dt*3);
 if(e.stagger>0){e.rig.rotation.z=Math.sin(e.stagger*45)*.035;e.motion=T.MathUtils.damp(e.motion,0,14,dt);poseMutant(e);continue;}e.rig.rotation.z=0;
 if(e.type==='eye')p.y=4.5+Math.sin(e.phase*.4)*.3;
 this.physics.jump(e,chasing?goal:{...p,y:0},dt);
 const melee=chasing&&e.lineOfSight!==false&&e.type!=='vomiter'&&e.type!=='eye'&&distance<e.radius+.85&&Math.abs(goal.y-p.y)<e.height;
 if(e.type==='vomiter'&&chasing&&e.lineOfSight&&distance<=8&&e.attack<=0&&!e.pendingAttack){e.pendingAttack=true;e.attackKind='vomit';e.windup=e.windupDuration=.25;this.voice(e,'prepare');}
 if(e.pendingAttack){e.windup=Math.max(0,e.windup-dt);e.attackTime=.21;if(!e.windup){if(e.attackKind==='vomit'){this.fireVomit(e,goal);}const victims=e.attackKind==='vomit'?[]:e.attackKind==='slam'?living.filter(v=>Math.hypot(v.p.position.x-p.x,v.p.position.z-p.z)<(e.bossPhase===3?4.5:3)):melee?[target]:[];for(const victim of victims){const origin=p.clone().add(new T.Vector3(0,1.4,0)),dir=new T.Vector3(victim.p.position.x,victim.p.position.y+1,victim.p.position.z).sub(origin),len=dir.length();if(obstacleDistance(origin,dir.normalize(),this.colliders,len)>=len-.05)victim.hurt(e.damage);}e.pendingAttack=false;e.attackTime=.21;}}
 if(melee&&e.attack<=0&&!e.pendingAttack){e.pendingAttack=true;e.attackCount=(e.attackCount||0)+1;e.attackKind=e.boss&&e.bossPhase>=2&&e.attackCount%3===0?'slam':'melee';e.windup=e.attackKind==='slam'?1.2:e.boss?.9:e.type==='brute'?.6:.18;e.windupDuration=e.windup;e.attack=e.boss?e.bossPhase===3?1.4:1.8:e.type==='runner'?.8:e.type==='brute'?1.6:1.05;}
 if(e.type==='eye')e.visual.root.scale.setScalar(e.attack<.55?1+Math.sin(e.attack*35)*.06:1);
 if(e.type==='eye'&&chasing&&distance<40&&e.attack<=0){const origin=p.clone(),direction=new T.Vector3(goal.x,goal.y+1,goal.z).sub(origin),len=direction.length();if(obstacleDistance(origin,direction.normalize(),this.colliders,len)>=len-.1){const mesh=this.mesh(this.scene,'sphere',[.2,0],'bullet',p.x,p.y,p.z);this.projectiles.push({mesh,velocity:direction.multiplyScalar(20),life:6});e.attack=2.2;}}
 if(!e.pendingAttack&&!melee&&(e.type!=='vomiter'||!e.lineOfSight||distance>7.5)&&(e.type!=='eye'||!e.lineOfSight||distance<12||distance>24||e.attack>1.5)){
  if(e.repath<=0){e.path=(e.type==='brute'?this.fatNav:e.type==='boss'?this.bossNav:e.type==='eye'?this.airNav:e.type==='vomiter'?this.vomitNav:this.nav).path(p,pursuit);e.repath=e.type==='eye'?1.2:.8+(e.id%4)*.08;}
  e.progressTimer=(e.progressTimer||0)+dt;if(!e.progressAnchor)e.progressAnchor={x:p.x,z:p.z};if(e.progressTimer>=3){if(Math.hypot(p.x-e.progressAnchor.x,p.z-e.progressAnchor.z)<.5){e.repath=0;e.path=[];this.physics.tryHop(e,pursuit);}e.progressAnchor={x:p.x,z:p.z};e.progressTimer=0;}
  const nav=e.type==='brute'?this.fatNav:e.type==='boss'?this.bossNav:e.type==='eye'?this.airNav:e.type==='vomiter'?this.vomitNav:this.nav;
  // Smooth grid routes only where the entire segment is clear; never cut corners.
  let point=e.path[0]||pursuit;for(let i=Math.min(e.path.length-1,10);i>0;i--)if(this.physics.clear(e,e.path[i])){e.path.splice(0,i);point=e.path[0];break;}
  if((Math.hypot(pursuit.x-p.x,pursuit.z-p.z)<12&&this.physics.clear(e,pursuit))||(chasing&&goal.y>.35&&distance<5))point=pursuit;if(e.hopTime>0&&e.hopGoal)point=e.hopGoal;
  if(point){const dx=point.x-p.x,dz=point.z-p.z,len=Math.hypot(dx,dz),step=Math.min(len,e.speed*(e.hp<e.maxHp*.35?.8:1)*dt);if(len>.001)this.physics.move(e,dx/len*step,dz/len*step);if(len<.15&&point===e.path[0])e.path.shift();const moved=Math.hypot(p.x-beforeX,p.z-beforeZ);e.blockedFor=moved<step*.15?(e.blockedFor||0)+dt:Math.max(0,(e.blockedFor||0)-dt*2);if(e.blockedFor>.65){e.repath=0;e.blockedFor=0;if(!this.physics.tryHop(e,pursuit)){e.recoverySide=-(e.recoverySide||((e.id%2)?1:-1));e.path.unshift({x:p.x-dz/Math.max(.001,len)*e.recoverySide,z:p.z+dx/Math.max(.001,len)*e.recoverySide});e.repath=.7;}}}

 }
 const travel=Math.hypot(p.x-beforeX,p.z-beforeZ);if(e.type==='brute'&&travel>.001&&Math.floor(e.phase/Math.PI)!==e.lastStride){e.lastStride=Math.floor(e.phase/Math.PI);if(distance<30)this.audio.play('stomp',1-distance/35);}e.motion=T.MathUtils.damp(e.motion,travel>1e-6?1:0,14,dt);e.phase+=travel/(e.scale*.9)*Math.PI*2;poseMutant(e);e.visual.root.rotation.x=e.reaction>0?-Math.sin(e.reaction/.22*Math.PI)*.10:0;if(e.hp<e.maxHp*.35)e.phase-=travel/(e.scale*.9)*Math.PI*.3;
 }
 this.physics.separate(this.items);
 for(const shot of this.projectiles){const shotStep=Math.min(dt,shot.life);shot.life-=dt;const origin=shot.mesh.position,delta=shot.velocity.clone().multiplyScalar(shotStep),length=delta.length(),direction=delta.clone().normalize(),ray=new T.Ray(origin.clone(),direction);let travel=obstacleDistance(origin,direction,this.colliders,length);
 for(const target of living){const p=target.p.position,hit=ray.intersectBox(new T.Box3(new T.Vector3(p.x-.32,p.y,p.z-.32),new T.Vector3(p.x+.32,p.y+1.8,p.z+.32)),new T.Vector3());if(hit&&hit.distanceTo(origin)<=travel){target.hurt(shot.kind==='vomit'?10:30,shot.kind==='vomit'?{poison:true,id:shot.id}:undefined);shot.life=0;break;}}
 if(travel<length-.001)shot.life=0;origin.add(delta);}
 this.items=this.items.filter(e=>{if(e.hp>0||e.corpse<2)return true;this.scene.remove(e.rig);return false;});
 this.projectiles=this.projectiles.filter(s=>{if(s.life<=0){this.scene.remove(s.mesh);return false;}return true;});
 }
 fireVomit(e,goal){e.attackCount=(e.attackCount||0)+1;e.attack=1+((e.id*13+e.attackCount*7)%101)/100;const p=e.rig.position,origin=e.rig.getObjectByName('mouth').getWorldPosition(new T.Vector3()),ray=new T.Vector3(goal.x,goal.y+1,goal.z).sub(origin),length=ray.length();if(Math.hypot(goal.x-p.x,goal.z-p.z)>8||length>8.05||obstacleDistance(origin,ray.clone().normalize(),this.colliders,length)<length-.05)return false;const mesh=this.mesh(this.scene,'sphere',[.14,1],'vomit',origin.x,origin.y,origin.z);mesh.scale.set(1,.7,1.6);mesh.lookAt(origin.clone().add(ray));for(let n=1;n<=2;n++)this.mesh(mesh,'sphere',[.055,1],'vomit',n%2?.04:-.04,0,-n*.18);this.projectiles.push({id:'vomit-'+e.id+'-'+e.attackCount,kind:'vomit',mesh,velocity:ray.normalize().multiplyScalar(5),life:1.6});this.voice(e,'vomit');return true;}
 shoot(origin,direction,damage=35,max=100){const wall=Math.min(max,obstacleDistance(origin,direction,this.colliders)),ray=new T.Ray(origin,direction);let nearest=wall,hit=null;for(const e of this.items){if(e.hp<=0)continue;const p=e.rig.position,r=e.radius,box=new T.Box3(new T.Vector3(p.x-r,p.y-(e.type==='eye'?r:0),p.z-r),new T.Vector3(p.x+r,p.y+(e.type==='eye'?r:e.height),p.z+r)),point=ray.intersectBox(box,new T.Vector3());if(point&&point.distanceTo(origin)<nearest){nearest=point.distanceTo(origin);hit=e;}}
 if(!hit)return false;hit.hp=Math.max(0,hit.hp-damage);if(hit.hp>0)this.blood.burst(hit,12);hit.stagger=.10;hit.reaction=.22;hit.visual.root.rotation.x=-.10;this.audio.play('hit');if(!hit.hp)this.die(hit);return true;}
 snapshot(){return this.items.filter(e=>e.hp>0||e.corpse<2).map(e=>({id:e.id,type:e.type,skin:e.skin,boss:!!e.boss,bossPhase:e.bossPhase||1,attackKind:e.attackKind||'melee',hp:e.hp,maxHp:e.maxHp,x:e.rig.position.x,y:e.rig.position.y,z:e.rig.position.z,rotation:e.rig.rotation.toArray(),phase:e.phase,motion:e.motion,attackTime:e.attackTime,attack:e.attack,attackCount:e.attackCount,windup:e.windup,windupDuration:e.windupDuration,idleTime:e.idleTime,pendingAttack:e.pendingAttack,corpse:e.corpse}));}
 apply(records){
  const keep=new Set(records.map(v=>v.id));
  this.items=this.items.filter(e=>{if(keep.has(e.id))return true;this.scene.remove(e.rig);return false;});
  for(const v of records){
   const existing=this.items.find(e=>e.id===v.id),e=existing||this.spawn(v.type,v.x,v.z,v.id,v.skin,{boss:v.boss,maxHp:v.maxHp});
   const wasAlive=existing?.hp>0;e.hp=v.hp;e.rig.position.set(v.x,v.y??0,v.z);if(v.rotation)e.rig.rotation.fromArray(v.rotation);
   if(e.hp<=0){this.die(e,wasAlive||(v.corpse??0)<.3,false);e.corpse=v.corpse??0;}
   else{e.rig.visible=true;e.dead=false;e.phase=v.phase??e.phase;e.motion=v.motion??0;e.attackTime=v.attackTime??0;e.attack=v.attack??1;e.attackCount=v.attackCount??0;e.bossPhase=v.bossPhase||1;e.attackKind=v.attackKind||'melee';if(e.warning){e.warning.visible=!!v.pendingAttack&&e.attackKind==='slam';e.warning.scale.setScalar((e.bossPhase===3?4.5:3)/e.scale);}if(e.type==='vomiter'&&v.pendingAttack&&!e.pendingAttack&&this.listener&&e.rig.position.distanceTo(new T.Vector3(this.listener.x,this.listener.y||0,this.listener.z))<15)this.audio.play('vomit-prepare',.5);e.windup=v.windup??0;e.windupDuration=v.windupDuration;e.idleTime=v.idleTime??e.idleTime;e.pendingAttack=!!v.pendingAttack;poseMutant(e);}
  }
 }
}
