import {WEAPONS,WEAPON_BY_ID} from './weapon-config.js';
export const AMMO_LIMIT=500,AMMO_PACK=50,FLASHLIGHT_DURATION=120;
export const STORE={x:10.35,z:9.95,w:9.4,d:9.4,terminal:{x:7.25,z:10.9},spawn:{x:10.35,y:0,z:17.1}};
const prices={Helsing:6500,Tikhar:4000,Shambler:3500,Shotgun:2000,Kalash:4500,VSV:6000,Scorpion:5000,Vector:3000,G36:5500};
export const SHOP_ITEMS=[...WEAPONS.filter(w=>w.id!=='Pistol').map(w=>({id:w.id,kind:'weapon',name:w.label,price:prices[w.id]})),{id:'ammo',kind:'ammo',name:'50 универсальных патронов',price:50},{id:'smallMedkit',kind:'medkit',name:'Малая аптечка · +25 HP',price:100},{id:'largeMedkit',kind:'medkit',name:'Большая аптечка · +75 HP',price:250},{id:'flashlight',kind:'equipment',name:'Фонарик · 120 секунд',price:300},{id:'battery',kind:'equipment',name:'Батарея · 120 секунд',price:150},{id:'trap',kind:'equipment',name:'Капкан · 300 урона',price:200},{id:'repair',kind:'repair',name:'Починить · +50 прочности',price:500}];
export function createInventory(){return {revision:0,completedWave:0,money:0,health:100,equipped:'Pistol',reserve:84,weapons:{Pistol:{ammo:8,durability:100,wearShots:0}},smallMedkit:0,largeMedkit:0,flashlight:false,flashlightCharge:0,batteries:0,traps:0};}
export function totalAmmo(a){return a.reserve+Object.values(a.weapons).reduce((n,w)=>n+w.ammo,0);}
export function rewardForHP(hp){return Math.round(Math.max(15,Math.min(100,5+(hp-125)/5)));}
export function equipWeapon(a,id){if(!a.weapons[id])return false;a.equipped=id;return true;}
export function recordShot(a,id=a.equipped){const w=a.weapons[id];if(!w||w.ammo<=0||w.durability<=0)return false;w.ammo--;w.wearShots++;if(w.wearShots>=100){w.durability=Math.max(0,w.durability-5);w.wearShots-=100;}return true;}
export function buy(a,id){
 const item=SHOP_ITEMS.find(i=>i.id===id);if(!item)return {ok:false,reason:'Товар не найден'};
 if(a.money<item.price)return {ok:false,reason:'Недостаточно денег'};
 if(item.kind==='weapon'&&(a.completedWave||0)<WEAPON_BY_ID[id].unlockAfter)return {ok:false,reason:'Оружие ещё не открыто'};
 if(item.kind==='weapon'&&a.weapons[id])return {ok:false,reason:'Оружие уже куплено'};
 if(id==='ammo'&&totalAmmo(a)+AMMO_PACK>AMMO_LIMIT)return {ok:false,reason:'Нет места для 50 патронов'};
 if(id==='repair'&&a.weapons[a.equipped].durability===100)return {ok:false,reason:'Оружие уже исправно'};
 if(id==='flashlight'&&a.flashlight)return {ok:false,reason:'Оружие уже куплено'};
 if(id==='battery'&&!a.flashlight)return {ok:false,reason:'Сначала купите фонарик'};
 a.money-=item.price;
 if(id==='flashlight'){a.flashlight=true;a.flashlightCharge=FLASHLIGHT_DURATION;}if(id==='battery')a.batteries++;if(id==='trap')a.traps++;
 if(item.kind==='weapon'){for(const old of Object.keys(a.weapons))if(old!=='Pistol'){a.reserve+=a.weapons[old].ammo;delete a.weapons[old];}a.weapons[id]={ammo:0,durability:100,wearShots:0};a.equipped=id;}
 if(id==='ammo')a.reserve+=AMMO_PACK;
 if(id==='smallMedkit'||id==='largeMedkit'){a[id]++;if(a.health>0&&a.health<100)useMedkit(a,id);}
 if(id==='repair')a.weapons[a.equipped].durability=Math.min(100,a.weapons[a.equipped].durability+50);
 return {ok:true,reason:'Покупка выполнена'};
}
export function useMedkit(a,kind){if(!['smallMedkit','largeMedkit'].includes(kind)||a[kind]<=0||a.health>=100||a.health<=0)return false;a[kind]--;a.health=Math.min(100,a.health+(kind==='smallMedkit'?25:75));return true;}
export function nearTerminal(p){return p.y<1.2&&Math.hypot(p.x-STORE.terminal.x,p.z-STORE.terminal.z)<2.1;}
// Private rooms trust client combat, as before; purchases, money and owned items stay server-owned.
export function syncCombat(a,v){if(!v||v.revision!==a.revision)return;if(a.flashlight&&Number.isFinite(v.flashlightCharge))a.flashlightCharge=Math.max(0,Math.min(a.flashlightCharge,v.flashlightCharge));for(const [id,w]of Object.entries(a.weapons)){const next=v.weapons?.[id];if(!next)continue;w.ammo=Math.max(0,Math.min(WEAPON_BY_ID[id].capacity,Math.floor(next.ammo)||0));w.durability=Math.max(0,Math.min(100,Math.floor(next.durability)||0));w.wearShots=Math.max(0,Math.min(99,Math.floor(next.wearShots)||0));}a.reserve=Math.max(0,Math.min(500,Math.floor(v.reserve)||0));a.reserve=Math.max(0,a.reserve-Math.max(0,totalAmmo(a)-AMMO_LIMIT));if(a.weapons[v.equipped])a.equipped=v.equipped;}

export function useSupply(a,id){if(id==='battery'){if(!a.flashlight||a.batteries<=0||a.flashlightCharge>=FLASHLIGHT_DURATION)return false;a.batteries--;a.flashlightCharge=FLASHLIGHT_DURATION;return true;}return useMedkit(a,id);}
export function consumeTrap(a){if(!(a.traps>0))return false;a.traps--;return true;}
