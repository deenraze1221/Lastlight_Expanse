// A new hit refreshes five ticks; poison does not stack or replay a network hit.
export class PoisonEffect{
 constructor(){this.reset();}
 reset(){this.remaining=0;this.timer=0;this.seen=new Set();}
 apply(id){if(id&&this.seen.has(id))return false;if(id){this.seen.add(id);if(this.seen.size>128)this.seen.delete(this.seen.values().next().value);}this.remaining=5;this.timer=0;return true;}
 step(dt,hurt){if(!this.remaining)return;this.timer+=dt;while(this.timer>=1-1e-9&&this.remaining){this.timer-=1;this.remaining--;hurt(5);}}
 get active(){return this.remaining>0;}
}
