import * as T from 'three';
import {mergeGeometries} from 'three/addons/utils/BufferGeometryUtils.js';

function batchStaticParts(group,owner){
 for(const child of [...group.children])if(child.isGroup)batchStaticParts(child,owner);
 const byMaterial=new Map();
 for(const mesh of group.children.filter(c=>c.isMesh)){const key=mesh.material;if(!byMaterial.has(key))byMaterial.set(key,[]);byMaterial.get(key).push(mesh);}
 owner.visualGeometry??=new Map();
 for(const [material,parts] of byMaterial){
  if(parts.length<2)continue;
  parts.forEach(p=>p.updateMatrix());
  const key=parts.map(p=>p.geometry.uuid+':'+p.matrix.elements.join(',')).join('|');
  if(!owner.visualGeometry.has(key)){
   const geometries=parts.map(p=>{const g=p.geometry.index?p.geometry.toNonIndexed():p.geometry.clone();return g.applyMatrix4(p.matrix);});
   owner.visualGeometry.set(key,mergeGeometries(geometries));geometries.forEach(g=>g.dispose());
  }
  parts.forEach(p=>p.visible=false);
  const combined=new T.Mesh(owner.visualGeometry.get(key),material);combined.name='batched-static-parts';combined.castShadow=true;group.add(combined);
 }
}


export const ZOMBIE_SKINS = Object.freeze([
 {id:'civilian',skin:'#9a917a',clothes:'#465d72',pants:'#303c4a',accent:'#b0a18b'},
 {id:'worker',skin:'#a18a65',clothes:'#b77932',pants:'#46463b',accent:'#e0b84c'},
 {id:'medic',skin:'#a8a493',clothes:'#c0bba2',pants:'#596c60',accent:'#8e292c'},
 {id:'soldier',skin:'#818e70',clothes:'#4a5840',pants:'#3d4634',accent:'#7c7954'},
 {id:'hoodie',skin:'#9a817b',clothes:'#703c46',pants:'#38373b',accent:'#99908a'},
 {id:'firefighter',skin:'#a18a77',clothes:'#74603c',pants:'#493f31',accent:'#d5bc58'},
 {id:'police',skin:'#929b88',clothes:'#273c54',pants:'#29303d',accent:'#a8adb3'},
 {id:'mechanic',skin:'#9d8a73',clothes:'#3f7779',pants:'#344c50',accent:'#b1b5a2'},
 {id:'infected',skin:'#9b9d79',clothes:'#726d5f',pants:'#45453d',accent:'#8d3c40'},
]);

export const BOSS_SKINS=Object.freeze([
 {id:'warden',skin:'#6e8386',clothes:'#293849',pants:'#303a43',accent:'#bd9c71'},
 {id:'plague',skin:'#8e9970',clothes:'#536343',pants:'#353e2e',accent:'#d2c487'},
 {id:'iron',skin:'#a08b7b',clothes:'#6b4541',pants:'#403434',accent:'#9caba9'},
 {id:'charred',skin:'#5d605b',clothes:'#292a2e',pants:'#303031',accent:'#cb794c'},
 {id:'reclaimer',skin:'#959b9c',clothes:'#a39d83',pants:'#434a49',accent:'#485e6b'},
]);
export function attackPose(e){const smooth=x=>{x=T.MathUtils.clamp(x,0,1);return x*x*(3-2*x);};if(e.pendingAttack){const duration=e.windupDuration||(e.attackKind==='slam'?1.2:e.boss?.9:e.type==='brute'?.6:.18),p=1-(e.windup||0)/duration,draw=e.boss&&e.attackKind==='slam'?-1.6:.35;return p<.7?draw*smooth(p/.7):draw+(-.95-draw)*smooth((p-.7)/.3);}return -.95*smooth((e.attackTime||0)/.21);}

