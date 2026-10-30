import * as THREE from 'three';
import weaponModels from './assets/models/weapons.json' with { type: 'json' };
import {WEAPONS} from './weapon-config.js';
import {MOTION} from './weapon-motion.js';
import {recordShot} from './economy.js';
export {WEAPONS};
export class Rifle {
  constructor(audio, random = Math.random) {
    this.audio=audio;this.random=random;this.scene=new THREE.Scene();this.camera=new THREE.PerspectiveCamera(45,1,.01,10);
    this.scene.add(new THREE.HemisphereLight('#aebfc0','#333c34',1.7));
    const light=new THREE.DirectionalLight('#c3d0cc',1.5);light.position.set(-2,3,2);this.scene.add(light);
    this.model=new THREE.Group();this.model.scale.setScalar(.8);this.scene.add(this.model);
    this.variants=weaponModels.map(record=>{
      const group=new THREE.Group();group.name=record.id;
      const make=part=>{
        const materials=part.materials.map((m,i)=>new THREE.MeshStandardMaterial({
          vertexColors:Boolean(m.vertexColors),color:m.vertexColors?'#ffffff':record.id==='Pistol'?(m.name==='Gun_metal'?'#495654':i===1?'#77816e':'#bd9254'):new THREE.Color(m.color).lerp(new THREE.Color('#73776d'),.08),
          metalness:.32,roughness:.8,flatShading:record.id==='Pistol',
        }));
        return new THREE.Mesh(new THREE.BufferGeometryLoader().parse(part.geometry),materials.length===1?materials[0]:materials);
      };
      // gunsv2 supplies single-piece guns; never detach an invented magazine.
      const body=make(record.body),magazine=record.magazine?make(record.magazine):new THREE.Group();body.name=record.id;magazine.name=record.id+'Magazine';
      group.add(body,magazine);this.model.add(group);return {group,body,magazine,record};
    });
    const glove=new THREE.MeshStandardMaterial({color:'#536049',roughness:1}),sleeve=new THREE.MeshStandardMaterial({color:'#354336',roughness:1});
    this.forearms=[];
    this.hands=[-1,1].map(side=>{
      const g=new THREE.Group();g.name=side<0?'support-hand':'trigger-hand';
      const palm=new THREE.Mesh(new THREE.BoxGeometry(.064,.085,.07),glove);g.add(palm);
      for(let i=0;i<4;i++){
        const finger=new THREE.Mesh(new THREE.CapsuleGeometry(.009,.026,2,5),glove);finger.rotation.z=side*.9;finger.position.set(-side*.030,-.026+i*.017,-.023);g.add(finger);
        const tip=new THREE.Mesh(new THREE.CapsuleGeometry(.008,.019,2,5),glove);tip.rotation.x=Math.PI/2;tip.position.set(-side*.041,-.026+i*.017,-.038);g.add(tip);
      }
      const thumb=new THREE.Mesh(new THREE.CapsuleGeometry(.012,.028,2,5),glove);thumb.rotation.z=-side*.6;thumb.position.set(side*.032,.025,-.012);g.add(thumb);
      const wrist=new THREE.Mesh(new THREE.CylinderGeometry(.027,.032,.08,8),glove);wrist.rotation.x=Math.PI/2;wrist.position.z=.065;g.add(wrist);
      const arm=new THREE.Mesh(new THREE.CylinderGeometry(.035,.052,1,8),sleeve);arm.name=side<0?'support-forearm':'trigger-forearm';this.forearms.push(arm);this.model.add(arm,g);return g;
    });
    this.flash=new THREE.Mesh(new THREE.ConeGeometry(.065,.18,5),new THREE.MeshBasicMaterial({color:'#ffe790'}));
    this.action=new THREE.Mesh(new THREE.BoxGeometry(.018,.025,.09),new THREE.MeshStandardMaterial({color:'#8d9993',metalness:.6,roughness:.45}));this.model.add(this.action);this.casings=[];this.casingGeometry=new THREE.CylinderGeometry(.007,.007,.03,6);this.casingMaterial=new THREE.MeshStandardMaterial({color:'#b99b55',metalness:.6,roughness:.5});
    this.flash.rotation.x=-Math.PI/2;this.model.add(this.flash);this.reset('Pistol');
  }
  get ammo(){return this.inventory?this.inventory.weapons[this.config.id].ammo:this._ammo;}
  set ammo(v){if(this.inventory)this.inventory.weapons[this.config.id].ammo=v;else this._ammo=v;}
  get reserve(){return this.inventory?this.inventory.reserve:this._reserve;}
  set reserve(v){if(this.inventory)this.inventory.reserve=v;else this._reserve=v;}
  get durability(){return this.inventory?this.inventory.weapons[this.config.id].durability:this._durability;}
  bindInventory(account){const changed=this.config.id!==account.equipped;this.inventory=null;if(changed)this.reset(account.equipped);this.inventory=account;}
  setUtility(value){if(!!this.utilityActive===value)return;this.switchFrom={variant:this.displayVariant||this.variant,utility:!!this.displayUtility};this.utilityActive=value;this.switchTime=1.5;}
  reset(id='Pistol'){
    const previous=this.displayVariant||this.variant,wasUtility=!!this.displayUtility;
    for(const c of this.casings||[])this.scene.remove(c.mesh);if(this.casings)this.casings=[];this.inventory=null;this._durability=100;this.wearShots=0;
    const selected=WEAPONS.findIndex(w=>w.id===id);
    const index=selected>=0?selected:Math.min(WEAPONS.length-1,Math.max(0,Math.floor(this.random()*WEAPONS.length)));
    this.config=WEAPONS[index];this.utilityActive=false;this.switchFrom=previous&&(previous.record.id!==this.config.id||wasUtility)?{variant:previous,utility:wasUtility}:null;this.switchTime=this.switchFrom?1.5:0;this.variant=this.variants.find(v=>v.record.id===this.config.id);this.gun=this.variant.body;this.magazine=this.variant.magazine;
    this.variants.forEach(v=>{v.group.visible=v===this.variant;v.magazine.position.set(0,0,0);v.magazine.rotation.set(0,0,0);});
    this.flash.position.set(0,this.variant.record.muzzleY,-this.variant.record.length/2-.08);
    this.ammo=this.config.capacity;this.reserve=this.config.reserve;this.cooldown=0;this.reloadTime=0;this.recoil=0;this.flashTime=0;this.flash.visible=false;
  }
  reload(){if(this.switchTime>0||this.reloadTime||this.ammo===this.config.capacity||!this.reserve)return;this.reloadTime=this.config.reload;this.audio.play('reload');}
  step(dt){
    this.switchTime=Math.max(0,(this.switchTime||0)-dt);
    for(const c of this.casings){c.life-=dt;c.velocity.y-=2.5*dt;c.mesh.position.addScaledVector(c.velocity,dt);c.mesh.rotation.x+=dt*11;c.mesh.rotation.z+=dt*8;}
    this.casings=this.casings.filter(c=>{if(c.life>0)return true;this.scene.remove(c.mesh);return false;});
    this.cooldown=Math.max(0,this.cooldown-dt);this.flashTime=Math.max(0,this.flashTime-dt);this.flash.visible=this.flashTime>0;this.recoil=Math.max(0,this.recoil-dt*1.8);
    if(this.reloadTime>0){this.reloadTime=Math.max(0,this.reloadTime-dt);if(!this.reloadTime){const n=Math.min(this.config.capacity-this.ammo,this.reserve);this.ammo+=n;this.reserve-=n;this.audio.play('reload');}}
  }
  fire(){if(this.switchTime>0||this.reloadTime||this.cooldown||this.durability<=0)return false;if(!this.ammo){this.cooldown=.25;this.audio.play('empty');return false;}if(this.inventory)recordShot(this.inventory);else{this.ammo--;this.wearShots++;if(this.wearShots>=100){this._durability=Math.max(0,this._durability-5);this.wearShots=0;}}this.cooldown=this.config.interval;this.recoil=MOTION[this.config.id].kick;const shell=new THREE.Mesh(this.casingGeometry,this.casingMaterial);shell.position.set(.27,-.17,-.39);this.scene.add(shell);this.casings.push({mesh:shell,life:.7,velocity:new THREE.Vector3(.5,.35,.12)});if(this.casings.length>24)this.scene.remove(this.casings.shift().mesh);this.flashTime=.045;this.audio.shot?.(this.config.id);return true;}
  pose(time,moving){
    const previous=this.switchTime>.75&&this.switchFrom,shown=previous?this.switchFrom:{variant:this.variant,utility:!!this.utilityActive};this.displayVariant=shown.variant;this.displayUtility=shown.utility;this.variants.forEach(v=>v.group.visible=v===shown.variant);this.model.visible=!shown.utility;if(this.utilityModel)this.utilityModel.visible=shown.utility;
    const zoom=shown.utility?1:(WEAPONS.find(w=>w.id===shown.variant.record.id)?.viewZoom||1);if(this.camera.zoom!==zoom){this.camera.zoom=zoom;this.camera.updateProjectionMatrix();}
    const transition=this.switchTime>0?(this.switchTime>.75?(1.5-this.switchTime)/.75:this.switchTime/.75):0,switchDrop=(.5-.5*Math.cos(Math.PI*transition))*.8;this.switchDrop=switchDrop;
    if(this.utilityModel){this.utilityModel.position.y=-.20-switchDrop;this.utilityModel.rotation.x=.7+switchDrop*.35;}

    const progress=this.reloadTime?1-this.reloadTime/this.config.reload:0,drop=this.reloadTime?Math.sin(progress*Math.PI):0;
    const viewLower=shown.utility?1:(WEAPONS.find(w=>w.id===shown.variant.record.id)?.viewLower||1);this.model.position.set(.16/zoom,(-.16*viewLower)/zoom-drop*.15-switchDrop,-.48+this.recoil);const motion=MOTION[this.config.id];this.model.rotation.set(this.recoil*motion.pitch*12,Math.sin(time*17)*this.recoil*motion.yaw*8,-drop*(motion.action==='drum'?.65:.25));
    this.model.rotation.x+=switchDrop*.35;
    this.magazine.position.y=-drop*.22;this.magazine.rotation.x=drop*.2;
    const loading=this.reloadTime&&this.config.reloadStyle!=='magazine'?Math.sin(progress*Math.PI*6)*drop:0;
    if(this.reloadTime)this.model.rotation.x+=drop*(this.config.reloadStyle==='breech'?.4:.12);
    const length=this.variant.record.length;
    const cycle=this.reloadTime?Math.sin(progress*Math.PI*motion.load):0;
    this.action.position.set(.042,.005,length*.05+(this.flashTime>0?.045:0)+(this.reloadTime&&['bolt','rack'].includes(motion.action)?Math.max(0,cycle)*.09:0));
    this.action.rotation.z=motion.action==='bolt'&&this.reloadTime?drop*.7:0;
    if(this.reloadTime&&motion.action==='break')this.model.rotation.x+=drop*.25;
    if(this.reloadTime&&motion.action==='drum')this.model.rotation.y+=drop*.22;
    const grips=shown.variant.record.grips;
    this.hands[1].position.fromArray(grips.trigger);
    this.hands[0].position.fromArray(grips.support);
    if(this.reloadTime){if(['bolt','rack'].includes(motion.action)){this.hands[0].position.z+=drop*length*.20;this.hands[0].position.y+=drop*.08;}this.hands[0].position.z+=cycle*.025;this.hands[0].position.x-=drop*.04;this.hands[0].position.y-=drop*.09;this.hands[0].position.z+=drop*.10+loading*.025;}
    this.hands[0].rotation.set(-.12,0,drop*.12);this.hands[1].rotation.set(.06,0,-.08);
    this.forearms.forEach((arm,i)=>{const side=i===0?-1:1,start=this.hands[i].position.clone().add(new THREE.Vector3(0,-.015,.10)),end=new THREE.Vector3(side<0?-.24:.23,-.38,.40),delta=end.clone().sub(start);arm.position.copy(start).add(end).multiplyScalar(.5);arm.quaternion.setFromUnitVectors(new THREE.Vector3(0,1,0),delta.clone().normalize());arm.scale.set(1,delta.length(),1);});
    if(moving){this.model.position.y+=Math.sin(time*10)*.007;this.model.position.x+=Math.cos(time*5)*.006;}
  }
}
