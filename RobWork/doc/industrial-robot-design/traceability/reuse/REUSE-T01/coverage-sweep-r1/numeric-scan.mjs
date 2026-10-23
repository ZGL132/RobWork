// 覆盖清查会话 P2：src 下未登记 .cpp 的数值/算法特征扫描（只读启发式，非语法分析）。
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const root = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot';
const unc = readFileSync('.cache/coverage-sweep/uncovered-files.tsv', 'utf8').split('\n').slice(1)
  .filter(Boolean).map(l => l.split('\t'))
  .filter(([unit, kind, path]) => kind === 'cpp' && path.includes('/src/') && !path.includes('/test/'));

// 数值/算法核心特征词（按风险加权）
const features = [
  [/Eigen::|LinearAlgebra|svd|jacobi|eigenValue|eigenVector/i, 'eigen-decomp', 3],
  [/\b(inverse|invert|solve|LU|QR|Cholesky|decompos)/i, 'solve-decomp', 3],
  [/quaternion|Quaternion|EAA|RPY|Rotation3D|Transform/i, 'rotation', 2],
  [/interpolat|lerp|slerp|blend/i, 'interp', 2],
  [/integrat|derivative|gradient|newton|iterate|converge|tolerance/i, 'iterative', 3],
  [/std::(sort|nth_element|stable_sort|min_element|max_element)|std::is_permutation/i, 'sort-select', 1],
  [/\bseed\b|random|shuffle|std::mt19937|uniform_int|uniform_real/i, 'random', 2],
  [/\b(round|floor|ceil|clamp|epsilon|epsilon|nextafter|denorm|nan|infinity)\b/i, 'float-edge', 2],
  [/\b(norm|distance|dot|cross|angle|proj)/i, 'vector-math', 2],
  [/for\s*\(.*\)\s*\{[^]*?(sum|acc|accum|total)/i, 'accum-loop', 1],
];

const hits = [];
for (const [unit, kind, path] of unc) {
  let txt;
  try { txt = readFileSync(join(root, path.slice(root.length + 1)), 'utf8'); } catch { continue; }
  const noComment = txt.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
  const found = [];
  let score = 0;
  for (const [re, name, w] of features) {
    const m = noComment.match(new RegExp(re.source, 'gi'));
    if (m) { found.push(`${name}x${m.length}`); score += w * Math.min(m.length, 20); }
  }
  if (score >= 6) hits.push({ path, unit, score, found: found.join(',') });
}
hits.sort((a, b) => b.score - a.score);
const tsv = ['score\tunit\tpath\tfeatures', ...hits.map(h => [h.score, h.unit, h.path, h.found].join('\t'))];
writeFileSync('.cache/coverage-sweep/numeric-core-candidates.tsv', tsv.join('\n') + '\n');
console.log(`src非测试未登记cpp共 ${unc.length}，数值核心候选 ${hits.length}（score>=6）`);
console.log(tsv.slice(1, 31).join('\n'));