// Separate shoulders, elbows, hips, knees and ankles; all measurements are local metres.
export function createMutantVisuals(owner,type,skinIndex){
 const root=new T.Group();root.name='anatomy';const limbs=[],legs=[],arms=[];
 const boss=type==='boss',brute=type==='brute',vomiter=type==='vomiter',runner=type==='runner',skin=(boss?BOSS_SKINS:ZOMBIE_SKINS)[skinIndex];
 const palette={skin:vomiter?'#718d43':boss?skin.skin:brute?'#938270':runner?'#a0967c':skin.skin,cloth:vomiter?'#596337':boss?skin.clothes:brute?'#625445':runner?'#4b5750':skin.clothes,pants:brute?'#4b4236':skin.pants,accent:vomiter?'#b6c66a':skin.accent,boot:'#292d29',wound:'#823c40',bone:'#bfb497',eye:'#e4c46b',black:'#252a23'};
 const materials=Object.fromEntries(Object.entries(palette).map(([key,color])=>[key,owner.material(color,key==='eye')]));
 function part(parent,name,kind,size,material,x,y,z){const p=owner.mesh(parent,kind,size,materials[material],x,y,z);p.name=name;p.castShadow=true;return p;}
 if(type==='eye'){
  part(root,'flesh-orb','sphere',[.8,2],'wound',0,0,0).scale.set(1,.94,.92);
  part(root,'sclera','sphere',[.52,2],'bone',0,.03,.48);
  part(root,'iris','sphere',[.31,2],'eye',0,.03,.89);
  part(root,'pupil','sphere',[.15,2],'black',0,.03,1.12);
  for(let n=0;n<9;n++){const a=n*Math.PI*2/9;part(root,'vein','capsule',[.035,.28,2,5],'skin',Math.cos(a)*.63,Math.sin(a)*.55,.12).rotation.z=a;}
  for(let n=0;n<6;n++){const a=n*Math.PI/3;const stalk=new T.Group();stalk.position.set(Math.cos(a)*.6,Math.sin(a)*.6,-.18);root.add(stalk);part(stalk,'tendril','cone',[.09,.48,6],'skin',0,0,-.2).rotation.x=Math.PI/2;arms.push({pivot:stalk,elbow:null,side:n%2?1:-1});}
  batchStaticParts(root,owner);return {root,limbs,legs,arms,eye:true};
 }
 const torso=new T.Group();torso.name='torso';torso.position.y=.96;root.add(torso);
 function upper(name,kind,size,material,x,y,z){return part(torso,name,kind,size,material,x,y-.96,z);}
 part(root,'pelvis','capsule',[brute?.29:.22,.13,4,8],'pants',0,.90,0);
 upper('chest','capsule',[brute?.30:.255,.27,4,8],'cloth',0,1.23,0);
 if(brute){
  upper('belly','sphere',[.35,2],'skin',0,1.08,.09).scale.set(1.1,.91,1.04);
  for(const side of [-1,1])upper('suspender','box',[.075,.46,.065],'cloth',side*.22,1.38,.30).rotation.z=side*-.17;
  for(let n=0;n<6;n++)upper('lesion','sphere',[.038+(n%2)*.015,1],'wound',Math.sin(n*2.4)*.30,1.02+n*.055,.45-Math.abs(Math.sin(n*2.4))*.10);
 }else{
  upper('torn-hem','box',[.35,.09,.27],'cloth',0,.94,.01);
  upper('shirt-seam','box',[.022,.42,.018],'accent',0,1.25,.257);
  for(const side of [-1,1])upper('pocket','box',[.095,.105,.035],'accent',side*.12,1.37,.24);
  upper('chest-wound','sphere',[.06,1],'wound',-.13,1.19,.235).scale.set(1,1.45,.38);
 }
 if(vomiter){upper('vomiter-belly','sphere',[.32,2],'skin',0,1.10,.13).scale.set(1,1.08,1);for(let n=0;n<9;n++){const x=Math.sin(n*2.4)*.25,y=1.00+n*.055;upper('green-blister','sphere',[.035+(n%3)*.01,1],'accent',x,y,.42-Math.abs(x)*.25);}for(const side of [-1,1])for(let n=0;n<3;n++){upper('fungal-stem','cylinder',[.015,.02,.12,6],'bone',side*(.18+n*.035),1.5+n*.04,-.12);upper('fungal-cap','sphere',[.055,1],'accent',side*(.18+n*.035),1.57+n*.04,-.12).scale.y=.4;}}
 upper('neck','capsule',[.085,.10,3,7],'skin',0,1.57,0);
 const head=new T.Group();head.name='head';head.position.set(0,1.72-.96,.025);torso.add(head);
 part(head,'skull','sphere',[.215,2],'skin',0,0,0).scale.set(.92,1.08,.91);
 part(head,'jaw','box',[.255,.12,.20],'skin',0,-.16,.048);
 part(head,'mouth','box',[.19,.055,.035],'wound',0,-.125,.164);
 for(let n=0;n<5;n++)part(head,'tooth','box',[.023,.027,.015],'bone',(n-2)*.033,-.106,.185);
 part(head,'nose','cone',[.046,.11,5],'skin',0,-.018,.20).rotation.x=-Math.PI/2;
 for(const side of [-1,1]){
  part(head,'cheekbone','sphere',[.065,1],'skin',side*.13,-.03,.13).scale.set(1,.8,.65);
  part(head,'eye-socket','sphere',[.058,1],'black',side*.085,.047,.165).scale.set(1,.7,.55);
  part(head,'eye','sphere',[.027,1],'eye',side*.085,.049,.19);
  part(head,'brow','box',[.10,.027,.04],'skin',side*.083,.09,.166).rotation.z=side*-.17;
  part(head,'ear','sphere',[.045,1],'skin',side*.207,-.008,0).scale.set(.55,1,.7);
 }
 if(boss){
  if(skin.id==='plague'){for(const side of [-1,1]){part(head,'plague-filter','cylinder',[.065,.065,.10,8],'accent',side*.12,-.09,.23).rotation.x=Math.PI/2;upper('canister','cylinder',[.09,.09,.36,8],'accent',side*.16,1.22,-.24);}}
  if(skin.id==='iron'){for(const side of [-1,1]){upper('iron-pauldron','box',[.18,.20,.32],'accent',side*.30,1.47,0);for(let n=0;n<3;n++)upper('iron-rivet','sphere',[.025,1],'bone',side*.30,1.40+n*.06,.17);}part(head,'iron-faceplate','box',[.26,.20,.04],'accent',0,-.04,.22);}
  if(skin.id==='charred')for(let n=0;n<6;n++){upper('charred-fissure','box',[.025,.12,.025],'accent',Math.sin(n*2.4)*.17,1.06+n*.065,.31);}
  if(skin.id==='reclaimer'){part(head,'reclaimer-cowl','sphere',[.25,1],'cloth',0,.05,-.08).scale.set(1,1.1,.7);for(const side of [-1,1])upper('reclaimer-straps','box',[.065,.44,.04],'accent',side*.15,1.26,.32);}
  upper('warden-armor','box',[.46,.36,.10],'black',0,1.29,.25);for(const side of [-1,1]){upper('warden-shoulder','box',[.22,.15,.27],'bone',side*.31,1.47,0);part(head,'warden-horn','cone',[.055,.22,5],'bone',side*.14,.19,-.02).rotation.z=side*-.35;}part(head,'warden-mask','box',[.29,.10,.06],'black',0,-.06,.195);part(head,'warden-eye-band','box',[.25,.028,.018],'wound',0,.048,.208);}
 for(let n=0;n<3;n++)upper('rib-scar','box',[.08,.018,.018],'wound',-.10,1.11+n*.045,.26);
 part(head,'temple-wound','sphere',[.05,1],'wound',.148,.105,.085).scale.set(.5,1,.55);
 if(!brute&&!runner&&!boss&&!vomiter){
  if(skin.id==='firefighter'){part(head,'fire-helmet','sphere',[.24,2],'accent',0,.16,0).scale.set(1,.45,1.15);part(head,'helmet-brim','box',[.49,.025,.46],'black',0,.13,.015);for(const y of [1.05,1.37])upper('reflective-band','box',[.44,.04,.03],'accent',0,y,.25);}
  else if(skin.id==='police'){upper('police-armor','box',[.42,.37,.09],'black',0,1.29,.24);upper('badge','box',[.055,.08,.02],'accent',-.11,1.4,.30);part(head,'police-cap','sphere',[.23,1],'cloth',0,.16,0).scale.set(1,.3,1);part(head,'police-visor','box',[.32,.025,.17],'black',0,.14,.15);}
  else if(skin.id==='mechanic'){for(const side of [-1,1])upper('overall-strap','box',[.06,.34,.035],'accent',side*.13,1.37,.25);upper('overall-bib','box',[.28,.24,.045],'cloth',0,1.24,.27);part(head,'goggle-band','box',[.34,.055,.04],'black',0,.16,.14);}
  else if(skin.id==='infected'){for(const side of [-1,1])upper('exposed-ribs','box',[.15,.24,.04],'wound',side*.11,1.2,.25);for(let n=0;n<4;n++)upper('bone-ribs','capsule',[.018,.15,2,5],'bone',-.1,1.12+n*.05,.28).rotation.z=Math.PI/2;part(head,'jaw-lesion','sphere',[.065,1],'wound',.12,-.16,.12);}
  else if(skin.id==='worker'){
   part(head,'hardhat','sphere',[.234,1],'accent',0,.148,-.01).scale.set(1,.43,1);
   part(head,'hardhat-brim','box',[.49,.025,.39],'accent',0,.128,.015);
   for(const side of [-1,1])upper('reflector','box',[.045,.31,.03],'bone',side*.18,1.25,.25);
  }else if(skin.id==='medic'){
   upper('coat-lapel','box',[.055,.30,.035],'bone',-.07,1.40,.25).rotation.z=.22;
   upper('medical-patch','box',[.095,.10,.022],'bone',.13,1.36,.268);
   upper('cross-vertical','box',[.024,.075,.01],'accent',.13,1.36,.285);
   upper('cross-horizontal','box',[.065,.023,.01],'accent',.13,1.36,.285);
  }else if(skin.id==='soldier'){
   upper('vest','box',[.41,.33,.12],'accent',0,1.30,.24);
   for(const side of [-1,1])upper('ammo-pouch','box',[.12,.14,.07],'cloth',side*.10,1.22,.33);
   part(head,'field-cap','sphere',[.225,1],'cloth',0,.16,0).scale.set(1,.30,1);
   part(head,'cap-visor','box',[.32,.025,.19],'cloth',0,.143,.15);
  }else if(skin.id==='hoodie'){
   upper('hood','sphere',[.22,1],'cloth',0,1.55,-.15).scale.set(1,.65,.55);
   for(const side of [-1,1])upper('drawstring','box',[.012,.16,.016],'accent',side*.08,1.40,.25);
  }else upper('jacket-collar','box',[.32,.065,.15],'accent',0,1.48,.11);
 }
 for(const side of [-1,1]){
  const shoulderMass=upper('shoulder-joint','capsule',[brute?.13:.09,brute?.24:.06,3,7],'cloth',side*(brute?.40:vomiter?.32:.275),1.43,0);shoulderMass.rotation.z=Math.PI/2;
  const hip=new T.Group();hip.name=side<0?'left-hip':'right-hip';hip.position.set(side*(brute?.25:.145),.85,0);root.add(hip);
  part(hip,'thigh','capsule',[brute?.13:.10,.19,3,7],'pants',0,-.195,0);
  const knee=new T.Group();knee.name='knee';knee.position.y=-.39;hip.add(knee);
  part(knee,'shin','capsule',[brute?.115:.085,.22,3,7],'pants',0,-.195,0);
  const ankle=new T.Group();ankle.name='ankle';ankle.position.y=-.39;knee.add(ankle);
  part(ankle,'boot','box',[brute?.24:.18,.14,.29],'boot',0,-.03,.065);
  part(ankle,'boot-sole','box',[brute?.245:.185,.035,.30],'black',0,-.0825,.065);
  legs.push({hip,knee,ankle,side});limbs.push(hip,knee,ankle);
  const shoulder=new T.Group();shoulder.name=side<0?'left-shoulder':'right-shoulder';
  // Wide shoulders keep both arms outside the bloated torso at every gait phase.
  shoulder.position.set(side*(brute?.53:vomiter?.42:.34),1.43-.96,0);torso.add(shoulder);
  shoulder.rotation.z=side*(brute?.09:.10);
  part(shoulder,'upper-arm','capsule',[brute?.105:.075,.19,3,7],'cloth',0,-.17,0);
  const elbow=new T.Group();elbow.name='elbow';elbow.position.y=-.34;shoulder.add(elbow);
  part(elbow,'forearm','capsule',[brute?.09:.063,.21,3,7],'skin',0,-.17,0);
  part(elbow,'hand','box',[brute?.15:.105,.15,.10],'skin',0,-.38,.012);
  for(let f=0;f<3;f++)part(elbow,'claw','cone',[.014,.13,5],'bone',(f-1)*.033,-.49,.036).rotation.z=Math.PI;
  arms.push({pivot:shoulder,elbow,side});limbs.push(shoulder,elbow);
 }
 batchStaticParts(root,owner);return {root,torso,head,limbs,legs,arms,brute,runner,eye:false};
}

