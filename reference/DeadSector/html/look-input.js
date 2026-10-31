// Relative input stays unscaled by frame time. Only stale events and isolated warps are rejected.
export class LookInput{
 constructor(){this.reset(0);}
 reset(now){this.x=0;this.y=0;this.readyAt=now+80;this.lastAt=-Infinity;this.previous=0;this.samples=0;}
 add(x,y,now,receivedAt=now){if(now<this.readyAt||now<this.lastAt||!Number.isFinite(x)||!Number.isFinite(y)||!Number.isFinite(now))return;const magnitude=Math.hypot(x,y),gap=now-this.lastAt;if(magnitude>600||(this.samples>=2&&gap>=0&&gap<24&&magnitude>Math.max(180,this.previous*12)))return;this.lastAt=now;this.previous=magnitude;this.samples++;this.x+=x;this.y+=y;}
 consume(dt,sensitivity=1){const limit=(Math.PI/2)/(.002*Math.max(.2,sensitivity)),x=Math.max(-limit,Math.min(limit,this.x)),y=Math.max(-limit,Math.min(limit,this.y));this.x=0;this.y=0;return {x,y};}
}
export async function requestLookLock(canvas){try{return await canvas.requestPointerLock({unadjustedMovement:true});}catch(error){if(error?.name!=='NotSupportedError')throw error;return await canvas.requestPointerLock();}}
