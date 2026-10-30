import test from 'node:test';import assert from 'node:assert/strict';import {LookInput} from '../look-input.js';
test('camera discards lock-transition warps and non-finite mouse input',()=>{const l=new LookInput();l.reset(100);l.add(300,100,120);l.add(10000,-10000,200);l.add(NaN,Infinity,200);assert.deepEqual(l.consume(.016),{x:0,y:0});l.add(15,-10,200);const v=l.consume(.016);assert.ok(v.x>0&&v.y<0);l.reset(250);assert.deepEqual(l.consume(.016),{x:0,y:0});});
test('large batches remain bounded and ordinary movement settles without endless camera drift',()=>{const l=new LookInput();for(let i=0;i<100;i++)l.add(80,0,200);const first=l.consume(.1,2);assert.ok(first.x*.004<=Math.PI/2+1e-9);for(let i=0;i<120;i++)l.consume(1/60,2);assert.equal(l.x,0);});

test('ordinary mouse movement applies once with no smoothing tail or frame-rate scaling',()=>{for(const dt of [1/30,1/60,1/144]){const l=new LookInput();l.add(25,-12,200);assert.deepEqual(l.consume(dt),{x:25,y:-12});assert.deepEqual(l.consume(dt),{x:0,y:0});}});

test('isolated spikes and stale queued input do not change sensitivity',()=>{const l=new LookInput();l.add(8,2,100);l.add(9,2,108);l.add(350,-300,109);l.add(8,2,116);l.add(40,0,50,400);assert.deepEqual(l.consume(.1),{x:25,y:6});});
test('normal accumulated movement keeps the same sensitivity across frame rates',()=>{for(const fps of [15,30,60,144]){const l=new LookInput();let total=0;for(let n=0;n<600;n++){l.add(4,0,100+n);if(n%Math.ceil(1000/fps)===0)total+=l.consume(1/fps).x;}total+=l.consume(1/fps).x;assert.equal(total,2400);}});

test('delayed delivery within an active lock keeps legitimate motion',()=>{const l=new LookInput();l.add(24,-9,200,2400);assert.deepEqual(l.consume(.1),{x:24,y:-9});});
