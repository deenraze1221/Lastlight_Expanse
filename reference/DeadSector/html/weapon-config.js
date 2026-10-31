// Shared by the client and room server. Intervals resolve qualitative rate requests.
export const WEAPONS = Object.freeze([
  {id:'Pistol',unlockAfter:0,label:'ПИСТОЛЕТ',capacity:8,reserve:84,damage:45,interval:.28,reload:1.15,automatic:false,reloadStyle:'magazine'},
  {id:'Helsing',viewZoom:1.54,unlockAfter:25,label:'HELSING',capacity:60,reserve:320,damage:35,interval:.075,reload:4,automatic:true,reloadStyle:'action'},
  {id:'Tikhar',unlockAfter:10,label:'TIKHAR',capacity:1,reserve:60,damage:130,interval:.45,reload:3,automatic:false,reloadStyle:'breech'},
  {id:'Shambler',viewZoom:1.5,unlockAfter:8,label:'SHAMBLER',capacity:5,reserve:100,damage:65,interval:.5,reload:3,automatic:false,reloadStyle:'shells'},
  {id:'Shotgun',unlockAfter:2,label:'SHOTGUN',capacity:2,reserve:80,damage:90,interval:.6,reload:2,automatic:false,reloadStyle:'breech'},
  {id:'Kalash',viewZoom:1.4,unlockAfter:10,label:'KALASH',capacity:30,reserve:240,damage:50,interval:.14,reload:3,automatic:true,reloadStyle:'action'},
  {id:'VSV',unlockAfter:20,label:'VSV',capacity:12,reserve:120,damage:80,interval:.24,reload:6,automatic:true,reloadStyle:'action'},
  {id:'Scorpion',viewZoom:1.3,unlockAfter:15,label:'SCORPION',capacity:35,reserve:280,damage:45,interval:.10,reload:4,automatic:true,reloadStyle:'action'},
  {id:'Vector',viewZoom:1.2,viewLower:1.5,unlockAfter:5,label:'VECTOR',capacity:20,reserve:200,damage:40,interval:.09,reload:3,automatic:true,reloadStyle:'action'},
  {id:'G36',viewZoom:1.25,unlockAfter:18,label:'G36',capacity:35,reserve:240,damage:40,interval:.14,reload:4,automatic:true,reloadStyle:'action'},
]);
export const WEAPON_BY_ID = Object.freeze(Object.fromEntries(WEAPONS.map(w=>[w.id,w])));
