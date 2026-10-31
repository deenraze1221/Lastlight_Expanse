import test from 'node:test';import assert from 'node:assert/strict';import {Rifle} from '../weapons.js';import {createInventory,buy} from '../economy.js';
test('equipment changes preserve the pistol magazine, and reload hands follow the equipped gun',()=>{
 const gun=new Rifle({play(){},shot(){}}),a=createInventory();gun.bindInventory(a);gun.fire();a.completedWave=25;a.money=5000;buy(a,'Kalash');gun.bindInventory(a);assert.equal(a.weapons.Pistol.ammo,7);gun.step(1.5);gun.reload();gun.step(.5);gun.pose(.5,false);assert.ok(gun.hands[0].position.y<gun.variant.record.grips.support[1]);assert.equal(gun.hands[1].position.z,gun.variant.record.grips.trigger[2]);
});
