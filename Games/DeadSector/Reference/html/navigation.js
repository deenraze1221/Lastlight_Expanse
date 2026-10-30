import {NAV_HALF} from './world-config.js';
// One-metre navigation grid shared by all enemies. Expanded obstacles keep bodies clear.
export function createNavigation(colliders, radius=.4, height=1.8, baseY=0) {
  const groups=new Map();for(const b of colliders){if(b.minY>=baseY+height||b.maxY<=baseY)continue;const key=b.owner??Symbol();if(!groups.has(key))groups.set(key,[]);groups.get(key).push(b);}
  const obstacles=[];for(const group of groups.values())for(let i=0;i<group.length;i++){const b=group[i];if(group.some((a,j)=>j!==i&&a.minX<=b.minX&&a.maxX>=b.maxX&&a.minZ<=b.minZ&&a.maxZ>=b.maxZ&&((a.maxX-a.minX)*(a.maxZ-a.minZ)>(b.maxX-b.minX)*(b.maxZ-b.minZ)||j<i)))continue;obstacles.push(b);}
  const offset=NAV_HALF,size=offset*2+1;
  const blocked = new Uint8Array(size * size);
  const index = (x, z) => (z + offset) * size + x + offset;
  const free = (x, z) => x >= -offset && x <= offset && z >= -offset && z <= offset && !blocked[index(x,z)];
  // Rasterize obstacle footprints directly instead of scanning every collider per cell.
  for(const b of obstacles){const minX=Math.max(-offset,Math.floor(b.minX-radius)+1),maxX=Math.min(offset,Math.ceil(b.maxX+radius)-1),minZ=Math.max(-offset,Math.floor(b.minZ-radius)+1),maxZ=Math.min(offset,Math.ceil(b.maxZ+radius)-1);if(minX>maxX)continue;for(let z=minZ;z<=maxZ;z++)blocked.fill(1,index(minX,z),index(maxX,z)+1);}
  function nearest(p) {
    const x = Math.max(-offset,Math.min(offset,Math.round(p.x))), z = Math.max(-offset,Math.min(offset,Math.round(p.z)));
    for(let r=0;r<7;r++){let best=null,score=Infinity;for(let dz=-r;dz<=r;dz++)for(let dx=-r;dx<=r;dx++)if(free(x+dx,z+dz)){const distance=(x+dx-p.x)**2+(z+dz-p.z)**2;if(distance<score){best=[x+dx,z+dz];score=distance;}}if(best)return best;}

    return null;
  }
  const fields=new Map();
  const clear=(a,b)=>{const length=Math.hypot(b.x-a.x,b.z-a.z),steps=Math.ceil(length*4);for(let i=0;i<=steps;i++){const t=steps?i/steps:0;if(!free(Math.round(a.x+(b.x-a.x)*t),Math.round(a.z+(b.z-a.z)*t)))return false;}return true;};
  return {clear,path(from,to){const a=nearest(from),b=nearest(to);if(!a||!b)return [];const start=index(...a),end=index(...b);let parents=fields.get(end);
  if(!parents){parents=new Int32Array(size*size).fill(-1);parents[end]=end;const queue=[end];for(let head=0;head<queue.length;head++){const current=queue[head],x=current%size-offset,z=Math.floor(current/size)-offset;for(const [dx,dz]of [[1,0],[-1,0],[0,1],[0,-1]])if(free(x+dx,z+dz)){const n=index(x+dx,z+dz);if(parents[n]===-1){parents[n]=current;queue.push(n);}}}fields.set(end,parents);if(fields.size>48)fields.delete(fields.keys().next().value);}
  if(parents[start]===-1)return [];const path=[];for(let n=start;n!==end;){n=parents[n];path.push({x:n%size-offset,z:Math.floor(n/size)-offset});}return path;}};
}
