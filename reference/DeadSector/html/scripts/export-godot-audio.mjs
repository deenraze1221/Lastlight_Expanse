import {createServer} from 'vite';import {chromium} from 'playwright';import {mkdir,writeFile} from 'node:fs/promises';
const vite=await createServer({server:{host:'127.0.0.1',port:0}});await vite.listen();
const browser=await chromium.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
try{
const page=await browser.newPage();await page.route('**/audio-export',r=>r.fulfill({body:'<body></body>',contentType:'text/html'}));await page.goto(vite.resolvedUrls.local[0]+'audio-export');
const results=await page.evaluate(async()=>{
 const {GameAudio}=await import('/audio.js'),{MUSIC_TRACKS,noteHz}=await import('/music-tracks.js');
 const clips={};
 async function render(name,duration,setup){
  const context=new OfflineAudioContext(1,Math.ceil(duration*22050),22050);
  const audio=new GameAudio();audio.context=new Proxy(context,{get(t,p){if(p==='state')return 'running';const v=Reflect.get(t,p,t);return typeof v==='function'?v.bind(t):v;}});audio.ensureBuses();audio.setVolumes({master:1,music:1,weather:1});
  setup(audio,context);
  const buffer=await context.startRendering(),data=buffer.getChannelData(0),bytes=new Uint8Array(44+data.length*2),view=new DataView(bytes.buffer);
  const txt=(o,s)=>[...s].forEach((v,i)=>bytes[o+i]=v.charCodeAt(0));
  txt(0,'RIFF');view.setUint32(4,36+data.length*2,true);txt(8,'WAVEfmt ');view.setUint32(16,16,true);view.setUint16(20,1,true);view.setUint16(22,1,true);view.setUint32(24,22050,true);view.setUint32(28,44100,true);view.setUint16(32,2,true);view.setUint16(34,16,true);txt(36,'data');view.setUint32(40,data.length*2,true);
  for(let i=0;i<data.length;i++)view.setInt16(44+i*2,Math.max(-32767,Math.min(32767,data[i]*32767)),true);
  let binary='';for(let i=0;i<bytes.length;i+=16384)binary+=String.fromCharCode(...bytes.subarray(i,i+16384));clips[name]=btoa(binary);
 }
 for(const id of ['Pistol','Helsing','Tikhar','Shambler','Shotgun','Kalash','VSV','Scorpion','Vector','G36'])await render('shot-'+id,.6,a=>a.shot(id));
 for(const kind of ['reload','hurt','hit','step','wave','wave-clear','cash','thunder','trap','beacon','vomit','vomit-prepare','spore-burst','mission','airdrop','stomp','snarl','growl','eye'])await render(kind,2,a=>a.play(kind));
 for(const type of ['zombie','runner','eye','vomiter','brute','boss'])for(const state of ['living','death'])await render(type+'-'+state,1.5,a=>a.creature(state,type));
 for(const type of ['click','hover'])await render('ui-'+type,.3,a=>a.ui(type));
 await render('terminal',.5,a=>a.terminalBeacon({x:0,y:0,z:0},{x:1,z:0},0,true));
 for(const kind of ['rain','overcast'])await render(kind==='rain'?'rain':'wind',8,a=>a.weatherAmbience(kind));
 await render('aircraft',4,a=>a.aircraft({x:0,y:0,z:0},{x:0,y:20,z:0},true));
 const manifest={calm:[],tense:[]};
 for(const mood of ['calm','tense'])for(let index=0;index<5;index++){
  const track=MUSIC_TRACKS[mood][index],tick=60/track.bpm/(mood==='tense'?2:1),duration=tick*32,name=mood+'-'+index;
  manifest[mood].push({file:name+'.wav',name:track.name});
  await render(name,duration,(audio,c)=>{
   for(let voice=0;voice<4;voice++){
    const o=c.createOscillator(),f=c.createBiquadFilter(),g=c.createGain();o.type=voice===3?'sine':track.wave;f.frequency.value=track.cutoff;g.gain.value=voice===3?.001:.009;o.connect(f);f.connect(g);g.connect(audio.musicBus);o.start(0);
    for(let n=0;n<32;n++){const t=n*tick,note=voice===3?track.melody[n%8]:track.chords[Math.floor(n/8)%4][voice];o.frequency.setTargetAtTime(noteHz(note),t,voice===3?.025:.15);if(voice===3){g.gain.setValueAtTime(.001,t);g.gain.linearRampToValueAtTime(mood==='tense'?.015:.01,t+.025);g.gain.exponentialRampToValueAtTime(.001,t+tick*.85);}}
    o.stop(duration);
   }
  });
 }
 return {clips,manifest};
});
await mkdir('godot/assets/audio',{recursive:true});for(const [name,data]of Object.entries(results.clips))await writeFile('godot/assets/audio/'+name+'.wav',Buffer.from(data,'base64'));await writeFile('godot/assets/audio/library.json',JSON.stringify(results.manifest));console.log('Exported '+Object.keys(results.clips).length+' HTML audio clips including all10 scores.');
}finally{await browser.close();await vite.close();}
