// REUSE-T49 落地批：黄金期望重放一致性夹具（F-959 收口证据）。
// 职责与 REUSE-T01/replay-generators.py 同型（运行时换本仓在位 Node——
// T01 记录同款 v24.14.1）：testdata 只读纪律下，把全部黄金数据集复制到
// .cache 隔离目录逐脚本执行，对照产物与库内版本的原始字节与规范化换行
// 两种口径；记录写入本目录 landing-generator-replay-acc-r1.json。
// 差异定位（T01 审计）：make_import_expectations.mjs 曾写 unsupported=8＋
// joint-velocity-limit×5 而入库 expected 为 unsupported=3＋mappedVelocityCount=5；
// 本批修复后重放须 29/29 exit0 且规范化对照零实质漂移。
'use strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';

const ROOT = 'D:/10_Source_Repos/21_robot/RobWork';
const PREFIX = path.join(ROOT, 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden');
const OUT = path.join(ROOT, '.cache/t49-golden-replay');
const NODE = 'D:/software/Nodev/node.exe';

function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
function norm(data) {
  return Buffer.from(data.toString('utf8').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').replace(/\n+$/, '') + '\n', 'utf8');
}

fs.rmSync(OUT, { recursive: true, force: true });
fs.mkdirSync(OUT, { recursive: true });
const rows = [];
const collect = (dir, base, acc) => {
  for (const e of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) collect(p, path.join(base, e.name), acc);
    else if (e.name.endsWith('.json')) acc.push(path.relative(OUT + path.sep + path.basename(OUT), p) && path.relative(OUT, p));
  }
  return acc;
};
const scripts = [];
for (const unit of fs.readdirSync(PREFIX).sort()) {
  const vDir = path.join(PREFIX, unit);
  if (!fs.statSync(vDir).isDirectory()) continue;
  for (const ver of fs.readdirSync(vDir).sort()) {
    const gen = path.join(vDir, ver, 'generate');
    if (!fs.existsSync(gen)) continue;
    for (const f of fs.readdirSync(gen).sort()) {
      if (f.endsWith('.mjs')) scripts.push(path.join(vDir, ver, 'generate', f));
    }
  }
}
for (const original of scripts) {
  const dataset = path.dirname(path.dirname(original));
  const copy = path.join(OUT, path.relative(PREFIX, dataset));
  fs.cpSync(dataset, copy, { recursive: true });
  const script = path.join(copy, 'generate', path.basename(original));
  let exitCode = 0, stdout = '', stderr = '';
  try {
    stdout = execFileSync(NODE, [script], { encoding: 'utf8', timeout: 30000, stdio: ['ignore', 'pipe', 'pipe'] });
  } catch (e) {
    exitCode = e.status ?? 1;
    stdout = e.stdout ?? '';
    stderr = (e.stderr ?? '') + String(e.message ?? '');
  }
  const compared = [];
  const walk = (dir, rel) => {
    for (const e of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
      const p = path.join(dir, e.name);
      const r = rel ? rel + '/' + e.name : e.name;
      if (e.isDirectory()) walk(p, r);
      else if (e.name.endsWith('.json')) {
        const actual = fs.readFileSync(p);
        const basePath = path.join(dataset, r);
        const before = fs.existsSync(basePath) ? fs.readFileSync(basePath) : Buffer.alloc(0);
        compared.push({
          path: path.relative(ROOT, basePath).split(path.sep).join('/'),
          existsBefore: fs.existsSync(basePath),
          originalRawSha256: sha256(before),
          replayedRawSha256: sha256(actual),
          rawEqual: before.equals(actual),
          normalizedEqual: norm(before).equals(norm(actual)),
        });
      }
    }
  };
  walk(copy, '');
  const drift = compared.filter((c) => !c.normalizedEqual).map((c) => c.path);
  rows.push({
    script: path.relative(ROOT, original).split(path.sep).join('/'),
    scriptRawSha256: sha256(fs.readFileSync(original)),
    command: [NODE, script],
    exitCode, stdout, stderr,
    artifacts: compared,
  });
  console.log(path.basename(path.dirname(path.dirname(original))), exitCode, 'drifted', drift.length ? drift : 0);
}
const result = {
  schemaVersion: 'ird-golden-replay/1',
  taskId: 'REUSE-T49',
  reviewedCommit: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8', cwd: ROOT }).trim(),
  nodeVersion: execFileSync(NODE, ['--version'], { encoding: 'utf8' }).trim(),
  isolation: OUT,
  scripts: rows,
  counts: {
    executed: rows.length,
    passed: rows.filter((x) => x.exitCode === 0).length,
    failed: rows.filter((x) => x.exitCode !== 0).length,
    skipped: 0,
    notRun: 0,
  },
  normalizedDriftCount: rows.reduce((n, x) => n + x.artifacts.filter((c) => !c.normalizedEqual).length, 0),
  scope: 'F-959 落地批重放一致性：脚本进程执行/隔离产物对照（原始字节与规范化换行分列）；非产品C++测试，非数学正确性或全量 PASS',
};
const dest = path.join(ROOT, 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T49/landing-generator-replay-acc-r1.json');
fs.writeFileSync(dest, JSON.stringify(result, null, 2) + '\n', 'utf8');
console.log('record ->', path.relative(ROOT, dest));
