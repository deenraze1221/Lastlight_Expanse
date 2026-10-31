// Physics is independent of rendering so fixed-step collision rules can be tested.
export const PLAYER = Object.freeze({ radius: 0.32, height: 1.8, eye: 1.65, lookDown: Math.PI / 3, walk: 4.5, run: 7.5, gravity: 20, jump: 7 / Math.SQRT2 });
export const FIXED_STEP = 1 / 120;

export function overlapsBody(position, box) {
  return position.x + PLAYER.radius > box.minX && position.x - PLAYER.radius < box.maxX &&
    position.z + PLAYER.radius > box.minZ && position.z - PLAYER.radius < box.maxZ &&
    position.y + PLAYER.height > box.minY && position.y < box.maxY;
}

export class Player {
  constructor(spawn) { this.spawn = { ...spawn }; this.reset(); }
  reset() {
    this.position = { ...this.spawn }; this.yaw = 0; this.pitch = 0;
    this.velocityX=0;this.velocityZ=0;this.speed=0;this.velocityY = 0; this.grounded = true; this.distance = 0;this.stamina=5;this.exhausted=false;this.sprinting=false;
  }
  look(dx, dy, sensitivity) {
    if(!Number.isFinite(dx)||!Number.isFinite(dy)||!Number.isFinite(sensitivity))return;
    this.yaw -= dx * 0.002 * sensitivity;
    this.pitch = Math.max(-PLAYER.lookDown, Math.min(Math.PI / 2 - 0.02, this.pitch - dy * 0.002 * sensitivity));
  }
  step(dt, input, colliders) {
    let forward = Number(input.forward) - Number(input.backward);
    let right = Number(input.right) - Number(input.left);
    const magnitude = Math.hypot(forward, right);
    if (magnitude > 0) { forward /= magnitude; right /= magnitude; }
    const wantsSprint=Boolean(input.sprint&&magnitude>0&&this.stamina>1e-8&&!this.exhausted),speed=wantsSprint?PLAYER.run:PLAYER.walk;
    const targetX=(right*Math.cos(this.yaw)-forward*Math.sin(this.yaw))*speed,targetZ=(-right*Math.sin(this.yaw)-forward*Math.cos(this.yaw))*speed;
    const vx=targetX-this.velocityX,vz=targetZ-this.velocityZ,difference=Math.hypot(vx,vz),limit=(magnitude>0?36:55)/1.15*dt,factor=difference>0?Math.min(1,limit/difference):1;this.velocityX+=vx*factor;this.velocityZ+=vz*factor;
    const dx=this.velocityX*dt,dz=this.velocityZ*dt;
    const oldX = this.position.x, oldZ = this.position.z;
    // Resolve axes separately: the player slides along walls instead of sticking.
    for (const [axis, delta, min, max] of [['x', dx, 'minX', 'maxX'], ['z', dz, 'minZ', 'maxZ']]) {
      this.position[axis] += delta;
      for (const box of colliders) {
        if (!overlapsBody(this.position, box)) continue;
        if (delta > 0) this.position[axis] = box[min] - PLAYER.radius;
        else if (delta < 0) this.position[axis] = box[max] + PLAYER.radius;
        if(axis==='x')this.velocityX=0;else this.velocityZ=0;
      }
    }
    const travel=Math.hypot(this.position.x-oldX,this.position.z-oldZ);this.distance+=travel;this.speed=travel/Math.max(dt,1e-6);this.sprinting=wantsSprint&&this.speed>.1;
    if(this.sprinting){this.stamina=Math.max(0,this.stamina-dt);if(this.stamina<1e-8){this.stamina=0;this.exhausted=true;}}else{this.stamina=Math.min(5,this.stamina+dt);if(this.stamina>=5-1e-8){this.stamina=5;this.exhausted=false;}}

    if (input.jump && this.grounded) { this.velocityY = PLAYER.jump; this.grounded = false; }
    const previousY = this.position.y;
    this.velocityY -= PLAYER.gravity * dt;
    this.position.y += this.velocityY * dt;
    this.grounded = false;
    for (const box of colliders) {
      if (!overlapsBody(this.position, box)) continue;
      if (this.velocityY <= 0 && previousY >= box.maxY - 0.001) {
        this.position.y = box.maxY; this.velocityY = 0; this.grounded = true;
      } else if (this.velocityY > 0 && previousY + PLAYER.height <= box.minY + 0.001) {
        this.position.y = box.minY - PLAYER.height; this.velocityY = 0;
      }
    }
    if (this.position.y <= 0) { this.position.y = 0; this.velocityY = 0; this.grounded = true; }
  }
}
