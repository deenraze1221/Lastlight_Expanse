function hash(v){v=Math.imul(v^(v>>>16),0x7feb352d);v=Math.imul(v^(v>>>15),0x846ca68b);return (v^(v>>>16))>>>0;}
export function missionForWave(seed,wave){if(wave<=2)return null;const h=hash((seed^Math.imul(wave,0x9e3779b1))>>>0);if(h/4294967296>=.15)return null;return {kind:['package','zone','generator','growth'][hash(h^0x85ebca6b)%4],reward:300+hash(h^0xc2b2ae35)%301};}

export function growthHealth(wave){return 500+Math.floor(Math.max(0,wave)/10)*500;}