export function poseMutant(enemy){
 const v=enemy.visual,phase=enemy.phase,motion=enemy.motion||0,attack=attackPose(enemy),idle=enemy.idleTime||0;
 if(v.eye){v.arms.forEach((a,i)=>a.pivot.rotation.z=Math.sin(phase+i)*.14);return;}
 v.torso.rotation.x=(v.runner?.15:.045)+Math.sin(phase*2)*.018*motion;
 v.torso.rotation.z=Math.sin(phase)*.018*motion;
 if(enemy.type==='vomiter'&&enemy.pendingAttack){v.torso.rotation.x+=.22;v.head.rotation.x=.15;}
 v.head.rotation.y=Math.sin(phase*.5)*.035+Math.sin(idle*.7+enemy.id)*.02;v.head.rotation.x=Math.sin(idle*1.2+enemy.id)*.015;v.torso.rotation.x+=Math.sin(idle*1.5+enemy.id)*.008;if(enemy.type==='vomiter'&&enemy.pendingAttack)v.head.rotation.x=.18;
 for(const leg of v.legs){
  const p=phase+(leg.side<0?0:Math.PI),z=Math.cos(p)*.18*motion*(enemy.hp<enemy.maxHp*.35&&leg.side<0?.6:1),lift=Math.max(0,Math.sin(p))*.12*motion*(enemy.hp<enemy.maxHp*.35&&leg.side<0?.55:1);
  const down=.75-lift,distance=Math.min(.779,Math.hypot(down,z));
  leg.hip.rotation.x=Math.atan2(-z,down)-Math.acos(T.MathUtils.clamp(distance/.78,-1,1));
  leg.knee.rotation.x=2*Math.acos(T.MathUtils.clamp(distance/.78,-1,1));
  leg.ankle.rotation.x=-leg.hip.rotation.x-leg.knee.rotation.x;
 }
 for(const arm of v.arms){
  const stride=Math.cos(phase+(arm.side<0?0:Math.PI));
  arm.pivot.rotation.x=-.12+stride*.22*motion+attack;
  arm.pivot.rotation.z=arm.side*(v.brute?.09:.10);
  arm.elbow.rotation.x=-.16+attack*.25-Math.max(0,-stride)*.10*motion;
 }
}
