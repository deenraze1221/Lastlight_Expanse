export function defaultServer(location){
 if(!['http:','https:'].includes(location.protocol))return '';
 return (location.protocol==='https:'?'wss://':'ws://')+location.host;
}
export function serverAddress(value,mode,protocol='http:',t=value=>value){
 let text=value.trim();
 if(!text)throw new Error(t('Введите адрес сервера'));
 if(!/^[a-z]+:\/\//i.test(text))text=(mode==='lan'?'ws://':'wss://')+text;
 const url=new URL(text);
 if(!['ws:','wss:'].includes(url.protocol)||url.username||url.password||url.hash)throw new Error(t('Нужен адрес WS или WSS'));
 if(mode==='lan'&&!url.port)url.port='3000';
 if(protocol==='https:'&&url.protocol==='ws:')throw new Error(t('Для локальной сети откройте игру по HTTP с компьютера хоста'));
 return url.href;
}
export function connectionSettings(t=value=>value){
 const mode=document.getElementById('connection-mode'),input=document.getElementById('server-url');
 let saved={};try{saved=JSON.parse(localStorage.getItem('dead-sector:connection')||'{}');}catch{}
 const addresses={render:saved.render||'',lan:saved.lan||''};
 mode.value=['render','lan'].includes(saved.mode)?saved.mode:'render';
 const automatic=defaultServer(location);
 if(automatic&&!addresses[mode.value])addresses[mode.value]=automatic;
 function refresh(){input.value=addresses[mode.value];input.placeholder=mode.value==='lan'?'192.168.1.10:3000':'your-game.onrender.com';}
 function save(){try{localStorage.setItem('dead-sector:connection',JSON.stringify({...addresses,mode:mode.value}));}catch{}}
 input.addEventListener('input',()=>{addresses[mode.value]=input.value;save();});
 mode.addEventListener('change',()=>{refresh();save();});refresh();
 return ()=>serverAddress(input.value,mode.value,location.protocol,t);
}
