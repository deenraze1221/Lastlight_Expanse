// Local-only coverage fade: never alter shared materials used by remote soldiers.
export class BodyVisibility {
 constructor(root){this.root=root;this.opacity=0;const clones=new Map();root.traverse(o=>{if(!o.isMesh)return;const copy=m=>{if(!clones.has(m)){const c=m.clone();c.alphaHash=true;c.opacity=0;clones.set(m,c);}return clones.get(m);};o.material=Array.isArray(o.material)?o.material.map(copy):copy(o.material);});this.materials=[...clones.values()];root.visible=false;}
 update(pitch,dt,active){const x=Math.max(0,Math.min(1,(-pitch-.85)/.35)),target=active?x*x*(3-2*x):0;this.opacity+=(target-this.opacity)*(1-Math.exp(-10*dt));if(Math.abs(this.opacity-target)<.001)this.opacity=target;for(const m of this.materials)m.opacity=this.opacity;this.root.visible=this.opacity>.003;return this.opacity;}
}
export function canSelectTrap(account){return Number.isFinite(account?.traps)&&account.traps>0;}
