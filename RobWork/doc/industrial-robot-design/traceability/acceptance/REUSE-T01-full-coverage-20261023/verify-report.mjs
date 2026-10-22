import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import cp from 'node:child_process';
const out = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1'));
const root = path.join(out, 'repo');
const read = p => fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n');
const load = f => JSON.parse(read(path.join(out, f)));
const hash = s => crypto.createHash('sha256').update(s.replace(/\n*$/, '\n')).digest('hex');
const check = (ok, message) => { if (!ok) throw new Error(message); };
const jsonFiles = fs.readdirSync(out).filter(f => f.endsWith('.json'));
jsonFiles.forEach(load);
const report = load('coverage.json'), inv = load('inventory-analysis.json');
const gaps = load('coverage-gaps.json').gaps, supplemental = load('supplemental-gaps.json');
const matrixDir = path.join(root, 'RobWork/doc/industrial-robot-design/reuse/functions');
const rows = fs.readdirSync(matrixDir).filter(f => f.endsWith('.json')).flatMap(f => JSON.parse(read(path.join(matrixDir, f))).functions);
const references = [];
for (const row of rows) {
  for (const s of row.sources ?? []) references.push({...s});
  for (const s of row.auditedSections ?? []) references.push({...s, path:s.path ?? row.sources?.[0]?.path});
}
check(rows.length === 548, 'matrix count');
check(inv.files.length === 1401 && inv.units.length === 21, 'inventory count');
check(inv.units.reduce((s, u) => s + u.noFunctionSource, 0) === 1263, 'unreferenced count');
check(inv.units.reduce((s, u) => s + u.sourceReferenced, 0) === 138, 'referenced count');
check(inv.units.reduce((s, u) => s + u.candidateImplementationOrScript, 0) === 1074, 'implementation candidates');
check(inv.units.reduce((s, u) => s + u.declarationDtoHeaderCandidates, 0) === 188, 'declaration candidates');
check(gaps.length === 20 && supplemental.length === 7, 'excerpt count');
for (const item of [...gaps, ...supplemental]) {
  const loc = item.location ?? item;
  const text = read(path.join(root, loc.path)), lines = text.split('\n');
  check(hash(text) === loc.fileSha256, item.id + ' file hash');
  const section = lines.slice(loc.startLine - 1, loc.endLine);
  if (loc.sectionSha256) check(hash(section.join('\n')) === loc.sectionSha256, item.id + ' section hash');
  const expected = loc.path + '\n' + loc.qualifiedSymbol + '\n' + section.map((l, i) => `${loc.startLine + i}: ${l}`).join('\n') + '\n';
  check(read(path.join(out, item.evidence)) === expected, item.id + ' excerpt content');
  const sameFile = references.filter(s => s.path === loc.path);
  const overlaps = sameFile.filter(s => s.startLine <= loc.endLine && s.endLine >= loc.startLine);
  check(overlaps.length === 0, item.id + ' source interval overlap');
  if (item.location) check(sameFile.length === 0, item.id + ' whole-file source reference');
}
for (const entry of load('mjs-function-index.json')) {
  const text = read(path.join(root, entry.path)), lines = text.split('\n');
  check(hash(text) === entry.fileSha256, entry.path + ' hash');
  for (const f of entry.functions) check(lines[f.line - 1].includes('function ' + f.symbol + '('), entry.path + ':' + f.line);
}
for (const f of report.evidenceFiles) check(fs.existsSync(path.join(out, f)), 'missing evidence ' + f);
check(report.result === 'NOT PASS' && report.T02Admission === false && report.fullT01Pass === false, 'result boundary');
check(JSON.stringify(report.metrics) === JSON.stringify(inv.summary), 'report metrics');
const sha = cp.execFileSync('git', ['rev-parse', 'HEAD'], {cwd:root, encoding:'utf8'}).trim();
const status = cp.execFileSync('git', ['status', '--porcelain'], {cwd:root, encoding:'utf8'});
check(sha === 'b83fa7a3fc8ff233e56736adabd53f11c914dbad', 'frozen commit');
check(status === '', 'checkout changed');
fs.writeFileSync(path.join(out, 'git-status-end.txt'), status);
const result = {result:'PASS',jsonFilesParsed:jsonFiles.length,frozenCommit:sha,repositoryClean:true,definitionsVerified:27,topGapsWithoutSources:20,supplementalWithoutIntervalOverlap:7,inventoryFiles:1401,unreferencedCandidates:1263,mjsIndexes:29,gateExitCodes:report.verification.gateExitCodes};
fs.writeFileSync(path.join(out, 'report-consistency-check.json'), JSON.stringify(result, null, 2) + '\n');
console.log(JSON.stringify(result, null, 2));
