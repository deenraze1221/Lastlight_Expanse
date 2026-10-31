export const ENEMY_TYPES={
 boss:{hp:1800,speed:1.3,damage:55,weight:0,radius:2.1,height:6.6,scale:3.3},
 zombie:{hp:250,speed:2.42,damage:25,weight:55,radius:.5,height:2,scale:1},
 runner:{hp:125,speed:3.78,damage:15,weight:20,radius:.38,height:1.5,scale:.75},
 eye:{hp:200,speed:2.3,damage:30,weight:20,radius:.7,height:1.5,scale:1},
 vomiter:{hp:300,speed:2.057,damage:10,weight:0,radius:.625,height:2.5,scale:1.25},
 brute:{hp:800,speed:1.1,damage:40,weight:5,radius:2.15,height:6,scale:3},
};
export function chooseEnemy(random=Math.random,wave=Infinity){const eligible=Object.entries(ENEMY_TYPES).filter(([type,s])=>s.weight>0&&(type==='eye'?wave>=6:type==='brute'?wave>=21:true));let roll=random()*eligible.reduce((n,[,s])=>n+s.weight,0);for(const [type,s]of eligible){roll-=s.weight;if(roll<0)return type;}return eligible[eligible.length-1][0];}
export class Waves {
 constructor(seed=1){this.seed=seed>>>0;this.number=1;this.time=0;this.spawned=0;this.vomiters=0;this.rest=false;this.clearing=false;this.warmup=true;}
 random(){this.seed=(1664525*this.seed+1013904223)>>>0;return this.seed/4294967296;}
 get variant(){return this.number%5===0?'boss':this.number%4===0?'blackout':this.number%3===0?'rush':this.number%3===2?'siege':'standard';}
 get total(){return 15+(this.number-1)*5;}
 step(dt,spawn,getAlive=()=>0){
  let remaining=dt;
  while(remaining>1e-9){
   if(this.warmup){const chunk=Math.min(remaining,60-this.time);this.time+=chunk;remaining-=chunk;if(this.time<60-1e-9)return;this.warmup=false;this.time=0;if(remaining<=1e-9)return;}
   if(this.clearing){
    if(getAlive()>0)return;
    this.clearing=false;this.rest=true;this.time=0;
   }
   const limit=this.rest?30:120,chunk=Math.min(remaining,limit-this.time);
   this.time+=chunk;remaining-=chunk;
   if(!this.rest){
    const due=Math.min(this.total,Math.floor((this.time+1e-8)*this.total/120));
    while(this.spawned<due){const roll=this.random(),type=this.variant==='rush'&&roll<.5?'runner':chooseEnemy(()=>roll,this.number),hive=this.variant==='siege'?this.number%4:Math.floor(this.random()*4),boss=this.variant==='boss'&&this.spawned===this.total-1;const vomit=!boss&&this.number>=6&&this.vomiters<Math.floor(this.total*.15)&&this.random()<.15;if(vomit)this.vomiters++;spawn(boss?'boss':vomit?'vomiter':type,hive,{boss});this.spawned++;}
   }
   if(this.time>=limit-1e-9){
    if(this.rest){this.number++;this.spawned=0;this.vomiters=0;this.rest=false;this.time=0;}
    else if(getAlive()>0){this.time=120;this.clearing=true;}
    else{this.rest=true;this.time=0;}
   }
  }
 }
 snapshot(){return {warmup:this.warmup,seed:this.seed,number:this.number,time:this.time,variant:this.variant,spawned:this.spawned,vomiters:this.vomiters,rest:this.rest,clearing:this.clearing};}
 restore(v){const {variant,...state}=v;Object.assign(this,state);this.clearing=Boolean(v.clearing);this.warmup=Boolean(v.warmup);}
}

export function bossHealth(wave=5){return 1800+Math.max(0,Math.floor(wave/5)-1)*600;}
