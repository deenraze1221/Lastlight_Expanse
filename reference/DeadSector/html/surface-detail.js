// Metre-scaled world coordinates prevent stretching across differently sized meshes.
export function installSurfaceDetail(material,kind){
 const mode={brick:1,concrete:2,plaster:2,pavement:3,road:4,wood:5,bark:5}[kind]||0;
 const wet={value:0};material.userData.surfaceWetness=wet;
 material.customProgramCacheKey=()=>`city-surfaces-v2-${mode}`;
 material.onBeforeCompile=shader=>{
  shader.uniforms.surfaceWetness=wet;
  shader.vertexShader=shader.vertexShader.replace('#include <common>','#include <common>\nvarying vec3 surfacePosition;').replace('#include <begin_vertex>',`#include <begin_vertex>
   vec4 surfaceVertex=vec4(transformed,1.);
   #ifdef USE_INSTANCING
   surfaceVertex=instanceMatrix*surfaceVertex;
   #endif
   surfacePosition=(modelMatrix*surfaceVertex).xyz;`);
  shader.fragmentShader=shader.fragmentShader.replace('#include <common>',`#include <common>
   varying vec3 surfacePosition;uniform float surfaceWetness;
   float cityHash(vec2 p){return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453);}
   float cityNoise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(cityHash(i),cityHash(i+vec2(1,0)),f.x),mix(cityHash(i+vec2(0,1)),cityHash(i+vec2(1,1)),f.x),f.y);}
  `).replace('#include <color_fragment>',`#include <color_fragment>
   vec3 worldFace=abs(normalize(cross(dFdx(surfacePosition),dFdy(surfacePosition))));
   vec2 wallUV=worldFace.x>worldFace.z?surfacePosition.zy:surfacePosition.xy;
   vec2 surfaceUV=worldFace.y>.6?surfacePosition.xz:wallUV;
   float broad=cityNoise(surfaceUV*.6),fine=cityNoise(surfaceUV*34.);
   float surfacePuddle=0.;
   diffuseColor.rgb*=mix(.78,1.08,broad)*mix(.94,1.04,fine);
   ${mode===1?`vec2 brick=surfaceUV/vec2(.48,.22);brick.x+=mod(floor(brick.y),2.)*.5;vec2 joint=min(fract(brick),1.-fract(brick));float mortar=1.-smoothstep(.018,.07,min(joint.x,joint.y));diffuseColor.rgb*=mix(.86,1.1,cityHash(floor(brick)));diffuseColor.rgb=mix(diffuseColor.rgb,vec3(.19,.19,.17),mortar*.7);`:''}
   ${mode===2?`float damp=(1.-smoothstep(.15,1.6,surfacePosition.y))*(.4+broad*.6);diffuseColor.rgb*=1.-damp*.22;float seams=1.-smoothstep(.008,.025,abs(fract(surfaceUV.x/3.)-.5));diffuseColor.rgb*=1.-seams*.1;`:''}
   ${mode===3?`vec2 joint=min(fract(surfaceUV/.72),1.-fract(surfaceUV/.72));float seam=1.-smoothstep(.008,.028,min(joint.x,joint.y));diffuseColor.rgb*=1.-seam*.32;`:''}
   ${mode===4?`float patches=cityNoise(surfaceUV*.22+vec2(16.,8.));surfacePuddle=smoothstep(.56,.72,patches)*surfaceWetness;float cracks=1.-smoothstep(.004,.022,abs(cityNoise(surfaceUV*1.5)-.48));diffuseColor.rgb*=1.-cracks*.15;diffuseColor.rgb=mix(diffuseColor.rgb,diffuseColor.rgb*.62,surfacePuddle);`:''}
   ${mode===5?`float grain=sin(surfaceUV.x*135.+cityNoise(surfaceUV*vec2(4.,.8))*7.);diffuseColor.rgb*=.92+grain*.08;`:''}
  `).replace('#include <normal_fragment_maps>',`#include <normal_fragment_maps>\n normal=normalize(normal+vec3(dFdx(fine),dFdy(fine),0.)*.13);`).replace('#include <roughnessmap_fragment>',`#include <roughnessmap_fragment>
   roughnessFactor=clamp(roughnessFactor+(fine-.5)*.1,.08,1.);
   ${mode===4?'roughnessFactor=mix(max(.58,roughnessFactor),.09,surfacePuddle);':''}
  `);
 };
}
