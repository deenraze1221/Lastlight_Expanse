// Deterministic wave event, identical for every peer; rest restores the district.
export const districtIndex=(x,z)=>x<0?(z<0?0:1):(z<0?2:3);
export function blackoutDistrict(wave){return !wave.warmup&&!wave.rest&&wave.variant==='blackout'?(Math.floor(wave.number/4)-Math.floor(wave.number/20)-1)%4:-1;}
export const POWER_SHADER=`float gridDistrict(vec3 p){return p.x<0.?(p.z<0.?0.:1.):(p.z<0.?2.:3.);}float gridPower(vec3 p){return abs(gridDistrict(p)-powerCut)<.1?.06:1.;}`;
export function installPowerMaterial(material,powerCut){material.customProgramCacheKey=()=> 'power-grid-light-v1';material.onBeforeCompile=shader=>{shader.uniforms.powerCut=powerCut;shader.vertexShader=shader.vertexShader.replace('#include <common>','#include <common>\nvarying vec3 gridPosition;').replace('#include <begin_vertex>',`#include <begin_vertex>
vec4 gp=vec4(transformed,1.);
#ifdef USE_INSTANCING
gp=instanceMatrix*gp;
#endif
gridPosition=(modelMatrix*gp).xyz;`);shader.fragmentShader=shader.fragmentShader.replace('#include <common>',`#include <common>\nvarying vec3 gridPosition;uniform float powerCut;${POWER_SHADER}`).replace('#include <color_fragment>','#include <color_fragment>\ndiffuseColor.rgb*=gridPower(gridPosition);');};}
