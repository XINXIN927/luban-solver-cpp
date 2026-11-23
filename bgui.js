const fs = require('fs');
const { spawnSync } = require('child_process');

// 编译器：优先使用环境变量 CXX，否则取 PATH 中的 g++
const GCC = process.env.CXX || 'g++';
const opt = process.argv[2] || '-O2';

function stripBOM(buf) {
  if (buf.length >= 3 && buf[0] === 0xEF && buf[1] === 0xBB && buf[2] === 0xBF) {
    return buf.slice(3);
  }
  return buf;
}

function compile(src, out, opts) {
  const buf = stripBOM(fs.readFileSync(src));
  const o = opts.split(' ').filter(Boolean);
  const r = spawnSync(GCC, ['-std=c++17', ...o, '-c', '-x', 'c++', '-', '-o', out], { input: buf, maxBuffer: 1 << 28 });
  const err = (r.stderr || '').toString();
  console.log('compile', src, '->', out, 'status=' + r.status, err ? err.slice(0, 1200) : 'OK');
  return r.status === 0;
}

const tag = (process.pid || 0) + '_' + Date.now();
if (!compile('luban_cli_solver.cpp', 'o_core' + tag + '.o', opt)) process.exit(1);
if (!compile('luban_win32_gui.cpp', 'o_gui' + tag + '.o', opt)) process.exit(1);

const link = spawnSync(GCC, [
  'o_core' + tag + '.o', 'o_gui' + tag + '.o',
  '-o', 'luban_gui.exe',
  '-static', '-static-libgcc', '-static-libstdc++',
  '-lopengl32', '-lgdi32', '-lcomctl32', '-luuid', '-mwindows'
], { maxBuffer: 1 << 28 });
console.log('link status=' + link.status, (link.stderr || '').toString().slice(0, 1500));
