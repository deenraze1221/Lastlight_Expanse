import * as T from 'three';
export class CitySky {
 constructor(scene){this.scene=scene;this.uniforms={sky:{value:new T.Color()},clock:{value:0},night:{value:0},detail:{value:1}};this.mesh=new T.Mesh(new T.SphereGeometry(360,24,12),new T.ShaderMaterial({side:T.BackSide,depthWrite:false,uniforms:this.uniforms,vertexShader:`varying vec3 direction;void main(){direction=position;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.);}`,fragmentShader:`varying vec3 direction;uniform vec3 sky;uniform float clock,night,detail;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);}
void main(){vec3 d=normalize(direction);float horizon=pow(1.-max(d.y,0.),3.);vec3 color=sky*mix(.46,1.,horizon);if(d.y>.01){vec2 uv=d.xz/(d.y+.23)*2.2+vec2(clock*.008,clock*.003);float cloud=noise(uv)*.65+noise(uv*2.07)*.25;if(detail>.5)cloud+=noise(uv*4.1)*.1;float cover=smoothstep(.40,.74,cloud);color=mix(color,color*.48+vec3(.022,.026,.03)*(1.-night*.8),cover*(1.-horizon*.55));vec3 moon=normalize(vec3(-.45,.68,-.4));float disc=smoothstep(.9993,.99965,dot(d,moon));float halo=pow(max(0.,dot(d,moon)),160.);color+=vec3(.28,.34,.42)*(disc+halo*.12)*night*(1.-cover*.8);
 if(night>.5&&detail>.5){vec2 stars=d.xz/(d.y+.35)*180.;vec2 cell=floor(stars);float seed=hash(cell);float dotSize=1.-smoothstep(.035,.15,length(fract(stars)-.5));color+=vec3(.13,.16,.19)*dotSize*step(.991,seed)*smoothstep(.18,.7,d.y)*(1.-cover);}}gl_FragColor=vec4(color,1.);
#include <tonemapping_fragment>
#include <colorspace_fragment>
}` }));this.mesh.name='city-cloud-sky';this.mesh.frustumCulled=false;this.mesh.renderOrder=-100;scene.add(this.mesh);}
 update(position,time,night,quality){this.mesh.position.set(position.x,position.y+1.65,position.z);this.uniforms.sky.value.copy(this.scene.background);this.uniforms.clock.value=time;this.uniforms.night.value=night?1:0;this.uniforms.detail.value=quality==='low'?0:1;}
}
