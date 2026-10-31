import {connectionSettings} from './connection-settings.js';
import {PoisonEffect} from './poison.js';
import {blackoutDistrict} from './power-grid.js';
import * as THREE from 'three';
import {t} from './i18n.js';
import {PerformanceMetrics} from './metrics.js';
import {applyEnvironmentLight} from './environment-light.js';
import {CitySky} from './city-sky.js';
import {FirstPersonBody} from './first-person-body.js';
import {BodyVisibility,canSelectTrap} from './body-visibility.js';
import { Player, PLAYER, FIXED_STEP } from './player.js';
import { createMap } from './map.js';
import { createPSX } from './psx.js';
import { Rifle,WEAPONS } from './weapons.js';
import { Enemies } from './enemies.js';
import { GameAudio } from './audio.js';
import {settingsUI} from './settings.js';
import {LookInput,requestLookLock} from './look-input.js';
import {updateCompass} from './compass.js';
import {Flashlight} from './flashlight.js';
import {Operations,makeTrapModel,AIRDROP_INTERVAL} from './operations.js';
import {PeerAvatar} from './peer-avatar.js';
import {LobbyUI} from './lobby-ui.js';
import {Session} from './network.js';
import {MoneyDrops} from './loot.js';
import {createInventory,totalAmmo,buy,equipWeapon,useMedkit,useSupply,consumeTrap,nearTerminal,STORE,FLASHLIGHT_DURATION} from './economy.js';
import {ShopUI} from './shop-ui.js';
import {EncounterEvents} from './encounter-events.js';
import {obstacleDistance} from './enemies.js';
import {WeatherEffects} from './weather.js';
import {Waves} from './waves.js';

const $ = id => document.getElementById(id);
for(const id of ['settings','network-panel'])document.body.append($(id));
const keys = new Set();const lookInput=new LookInput();
const movementKeys = new Set(['KeyW', 'KeyA', 'KeyS', 'KeyD', 'ShiftLeft', 'ShiftRight', 'Space']);
const announcedMissionCompletions=new Set();
let lastMissionKey='',lastMissionCompleteKey='',missionNoticeUntil=0,missionNoticeRecord=null;
let queuedEquip=null;
let selfAvatar,selfBody,selfLegs,flashlight,operations,trapSelected=false,trapPreview;let lastCompletedWave=0,waveNoticeMessage='';
let renderer, scene, camera, player,lastMenuFrame=-Infinity;
let needsCapture=false,lobbyOpen=false,matchReady=false,lobbyPhase='lobby',countdownAt=0,members=[],localTimer;const peers=new Map();
let weather,events,cashCooldown=0,lastAnnouncedWave=0,waveNoticeUntil=0;
let psx, rifle, enemies, colliders=[], world, waves, peer, peerPose=null, netTime=0,loot,account=createInventory(),shopOpen=false,footstep=0,downed=false;
let seed=crypto.getRandomValues(new Uint32Array(1))[0];
const session=new Session(receiveNetwork,text=>{$('network-status').textContent=text;$('lobby-status').textContent=text;});
const lobby=new LobbyUI({start:startLobby,open:()=>connectRoom('create'),leave:leaveLobby});
const audio = new GameAudio();
document.addEventListener('pointerdown',e=>{if(e.target.closest('button,input,select'))audio.unlock();});
document.addEventListener('click',e=>{const b=e.target.closest('button');if(b&&!b.disabled)audio.ui();});
document.addEventListener('pointerover',e=>{const b=e.target.closest('button');if(b&&!b.disabled&&!b.contains(e.relatedTarget))audio.ui('hover');});
document.addEventListener('change',e=>{if(e.target.matches('select,input[type=range]'))audio.ui();});
$('close-network').addEventListener('click',()=>{$('network-panel').hidden=true;});
$('close-settings').addEventListener('click',()=>{$('settings').hidden=true;$('settings-button').setAttribute('aria-expanded','false');});
const metrics=new PerformanceMetrics($('performance-metrics'));const preferences=settingsUI(applyPreferences);audio.setVolumes(preferences);
const shop=new ShopUI(submitAction,closeShop);
const poison=new PoisonEffect();
let hp=100, outcome='', shooting=false, triggerPressed=false, hitTime=0, hurtTime=0, elapsed=0;
let running = false, started = false, jumpQueued = false, accumulator = 0, previousTime = 0;
let sensitivity = 1;
try {
  const saved = Number(localStorage.getItem('dead-sector:sensitivity'));
  if (saved >= .2 && saved <= 2) sensitivity = saved;
} catch { /* Storage may be unavailable in private/restricted browsers. */ }
$('sensitivity').value = sensitivity;
$('sensitivity-value').value = sensitivity.toFixed(1);
$('sensitivity').addEventListener('input', event => {
  sensitivity = Number(event.target.value); $('sensitivity-value').value = sensitivity.toFixed(1);
  try { localStorage.setItem('dead-sector:sensitivity', sensitivity); } catch { /* Optional preference. */ }
});
$('settings-button').addEventListener('click', () => {
  $('network-panel').hidden=true;
  $('settings').hidden = !$('settings').hidden;
  $('settings-button').setAttribute('aria-expanded', String(!$('settings').hidden));
});

