import {MAP_HALF,BACKDROP_DEPTH} from './world-config.js';
// Decorative city belt: four 100 m deep districts beyond the playable perimeter.
export function generateSkyline(seed=1){let state=(seed^0x51c17)>>>0;const random=()=>{state=(Math.imul(state,1664525)+1013904223)>>>0;return state/4294967296;};const buildings=[];for(let gx=-8;gx<=8;gx++)for(let gz=-8;gz<=8;gz++){const x=gx*26,z=gz*26;if(Math.abs(x)<MAP_HALF+10&&Math.abs(z)<MAP_HALF+10)continue;const w=13+random()*7,d=13+random()*7,h=12+random()*30,variant=Math.floor(random()*16);buildings.push({x,z,w,d,h,variant});}return buildings;}
