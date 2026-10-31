// Reuse the actual HTML shop drawings, without importing its DOM-dependent UI.
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {runInNewContext} from 'node:vm';
const source=await readFile('shop-ui.js','utf8');
const supply=runInNewContext('('+source.slice(source.indexOf('function supplyIcon(')).trim()+')',{t:value=>value});
await mkdir('godot/assets/icons',{recursive:true});
for(const id of ['ammo','smallMedkit','largeMedkit','flashlight','battery','trap','repair']){
 let svg=supply(id).replace('<svg ','<svg xmlns="http://www.w3.org/2000/svg" width="116" height="80" ');
 svg=svg.replace('class="medical-cross"','fill="#14201b"').replace(/<path(?![^>]*\bfill=)/g,'<path fill="#c0cdb5"');
 await writeFile('godot/assets/icons/'+id+'.svg',svg);
}
console.log('Exported seven original HTML supply icons.');