function clearInput() {if(player){player.velocityX=0;player.velocityZ=0;player.speed=0;} lookInput.reset(performance.now());keys.clear(); shooting=false; triggerPressed=false; jumpQueued = false; accumulator = 0; }
function setRunning(value) {
  value=!!value&&matchReady;if(value){needsCapture=false;$('settings').hidden=true;$('settings-button').setAttribute('aria-expanded','false');}$('capture-mouse').hidden=!needsCapture;running = value; clearInput();
  document.body.classList.toggle('playing', value||shopOpen||needsCapture);
  for (const id of ['menu', 'briefing', 'footer']) $(id).hidden = value||shopOpen||lobbyOpen||needsCapture;
  $('hud').hidden = !value&&!shopOpen&&!needsCapture;
  $('restart').hidden = !started;
  $('start').innerHTML = outcome ? t('НОВЫЙ ЗАБЕГ <span>↗</span>') : started ? t('ПРОДОЛЖИТЬ <span>↗</span>') : t('ИГРАТЬ <span>↗</span>');
}

function rebuild(seedValue){poison.reset();
  if(shopOpen){shopOpen=false;shop.hide();}
  if(scene){scene.traverse(o=>{if(o.isInstancedMesh)o.dispose();o.geometry?.dispose();for(const m of (Array.isArray(o.material)?o.material:[o.material]))if(m){m.map?.dispose();m.dispose();}});}
  scene=new THREE.Scene();world=createMap(scene,seedValue);applyEnvironmentLight(scene,renderer);seed=world.seed;colliders=world.colliders;
  selfAvatar=new PeerAvatar();selfAvatar.head.visible=false;selfAvatar.upper.visible=false;selfAvatar.root.visible=false;selfLegs=new FirstPersonBody(selfAvatar);selfBody=new BodyVisibility(selfAvatar.root);scene.add(selfAvatar.root);flashlight=new Flashlight(scene);operations=new Operations(scene,world,seed);trapSelected=false;queuedEquip=null;lastMissionKey='';lastMissionCompleteKey='';announcedMissionCompletions.clear();missionNoticeUntil=0;missionNoticeRecord=null;player=new Player(world.spawn);loot=new MoneyDrops(scene);enemies=new Enemies(scene,colliders,audio);enemies.base={...world.spawn};scene.userData.citySky=new CitySky(scene);enemies.listener=player.position;enemies.onDeath=e=>{if(!session.active||session.host)loot.spawn(e);};waves=new Waves(seed);lastAnnouncedWave=0;lastCompletedWave=0;waveNoticeMessage='';waveNoticeUntil=0;events=new EncounterEvents(scene,seed);weather=new WeatherEffects(scene,seed,world.buildings);
  peer=new THREE.Group();const material=new THREE.MeshStandardMaterial({color:'#7d9676'});const body=new THREE.Mesh(new THREE.CapsuleGeometry(.28,.8,3,6),material);body.position.y=1;peer.add(body);const head=new THREE.Mesh(new THREE.IcosahedronGeometry(.23,1),material);head.position.y=1.75;peer.add(head);peer.visible=false;scene.add(peer);peerPose=null;peers.clear();if(renderer)applyPreferences(preferences);
}
function resetCombat(){needsCapture=false;clearTimeout(localTimer);matchReady=false;lobbyOpen=false;$('lobby').hidden=true;session.close();rebuild(crypto.getRandomValues(new Uint32Array(1))[0]);account=createInventory();rifle.bindInventory(account);hp=100;outcome='';hitTime=0;hurtTime=0;elapsed=0;footstep=0;downed=false;updateCamera();}
function receiveDamage(damage,effect){if(hp<=0)return;hp=Math.max(0,hp-damage);account.health=hp;if(effect?.poison&&hp>0)poison.apply(effect.id);hurtTime=.3;audio.play('hurt');if(!hp){if(session.active){downed=true;pause();$('message').textContent=t('Ожидайте помощи напарника · E рядом с раненым');}else finish('dead');}}
function finish(result){if(outcome)return;outcome=result;if(session.active&&session.host)session.send({type:'end',result});pause();$('message').textContent=result==='win'?t('СЕКТОР ЗАЧИЩЕН. Вы добрались до выхода.'):t('ВЫ ПОГИБЛИ. Начните зачистку заново.');}
async function enter() {
  if(shopOpen)return;
  if(session.reconnecting||session.hostAway){$('message').textContent=t('Переподключение. Дождитесь хозяина комнаты.');return;}
  if(session.active&&hp<=0){$('message').textContent=t('Ожидайте помощи напарника · E рядом с раненым');return;}
  if(outcome)resetCombat();
  audio.unlock();
  try {
    await requestLookLock($('scene'));
  } catch {
    $('message').textContent = t('Браузер не разрешил захват мыши. Нажмите «Войти в сектор» ещё раз или откройте игру в отдельной вкладке.');
  }
}
$('start').addEventListener('click',()=>{if(!started&&!matchReady){if(outcome)resetCombat();showLobby();}else enter();});
$('capture-mouse').addEventListener('click',enter);
$('restart').addEventListener('click', () => { resetCombat();showLobby(); });
document.addEventListener('pointerlockchange', () => {
  const locked = document.pointerLockElement === $('scene');
  if (locked&&matchReady) { started = true; $('message').textContent = t('Пауза · движение остановлено. Продолжить — захватить мышь.'); }
  setRunning(locked&&matchReady);
  if(outcome) $('message').textContent=outcome==='win'?t('СЕКТОР ЗАЧИЩЕН. Вы добрались до выхода.'):t('ВЫ ПОГИБЛИ. Начните зачистку заново.');
  if (!locked) $('start').focus({ preventScroll: true });
});
document.addEventListener('pointerlockerror', () => {
  $('message').textContent = t('Не удалось захватить мышь. Повторите вход в сектор в отдельной вкладке браузера.');
});
function pause() { if (document.pointerLockElement) document.exitPointerLock(); setRunning(false); }
window.addEventListener('blur', pause);
document.addEventListener('visibilitychange', () => { if (document.hidden) pause(); });
document.addEventListener('keydown', event => {
  if(lobbyOpen){if(event.code==='Escape'&&session.active&&!session.host)session.send({type:'ready',ready:false});return;}
  if(shopOpen){if(event.code==='Escape'||event.code==='KeyE'){event.preventDefault();closeShop();}return;}
  if(running&&event.code==='Digit3'){event.preventDefault();if(!canSelectTrap(account))return;trapSelected=true;rifle.setUtility(true);shooting=false;return;}
  if(running&&event.code==='KeyF'&&!event.repeat){flashlight.toggle(account);audio.ui();return;}
  if(running&&event.code==='KeyB'&&!event.repeat){submitAction('use','battery');return;}
  if(running&&(event.code==='Digit1'||event.code==='Digit2')){trapSelected=false;rifle.setUtility(false);const id=event.code==='Digit2'?'Pistol':Object.keys(account.weapons).find(id=>id!=='Pistol');if(id)submitAction('equip',id);event.preventDefault();return;}
  if(running&&(event.code==='KeyH'||event.code==='KeyJ')){submitAction('use',event.code==='KeyH'?'smallMedkit':'largeMedkit');return;}
  if (event.code === 'Escape' && running) { pause(); return; }
  if(running && event.code==='KeyE'){keys.add('KeyE');if(operations.nearby(player.position))return;
    const down=nearDowned();if(session.active&&down){session.send({type:'revive',target:down.id});return;}
    $('event-objective').textContent='';
    if(events.interact(player.position))return;
    if(nearTerminal(player.position)){openShop();return;}

  }
  if(running && event.code==='KeyR'&&!trapSelected){rifle.reload();event.preventDefault();return;}
  if (!running || !movementKeys.has(event.code)) return;
  event.preventDefault();
  if (event.code === 'Space' && !keys.has('Space') && !event.repeat) jumpQueued = true;
  keys.add(event.code);
});
document.addEventListener('keyup', event => { keys.delete(event.code); });
document.addEventListener('mousemove', event => { if (running&&document.pointerLockElement===$('scene'))lookInput.add(event.movementX,event.movementY,event.timeStamp,performance.now()); });

