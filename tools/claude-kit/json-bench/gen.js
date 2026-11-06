// 生成 JSON 读写测试的基准文件（tests/golden/）。基准 = Node 的 JSON.parse + JSON.stringify。
// 用法：node gen.js <输出目录>
// 每组输出：<名字>.in.json（输入文本）、<名字>.out2.json（JSON.stringify(v, null, 2)）、<名字>.out0.json（JSON.stringify(v)）
// 随机数用固定种子，重复运行结果相同。
'use strict';
const fs = require('fs');
const path = require('path');
const out = process.argv[2] || '.';
fs.mkdirSync(out, { recursive: true });

let seed = 20261102;
function rnd() { seed ^= seed << 13; seed >>>= 0; seed ^= seed >>> 17; seed ^= seed << 5; seed >>>= 0; return seed / 4294967296; }
function ri(n) { return Math.floor(rnd() * n); }

function emit(name, text) {
  const v = JSON.parse(text);
  fs.writeFileSync(path.join(out, name + '.in.json'), text);
  fs.writeFileSync(path.join(out, name + '.out2.json'), JSON.stringify(v, null, 2));
  fs.writeFileSync(path.join(out, name + '.out0.json'), JSON.stringify(v));
}

// 1. 手写的边界情况（每一行一个，组成数组；对象相关的单独成对象）
const edge = [
  '0', '-0', '1', '-1', '0.1', '-0.1', '1.5', '100', '1e2', '1E2', '1e+2', '1e-2', '0.000001', '0.0000001', '1e-7', '1.5e-7',
  '123456789012345680000', '123456789012345678901', '1e21', '1e20', '999999999999999999999', '1e22', '12e20',
  '9007199254740991', '9007199254740992', '9007199254740993', '18446744073709551615', '-9223372036854775808',
  '5e-324', '2.2250738585072014e-308', '2.225073858507201e-308', '1.7976931348623157e308', '1e308', '1e309', '-1e309', '1e400', '-1e-400', '1e-400',
  '0.1e1', '0.30000000000000004', '3.0000000000000004', '1.0', '1.10', '100.000', '0.5e-6', '4.35', '0.000123', '1234.5678e-10',
  '00000000000000000000000000000000000001e-37'.replace(/^0+/, '0.') /* 0.0…01e-37 */,
  '1797693134862315708145274237317043567981e269', '24.000000000000004', '1.2e-6', '-1.2e-7',
  '"中文"', '"emoji 😀 🇨🇳"', '"\\ud83d\\ude00"', '"\\ud800"', '"\\udc00"', '"a\\ud800b"', '"\\udc00\\ud800"', '"\\ud800\\ud800\\udc00"',
  '"\\u0000\\u0001\\u001f\\u007f\\u0080\\u00ff"', '"\\b\\f\\n\\r\\t"', '"\\"\\\\\\/"', '"/"', '"\\u2028\\u2029"', '"\\uFEFF"', '"\\uABCD\\uabcd"',
  'true', 'false', 'null', '[]', '{}', '[[]]', '[{}]', '{"a":[]}', '{"a":{}}', '[[[[[[]]]]]]',
  '""', '" "', '"\\t"',
];
emit('edge', '[\n' + edge.join(',\n') + '\n]');

// 2. 对象的键顺序：像整数的键、重复的键
emit('keys', JSON.stringify({}) === '{}' ? '{"b":1,"2":2,"a":3,"1":4,"b":5,"01":6,"4294967294":7,"4294967295":8,"-1":9,"1.5":10,"0":11,"10":12,"9":13,"":14,"__proto__":15,"constructor":16}' : '');
emit('dupnested', '{"x":{"k":1,"k":{"z":1},"j":2},"x":{"k":3},"y":[{"a":1,"a":2}],"1":true,"1":false}');
emit('ws', ' \t\r\n{ "a" : [ 1 , 2 , { "b" : null } ] , "c" : "d" } \n');

