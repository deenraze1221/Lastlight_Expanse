import * as T from 'three';
// Harmless world-space death effect, bounded and independent of combat collision.
export class SporeClouds{
 constructor(scene){const size=32,data=new Uint8Array(size*size*4);for(let y=0;y<size;y++)for(let x=0;x<size;x++){const r=Math.hypot((x-15.5)/15.5,(y-15.5)/15.5);data.set([148,186,64,Math.round(Math.max(0,1-r)**2*220)],(y*size+x)*4);}const texture=new T.DataTexture(data,size,size);texture.needsUpdate=true;texture.colorSpace=T.SRGBColorSpace;this.pool=Array.from({length:96},()=>{const sprite=new T.Sprite(new T.SpriteMaterial({map:texture,transparent:true,depthWrite:false,opacity:0}));sprite.visible=false;sprite.name='harmless-spore-cloud';scene.add(sprite);return {sprite,age:3};});this.cursor=0;}
 burst(e){for(let n=0;n<8;n++){const item=this.pool[this.cursor++%this.pool.length],a=n*Math.PI*.763;item.age=0;item.sprite.position.set(e.rig.position.x+Math.cos(a)*.3,e.rig.position.y+.7+n*.13,e.rig.position.z+Math.sin(a)*.3);item.dx=Math.cos(a)*.22;item.dz=Math.sin(a)*.22;item.sprite.visible=true;item.sprite.scale.setScalar(.8);}}
 step(dt){for(const p of this.pool)if(p.age<2.5){p.age+=dt;p.sprite.visible=p.age<2.5;p.sprite.position.x+=p.dx*dt;p.sprite.position.z+=p.dz*dt;p.sprite.position.y+=dt*.12;p.sprite.scale.setScalar(.8+p.age*.55);p.sprite.material.opacity=Math.max(0,(1-p.age/2.5)*.28);}}
 clear(){for(const p of this.pool){p.age=3;p.sprite.visible=false;}}
 get active(){return this.pool.filter(p=>p.age<2.5).length;}
}
