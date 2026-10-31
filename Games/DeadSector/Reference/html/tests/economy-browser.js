import {launchLobby} from './lobby-helpers.js';
import {chromium} from 'playwright';import assert from 'node:assert/strict';import {mkdir} from 'node:fs/promises';
import {createGameServer} from '../server/index.js';import {SHOP_ITEMS,totalAmmo,STORE} from '../economy.js';
const game=createGameServer();await new Promise(r=>game.server.listen(0,'127.0.0.1',r));
const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||undefined,headless:true,args:['--no-sandbox','--enable-unsafe-swiftshader','--use-gl=angle','--use-angle=swiftshader']});
try{
 const p=await browser.newPage({viewport:{width:1440,height:900}}),errors=[];p.on('pageerror',e=>errors.push(e.stack));p.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
 await p.goto(process.env.BASE_URL||'http://127.0.0.1:5173/app.html');await p.waitForFunction(()=>!document.getElementById('start').disabled);
 await p.evaluate(async()=>window.readGame=(await import('/game.js')).getSnapshot);
 const read=()=>p.evaluate(()=>readGame());
 async function wait(predicate,description,timeout=45000){const end=Date.now()+timeout;while(Date.now()<end){const s=await read();if(predicate(s))return s;await p.waitForTimeout(50);}throw Error(description+': '+JSON.stringify(await read())+' errors '+JSON.stringify(errors));}
 let s=await read();assert.equal(s.weapon,'Pistol');assert.equal(s.money,0);assert.equal(s.totalAmmo,92);assert.deepEqual(s.supplies,[]);
 await p.locator('#settings-button').click();await p.locator('#server-url').fill('ws://127.0.0.1:'+game.server.address().port);await p.locator('#close-settings').click();await p.locator('#start').click();await p.locator('#host-room').click();s=await wait(s=>s.networkActive,'room');const room=game.rooms.get(s.room);
 const pushAccount=()=>{room.host.account.revision++;room.host.socket.send(JSON.stringify({type:'economy',account:room.host.account}));};
 await launchLobby(p);await wait(s=>s.running,'play');await p.keyboard.press('Digit3');assert.equal((await read()).trapSelected,false);
 room.cash.set('cash-ui',{id:'cash-ui',x:STORE.spawn.x,z:STORE.spawn.z,value:30,taken:false});room.host.socket.send(JSON.stringify({type:'loot-state',cash:[...room.cash.values()]}));await wait(s=>s.drops.length===1,'cash appears');
 await wait(s=>s.money===30,'cash pickup');assert.equal(room.cash.get('cash-ui').taken,true);
 // Isolated room fixture funds purchases; no debug grant exists in the game client.
 room.host.account.completedWave=25;room.host.account.money=90000;pushAccount();await wait(s=>s.money===90000,'fixture funds');
 await p.keyboard.down('KeyW');await wait(s=>s.position.z<=12.3,'walk through shop door');await p.keyboard.up('KeyW');
 await p.keyboard.down('KeyA');await wait(s=>s.position.x<=8.35,'walk to terminal');await p.keyboard.up('KeyA');
 await p.keyboard.press('KeyE');await wait(s=>s.shopOpen,'terminal modal');assert.equal(await p.locator('#menu').isVisible(),false);assert.equal(await p.locator('#shop-panel').isVisible(),true);
 const prices=await p.locator('[data-shop-action=buy]').evaluateAll(buttons=>buttons.map(b=>Number(b.textContent.match(/\$(\d+)/)?.[1])));assert.deepEqual(prices,[...prices].sort((a,b)=>a-b));assert.equal(await p.locator('.weapon-silhouette').count(),9);await p.waitForFunction(()=>!document.pointerLockElement);await p.locator('.weapon-card').first().focus();await p.waitForFunction(()=>Number(getComputedStyle(document.querySelector('.weapon-stats')).opacity)>.95,{},{timeout:15000});
 if(process.env.ARTIFACT_DIR){await mkdir(process.env.ARTIFACT_DIR,{recursive:true});await p.screenshot({path:process.env.ARTIFACT_DIR+'/market-weapons.png'});}
 for(const item of SHOP_ITEMS.filter(i=>i.kind==='weapon')){
  const before=(await read()).money;await p.locator(`[data-shop-action=buy][data-item=${item.id}]`).click();s=await wait(s=>s.weapon===item.id,'buy '+item.id);
  assert.equal(s.money,before-item.price);assert.deepEqual(Object.keys(s.inventory.weapons).sort(),['Pistol',item.id].sort());console.log('PASS buy/equip and replace primary',item.id);
 }
 await p.locator('[data-shop-tab=gear]').click();assert.equal(await p.locator('.supply-silhouette svg').count(),7);
 for(const id of ['flashlight','battery','trap']){await p.locator(`[data-shop-action=buy][data-item=${id}]`).click();await wait(s=>id==='flashlight'?s.inventory.flashlight:id==='battery'?s.inventory.batteries===1:s.inventory.traps===1,'buy '+id);}
 for(const id of ['smallMedkit','largeMedkit']){await p.locator(`[data-shop-action=buy][data-item=${id}]`).click();await wait(s=>s.inventory[id]===1,'buy '+id);}
 room.host.account.health=10;room.host.pose.hp=10;room.host.account.weapons.G36.durability=30;room.host.account.reserve=492;pushAccount();await wait(s=>s.hp===10&&s.durability===30&&s.totalAmmo===500,'condition and ammo fixture');
 await p.locator('[data-shop-action=buy][data-item=smallMedkit]').click();await wait(s=>s.hp===35,'purchase immediately heals');await p.locator('[data-shop-action=buy][data-item=largeMedkit]').click();await wait(s=>s.hp===100,'large purchase heals to cap');room.host.account.health=10;room.host.pose.hp=10;pushAccount();await wait(s=>s.hp===10,'stored kit fixture');
 assert.equal(await p.locator('[data-shop-action=buy][data-item=ammo]').isDisabled(),true);
 await p.locator('[data-shop-action=buy][data-item=repair]').click();await wait(s=>s.durability===80,'repair +50');
 await p.locator('[data-shop-action=buy][data-item=repair]').click();await wait(s=>s.durability===100,'repair capped');assert.equal(await p.locator('[data-shop-action=buy][data-item=repair]').isDisabled(),true);
 const artifacts=process.env.ARTIFACT_DIR;if(artifacts){await mkdir(artifacts,{recursive:true});await p.screenshot({path:artifacts+'/market-terminal.png'});}
 await p.locator('#close-shop').click();await wait(s=>s.running&&!s.shopOpen,'return to play');
 assert.equal(await p.locator('#flashlight-battery').getAttribute('aria-valuemax'),'120');assert.equal(await p.locator('#flashlight-battery').isVisible(),true);await p.keyboard.press('KeyF');await wait(s=>s.flashlightOn&&s.inventory.flashlightCharge<120,'flashlight drains');await p.keyboard.press('KeyF');await p.keyboard.press('KeyB');await wait(s=>s.inventory.batteries===0&&s.inventory.flashlightCharge===120,'battery refill');await p.keyboard.press('Digit3');await wait(s=>s.trapSelected,'select trap');await wait(s=>s.selectionReady,'selection animation');const ammoBefore=(await read()).ammo;await p.mouse.click(600,400);await wait(s=>s.inventory.traps===0&&s.operations.traps.some(t=>!t.used),'trap underfoot');assert.equal((await read()).ammo,ammoBefore);await wait(s=>!s.trapSelected,'empty trap returns to gun');await p.keyboard.press('Digit3');assert.equal((await read()).trapSelected,false);
 await p.keyboard.press('Digit2');await wait(s=>s.weapon==='Pistol','slot 2 pistol');await p.keyboard.press('Digit1');await wait(s=>s.weapon==='G36','slot 1 primary');
 await wait(s=>s.selectionReady,'selection animation');await p.keyboard.press('KeyR');await wait(s=>s.reloading,'reload starts');await wait(s=>!s.reloading&&s.ammo===35,'reload from universal pool',120000);
 await p.mouse.down();await wait(s=>s.ammo<=28,'automatic primary fires');await p.mouse.up();
 await p.keyboard.press('KeyH');await wait(s=>s.hp===35,'small medkit');await p.keyboard.press('KeyJ');await wait(s=>s.hp===100,'large medkit');
 assert.equal((await read()).inventory.smallMedkit,0);assert.equal((await read()).inventory.largeMedkit,0);
 for(const [width,height]of [[1440,900],[900,600],[640,480]]){
  await p.setViewportSize({width,height});await p.waitForTimeout(100);
  const boxes=await p.evaluate(()=>['.hud-bottom','.ammo-panel','.wave-panel'].map(q=>{const r=document.querySelector(q).getBoundingClientRect();return {left:r.left,right:r.right,top:r.top,bottom:r.bottom};}));
  for(const b of boxes){assert.ok(b.left>=0&&b.right<=width+.5&&b.top>=0&&b.bottom<=height+.5);}
  assert.ok(boxes[0].right<boxes[1].left);assert.equal(await p.locator('.hud-bottom p,.briefing .key-row').count(),0);
 }
 if(artifacts)await p.screenshot({path:artifacts+'/economy-hud.png'});
 assert.deepEqual(errors,[]);console.log('PASS physical shop access, cash pickup, all nine prices, two slots, universal ammo, repair, medkits, HUD breakpoints');
}finally{await browser.close();for(const r of game.rooms.values()){r.host.socket?.terminate();r.guest?.socket?.terminate();}await new Promise(r=>game.wss.close(r));await new Promise(r=>game.server.close(r));}
