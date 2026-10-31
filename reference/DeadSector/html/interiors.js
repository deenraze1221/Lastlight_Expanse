import {DIMENSIONS,bounds,intersects} from './asset-catalog.js';
import {STORE} from './economy.js';

export function createInteriors(b,wall,addFurniture,random){
 const t=.22,left=b.x-b.w/2+t,right=b.x+b.w/2-t,back=b.z-b.d/2+t,front=b.z+b.d/2-t;
 const rooms=[],occupied=[],reserved=[];b.partitions=[];b.floorplan=b.type==='grocery'?'market':['spine','front-lounge','service-band'][b.variant%3];
 function partition(x,z,w,d){wall(x,0,z,w,3.12,d,'partition',b.id);}
 function cross(z,start,end,doors){let cursor=start;for(const x of doors.sort((a,b)=>a-b)){if(x-.8>cursor)partition((cursor+x-.8)/2,z,x-.8-cursor,.12);cursor=x+.8;}if(end>cursor)partition((cursor+end)/2,z,end-cursor,.12);}
 function room(name,minX,maxX,minZ,maxZ,entry){const r={id:`${b.id}/${rooms.length}`,building:b.id,name,minX,maxX,minZ,maxZ,accessPoint:entry};rooms.push(r);return r;}
 function reserve(minX,maxX,minZ,maxZ){reserved.push({minX,maxX,minZ,maxZ,minY:0,maxY:3});}
 function put(r,type,x,z,yaw=0){const f={id:`interior-${b.id}-${occupied.length}`,type,x,z,y:0,yaw,variant:Math.floor(random()*16),building:b.id,room:r.id};[f.w,f.h,f.d]=DIMENSIONS[type];const a=bounds(f);if(a.minX<r.minX+.08||a.maxX>r.maxX-.08||a.minZ<r.minZ+.08||a.maxZ>r.maxZ-.08||occupied.some(p=>intersects(a,p,.12))||reserved.some(p=>intersects(a,p,.025)))return null;occupied.push(a);addFurniture(f);return f;}
 if(b.type==='grocery'){
  const r=room('grocery',left,right,back,front,{x:b.x,z:front-.5});
  for(const [type,x,z,yaw=0]of [
   ['terminal',STORE.terminal.x,STORE.terminal.z],['checkout',7.3,13.5],['produceStand',13,13.1],['refrigeratedCase',13.35,6.1],['freezer',10.5,6.1],['basketStand',9,13.8],['shoppingCart',6.7,8.3],['stockBoxes',6.7,6.2],['groceryShelf',10.5,8.9,Math.PI/2],['groceryShelf',10.5,11,Math.PI/2],['displayRack',13.5,9.2]])put(r,type,x,z,yaw);
  b.rooms=rooms;return rooms;
 }
 const residential=['apartment','hotel','motel','residentialHighrise'].includes(b.type),service=['garage','warehouse','workshop','utility','firestation','communicationsTower','freightDepot','observatory'].includes(b.type);
 const mainName=residential?'living-kitchen':service?'workshop':['shop','bakery','laundry','postoffice'].includes(b.type)?'retail':['clinic','hospitalTower'].includes(b.type)?'consultation':'office';
 const secondary=residential?'bedroom':service?'storage':['clinic','hospitalTower'].includes(b.type)?'treatment':'meeting';
 if(b.floorplan==='spine'){
  const spine=b.x+((b.variant%2)*2-1)*.18,vestibule=1.4+(b.variant%3)*.25,end=front-vestibule,half=1.05;
  reserve(spine-.65,spine+.65,back+1.8,front+.2);
  for(const side of [-1,1]){
   const partitionX=spine+side*half,opening=Math.round(b.z+((b.variant%2)?-.8:.8)),minX=side<0?left:partitionX+.06,maxX=side<0?partitionX-.06:right;
   for(const [a,c]of [[back,opening-.8],[opening+.8,end]])if(c>a)partition(partitionX,(a+c)/2,.12,c-a);
   const r=room(side<0?mainName:secondary,minX,maxX,back,end,{x:side<0?maxX+.35:minX-.35,z:opening});
   reserve(minX,maxX,opening-.55,opening+.55);r.route=[{x:spine,z:front+.6},{x:spine,z:opening},{x:(minX+maxX)/2,z:opening}];
  }
  const wash=room('washroom',spine-half+.06,spine+half-.06,back,back+1.75,{x:spine,z:back+1.4});
  for(const side of [-1,1])partition(spine+side*.92,back+1.75,.14,.12);
  wash.route=[{x:spine,z:front+.5},{x:spine,z:back+1.3}];
 }else{
  const split=b.floorplan==='front-lounge'?b.z-.2:back+2.2,mid=b.x+((b.variant%2)*2-1)*.5;
  const lx=Math.round((left+mid)/2),rx=Math.round((mid+right)/2);
  cross(split,left,right,[lx,rx]);partition(mid,(back+split)/2,.12,split-back-.06);
  const main=room(mainName,left,right,split+.08,front,{x:b.x,z:front-.7});main.route=[{x:b.x,z:front+.5},{x:b.x,z:split+.8}];
  reserve(b.x-.6,b.x+.6,split+.1,front+.1);reserve(left,right,split+.15,split+1.2);
  const a=room('washroom',left,mid-.07,back,split-.08,{x:lx,z:split-.5});a.route=[{x:b.x,z:front+.5},{x:b.x,z:split+.7},{x:lx,z:split+.7},{x:lx,z:split-.5}];
  const c=room(secondary,mid+.07,right,back,split-.08,{x:rx,z:split-.5});c.route=[{x:b.x,z:front+.5},{x:b.x,z:split+.7},{x:rx,z:split+.7},{x:rx,z:split-.5}];
  for(const x of [lx,rx])reserve(x-.55,x+.55,split-1,split+.2);
 }
 function against(r,type,side,offset=0){const [w,,d]=DIMENSIONS[type],cx=(r.minX+r.maxX)/2,cz=(r.minZ+r.maxZ)/2;
  if(side===0)return put(r,type,cx+offset,r.minZ+d/2+.11,0);
  if(side===1)return put(r,type,cx+offset,r.maxZ-d/2-.11,Math.PI);
  if(side===2)return put(r,type,r.minX+d/2+.11,cz+offset,Math.PI/2);
  return put(r,type,r.maxX-d/2-.11,cz+offset,-Math.PI/2);
 }
 for(const r of rooms){
  if(r.name==='washroom'){
   put(r,'toilet',r.minX+.4,r.minZ+.46);put(r,'sink',r.maxX-.53,r.minZ+.38);
  }else{
   const program=r.name==='bedroom'?['bed','cabinet','tvstand','stool']:r.name==='treatment'?['examBed','sink','cabinet','stool']:r.name==='storage'?['shelf','workbench','locker','crate']:r.name==='workshop'?['workbench','shelf','cabinet','stool']:r.name==='living-kitchen'?['desk','fridge','sink','oven','sofa','table']:r.name==='retail'?['counter','shelf','shelf','cabinet']:['desk','table','cabinet','sofa','shelf'];
   const rotation=(b.variant+Number(r.id.split('/')[1]))%4;
   for(const [index,type]of program.entries()){
    for(let attempt=0;attempt<4;attempt++){
     const side=(rotation+index+attempt)%4,offset=((index%3)-1)*.72,f=against(r,type,side,offset);
     if(!f)continue;
     if(['desk','table','workbench','counter'].includes(type)){const distance=DIMENSIONS[type][2]/2+.52;put(r,'chair',f.x+Math.sin(f.yaw)*distance,f.z+Math.cos(f.yaw)*distance,f.yaw+Math.PI);}
     break;
    }
   }
  }
  // Small useful furnishings fill only free perimeter positions, never a door route.
  for(const kind of r.name==='washroom'?[]:['cabinet','stool','shelf'])for(const [dx,dz]of [[.55,.55],[-.55,.55],[.55,-.55],[-.55,-.55]]){
   const count=occupied.filter(a=>a.minX>=r.minX&&a.maxX<=r.maxX&&a.minZ>=r.minZ&&a.maxZ<=r.maxZ).length;if(count>=3)break;
   const [w,,d]=DIMENSIONS[kind];put(r,kind,dx>0?r.maxX-w/2-.12:r.minX+w/2+.12,dz>0?r.maxZ-d/2-.12:r.minZ+d/2+.12,dz>0?Math.PI:0);
  }
 }
 b.rooms=rooms;return rooms;
}
