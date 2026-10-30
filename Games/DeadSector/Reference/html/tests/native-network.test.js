import test from 'node:test';
import assert from 'node:assert/strict';
import {WebSocket} from 'ws';
import {createGameServer} from '../server/index.js';
test('Godot relay: four seats, authority, codes, countdown, reconnect and HTML isolation', async()=>{
 const game=createGameServer();await new Promise(r=>game.server.listen(0,'127.0.0.1',r));const sockets=[];
 const send=(s,m)=>s.send(JSON.stringify(m));
 async function connect(native=true){const s=new WebSocket('ws://127.0.0.1:'+game.server.address().port+(native?'/godot':''));sockets.push(s);s.messages=[];s.on('message',b=>s.messages.push(JSON.parse(b)));await new Promise(r=>s.once('open',r));return s;}
 async function msg(s,type,timeout=2000){const until=Date.now()+timeout;while(Date.now()<until){const i=s.messages.findIndex(m=>m.type===type);if(i>=0)return s.messages.splice(i,1)[0];await new Promise(r=>setTimeout(r,10));}throw Error('Missing '+type);}
 try{
  const host=await connect();send(host,{type:'create',seed:12345});const room=await msg(host,'room');assert.match(room.code,/^\d{8}$/);assert.equal(room.seed,12345);assert.equal(room.host,true);
  const guests=[];for(let i=0;i<3;i++){const g=await connect();send(g,{type:'join',code:room.code});const seat=await msg(g,'room');assert.equal(seat.host,false);assert.equal(seat.members.length,i+2);guests.push({g,seat});}
  const extra=await connect();send(extra,{type:'join',code:room.code});assert.match((await msg(extra,'error')).message,/full/);
  const html=await connect(false);send(html,{type:'join',code:room.code});assert.equal((await msg(html,'error')).type,'error');
  send(guests[0].g,{type:'start'});send(guests[0].g,{type:'snapshot',enemies:[]});await new Promise(r=>setTimeout(r,40));assert.ok(!host.messages.some(m=>m.type==='start'||m.type==='snapshot'));
  send(host,{type:'countdown'});assert.equal((await msg(guests[0].g,'countdown')).seconds,5);send(host,{type:'start'});await msg(host,'start');await msg(guests[0].g,'start');
  send(guests[0].g,{type:'pose',p:[1,0,2],yaw:0,hp:100,id:'host'});assert.equal((await msg(host,'pose')).id,'guest');
  send(guests[0].g,{type:'shot',direction:[0,0,-1],weapon:'Pistol'});assert.equal((await msg(host,'shot')).id,'guest');
  send(host,{type:'snapshot',enemies:[{key:'1',hp:250}],credits:{guest:50}});assert.equal((await msg(guests[0].g,'snapshot')).credits.guest,50);
  send(host,{type:'damage',target:'guest3',amount:25});assert.equal((await msg(guests[2].g,'damage')).target,'guest3');
  guests[1].g.terminate();await msg(host,'away');const resumed=await connect();send(resumed,{type:'resume',code:room.code,token:guests[1].seat.token});assert.equal((await msg(resumed,'room')).id,'guest2');
  const bad=await connect();send(bad,{type:'resume',code:room.code,token:'wrong'});assert.equal((await msg(bad,'error')).message,'Session expired');
  send(host,{type:'leave'});await msg(guests[0].g,'closed');
 }finally{for(const s of sockets)s.terminate();await new Promise(r=>game.wss.close(r));await new Promise(r=>game.server.close(r));}
});