// 3. 数字：随机 double（随机位）、随机小数、随机整数、各种写法
function randBitsDouble() {
  const b = Buffer.alloc(8);
  for (let k = 0; k < 8; k++) b[k] = ri(256);
  const d = b.readDoubleLE(0);
  return Number.isFinite(d) ? d : 0;
}
const nums = [];
for (let k = 0; k < 20000; k++) nums.push(String(randBitsDouble()));
for (let k = 0; k < 10000; k++) { const d = randBitsDouble(); nums.push(d.toPrecision(1 + ri(21))); }
for (let k = 0; k < 10000; k++) nums.push((rnd() * 360 - 180).toFixed(ri(10)));       // 经纬度那样的小数
for (let k = 0; k < 5000; k++) nums.push(String(ri(2 ** 30) * (rnd() < 0.5 ? -1 : 1)));
for (let k = 0; k < 3000; k++) nums.push(String(1793537178175 + ri(1e9)));               // updatedAt 那样的毫秒时间戳
for (let k = 0; k < 2000; k++) nums.push((rnd() * 10 ** (ri(40) - 20)).toExponential(ri(17)));
for (let k = 0; k < 2000; k++) { const e = ri(30) - 10; nums.push('1e' + e); nums.push('-' + (ri(9) + 1) + '.' + ri(1000) + 'e' + (e * 10)); }
for (let k = 1; k <= 1100; k++) { nums.push('1e' + (k - 400)); }                          // 1e-399 … 1e700（溢出、下溢、次正规数）
emit('numbers', '[' + nums.join(',') + ']');

// 4. 随机文档：随机键（含中文、整数样、重复）、随机字符串（含控制字符、代理字符）、随机嵌套
function randStr() {
  const pool = ['a', 'Z', '中', '文', '😀', ' ', '"', '\\', '/', '\n', '\t', '\u0001', '\u001f', '\u007f', ' ', 'é', '\ud800', '\udfff', '\u0000'];
  let s = '';
  const n = ri(12);
  for (let k = 0; k < n; k++) s += pool[ri(pool.length)];
  return s;
}
function lit(s) {   // 把任意字符串写成 JSON 字面量（孤立代理也写成 \u 转义）
  let o = '"';
  for (const ch of s.split('')) {
    const c = ch.charCodeAt(0);
    if (ch === '"' || ch === '\\') o += '\\' + ch;
    else if (c < 0x20 || (c >= 0xd800 && c <= 0xdfff) || rnd() < 0.05) o += '\\u' + c.toString(16).padStart(4, '0').toUpperCase();
    else o += ch;
  }
  return o + '"';
}
function randKey() {
  const r = rnd();
  if (r < 0.15) return String(ri(30));
  if (r < 0.2) return ['0', '01', '-0', '1e3', '4294967295', '4294967294'][ri(6)];
  if (r < 0.6) return ['id', 'name', 'lat', 'lng', 'stops', 'date', 'note', 'x'][ri(8)];
  return randStr();
}
function randVal(d) {
  const r = rnd();
  if (d > 6 || r < 0.35) {
    const t = ri(6);
    if (t === 0) return String(randBitsDouble());
    if (t === 1) return (rnd() * 200 - 100).toFixed(ri(8));
    if (t === 2) return lit(randStr());
    if (t === 3) return ['true', 'false', 'null'][ri(3)];
    if (t === 4) return String(ri(100000));
    return lit('');
  }
  if (r < 0.65) { const n = ri(6); const a = []; for (let k = 0; k < n; k++) a.push(randVal(d + 1)); return '[' + a.join(',') + ']'; }
  const n = ri(7); const a = []; for (let k = 0; k < n; k++) a.push(lit(randKey()) + ':' + randVal(d + 1)); return '{' + a.join(',') + '}';
}
const docs = [];
for (let k = 0; k < 400; k++) docs.push(randVal(0));
emit('random', '[' + docs.join(',') + ']');

// 5. 深层嵌套
emit('deep', '['.repeat(500) + '{"a":' + '[1]'.repeat(1) + '}' + ']'.repeat(500));

console.log('ok', fs.readdirSync(out).length, 'files');
