import * as T from 'three';
export function createPSX(scene,renderer){
 const target=new T.WebGLRenderTarget(1,1,{type:T.HalfFloatType,minFilter:T.NearestFilter,magFilter:T.NearestFilter}),post=new T.Scene(),camera=new T.OrthographicCamera(-1,1,1,-1,0,1),uniforms={source:{value:target.texture},texel:{value:new T.Vector2(1,1)},clock:{value:0},detail:{value:1}};
 const material=new T.ShaderMaterial({depthTest:false,depthWrite:false,uniforms,vertexShader:'varying vec2 screenUV;void main(){screenUV=uv;gl_Position=vec4(position.xy,0.,1.);}',fragmentShader:`varying vec2 screenUV;uniform sampler2D source;uniform vec2 texel;uniform float clock,detail;
 void main(){vec3 c=texture2D(source,screenUV).rgb;vec3 neighbors=texture2D(source,screenUV+vec2(texel.x,0)).rgb+texture2D(source,screenUV-vec2(texel.x,0)).rgb+texture2D(source,screenUV+vec2(0,texel.y)).rgb+texture2D(source,screenUV-vec2(0,texel.y)).rgb;vec3 glow=max(neighbors*.25-vec3(1.0),vec3(0.));c=max(vec3(0.),c+(c-neighbors*.25)*.07*detail)+glow*.10*detail;float luminance=dot(c,vec3(.2126,.7152,.0722));c=mix(vec3(luminance),c,.94);c*=vec3(.98,1.0,1.025);vec2 edge=screenUV*2.-1.;c*=1.-dot(edge,edge)*.055;gl_FragColor=vec4(c,1.);
 #include <tonemapping_fragment>
 #include <colorspace_fragment>
 float grain=fract(sin(dot(gl_FragCoord.xy+floor(clock*24.),vec2(12.9898,78.233)))*43758.5453)-.5;gl_FragColor.rgb+=grain*.004*detail;}`});post.add(new T.Mesh(new T.PlaneGeometry(2,2),material));
 return {target,resize(width,height){const h=Math.min(height,540),w=Math.max(1,Math.round(h*width/height));renderer.setPixelRatio(1);renderer.setSize(w,h,false);target.setSize(w,h);uniforms.texel.value.set(1/w,1/h);},present(time,quality){uniforms.clock.value=time;uniforms.detail.value=quality==='low'?0:quality==='high'?1:.65;renderer.setRenderTarget(null);renderer.autoClear=true;renderer.render(post,camera);}};
}
