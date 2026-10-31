import {chromium} from 'playwright';
import assert from 'node:assert/strict';
import {mkdir} from 'node:fs/promises';

const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||undefined,headless:true,args:['--no-sandbox','--enable-unsafe-swiftshader','--use-gl=angle','--use-angle=swiftshader']});
try{
 const page=await browser.newPage({viewport:{width:1440,height:900}}),errors=[];
 page.on('pageerror',e=>errors.push(e.stack));page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
 await page.route('**/models-fixture.html',r=>r.fulfill({contentType:'text/html',body:'<!doctype html><html><body style="margin:0;background:#252d29"></body></html>'}));
 await page.goto(new URL('/models-fixture.html',process.env.BASE_URL||'http://127.0.0.1:5173/app.html').href);
 const result=await page.evaluate(async()=>{
  const T=await import('/node_modules/.vite/deps/three.js'),{Enemies}=await import('/enemies.js'),{poseMutant}=await import('/mutant-visuals.js');
  const scene=new T.Scene();scene.background=new T.Color('#414a43');scene.add(new T.HemisphereLight('#e5e2ce','#303b32',3));
  const sun=new T.DirectionalLight('#eedbc1',3);sun.position.set(-4,9,7);scene.add(sun);
  const ground=new T.Mesh(new T.PlaneGeometry(30,30),new T.MeshStandardMaterial({color:'#62685a'}));ground.rotation.x=-Math.PI/2;scene.add(ground);
  const enemies=new Enemies(scene,[],{play(){}});
  for(let i=0;i<5;i++)enemies.spawn('zombie',-5+i*1.6,0,i,i);
  const brute=enemies.spawn('brute',5,0,5),runner=enemies.spawn('runner',2,0,6),eye=enemies.spawn('eye',-3,3,7);eye.rig.position.y=4;enemies.spawn('boss',2,3,8);
  const camera=new T.PerspectiveCamera(48,1440/900,.1,100);camera.position.set(10,6.7,17);camera.lookAt(.5,2.4,0);
  const renderer=new T.WebGLRenderer({antialias:true});renderer.setSize(1440,900);renderer.outputColorSpace=T.SRGBColorSpace;renderer.toneMapping=T.ACESFilmicToneMapping;document.body.append(renderer.domElement);
  for(const e of enemies.items){e.motion=1;e.phase=1.3;poseMutant(e);}renderer.render(scene,camera);
  window.fixture={T,scene,enemies,camera,renderer,poseMutant};
  return {types:enemies.items.map(e=>e.type),skins:enemies.items.slice(0,5).map(e=>e.skin),calls:renderer.info.render.calls};
 });
 assert.equal(new Set(result.skins).size,5);assert.ok(result.calls<300,'static anatomy details are batched');
 const artifacts=process.env.ARTIFACT_DIR;if(artifacts){await mkdir(artifacts,{recursive:true});await page.screenshot({path:artifacts+'/mutants-lineup.png'});}
 const death=await page.evaluate(()=>{
  const {T,scene,enemies,camera,renderer}=fixture,e=enemies.items[0];enemies.die(e);const start=enemies.blood.active;
  enemies.updateEffects(.18);renderer.render(scene,camera);return {visible:e.rig.visible,particles:start,rotation:e.rig.rotation.x};
 });
 assert.deepEqual(death,{visible:false,particles:48,rotation:0});
 if(artifacts)await page.screenshot({path:artifacts+'/mutants-burst.png'});
 const weapons=await page.evaluate(async()=>{
  const {T,renderer}=fixture,{Rifle,WEAPONS}=await import('/weapons.js');
  const rifle=new Rifle({play(){},shot(){}});renderer.setScissorTest(true);
  const result=[];
  for(let i=0;i<WEAPONS.length;i++){
   const spec=WEAPONS[i];rifle.reset(spec.id);rifle.pose(0,false);rifle.camera.aspect=480/225;rifle.camera.updateProjectionMatrix();
   const x=(i%3)*480,y=900-(Math.floor(i/3)+1)*225;renderer.setViewport(x,y,480,225);renderer.setScissor(x,y,480,225);
   renderer.setClearColor('#46534a');renderer.clear();rifle.hands.forEach(h=>h.visible=false);renderer.render(rifle.scene,rifle.camera);const bodyCalls=renderer.info.render.calls;rifle.hands.forEach(h=>h.visible=true);renderer.clear();renderer.render(rifle.scene,rifle.camera);
   const label=document.createElement('span');label.textContent=spec.id;label.style=`position:absolute;left:${x+16}px;top:${900-y-225+15}px;color:#e1e5c6;font:18px monospace`;document.body.append(label);
   const body=rifle.gun.geometry;body.computeBoundingBox();const b=body.boundingBox;
   result.push({id:spec.id,vertices:body.attributes.position.count,finite:[...b.min.toArray(),...b.max.toArray()].every(Number.isFinite),calls:bodyCalls});
  }
  renderer.setScissorTest(false);return result;
 });
 assert.equal(weapons.length,10);assert.ok(weapons.every(w=>w.vertices>500&&w.finite&&w.calls>0));
 if(artifacts)await page.screenshot({path:artifacts+'/weapons-v2.png'});
 assert.deepEqual(errors,[]);console.log('PASS five zombie outfits, articulated brute, bounded red death particles and all ten weapon models',result);
}finally{await browser.close();}
