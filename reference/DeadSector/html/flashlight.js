import * as T from 'three';
export class Flashlight{
 constructor(scene){this.on=false;this.light=new T.SpotLight('#d7e5d2',35,42,Math.PI/7,.55,1.4);this.light.name='player-flashlight';this.light.shadow.mapSize.set(512,512);this.light.shadow.bias=-.0004;this.light.visible=false;scene.add(this.light,this.light.target);}
 toggle(a){if(!a.flashlight||a.flashlightCharge<=0){this.on=false;return false;}this.on=!this.on;return true;}
 drain(dt,a){if(this.on&&a.flashlight){a.flashlightCharge=Math.max(0,a.flashlightCharge-dt);if(!a.flashlightCharge)this.on=false;}}
 update(camera,a,quality){this.light.visible=this.on&&!!a.flashlight&&a.flashlightCharge>0;this.light.castShadow=quality==='high';this.light.position.copy(camera.position);this.light.position.y-=.08;const direction=camera.getWorldDirection(new T.Vector3());this.light.target.position.copy(this.light.position).addScaledVector(direction,20);}
}