document.addEventListener('mousedown', event=>{if(!running)return;if(event.button===0){if(trapSelected){placeTrap();return;}shooting=true;triggerPressed=true;}});
document.addEventListener('mouseup', event=>{if(event.button===0)shooting=false;});
$('scene').addEventListener('contextmenu',event=>event.preventDefault());

function updateCamera() {
  camera.position.set(player.position.x, player.position.y + PLAYER.eye, player.position.z);
  camera.rotation.set(player.pitch, player.yaw, 0, 'YXZ');
}
function resize() {
  camera.aspect = innerWidth / innerHeight; camera.updateProjectionMatrix();
  rifle.camera.aspect=camera.aspect;rifle.camera.updateProjectionMatrix();
  psx.resize(innerWidth, innerHeight);
}

// Read-only snapshot for reproducible browser checks, without allowing state mutation.
export function getSnapshot() {
  return { running, started, position: player ? { ...player.position } : null, yaw: player?.yaw, pitch: player?.pitch, grounded: player?.grounded,fov:camera?.fov,weather:weather?.state,wave:waves?.snapshot(),event:events?.snapshot(), hp, outcome, ammo:rifle?.ammo, reserve:rifle?.reserve, reloading:rifle?.reloadTime>0, weapon:rifle?.config.id, capacity:rifle?.config.capacity, seed,room:session.code,networkActive:session.active,peerVisible:[...peers.values()].some(p=>p.mesh.visible),peerCount:peers.size,bodyVisible:selfAvatar?.root.visible,bodyOpacity:selfBody?.opacity,peerWeapons:[...peers.values()].map(p=>p.avatar.id),lobbyOpen,lobbyPhase,matchReady,members,downed,shopOpen,poisonTicks:poison.remaining,inventory:account,operations:operations?.snapshot(),flashlightOn:flashlight?.on,trapSelected,selectionReady:!rifle?.switchTime&&!!rifle?.displayUtility===trapSelected&&rifle?.displayVariant===rifle?.variant,money:account.money,totalAmmo:totalAmmo(account),durability:rifle?.durability,stamina:player?.stamina,terminal:world?.shop.terminal,streetDensity:world?.streetDensity,drops:loot?.snapshot(),supplies:[],remaining:enemies?.alive, enemies:enemies?.items.map(e=>({id:e.id,type:e.type,hp:e.hp,fast:e.type==='runner',x:e.rig.position.x,y:e.rig.position.y,z:e.rig.position.z})) };
}

