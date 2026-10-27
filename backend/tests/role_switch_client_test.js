const assert = require('assert');
let users = [];
let taps = [];
let sent = [];
global.wx = {
  getStorageSync: () => 'test-token',
  getAccountInfoSync: () => ({ miniProgram: { envVersion: 'develop' } }),
  showActionSheet: options => {
    assert(options.itemList.length <= 6);
    const tap = taps.shift();
    if (tap === 'cancel') options.fail({});
    else options.success({ tapIndex: tap });
  },
  request: options => {
    const listing = options.url.endsWith('/demo/learners');
    if (!listing) sent.push(options.data);
    options.success({ statusCode: 200, data: { code: 0, data: listing ? { items: users } : { user: { id: options.data.userId } } } });
  }
};
const api = require('../../utils/api');
(async () => {
  await assert.rejects(api.switchRole('learner'), /暂无/);
  users = [{ id: 'real-admin', role: 'admin' }, { id: 'real-learner', role: 'learner' }];
  await api.switchRole('learner');
  await api.switchRole('admin');
  assert.deepStrictEqual(sent, [{ role: 'learner', userId: 'real-learner' }, { role: 'admin', userId: 'real-admin' }]);
  users = Array.from({ length: 8 }, (_, i) => ({ id: `learner-${i}`, role: 'learner', displayName: `Learner ${i}` }));
  taps = [5, 2];
  await api.switchRole('learner');
  assert.strictEqual(sent.at(-1).userId, 'learner-7');
  const count = sent.length;
  taps = ['cancel'];
  await assert.rejects(api.switchRole('learner'), error => error.cancelled === true);
  assert.strictEqual(sent.length, count);
  console.log('Role switch client: empty, single, both roles, pagination and cancel passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
