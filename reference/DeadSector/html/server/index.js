import {createNativeRelay} from './native.js';
import {MAP_HALF} from '../world-config.js';
import {createServer} from 'node:http';import {readFile} from 'node:fs/promises';import {resolve,extname} from 'node:path';import {randomBytes,randomInt} from 'node:crypto';import {WebSocketServer,WebSocket} from 'ws';
import {WEAPON_BY_ID} from '../weapon-config.js';
import {createInventory,syncCombat,buy,equipWeapon,useMedkit,useSupply,consumeTrap,nearTerminal,rewardForHP} from '../economy.js';
import {ENEMY_TYPES} from '../waves.js';
const GRACE=120000,ROLES=['host','guest','guest2','guest3'];
export function createGameServer(){const root=resolve('dist');const server=createServer(async(req,res)=>{try{const pathname=new URL(req.url,'http://local').pathname,file=resolve(root,'.'+decodeURIComponent(pathname==='/'?'/index.html':pathname));if(!file.startsWith(root+'/'))return res.writeHead(403).end();const bytes=await readFile(file);res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css'})[extname(file)]||'application/octet-stream');res.end(bytes);}catch{res.writeHead(404).end('Build the game with npm run build.');}});
const native=createNativeRelay();
const wss=new WebSocketServer({noServer:true,maxPayload:524288});
server.on('upgrade',(req,socket,head)=>{const hub=new URL(req.url,'http://local').pathname==='/godot'?native.wss:wss;hub.handleUpgrade(req,socket,head,ws=>hub.emit('connection',ws,req));});
wss.on('close',()=>native.wss.close());
const rooms=new Map(),send=(s,m)=>{if(s?.readyState===WebSocket.OPEN)s.send(JSON.stringify(m));};
const other=(r,role)=>ROLES.filter(key=>key!==role).map(key=>r[key]).find(Boolean);
const broadcast=(r,m,except)=>{for(const role of ROLES)if(role!==except)send(r[role]?.socket,m);};
const members=r=>ROLES.map((role,slot)=>({id:role,slot,connected:!!r[role]?.socket,reserved:!!r[role],ready:!!r[role]?.ready}));
const lobby=r=>broadcast(r,{type:'lobby',members:members(r),phase:r.phase,startAt:r.startAt});
function cancelStart(r){if(r.phase==='countdown'){clearTimeout(r.startTimer);r.phase='lobby';r.startAt=null;broadcast(r,{type:'countdown-cancelled'});}}
function remove(s,explicit=false){const r=s.room,role=s.role;if(!r||r[role]?.socket!==s)return;cancelStart(r);const seat=r[role];seat.socket=null;seat.ready=false;seat.until=Date.now()+GRACE;s.room=null;if(explicit&&role==='host'){broadcast(r,{type:'closed'});for(const key of ROLES)if(r[key]?.socket)r[key].socket.room=null;clearTimeout(r.startTimer);rooms.delete(r.code);}else if(explicit){r[role]=null;broadcast(r,{type:'left',role});lobby(r);}else{broadcast(r,{type:'away',role});lobby(r);}}
function attach(s,r,role,resume=false){const seat=r[role];seat.socket=s;seat.until=0;s.room=r;s.role=role;send(s,{type:'room',code:r.code,role,slot:ROLES.indexOf(role),members:members(r),phase:r.phase,startAt:r.startAt,host:role==='host',seed:r.seed,token:seat.token,resume,snapshot:r.snapshot,selfPose:seat.pose,account:seat.account,result:r.result});for(const key of ROLES)if(key!==role&&r[key]?.pose)send(s,{...r[key].pose,id:key});broadcast(r,{type:'joined',role},role);lobby(r);}
const timer=setInterval(()=>{for(const r of rooms.values()){if(!r.host.socket&&r.host.until<Date.now()){clearTimeout(r.startTimer);broadcast(r,{type:'closed'});rooms.delete(r.code);}else for(const role of ROLES.slice(1))if(r[role]&&!r[role].socket&&r[role].until<Date.now()){r[role]=null;lobby(r);}}},5000);timer.unref();wss.on('close',()=>{clearInterval(timer);for(const r of rooms.values())clearTimeout(r.startTimer);});
wss.on('connection',s=>{s.on('message',data=>{try{const m=JSON.parse(data),r=s.room;if(m.type==='create'){remove(s,true);if(rooms.size>=100)return send(s,{type:'error',message:'Server full'});let code;do{code=String(randomInt(10000000,100000000));}while(rooms.has(code));const room={code,host:{token:randomBytes(24).toString('hex'),account:createInventory()},guest:null,guest2:null,guest3:null,phase:'lobby',startAt:null,seed:m.seed>>>0,snapshot:null,cash:new Map(),rewarded:new Set()};rooms.set(code,room);attach(s,room,'host');}
else if(m.type==='join'){const room=rooms.get(String(m.code)),role=room&&ROLES.slice(1).find(key=>!room[key]);if(!room||!role||room.phase!=='lobby')return send(s,{type:'error',message:'Lobby unavailable, full or already started'});remove(s,true);room[role]={token:randomBytes(24).toString('hex'),account:createInventory()};attach(s,room,role);}
else if(m.type==='resume'){const room=rooms.get(String(m.code).toUpperCase()),role=room&&ROLES.find(key=>room[key]?.token===m.token);if(!role||room[role].socket)return send(s,{type:'error',message:'Resume expired or already connected'});attach(s,room,role,true);}
else if(r&&m.type==='ping'&&Number.isFinite(m.sent))send(s,{type:'pong',sent:m.sent});
else if(r&&m.type==='ready'&&r.phase==='lobby'){r[s.role].ready=!!m.ready;lobby(r);}
else if(r&&m.type==='start'&&s.role==='host'&&r.phase==='lobby'){r.phase='countdown';r.startAt=Date.now()+5000;broadcast(r,{type:'countdown',startAt:r.startAt});lobby(r);r.startTimer=setTimeout(()=>{if(r.phase==='countdown'&&r.host.socket){r.phase='playing';broadcast(r,{type:'match-start',seed:r.seed});lobby(r);}},5000);}
else if(r&&m.type==='leave')remove(s,true);
else if(r&&m.type==='pose'&&[m.x,m.y,m.z,m.yaw,m.hp].every(Number.isFinite)&&Math.abs(m.x)<MAP_HALF&&Math.abs(m.z)<MAP_HALF&&m.y>=0&&m.y<30){const own=r[s.role];if(!m.inventory||m.inventory.revision===own.account.revision)own.account.health=m.hp;else m.hp=own.account.health;own.pose=m;syncCombat(own.account,m.inventory);broadcast(r,{...m,id:s.role},s.role);}
else if(r&&m.type==='state'&&s.role==='host'&&Array.isArray(m.enemies)&&m.enemies.length<=1000){for(const e of m.enemies){const id='cash-'+e.id;if(e.hp<=0&&ENEMY_TYPES[e.type]&&!r.cash.has(id))r.cash.set(id,{id,x:e.x,z:e.z,value:rewardForHP(ENEMY_TYPES[e.type].hp),taken:false});}m.cash=[...r.cash.values()].filter(c=>!c.taken);r.snapshot=m;for(const t of m.operations?.traps||[])r.pendingTraps?.delete(t.id);
const reward=(key,amount)=>{if(r.rewarded.has(key))return;r.rewarded.add(key);for(const role of ROLES){const seat=r[role];if(seat){seat.account.money+=amount;seat.account.revision++;send(seat.socket,{type:'economy',account:seat.account,reason:'Награда получена'});}}};
if(m.operations?.mission?.status==='complete'){const q=m.operations.mission;if(/^mission-\d+$/.test(q.key)&&Number.isInteger(q.reward)&&q.reward>=300&&q.reward<=600)reward(q.key,q.reward);}
for(const d of m.operations?.drops||[])if(d.status==='opened'&&/^airdrop-\d+$/.test(d.id)&&Number.isInteger(d.reward)&&d.reward>=100&&d.reward<=500&&ROLES.includes(d.opener)&&!r.rewarded.has(d.id)){r.rewarded.add(d.id);const a=r[d.opener];if(a){a.account.money+=d.reward;a.account.revision++;send(a.socket,{type:'economy',account:a.account,reason:'Награда получена'});}}

if(m.wave?.rest&&Number.isInteger(m.wave.number)){for(const role of ROLES)if(r[role])r[role].account.completedWave=Math.max(r[role].account.completedWave||0,m.wave.number);reward('wave-'+m.wave.number,m.wave.number%5===0?500:75+m.wave.number*25);}
broadcast(r,m,'host');}
else if(r&&m.type==='shot'&&s.role!=='host'&&r[s.role].pose?.hp>0&&Array.isArray(m.direction)&&m.direction.length===3&&m.direction.every(Number.isFinite)){const damage=WEAPON_BY_ID[m.weapon]?.damage,p=r[s.role].pose;if(damage)send(r.host.socket,{type:'shot',origin:[p.x,p.y+1.65,p.z],direction:m.direction,damage});}
else if(r&&m.type==='damage'&&s.role==='host'&&ROLES.slice(1).includes(m.target))send(r[m.target]?.socket,{type:'damage',amount:[10,15,25,30,40,55].includes(m.amount)?m.amount:25,poison:m.amount===10&&m.poison===true,hitId:typeof m.hitId==='string'?m.hitId.slice(0,80):undefined});
else if(r&&m.type==='end'&&s.role==='host'){r.result=m.result;broadcast(r,m,'host');}
else if(r&&m.type==='event-interact'&&s.role!=='host'&&r[s.role].pose?.hp>0)send(r.host.socket,{type:'event-interact',position:{x:r[s.role].pose.x,y:r[s.role].pose.y,z:r[s.role].pose.z}});
else if(r&&m.type==='pickup'){
 const own=r[s.role],cash=r.cash.get(m.id),p=own.pose;
 if(cash&&!cash.taken&&p?.hp>0&&Math.hypot(p.x-cash.x,p.z-cash.z)<1.6&&p.y<1.3){syncCombat(own.account,m.inventory);if(m.inventory?.revision===own.account.revision&&Number.isFinite(m.health))own.account.health=Math.max(0,Math.min(100,m.health));cash.taken=true;for(const role of ROLES){const seat=r[role];if(!seat)continue;seat.account.money+=cash.value;seat.account.revision++;send(seat.socket,{type:'economy',account:seat.account,reason:'Деньги подобраны'});}const all=[...r.cash.values()].filter(c=>!c.taken);if(r.snapshot)r.snapshot.cash=all;broadcast(r,{type:'loot-state',cash:all});}
}
else if(r&&m.type==='market'){
 const own=r[s.role],a=own.account,p=own.pose;if(m.inventory&&m.inventory.revision!==a.revision)return send(s,{type:'economy',account:a,reason:'Состояние обновлено',ok:false});syncCombat(a,m.inventory);
 if(Number.isFinite(m.health)&&p){p.hp=Math.max(0,Math.min(100,m.health));a.health=p.hp;}
 let result={ok:false,reason:'Подойдите к терминалу'};
 if(p?.hp>0){if(m.action==='buy'&&nearTerminal(p))result=buy(a,m.id);else if(m.action==='equip')result={ok:equipWeapon(a,m.id),reason:''};else if(m.action==='use')result={ok:useSupply(a,m.id),reason:''};else if(m.action==='trap'&&p.y<.35&&a.traps>0){r.pendingTraps??=new Map();const active=[...(r.snapshot?.operations?.traps||[]),...r.pendingTraps.values()];if(active.filter(t=>!t.used).length<100&&!active.some(t=>!t.used&&Math.hypot(t.x-p.x,t.z-p.z)<.9)&&consumeTrap(a)){r.trapId=(r.trapId||0)+1;const trap={id:'room-trap-'+r.trapId,x:p.x,z:p.z,used:false};r.pendingTraps.set(trap.id,trap);broadcast(r,{type:'trap-placed',trap});result={ok:true,reason:''};}}}
 if(result.ok)a.revision++;if(p)p.hp=a.health;send(s,{type:'economy',account:a,reason:result.reason,ok:result.ok});
}
else if(r&&m.type==='revive'){const own=r[s.role],peer=m.target&&m.target!==s.role?r[m.target]:ROLES.filter(key=>key!==s.role).map(key=>r[key]).find(seat=>seat?.pose?.hp<=0&&seat.socket);if(own.pose?.hp>0&&peer?.pose?.hp<=0&&Math.hypot(own.pose.x-peer.pose.x,own.pose.z-peer.pose.z)<2.5){peer.pose.hp=50;peer.account.health=50;send(peer.socket,{type:'revived',hp:50});send(own.socket,{type:'revive-success'});}}

}catch{send(s,{type:'error',message:'Invalid packet'});}});s.on('close',()=>remove(s));});return {server,wss,rooms};}
if(process.argv[1]===resolve('server/index.js')){const {server}=createGameServer();server.listen(Number(process.env.PORT)||3000,'0.0.0.0',()=>console.log('DEAD SECTOR server on port '+(process.env.PORT||3000)));}