try {
  renderer = new THREE.WebGLRenderer({ canvas: $('scene'), antialias: false, powerPreference: 'high-performance' });
  renderer.shadowMap.enabled = true; renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping; renderer.toneMappingExposure = .85;
  scene = new THREE.Scene(); camera = new THREE.PerspectiveCamera(73, 1, .1, 500);
  rebuild(seed);rifle=new Rifle(audio);rifle.bindInventory(account);
  trapPreview=makeTrapModel();trapPreview.position.set(.06,-.20,-.62);trapPreview.rotation.x=.7;trapPreview.scale.setScalar(.55);trapPreview.visible=false;for(const [i,hand]of rifle.hands.entries()){const h=hand.clone();h.position.set(i? .35:-.35,.02,0);h.scale.setScalar(1.5);trapPreview.add(h);const arm=rifle.forearms[i].clone(),start=h.position.clone().add(new THREE.Vector3(0,0,.10)),end=new THREE.Vector3(i?.65:-.65,-.5,.9),v=end.clone().sub(start);arm.position.copy(start).add(end).multiplyScalar(.5);arm.quaternion.setFromUnitVectors(new THREE.Vector3(0,1,0),v.clone().normalize());arm.scale.set(1.5,v.length(),1.5);trapPreview.add(arm);}rifle.scene.add(trapPreview);rifle.utilityModel=trapPreview;
  psx = createPSX(scene, renderer);applyPreferences(preferences);
  updateCamera(); resize(); window.addEventListener('resize', resize);
  $('scene').addEventListener('webglcontextlost', event => {
    event.preventDefault(); pause(); $('start').disabled = true;
    $('message').textContent = t('Графический контекст потерян. Перезагрузите страницу, чтобы восстановить сектор.');
  });
  $('start').disabled = false; setRunning(false);
  function frame(time) {
    if(!matchReady&&time-lastMenuFrame<180)return;if(!matchReady)lastMenuFrame=time;
    const delta = Math.min(Math.max((time - previousTime) / 1000, 0), .1); previousTime = time;
    if(running){const look=lookInput.consume(delta,sensitivity);player.look(look.x,look.y,sensitivity);}
    if (matchReady&&(running || (session.active && started && !outcome))) {
      accumulator += delta;
      while (accumulator >= FIXED_STEP) {
        player.step(FIXED_STEP, {
          forward: keys.has('KeyW'), backward: keys.has('KeyS'), left: keys.has('KeyA'), right: keys.has('KeyD'),
          sprint: keys.has('ShiftLeft') || keys.has('ShiftRight'), jump: jumpQueued,
        }, colliders);
        jumpQueued = false; accumulator -= FIXED_STEP;
        cashCooldown=Math.max(0,cashCooldown-FIXED_STEP);const cash=loot.nearby(player.position);if(hp>0&&cash&&Math.hypot(cash.x-player.position.x,cash.z-player.position.z)<.85&&cashCooldown<=0){cashCooldown=.3;if(session.active)session.send({type:'pickup',id:cash.id,health:hp,inventory:account});else if(loot.collect(cash.id,account))audio.play('cash');}
        if(hp>0)poison.step(FIXED_STEP,receiveDamage);else poison.reset();elapsed+=FIXED_STEP;rifle.step(FIXED_STEP);flashlight.drain(FIXED_STEP,account);
        updateCamera();camera.updateMatrixWorld();
        if(hp>0&&!trapSelected&&((shooting && rifle.config.automatic)||triggerPressed) && rifle.fire()){
          const direction=camera.getWorldDirection(new THREE.Vector3());enemies.hear(camera.position,90);
          if(session.active&&!session.host)session.send({type:'shot',direction:direction.toArray(),weapon:rifle.config.id,visualWeapon:rifle.displayUtility?'trap':rifle.displayVariant?.record.id||rifle.config.id,weaponDrop:rifle.switchDrop||0,inventory:account});else if(combatShot(camera.position,direction,rifle.config.damage))hitTime=.15;
          player.pitch=Math.min(Math.PI/2-.02,player.pitch+.008);
        }
        triggerPressed=false;
        if(!session.active||session.host){
          waves.step(FIXED_STEP,(type,index,options)=>{const h=world.hives[index];enemies.spawn(type,h.x-Math.sign(h.x)*1.5,h.z-Math.sign(h.z)*1.5,undefined,undefined,{...options,wave:waves.number});},()=>enemies.alive);

          operations.step(FIXED_STEP,waves,[{id:session.active?session.role:'solo',hp,position:player.position,interacting:running&&keys.has('KeyE')},...[...peers].filter(([,v])=>v.pose).map(([id,v])=>({id,hp:v.pose.hp,position:v.pose,interacting:!!v.pose.interacting}))],enemies);events.bind(operations.mission);rewardOperations();
          player.hp=hp;const targets=[...peers].filter(([,v])=>v.pose?.hp>0).map(([id,v])=>({p:{hp:v.pose.hp,position:v.pose},hurt:(amount,effect)=>session.send({type:'damage',target:id,amount,poison:!!effect?.poison,hitId:effect?.id})}));if(hp>0||targets.length)enemies.step(FIXED_STEP,player,receiveDamage,targets);else if(session.active)finish('dead');
        }
        hitTime=Math.max(0,hitTime-FIXED_STEP);hurtTime=Math.max(0,hurtTime-FIXED_STEP);
        if(!running&&!session.active)break;
      }
      $('movement').textContent = !player.grounded ? t('В ВОЗДУХЕ') : player.sprinting ? t('БЕГ') : t('ПЕШКОМ');
      if(running&&player.grounded&&player.distance-footstep>1.7){audio.play('step');footstep=player.distance;}
      updateCamera();
    } else if (!started) {
      camera.position.set(36, 28, 95); camera.lookAt(0, 2, 40);
    }
    if(started&&!waves.warmup&&!waves.rest&&lastAnnouncedWave!==waves.number){lastAnnouncedWave=waves.number;waveNoticeUntil=performance.now()+5000;waveNoticeMessage=`${t('ВОЛНА')} ${waves.number} · ${t('НАЧИНАЕТСЯ')}`;audio.play('wave');}
    if(started&&waves.rest&&lastCompletedWave!==waves.number){lastCompletedWave=waves.number;waveNoticeUntil=performance.now()+5000;waveNoticeMessage=`${t('ВОЛНА')} ${waves.number} · ${t('ЗАВЕРШЕНА')}`;audio.play('wave-clear');}
    $('wave-notice').hidden=!started||performance.now()>waveNoticeUntil;$('wave-notice').textContent=`${t('ВОЛНА')} ${waves.number} · ${t(waves.rest?'ЗАВЕРШЕНА':'НАЧИНАЕТСЯ')}`;audio.music(started&&!outcome&&(running||shopOpen||session.active)?(waves.warmup||waves.rest)?'calm':'tense':'calm',waves.number);
    updateCompass($('compass'),player.position,player.yaw,[{...STORE,label:t('МАГАЗИН'),shop:true},...[...peers].filter(([,v])=>v.pose).map(([id,v])=>({...v.pose,label:`${t('ИГРОК')} ${['host','guest','guest2','guest3'].indexOf(id)+1}`})),...(activeMission()?[{...activeMission(),quest:true,label:t('MISSION')}]:[]),...operations.drops.filter(d=>d.status!=='opened').slice(0,1).map(d=>({...d,drop:true,label:t('SUPPLY')})),...enemies.items.filter(e=>e.hp>0&&(waves.clearing||Math.hypot(e.rig.position.x-player.position.x,e.rig.position.z-player.position.z)<=25)).map(e=>({...e.rig.position,enemy:true,label:t('Enemy')}))],t);
    audio.terminalBeacon(player.position,STORE.terminal,player.yaw,started&&(running||shopOpen||session.active));
    $('health').textContent=hp;$('health-bar').style.width=`${hp}%`;
    $('stamina-bar').style.width=`${player.stamina/5*100}%`;$('stamina').textContent=`${Math.ceil(player.stamina/5*100)}%`;
    $('squad-status').textContent=[...peers].filter(([,v])=>v.pose).map(([id,v])=>`${t('ИГРОК')} ${['host','guest','guest2','guest3'].indexOf(id)+1} · ${v.pose.hp} HP`).join('  |  ');
    const primary=Object.keys(account.weapons).find(id=>id!=='Pistol');$('slot-primary').textContent=`1 · ${primary||t('ПУСТО')}`;$('slot-pistol').textContent=`2 · ${t('ПИСТОЛЕТ')}`;$('slot-primary').classList.toggle('active',account.equipped!=='Pistol');$('slot-pistol').classList.toggle('active',account.equipped==='Pistol');
    $('money').textContent=`$${account.money}`;$('durability').textContent=`${rifle.durability}%`;$('ammo-total').textContent=`${totalAmmo(account)}/500`;$('medkit-counts').textContent=`H · ${account.smallMedkit} / J · ${account.largeMedkit}`;
    $('ammo').textContent=rifle.ammo;$('reserve').textContent=rifle.reserve;
    $('weapon-state').textContent=rifle.switchTime>0?t('SWITCHING…'):rifle.durability<=0?t('ОРУЖИЕ СЛОМАНО'):rifle.reloadTime?t('ПЕРЕЗАРЯДКА…'):!rifle.ammo?t('R — ПЕРЕЗАРЯДИТЬ'):t(rifle.config.label);
    if(!session.active&&waves.rest&&account.lastRewardWave!==waves.number){account.lastRewardWave=waves.number;account.completedWave=Math.max(account.completedWave||0,waves.number);account.money+=waves.number%5===0?500:75+waves.number*25;}
    $('objective').textContent=waves.warmup?`${t('ПОДГОТОВКА')} · ${Math.ceil(60-waves.time)}s`:`${t('ВОЛНА')} ${waves.number} · ${waves.clearing?t('ДОБЕЙТЕ ОСТАВШИХСЯ'):waves.rest?t('ПЕРЕРЫВ'):t('НАСТУПЛЕНИЕ')}${waves.clearing?'':` ${Math.ceil((waves.rest?30:120)-waves.time)}s`}`;
    const boss=enemies.items.find(e=>e.boss&&e.hp>0);
    $('wave-details').textContent=`${t(waves.variant==='boss'?'МИНИ-БОСС':waves.variant==='rush'?'БЫСТРОЕ НАСТУПЛЕНИЕ':waves.variant==='siege'?'ОСАДА':waves.variant==='blackout'?'ОТКЛЮЧЕНИЕ СВЕТА':'СТАНДАРТ')} · ${t('ЖИВЫХ')}: ${enemies.alive} · ${t('ВЫШЛО')}: ${waves.spawned}/${waves.total}${boss?` · BOSS ${boss.hp}/${boss.maxHp} HP · ${t('PHASE')} ${boss.bossPhase||1}`:''}`;
    $('hit-marker').hidden=hitTime<=0;document.body.classList.toggle('hurt',hurtTime>0&&running);$('low-health-vignette').hidden=!started||hp<=0||hp>=25||!!outcome||(!running&&!shopOpen&&!needsCapture);$('poison-status').textContent=poison.active?`${t('Отравление')} · ${poison.remaining}s`:'';
    const event=events.state;$('event-objective').textContent=event?.status==='active'?`${t(event.label)} · $${event.reward} · ${Math.round(Math.hypot(player.position.x-event.x,player.position.z-event.z))}m${event.id==='defend'?` · ${Math.floor(event.progress)}/25s`:event.id==='growth'?` · ${event.hp} HP`:t(' · E рядом')}`:event?.status==='complete'&&event.wave===waves.number?t('Событие выполнено'):'';
    weather.maxDrops={low:160,medium:350,high:650}[preferences.quality];weather.step(delta,waves,player.position,audio,matchReady&&(running||session.active));$('weather-status').textContent=`${String(Math.floor(weather.cycle?.hour??6)).padStart(2,'0')}:${String(Math.floor(((weather.cycle?.hour??6)%1)*60)).padStart(2,'0')} · ${t(weather.cycle?.night?'Ночь':'День')} · ${t({overcast:'Пасмурно',rain:'Дождь',storm:'Гроза',clear:'Ясно'}[weather.state.weather])} · ${t({light:'Слабый туман',medium:'Средний туман',heavy:'Сильный туман'}[weather.state.fog])}`;
    scene.userData.powerCut.value=blackoutDistrict(waves);scene.userData.atmosphere?.update(elapsed,events.powered,preferences.quality,player.position,delta,weather.state.weather,weather.cycle?.night);audio.weatherAmbience(matchReady&&(running||shopOpen||session.active)?weather.state.weather:'paused');
    events.step(delta);flashlight.update(camera,account,preferences.quality);operations.visualStep(player.position,audio,running||(session.active&&started&&!outcome));updateSurvivalUI();updateMissionNotice();
    const near=loot.nearby(player.position);
    $('interaction').textContent=hp<=0?t('Ожидайте помощи напарника · E рядом с раненым'):session.active&&nearDowned()?t('E · Поднять напарника'):near?`$${near.value} · ${t('Автоподбор')}`:nearTerminal(player.position)?t('E · Открыть магазин'):'';
    enemies.listener=player.position;enemies.updateEffects((running||(session.active&&started&&!outcome))?delta:0,session.active&&!session.host);
    if(trapSelected&&!canSelectTrap(account))trapSelected=false;rifle.setUtility(trapSelected);rifle.pose(elapsed,player.grounded&&player.speed>.1);if(trapSelected){$('weapon-state').textContent=t(rifle.switchTime>0?'SWITCHING…':'Bear trap');$('ammo').textContent=account.traps||0;$('reserve').textContent='—';}
    const nearbyOperation=operations.nearby(player.position);if(nearbyOperation)$('interaction').textContent=nearbyOperation.type==='drop'?`${t('Hold E · Open supply crate')} · ${nearbyOperation.record.progress.toFixed(1)}/10s`:t('E · Collect mission package');
    account.health=hp;
    if(lobbyOpen&&countdownAt)$('lobby-countdown').textContent=`${t('СТАРТ ЧЕРЕЗ')} ${Math.max(0,Math.ceil((countdownAt-Date.now())/1000))}`;
    if(session.active&&matchReady){netTime+=delta;if(netTime>.1){netTime=0;session.send({type:'pose',...player.position,yaw:player.yaw,pitch:player.pitch,flashlightOn:flashlight.on,interacting:running&&keys.has('KeyE'),hp,ammo:rifle.ammo,reserve:rifle.reserve,weapon:rifle.config.id,visualWeapon:rifle.displayUtility?'trap':rifle.displayVariant?.record.id||rifle.config.id,weaponDrop:rifle.switchDrop||0,inventory:account});if(session.host)session.send({type:'state',wave:waves.snapshot(),event:events.snapshot(),operations:operations.snapshot(),cash:loot.snapshot(),enemies:enemies.snapshot(),projectiles:enemies.projectiles.map(p=>({position:p.mesh.position.toArray(),kind:p.kind||'eye',id:p.id}))});}}
    for(const p of peers.values())p.avatar.animate(delta);
    selfLegs.update(player,delta);selfBody.update(player.pitch,delta,started&&!outcome);
    scene.userData.citySky.update(player.position,elapsed,weather.cycle?.night,preferences.quality);session.measurePing(time);metrics.update(time,preferences.metrics,session.pingMs,session.active);
    renderer.setRenderTarget(psx.target);renderer.autoClear=true;renderer.render(scene, camera);
    if(started&&!outcome){renderer.autoClear=false;renderer.clearDepth();renderer.render(rifle.scene,rifle.camera);}
    psx.present(elapsed,preferences.quality);
  }
  renderer.setAnimationLoop(frame);
} catch (error) {
  console.error('DEAD SECTOR initialization failed:', error);
  $('start').disabled = true; $('start').textContent = t('ЗАПУСК НЕДОСТУПЕН');
  $('message').textContent = t('Не удалось запустить 3D-сцену. Нужен браузер с WebGL 2 и включённым аппаратным ускорением.');
}

