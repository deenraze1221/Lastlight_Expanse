import * as T from 'three';
const environments=new WeakMap();
// Original sky cubemap, convolved once per renderer and reused after restarting.
export function applyEnvironmentLight(scene,renderer){
 let target=environments.get(renderer);
 if(!target){const faces=Array.from({length:6},(_,face)=>{const canvas=document.createElement('canvas');canvas.width=canvas.height=64;const c=canvas.getContext('2d'),gradient=c.createLinearGradient(0,0,0,64);if(face===2){gradient.addColorStop(0,'#8298ae');gradient.addColorStop(1,'#aab7bf');}else if(face===3){gradient.addColorStop(0,'#202724');gradient.addColorStop(1,'#222a28');}else{gradient.addColorStop(0,'#879bac');gradient.addColorStop(.52,'#abb9bd');gradient.addColorStop(.57,'#444e4d');gradient.addColorStop(1,'#202724');}c.fillStyle=gradient;c.fillRect(0,0,64,64);return canvas;});const cube=new T.CubeTexture(faces);cube.colorSpace=T.SRGBColorSpace;cube.needsUpdate=true;const generator=new T.PMREMGenerator(renderer);target=generator.fromCubemap(cube);generator.dispose();cube.dispose();environments.set(renderer,target);}
 scene.environment=target.texture;scene.environmentIntensity=.38;
}
