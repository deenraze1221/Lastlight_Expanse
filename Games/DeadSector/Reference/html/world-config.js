// Shared by town generation, navigation, missions and server pose validation.
export const MAP_SIZE=250,MAP_HALF=MAP_SIZE/2,WALL_INNER=MAP_HALF-.5,NAV_HALF=Math.floor(WALL_INNER),SAFE_EDGE=MAP_HALF-6;
export const BLOCK_CENTERS=Array.from({length:10},(_,i)=>-108+i*24);
export const ROAD_CENTERS=Array.from({length:11},(_,i)=>-120+i*24);
export const INNER_ROADS=ROAD_CENTERS.slice(1,-1);
export const BACKDROP_DEPTH=100;