function applyState(m){events.restore(m.event);operations.restore(m.operations);events.bind(operations.mission);if(m.wave)waves.restore(m.wave);loot.apply(m.cash||[]);enemies.apply(m.enemies);
 if(!session.host){for(const p of enemies.projectiles)scene.remove(p.mesh);enemies.projectiles=(m.projectiles||[]).map(v=>{const position=v.position||v;if(v.kind==='vomit'&&!enemies.seenVomitShots.has(v.id)){enemies.seenVomitShots.add(v.id);const distance=Math.hypot(position[0]-player.position.x,position[2]-player.position.z);if(distance<20)audio.play('vomit',1-distance/20);}return {kind:v.kind||'eye',id:v.id,mesh:enemies.mesh(scene,'sphere',[v.kind==='vomit'?.14:.2,1],v.kind==='vomit'?'vomit':'bullet',...position),life:6,velocity:new THREE.Vector3()};});if(enemies.seenVomitShots.size>256)enemies.seenVomitShots.clear();}}

function receiveNetwork(m){
 if(m.type==='transport-lost'||(m.type==='away'&&m.role==='host')){pause();$('message').textContent=t('Переподключение. Дождитесь хозяина комнаты.');}

 if(m.type==='room'){
   if(!m.resume){account=createInventory();hp=100;outcome='';started=false;}
   if(!m.resume||seed!==m.seed)rebuild(m.seed);
   if(!m.resume){player.position.x=world.spawn.x+(m.slot%2)*.85;player.position.z=world.spawn.z+Math.floor(m.slot/2)*.85;}
   if(m.selfPose){Object.assign(player.position,{x:m.selfPose.x,y:m.selfPose.y,z:m.selfPose.z});hp=m.selfPose.hp;player.yaw=m.selfPose.yaw;rifle.reset(m.selfPose.weapon);rifle.ammo=m.selfPose.ammo??rifle.ammo;rifle.reserve=m.selfPose.reserve??rifle.reserve;}
   if(m.account){account=m.account;hp=account.health;}rifle.bindInventory(account);if(m.snapshot)applyState(m.snapshot);setRunning(false);downed=hp<=0;if(m.result)finish(m.result);members=m.members||[];lobbyPhase=m.phase||'lobby';if(lobbyPhase==='playing'){matchReady=true;started=true;lobbyOpen=false;$('lobby').hidden=true;}else{lobbyOpen=true;matchReady=false;renderLobby();}setRunning(false);
 }

 if(m.type==='lobby'){members=m.members;lobbyPhase=m.phase;countdownAt=m.startAt||0;if(lobbyOpen)renderLobby();}
 if(m.type==='countdown'){lobbyPhase='countdown';countdownAt=m.startAt;renderLobby();}
 if(m.type==='countdown-cancelled'){countdownAt=0;lobbyPhase='lobby';renderLobby();}
 if(m.type==='match-start')beginMatch();
 if(m.type==='trap-placed')operations.addTrap(m.trap);
 if(m.type==='pose'){let p=peers.get(m.id);if(!p){const avatar=new PeerAvatar(),mesh=avatar.root;scene.add(mesh);p={avatar,mesh,pose:m};peers.set(m.id,p);}p.pose=m;p.mesh.visible=true;p.avatar.update(m,elapsed);peerPose=m;}
 if(m.type==='left'||(m.type==='away'&&m.role!=='host')){const p=peers.get(m.role);if(p)scene.remove(p.mesh);peers.delete(m.role);}
 if(m.type==='closed'){for(const p of peers.values())scene.remove(p.mesh);peers.clear();if(hp<=0)finish('dead');else{resetCombat();showLobby();}}
 if(m.type==='state'&&!session.host)applyState(m);
 if(m.type==='shot'&&session.host){const direction=new THREE.Vector3(...m.direction);if(direction.lengthSq()>.1){const origin=new THREE.Vector3(...m.origin);enemies.hear(origin,90);direction.normalize();combatShot(origin,direction,m.damage);}}
 if(m.type==='damage')receiveDamage(m.amount,m.poison?{poison:true,id:m.hitId}:undefined);
 if(m.type==='revived'){poison.reset();hp=m.hp;account.health=hp;downed=false;outcome='';$('message').textContent=t('Напарник помог. Продолжайте игру.');}
 if(m.type==='event-interact'&&session.host)events.interact(m.position);
 if(m.type==='economy'){if(m.reason==='Деньги подобраны')audio.play('cash');if(account.equipped!==m.account.equipped)trapSelected=false;account=m.account;hp=account.health;rifle.bindInventory(account);shop.pending=false;shop.message(m.reason||'');if(shopOpen)shop.render(account);if(queuedEquip){const next=queuedEquip;queuedEquip=null;submitAction('equip',next);}}
 if(m.type==='loot-state')loot.apply(m.cash);
 if(m.type==='end'&&!session.host)finish(m.result);
}
$('join-toggle').addEventListener('click',()=>{$('settings').hidden=true;$('network-panel').hidden=!$('network-panel').hidden;});
$('join-room').addEventListener('click',()=>{const code=$('room-code').value.trim();if(!/^\d{8}$/.test(code)){$('network-status').textContent=t('Введите 8 цифр');return;}connectRoom('join',code);});
function connectRoom(mode,code){try{session.connect(getServerAddress(),mode,seed,code);}catch(error){$('network-status').textContent=error.message;$('lobby-status').textContent=error.message;}}
$('resume-room').addEventListener('click',()=>session.resumeSaved());
const getServerAddress=connectionSettings(t);
window.addEventListener('game-language-change',()=>{setRunning(false);$('message').textContent=t($('message').textContent);});

