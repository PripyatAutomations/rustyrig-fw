const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ctx = {
   UserCache: {get_all: () => [{name: 'alice'}, {name: 'bob'}]},
   mediaChannels: {'rx-a': {subscribed: true}, 'rx-b': {subscribed: false}},
   mediaLastList: ['rx-a', 'rx-b']
};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync('www/js/webui.chat.completion.js', 'utf8'), ctx);
function candidates(line) { return Array.from(ctx.chat_parameter_candidates(line) || []); }
assert.deepEqual(candidates('/whois a'), ['alice']);
assert.deepEqual(candidates('/whois alice a'), []);
assert.deepEqual(candidates('/quota SET alice '), []);
assert.deepEqual(candidates('/quota SHOW a'), ['alice']);
assert.deepEqual(candidates('/media SUB #'), ['#1', '#2']);
assert.deepEqual(candidates('/media UNSUB rx-'), ['rx-a']);
assert.deepEqual(candidates('/media SUB rx-a '), []);
assert.deepEqual(candidates('/syslog of'), ['off']);
console.log('PASS: browser command parameter completion parity');
