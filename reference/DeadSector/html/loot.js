import * as T from 'three';
import {rewardForHP} from './economy.js';
export class MoneyDrops{
 constructor(scene){this.scene=scene;this.items=[];this.geometry=new T.BoxGeometry(.38,.11,.25);this.paper=new T.MeshStandardMaterial({color:'#85a876',roughness:1});this.band=new T.MeshStandardMaterial({color:'#e1c574',roughness:1});}
 spawn(enemy){return this.add({id:'cash-'+enemy.id,x:enemy.rig.position.x,z:enemy.rig.position.z,value:rewardForHP(enemy.maxHp),taken:false});}
 add(record){const old=this.items.find(v=>v.id===record.id);if(old){old.taken=Boolean(record.taken);old.group.visible=!old.taken;return old;}const group=new T.Group();group.name='money-drop';group.position.set(record.x,.07,record.z);group.add(new T.Mesh(this.geometry,this.paper));const band=new T.Mesh(new T.BoxGeometry(.065,.115,.255),this.band);group.add(band);this.scene.add(group);const item={...record,group,taken:Boolean(record.taken)};group.visible=!item.taken;this.items.push(item);return item;}
 nearby(p){return this.items.find(v=>!v.taken&&p.y<1.3&&Math.hypot(v.x-p.x,v.z-p.z)<1.6);}
 collect(id,account){const v=this.items.find(v=>v.id===id);if(!v||v.taken)return false;v.taken=true;this.scene.remove(v.group);v.group.children[1].geometry.dispose();this.items=this.items.filter(item=>item!==v);account.money+=v.value;return true;}
 snapshot(){return this.items.filter(v=>!v.taken).map(({id,x,z,value,taken})=>({id,x,z,value,taken}));}
 apply(records){const ids=new Set((records||[]).map(r=>r.id));this.items=this.items.filter(r=>{if(ids.has(r.id))return true;this.scene.remove(r.group);r.group.children[1].geometry.dispose();return false;});for(const r of records||[])this.add(r);}
}
