import test from 'node:test';import assert from 'node:assert/strict';
import {defaultServer,serverAddress} from '../connection-settings.js';
test('Render and LAN resolve to the same origin and preserve explicit ports',()=>{
 assert.equal(defaultServer({protocol:'http:',host:'192.168.1.10:3000'}),'ws://192.168.1.10:3000');
 assert.equal(defaultServer({protocol:'https:',host:'game.onrender.com'}),'wss://game.onrender.com');
 assert.equal(defaultServer({protocol:'file:',host:''}),'');
 assert.equal(serverAddress('game.onrender.com','render'),'wss://game.onrender.com/');
 assert.equal(serverAddress('192.168.1.10','lan'),'ws://192.168.1.10:3000/');
 assert.equal(serverAddress('ws://localhost:4567','lan'),'ws://localhost:4567/');
 assert.throws(()=>serverAddress('192.168.1.10','lan','https:'));
 assert.throws(()=>serverAddress('https://example.com','render'));
});
