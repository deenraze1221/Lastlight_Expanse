import {bounds,intersects} from './asset-catalog.js';
export function validateTown(layout){const errors=[],byId=new Map(layout.props.map(p=>[p.id,p]));
 const report=(message,id)=>errors.push({message,id});
 for(const p of layout.props){const a=bounds(p);if(p.y){const support=byId.get(p.supportId);if(!support)report('Unsupported elevated object',p.id);else{const s=bounds(support);if(Math.abs(a.minY-s.maxY)>.001||a.minX<s.minX||a.maxX>s.maxX||a.minZ<s.minZ||a.maxZ>s.maxZ)report('Invalid support footprint',p.id);}}
  if(p.type==='fence'&&(p.length<5||Math.abs(Math.sin(p.yaw*2))>.001||p.context!=='boundary'))report('Disconnected or unaligned fence',p.id);
  if(p.type==='bin'){const pair=byId.get(p.pairedWith);if(!pair||!['bench','busstop'].includes(pair.type)||Math.hypot(pair.x-p.x,pair.z-p.z)>3)report('Unpaired bin',p.id);}
  if(p.type==='bench'||p.type==='busstop'){const dx=p.faces.x-p.x,dz=p.faces.z-p.z,distance=Math.hypot(dx,dz);if((Math.sin(p.yaw)*dx+Math.cos(p.yaw)*dz)/distance<.98)report('Seating faces away from pedestrian zone',p.id);}
  if(p.context==='traffic'){if(Math.abs(p.x-p.road)+(a.maxX-a.minX)/2>2.75+.001||p.yaw!==(p.direction==='north'?0:Math.PI))report('Vehicle is outside lane or faces wrong direction',p.id);}
  if(p.context==='parking'){const bay=layout.parking.find(b=>b.id===p.parkingId);if(!bay||a.minX<bay.x-bay.w/2||a.maxX>bay.x+bay.w/2||a.minZ<bay.z-bay.d/2||a.maxZ>bay.z+bay.d/2||p.yaw!==bay.yaw)report('Vehicle outside assigned bay',p.id);}
  for(const b of layout.buildings)if(intersects(a,{minX:b.x-b.w/2,maxX:b.x+b.w/2,minY:0,maxY:b.h,minZ:b.z-b.d/2,maxZ:b.z+b.d/2}))report('Exterior prop intersects building',p.id);
  for(const wall of layout.walls){const b={minX:wall.x-wall.w/2,maxX:wall.x+wall.w/2,minY:0,maxY:wall.h,minZ:wall.z-wall.d/2,maxZ:wall.z+wall.d/2};if(intersects(a,b))report('Prop intersects perimeter',p.id);}
 }
 for(let i=0;i<layout.props.length;i++)for(let j=i+1;j<layout.props.length;j++){const a=layout.props[i],b=layout.props[j];if(a.type==='fence'&&b.type==='fence'&&a.building===b.building){const ends=p=>[-1,1].map(side=>({x:p.x+side*Math.cos(p.yaw)*p.length/2,z:p.z-side*Math.sin(p.yaw)*p.length/2}));if(intersects(bounds(a),bounds(b))&&!ends(a).some(x=>ends(b).some(y=>Math.hypot(x.x-y.x,x.z-y.z)<1e-5)))report('Fence lines intersect without a common end post',a.id+'/'+b.id);continue;}if(intersects(bounds(a),bounds(b)))report('Exterior props intersect',a.id+'/'+b.id);}
 for(const f of layout.furniture){const a=bounds(f),room=layout.rooms.find(r=>r.id===f.room);if(f.y!==0||!room||a.minX<room.minX+.05||a.maxX>room.maxX-.05||a.minZ<room.minZ+.05||a.maxZ>room.maxZ-.05)report('Furniture outside room or unsupported',f.id);for(const c of layout.colliders)if(['wall','partition'].includes(c.kind)&&intersects(a,c))report('Furniture intersects wall',f.id);}
 for(let i=0;i<layout.furniture.length;i++)for(let j=i+1;j<layout.furniture.length;j++){const a=layout.furniture[i],b=layout.furniture[j];if(a.building===b.building&&intersects(bounds(a),bounds(b)))report('Furniture intersects furniture',a.id+'/'+b.id);}
 for(const room of layout.rooms)if(layout.furniture.filter(f=>f.room===room.id).length<2)report('Unfurnished accessible room',room.id);
 return errors;
}
