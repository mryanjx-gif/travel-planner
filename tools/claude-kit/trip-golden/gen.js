// 生成 tests/golden/electron-sample.trip：用 Electron 版自己的 lib/trip-pack.js pack()（只读引用 D:\旅行计划App），
// 内容照 Electron export.js packOne 的样子：站点 order、legMode、站点照片对象、coverPhoto、攻略和游记里的 ![](photos/…)、photoCredits、一个不认识的字段。
// 用法：node gen.js [Electron 源码目录]
'use strict';
const path = require('path'), fs = require('fs');
const src = process.argv[2] || 'D:/旅行计划App';
const tripPack = require(path.join(src, 'lib', 'trip-pack.js'));
const jpg = (tag) => Buffer.concat([Buffer.from([0xff, 0xd8, 0xff, 0xe0]), Buffer.from('test photo ' + tag), Buffer.from([0xff, 0xd9])]);
const plan = {
  id: 'p_el1', name: '京都三日', type: 'route', status: 'planning', startDate: '2026-04-01', endDate: '2026-04-03', color: '#c96',
  coverPhoto: 'photos/p_el1/cover.jpg', currency: 'JPY', mood: '开心',
  guide: '看枫叶\n![金阁寺](photos/p_el1/kinkaku.jpg)\n完',
  stops: [
    { id: 's2', name: '岚山', lat: 35.0094, lng: 135.6668, order: 1, arriveDate: '2026-04-02', schedule: [{ date: '2026-04-02', time: '09:00', text: '竹林' }] },
    { id: 's1', name: '京都站', lat: 34.9858, lng: 135.7588, order: 0, arriveDate: '2026-04-01', legMode: 'train', legDist: 12000,
      photos: [{ id: 'ph_a', path: 'photos/p_el1/station.jpg', caption: '站台', takenAt: '2026-04-01T10:00', lat: 34.98, lng: 135.75 }] }
  ],
  expenses: [{ id: 'e1', date: '2026-04-01', cat: 'food', title: '拉面', amount: 1200 }],
  people: [{ id: 'u1', name: '小明' }],
  updatedAt: 1775000000000
};
const journal = [{ id: 'j1', planId: 'p_el1', stopId: 's1', title: '第一天', date: '2026-04-01', body: '到了\n![](photos/p_el1/station.jpg)' }];
const photoCredits = { 'photos/p_el1/kinkaku.jpg': { src: 'wikimedia', title: 'Kinkaku-ji', author: 'Someone', license: 'CC BY-SA 4.0', url: 'https://commons.wikimedia.org/' } };
const files = ['cover', 'station', 'kinkaku'].map(n => ({ rel: `photos/p_el1/${n}.jpg`, data: jpg(n) }));
const buf = tripPack.pack({ plan, journal, photoCredits, appVersion: '6.3.0', exportedAt: '2026-11-08T00:00:00.000Z' }, files);
const out = path.join(__dirname, '..', '..', '..', 'tests', 'golden', 'electron-sample.trip');
fs.writeFileSync(out, buf);
console.log('写好', out, buf.length, '字节');
