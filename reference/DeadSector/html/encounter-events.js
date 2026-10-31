import * as T from 'three';
import {createGenerator,createGrowth} from './mission-models.js';
// Visuals and interactions for two mission types; Operations owns the single mission lifecycle.
export class EncounterEvents{
 constructor(scene){this.scene=scene;this.state=null;this.powered=false;this.time=0;this.group=new T.Group();scene.add(this.group);this.body=createGenerator();this.growth=createGrowth();this.group.add(this.body,this.growth);this.group.visible=false;}
 bind(mission){this.state=mission&&['generator','growth'].includes(mission.id)?mission:null;this.syncVisual();}
 syncVisual(){const s=this.state;this.group.visible=!!s&&s.status==='active';if(s){this.body.visible=s.id==='generator';this.growth.visible=s.id==='growth';this.group.position.set(s.x,0,s.z);}}
 complete(){if(this.state?.status!=='active')return false;this.state.status='complete';if(this.state.id==='generator')this.powered=true;this.syncVisual();return true;}
 step(dt){this.time+=dt;if(this.state?.id==='growth'){const core=this.growth.userData.core,health=this.state.hp/this.state.maxHp;core.scale.set(1,1.2,1).multiplyScalar(1+Math.sin(this.time*(3+(1-health)*3))*.035);core.material.emissiveIntensity=.45+Math.sin(this.time*3)*.12+(1-health)*.25;}this.syncVisual();}
 interact(p){const s=this.state;return s?.id==='generator'&&s.status==='active'&&p.y<1.5&&Math.hypot(p.x-s.x,p.z-s.z)<2.4?this.complete():false;}
 hit(origin,direction){const s=this.state;if(s?.id!=='growth'||s.status!=='active')return null;return new T.Ray(origin,direction).intersectBox(new T.Box3(new T.Vector3(s.x-.9,0,s.z-.9),new T.Vector3(s.x+.9,1.65,s.z+.9)),new T.Vector3());}
 distance(origin,direction){const point=this.hit(origin,direction);return point?point.distanceTo(origin):Infinity;}
 shoot(origin,direction,damage,wall=100){const point=this.hit(origin,direction);if(!point||point.distanceTo(origin)>wall)return false;this.state.hp=Math.max(0,this.state.hp-damage);if(!this.state.hp)this.complete();return true;}
 snapshot(){return {powered:this.powered};}
 restore(v){if(v)this.powered=!!v.powered;}
}