function openShop(){shopOpen=true;pause();shop.message('');shop.show(account);}
function closeShop(){shopOpen=false;shop.hide();setRunning(false);enter();}
function submitAction(action,id){
 if(action==='buy'&&WEAPONS.some(w=>w.id===id))trapSelected=false;
 if(action==='equip'){trapSelected=false;rifle.setUtility(false);if(session.active&&shop.pending){queuedEquip=id;return;}}
 if(session.active){if(shop.pending)return;shop.pending=true;shop.render(account);session.send({type:'market',action,id,health:hp,inventory:account});return;}
 let result;
 if(action==='buy'){if(!nearTerminal(player.position))return;account.health=hp;result=buy(account,id);hp=account.health;}
 else if(action==='equip')result={ok:equipWeapon(account,id),reason:''};
 else{account.health=hp;result={ok:useSupply(account,id),reason:''};hp=account.health;}
 if(result.ok){rifle.bindInventory(account);audio.play('reload');}
 shop.message(result.reason);if(shopOpen)shop.render(account);
}



function combatShot(origin,direction,damage){const wall=obstacleDistance(origin,direction,colliders),distance=events.distance(origin,direction);if(enemies.shoot(origin,direction,damage,Math.min(wall,distance)))return true;if(events.shoot(origin,direction,damage,wall))return true;return false;}

