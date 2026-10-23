// 覆盖清查会话 P1：当前HEAD全部产品文件 vs 矩阵已引用文件 的差集清点（只读）。
import { readFileSync, writeFileSync, readdirSync, statSync } from 'node:fs';
import { join, relative, sep } from 'node:path';

const prodRoot = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot';
const reuseRoot = 'RobWork/doc/industrial-robot-design/reuse';

function walk(dir, out = []) {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = join(dir, e.name);
    if (e.isDirectory()) walk(p, out);
    else out.push(p.replace(/\\/g, '/'));
  }
  return out;
}

// 1) 产品文件全集（含 CMake/mjs/testdata）；2) 治理脚本
const prodFiles = walk(prodRoot);
const govScripts = walk('RobWork/scripts/industrialrobot').filter(p => p.endsWith('.ps1') || p.endsWith('.mjs'));

// 3) 矩阵引用集合
const units = {};
for (const f of readdirSync(reuseRoot + '/functions')) {
  if (f.endsWith('.json')) units[f.replace('.json', '')] = JSON.parse(readFileSync(join(reuseRoot, 'functions', f), 'utf8')).functions;
}
const srcRef = new Set(), inputRef = new Set();
for (const [unit, rows] of Object.entries(units)) {
  for (const r of rows) {
    for (const s of r.sources ?? []) srcRef.add(s.path.replace(/\\/g, '/'));
    for (const a of r.auditedSections ?? []) {
      const p = typeof a === 'string' ? a : (a.path ?? '');
      if (p) srcRef.add(p.replace(/\\/g, '/'));
    }
    for (const i of r.inputs ?? []) inputRef.add(i.path.replace(/\\/g, '/'));
  }
}

// 4) hpp 实现特征：粗启发式——去掉注释后含 "template<" 或 行内函数体开始的行
function hppHasImpl(p) {
  const txt = readFileSync(p, 'utf8');
  const noComment = txt.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
  return /template\s*</.test(noComment) || /\)\s*(const\s*)?\{/.test(noComment);
}

const rows = [];
for (const p of prodFiles) {
  const unit = p.slice(prodRoot.length + 1).split('/')[0];
  const ext = p.endsWith('/') ? '' : p.split('.').pop();
  const kind = /CMakeLists\.txt$/.test(p) ? 'cmake' : ext;
  let impl = null;
  if (ext === 'hpp') impl = hppHasImpl(p) ? 'hpp-impl' : 'hpp-decl';
  const ref = srcRef.has(p) ? 'sources' : (inputRef.has(p) ? 'inputs-only' : 'none');
  rows.push({ unit, path: p, kind, impl, ref });
}
for (const p of govScripts) rows.push({ unit: '(governance-scripts)', path: p, kind: p.split('.').pop(), impl: null, ref: srcRef.has(p) ? 'sources' : (inputRef.has(p) ? 'inputs-only' : 'none') });

// 5) 汇总
const byUnit = {};
for (const r of rows) {
  const b = byUnit[r.unit] ??= { total: 0, sources: 0, inputsOnly: 0, none: 0, cpp: 0, cppNone: 0, hppImpl: 0, hppImplNone: 0 };
  b.total++;
  if (r.ref === 'sources') b.sources++; else if (r.ref === 'inputs-only') b.inputsOnly++; else b.none++;
  if (r.kind === 'cpp') { b.cpp++; if (r.ref === 'none') b.cppNone++; }
  if (r.impl === 'hpp-impl') { b.hppImpl++; if (r.ref === 'none') b.hppImplNone++; }
}

const tsv = [];
tsv.push('unit\ttotal\tsources\tinputs-only\tnone\tcpp\tnone-cpp\thpp-impl\tnone-hpp-impl');
for (const [u, b] of Object.entries(byUnit)) tsv.push([u, b.total, b.sources, b.inputsOnly, b.none, b.cpp, b.cppNone, b.hppImpl, b.hppImplNone].join('\t'));

// 6) 未登记实现文件清单（.cpp 全部 + hpp-impl + 脚本/mjs + cmake），排除 none 之外的
const unc = rows.filter(r => r.ref === 'none' && (r.kind === 'cpp' || r.impl === 'hpp-impl' || r.kind === 'mjs' || r.kind === 'ps1' || r.kind === 'cmake'));
const uncTsv = ['unit\tkind\tpath', ...unc.map(r => [r.unit, r.impl ?? r.kind, r.path].join('\t'))];

writeFileSync('.cache/coverage-sweep/unit-stats.tsv', tsv.join('\n') + '\n');
writeFileSync('.cache/coverage-sweep/uncovered-files.tsv', uncTsv.join('\n') + '\n');
console.log(tsv.join('\n'));
console.log('--- uncovered implementation candidates:', unc.length);
