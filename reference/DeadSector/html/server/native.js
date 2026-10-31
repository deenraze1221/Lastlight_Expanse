// Native Godot rooms share the deployment/port, but never the HTML simulation.
import {WebSocketServer, WebSocket} from 'ws';
import {randomBytes, randomInt} from 'node:crypto';
export function createNativeRelay() {
 const wss=new WebSocketServer({noServer:true,maxPayload:524288});
 const rooms=new Map(),roles=['host','guest','guest2','guest3'];
 const send=(s,m)=>{if(s?.readyState===WebSocket.OPEN)s.send(JSON.stringify(m));};
 const broadcast=(r,m,except)=>{for(const id of roles)if(id!==except)send(r.seats[id]?.socket,m);};
 const members=r=>roles.filter(id=>r.seats[id]).map(id=>({id,connected:!!r.seats[id].socket}));
 const lobby=r=>broadcast(r,{type:'lobby',members:members(r),phase:r.phase});
 function detach(s,explicit=false){const r=s.room,id=s.role;if(!r||r.seats[id]?.socket!==s)return;s.room=null;
  if(explicit&&id==='host'){broadcast(r,{type:'closed'});for(const seat of Object.values(r.seats))if(seat.socket)seat.socket.room=null;rooms.delete(r.code);return;}
  if(explicit)delete r.seats[id];else{r.seats[id].socket=null;r.seats[id].until=Date.now()+120000;}
  broadcast(r,{type:'away',id});lobby(r);
 }
 function attach(s,r,id){s.room=r;s.role=id;r.seats[id].socket=s;
  send(s,{type:'room',id,host:id==='host',code:r.code,token:r.seats[id].token,seed:r.seed,phase:r.phase,members:members(r)});lobby(r);
 }
 const finiteVector=v=>Array.isArray(v)&&v.length===3&&v.every(n=>Number.isFinite(n)&&Math.abs(n)<10000);
 wss.on('connection',s=>{s.on('message',raw=>{try{
  const m=JSON.parse(raw);if(!m||typeof m!=='object')return;const r=s.room;
  if(m.type==='create'){if(rooms.size>=100)return send(s,{type:'error',message:'Server full'});detach(s,true);let code;do{code=String(randomInt(10000000,100000000));}while(rooms.has(code));const room={code,seed:(m.seed>>>0)||42,phase:'lobby',seats:{host:{token:randomBytes(24).toString('hex')}}};rooms.set(code,room);attach(s,room,'host');}
  else if(m.type==='join'){const room=rooms.get(String(m.code)),id=room&&roles.slice(1).find(key=>!room.seats[key]);if(!id||room.phase!=='lobby')return send(s,{type:'error',message:'Lobby unavailable, full or started'});detach(s,true);room.seats[id]={token:randomBytes(24).toString('hex')};attach(s,room,id);}
  else if(m.type==='resume'){const room=rooms.get(String(m.code)),id=room&&roles.find(key=>room.seats[key]?.token===m.token);if(!id||room.seats[id].socket)return send(s,{type:'error',message:'Session expired'});attach(s,room,id);}
  else if(r&&m.type==='leave')detach(s,true);
  else if(r&&m.type==='ping')send(s,{type:'pong',sent:m.sent});
  else if(r&&m.type==='countdown'&&s.role==='host'&&r.phase==='lobby'){r.phase='countdown';broadcast(r,{type:'countdown',seconds:5});lobby(r);}
  else if(r&&m.type==='start'&&s.role==='host'&&['lobby','countdown'].includes(r.phase)){r.phase='playing';broadcast(r,{type:'start',seed:r.seed});lobby(r);}
  else if(r&&m.type==='pose'&&finiteVector(m.p)&&Number.isFinite(m.yaw)&&Number.isFinite(m.hp))broadcast(r,{...m,id:s.role},s.role);
  else if(r&&r.phase==='playing'){
   if(s.role==='host'&&['snapshot','damage','grant','effect','revived'].includes(m.type))broadcast(r,{...m,id:s.role},s.role);
   else if(['shot','trap','revive'].includes(m.type)){
    if(m.type==='shot'&&(!finiteVector(m.direction)||typeof m.weapon!=='string'))return;
    send(r.seats.host.socket,{...m,id:s.role});
   }
  }
 }catch{send(s,{type:'error',message:'Invalid packet'});}});s.on('close',()=>detach(s));s.on('error',()=>detach(s));});
 const timer=setInterval(()=>{for(const r of rooms.values()){
  if(!r.seats.host.socket&&r.seats.host.until<Date.now()){broadcast(r,{type:'closed'});rooms.delete(r.code);continue;}
  for(const id of roles.slice(1))if(r.seats[id]&&!r.seats[id].socket&&r.seats[id].until<Date.now()){delete r.seats[id];lobby(r);}
 }},5000);timer.unref();wss.on('close',()=>clearInterval(timer));
 return {wss,rooms};
}