function nearDowned(){return [...peers].map(([id,v])=>({...v.pose,id})).find(p=>p.hp<=0&&Math.hypot(p.x-player.position.x,p.z-player.position.z)<2.5);}
function renderLobby(){lobby.root.hidden=!lobbyOpen;lobby.render(session.active?members.map(m=>({...m,self:m.id===session.role})):[{id:'host',slot:0,connected:true,self:true}],!session.active||session.host,session.code,lobbyPhase);setRunning(false);}
function showLobby(){lobbyOpen=true;lobbyPhase='lobby';countdownAt=0;renderLobby();}
function leaveLobby(){poison.reset();clearTimeout(localTimer);countdownAt=0;session.close();lobbyOpen=false;matchReady=false;started=false;lobby.root.hidden=true;if(document.pointerLockElement)document.exitPointerLock();setRunning(false);}
function startLobby(){audio.unlock();requestLookLock($('scene')).catch(()=>{});if(session.active){if(session.host)session.send({type:'start'});else session.send({type:'ready',ready:true});}else{lobbyPhase='countdown';countdownAt=Date.now()+5000;renderLobby();localTimer=setTimeout(beginMatch,5000);}}
function beginMatch(){needsCapture=document.pointerLockElement!==$('scene');matchReady=true;started=true;lobbyOpen=false;countdownAt=0;lobbyPhase='playing';lobby.root.hidden=true;setRunning(document.pointerLockElement===$('scene'));updateCamera();}

function applyPreferences(v){audio.setVolumes(v);if(!renderer||!scene)return;renderer.shadowMap.enabled=v.quality!=='low';const sun=scene.userData.sun||scene.children.find(o=>o.isDirectionalLight&&o.castShadow);if(sun){sun.castShadow=v.quality!=='low';const size=v.quality==='high'?2048:1024;if(sun.shadow.mapSize.x!==size){sun.shadow.map?.dispose();sun.shadow.map=null;sun.shadow.mapSize.set(size,size);}sun.shadow.needsUpdate=true;}if(scene.userData.appliedQuality!==v.quality){scene.traverse(o=>{for(const m of Array.isArray(o.material)?o.material:[o.material])if(m)m.needsUpdate=true;});scene.userData.appliedQuality=v.quality;}}

try{$('resume-room').hidden=!sessionStorage.getItem('dead-sector:room');}catch{$('resume-room').hidden=true;}

function placeTrap(){if(rifle.switchTime>0||hp<=0||!account.traps)return;if(session.active){submitAction('trap','trap');return;}const id='solo-trap-'+Math.floor(elapsed*1000);if(operations.placeTrap(player.position,id)){consumeTrap(account);audio.ui();}}
function rewardOperations(){if(session.active)return;const m=operations.mission;if(m?.status==='complete'&&!m.paid){m.paid=true;account.money+=m.reward;audio.play('cash');}for(const d of operations.drops)if(d.status==='opened'&&!d.paid){d.paid=true;account.money+=d.reward;}}
function updateSurvivalUI(){const m=operations.mission;$('survival-objective').textContent=m&&(m.status==='active'||operations.time-(m.completedAt||0)<8)?`${missionLabel(m.id)} · $${m.reward} · ${m.status==='complete'?t('Событие выполнено'):`${Math.round(Math.hypot(m.x-player.position.x,m.z-player.position.z))}m${m.id==='zone'?` · ${Math.floor(m.progress)}/30s`:m.id==='growth'?` · ${m.hp}/${m.maxHp} HP`:m.id==='generator'?t(' · E рядом'):''}`}`:'';const drop=operations.drops.find(d=>d.status!=='opened');$('airdrop-status').textContent=drop?`${t('Supply crate')} · ${Math.round(Math.hypot(drop.x-player.position.x,drop.z-player.position.z))}m · ${t(drop.status==='approaching'?'Aircraft approaching':drop.status==='falling'?'Descending':'Hold E for 10 seconds')}`:`${t('Next airdrop')} · ${Math.ceil(AIRDROP_INTERVAL-operations.time%AIRDROP_INTERVAL)}s`;$('gear-summary').textContent=`F · ${t('Flashlight')}${account.flashlight?'':' —'}`;const charge=Math.max(0,Math.min(FLASHLIGHT_DURATION,account.flashlightCharge||0)),percent=charge/FLASHLIGHT_DURATION*100,bar=$('flashlight-battery');bar.hidden=$('flashlight-percent').hidden=!account.flashlight;bar.setAttribute('aria-valuenow',String(Math.round(charge)));bar.setAttribute('aria-label',t('Flashlight battery'));bar.classList.toggle('low-charge',percent<=20);bar.classList.toggle('lit',flashlight.on);$('flashlight-charge').style.width=percent+'%';$('flashlight-percent').textContent=Math.ceil(percent)+'%';$('gear-supplies').textContent=` · B ×${account.batteries||0} · 3 ${t('Traps')} ×${account.traps||0}${trapSelected?' · '+t('LMB · Place underfoot'):''}`;}

function missionLabel(id){return t({package:'Collect building package',zone:'Hold terminal zone',generator:'Запустить генератор',growth:'Уничтожить нарост'}[id]);}
function activeMission(){const m=operations?.mission;return m?.status==='active'?{...m,label:missionLabel(m.id)}:null;}
function updateMissionNotice(){if(!started){$('mission-notice').hidden=true;return;}const m=activeMission();if(m&&m.key!==lastMissionKey){lastMissionKey=m.key;missionNoticeRecord={...m,complete:false};missionNoticeUntil=performance.now()+6000;audio.play('mission');}const completed=[operations.mission].find(e=>e?.status==='complete'&&!announcedMissionCompletions.has(e.key));if(completed){announcedMissionCompletions.add(completed.key);lastMissionCompleteKey=completed.key;missionNoticeRecord={...completed,complete:true};missionNoticeUntil=performance.now()+5000;audio.play('wave-clear');}const r=missionNoticeRecord;$('mission-notice').hidden=!r||performance.now()>missionNoticeUntil;if(r)$('mission-notice').textContent=r.complete?`${t('MISSION COMPLETE')} · +$${r.reward}`:`${t('NEW SHARED MISSION')} · ${missionLabel(r.id)} · $${r.reward}`;}
