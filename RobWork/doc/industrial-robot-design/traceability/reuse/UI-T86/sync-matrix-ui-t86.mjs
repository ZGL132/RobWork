// UI-T86 矩阵同步脚本（本包产物——重跑可复现同一结果）。
// 职责：按治理 Digest 口径（BOM 剥离、CRLF→LF、单一尾换行——validate-reuse.ps1
// Digest 同款）重算本包产品改动文件摘要，执行：
//   1) functions/ui.json：ui/CMakeLists.txt 全部 inputs 摘要重钉；
//      ui.CMake.targetWiring sources 区段重定位（整文件行 1..N）；
//      ui.FixtureView sources 区段重定位＋摘要；ui.AnchorCaptureExecutorTest
//      整文件行重定位；ui.FixtureView/ui.ObservedView 等 nonOverlapEvidence
//      整文件摘要重钉（按路径匹配，与行归属无关）；
//   2) permissions.json：REUSE-T17-KinEvaluationChannelTest.cpp source/build
//      摘要重钉＋lifetime 文本登记锚能力迁移；REUSE-T41-UiPlugin.{hpp,cpp}
//      build 摘要重钉；
//   3) reuse/changes/UI-T86.json 逐文件凭证生成（-includeHost 参数控制是否
//      含 HostCompilePipelineTest.cpp——该文件为 blocked 取证模式的临时面，
//      终态两文件模式不含）。
// JSON 回写保持各文件原换行风格（permissions 为 CRLF，其余按探测）。
import fs from 'node:fs';
import crypto from 'node:crypto';

const ROOT = 'D:/10_Source_Repos/21_robot/RobWork';
const P = {
  uiCMake: 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/CMakeLists.txt',
  kinTest: 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/test/KinEvaluationChannelTest.cpp',
  hostTest: 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/test/HostCompilePipelineTest.cpp',
  readme: 'RobWork/doc/industrial-robot-design/traceability/reuse/UI-T86/README.md',
};
const FN = 'RobWork/doc/industrial-robot-design/reuse/functions/';
const PERM = 'RobWork/doc/industrial-robot-design/reuse/permissions.json';
const CHANGES = 'RobWork/doc/industrial-robot-design/reuse/changes/UI-T86.json';

const includeHost = process.argv.includes('-includeHost');

function digest(text) {
  const t = text.replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').replace(/\n+$/, '');
  return crypto.createHash('sha256').update(Buffer.from(t + '\n', 'utf8')).digest('hex');
}
function fileDigest(rel) { return digest(fs.readFileSync(ROOT + '/' + rel, 'utf8')); }
function textLines(rel) {
  return fs.readFileSync(ROOT + '/' + rel, 'utf8').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').split('\n');
}
function lineCount(rel) {
  const arr = textLines(rel);
  return arr[arr.length - 1] === '' ? arr.length - 1 : arr.length;
}
function regionDigest(rel, start, end) {
  return digest(textLines(rel).slice(start - 1, end).join('\n'));
}
function lineOf(rel, needle) {
  const arr = textLines(rel);
  const i = arr.findIndex((l) => l.includes(needle));
  if (i < 0) throw new Error('marker not found in ' + rel + ': ' + needle);
  return i + 1;
}
function loadJson(rel) {
  const raw = fs.readFileSync(ROOT + '/' + rel, 'utf8');
  return { j: JSON.parse(raw), crlf: raw.includes('\r\n') };
}
function saveJson(rel, obj, crlf) {
  let out = JSON.stringify(obj, null, 2) + '\n';
  if (crlf) out = out.replace(/\n/g, '\r\n');
  fs.writeFileSync(ROOT + '/' + rel, out);
}

const digestCMake = fileDigest(P.uiCMake);
const digestKin = fileDigest(P.kinTest);
const kinLines = lineCount(P.kinTest);
const cmakeLines = lineCount(P.uiCMake);
console.log('digestCMake', digestCMake.slice(0, 12), 'lines', cmakeLines);
console.log('digestKin  ', digestKin.slice(0, 12), 'lines', kinLines);

// ---- FixtureView 类区段重定位（doc 起于 @brief 夹具模型视图 前的 /**）----
const briefLine = lineOf(P.kinTest, '@brief 夹具模型视图');
const fixtureStart = briefLine - 1; // '/**' 开启行
const anchorLine = lineOf(P.kinTest, 'm_anchor;  ///< 锚缓存');
const fixtureEnd = anchorLine + 1; // 类收尾 '};'
const fixtureRegion = regionDigest(P.kinTest, fixtureStart, fixtureEnd);
console.log('FixtureView region', fixtureStart, '..', fixtureEnd, fixtureRegion.slice(0, 12));
if (!textLines(P.kinTest).slice(fixtureStart - 1, fixtureEnd).join('\n').includes('FixtureView')) {
  throw new Error('FixtureView region missing symbol');
}

// ---- 1) functions/ui.json 重钉 ----
let repinCMake = 0, repinKinN = 0;
for (const name of fs.readdirSync(ROOT + '/' + FN)) {
  if (name !== 'ui.json') continue;
  const rel = FN + name;
  const { j, crlf } = loadJson(rel);
  for (const r of j.functions) {
    for (const inp of r.inputs || []) {
      if (inp.path === P.uiCMake && inp.sha256 !== digestCMake) { inp.sha256 = digestCMake; repinCMake++; }
      if (inp.path === P.kinTest && inp.sha256 !== digestKin) { inp.sha256 = digestKin; repinKinN++; }
    }
    if (r.nonOverlapEvidence && r.nonOverlapEvidence.path === P.kinTest) {
      r.nonOverlapEvidence.sha256 = digestKin;
    }
    if (r.functionId === 'ui.FixtureView') {
      r.sources[0].startLine = fixtureStart;
      r.sources[0].endLine = fixtureEnd;
      r.sources[0].sha256 = fixtureRegion;
    }
    if (r.functionId === 'ui.AnchorCaptureExecutorTest') {
      r.sources[0].startLine = 1;
      r.sources[0].endLine = kinLines;
      r.sources[0].sha256 = digestKin;
    }
    if (r.functionId === 'ui.ObservedView') {
      // ObservedView 函数局部类区段重定位（原区段＝类声明行至类收尾 '};'，
      // 无独立 doc 块——按 'class ObservedView final' 行起算，收尾按 trim '};'）。
      const clsLine = lineOf(P.kinTest, 'class ObservedView final');
      const arr = textLines(P.kinTest);
      let obsEnd = -1;
      for (let i = clsLine; i < arr.length; i++) {
        if (arr[i].trim() === '};') { obsEnd = i + 1; break; }
      }
      if (obsEnd < 0) { throw new Error('ObservedView closing not found'); }
      r.sources[0].startLine = clsLine;
      r.sources[0].endLine = obsEnd;
      r.sources[0].sha256 = regionDigest(P.kinTest, clsLine, obsEnd);
      console.log('ObservedView region', clsLine, '..', obsEnd);
    }
    if (r.functionId === 'ui.CMake.targetWiring') {
      r.sources[0].endLine = cmakeLines;
      r.sources[0].sha256 = regionDigest(P.uiCMake, 1, cmakeLines);
    }
  }
  saveJson(rel, j, crlf);
  console.log('ui.json repinned: cmakeInputs', repinCMake, 'kinInputs', repinKinN);
}

// ---- 2) permissions.json 重钉 ----
{
  const { j, crlf } = loadJson(PERM);
  for (const g of j.grants) {
    if (g.id === 'REUSE-T17-KinEvaluationChannelTest.cpp') {
      g.source.sha256 = digestKin;
      if (g.build && g.build.path === P.uiCMake) g.build.sha256 = digestCMake;
      g.lifetime =
        'REUSE-T17锚语义executor级用例＋UI-T86（F-973诊断返工）锚能力迁移登记：write-state仅及任务私有State'
        + '（device->setQ于makeState副本，证明跨任务State隔离）；模型消费经runtime编译快照受控视图；'
        + 'FixtureView自UI-T86起覆盖runtimeModelView()——经kinematics/test夹具compileRealSnapshot以本替身'
        + 'CanonicalModel走RuntimeSnapshotFactory S6-S10产品路径（WC构造在runtime owner产品路径内，替身只注入'
        + '模型值并调用工厂），供真实FK/IK用例锚消费；仅测试目标，无生产权限、无模型所有权主张。';
    }
    if ((g.id === 'REUSE-T41-UiPlugin.hpp' || g.id === 'REUSE-T41-UiPlugin.cpp')
      && g.build && g.build.path === P.uiCMake) {
      g.build.sha256 = digestCMake;
    }
  }
  saveJson(PERM, j, crlf);
  console.log('permissions.json repinned');
}

// ---- 3) changes/UI-T86.json ----
{
  const files = [];
  const kinBoundary =
    'UI-T86（F-973 诊断返工）：FixtureView 补 runtimeModelView() 锚覆盖（REUSE-T18 在案模式——'
    + 'testfixture::compileRealSnapshot 懒编译真实快照，T18 TestView 同款懒构造纪律；T17 契约值替身'
    + 'AnchorlessValueView 保持无锚不混同）——修复 T18 换核后 ui 12 项真实数值链用例夹具未同步迁移的'
    + '锚缺失失败（fk.ok()=false→NoDevice→批量 not-run）；产品 FK/IK 与其余单元零触碰';
  const cmakeBoundary =
    'UI-T86：sdurws_ird_ui_test 集成门控块内增 IRD_HAVE_RUNTIME_COMPILE=1 编译定义'
    + '（kinematics_test:515 先例同款机制——夹具真实编译锚的编译期开关；本守卫块整体冒烟树不注册，'
    + '冒烟面零影响）；宏体/门禁/链接面零改动';
  files.push({ mode: 'business', path: P.kinTest, sha256: digestKin, capabilities: [],
    verify: ROOT.replace(/\\/g, '/') + '/doc'.slice(0,0) + 'RobWork/doc/industrial-robot-design/traceability/reuse/UI-T86/README.md'
      .replace(/^RobWork/, 'RobWork'),
    boundary: kinBoundary });
  // verify 路径统一常量（上一行保持字面生成可读性）
  files[0].verify = 'RobWork/doc/industrial-robot-design/traceability/reuse/UI-T86/README.md';
  if (includeHost) {
    const digestHost = fileDigest(P.hostTest);
    files.push({ mode: 'business', path: P.hostTest, sha256: digestHost, capabilities: [],
      verify: 'RobWork/doc/industrial-robot-design/traceability/reuse/UI-T86/README.md',
      boundary:
        'UI-T86 取证面（blocked 登记随附候选）：NamedTcpClosureCompilesAndSolvesRealNumerics 本地 View 补'
        + ' runtimeModelView() 锚覆盖（锚＝同源物化快照本体）——修复后全量 344/1（仅 F-560）实证于'
        + ' green13-full-suite.*；本文件因 UI-T78 渲染面 rw/graphics 头依赖在 IRD-GATE-AD 下无测试 grant'
        + ' 可得（graphics 仅 host-presentation origin 可授且宿主例外文件族硬校验为 UiPlugin.{hpp,cpp}），'
        + '终态回退，保留为所有者裁决后的落点候选' });
  }
  files.push({ mode: 'governance', path: P.uiCMake, sha256: digestCMake, capabilities: [],
    verify: 'RobWork/doc/industrial-robot-design/traceability/reuse/UI-T86/README.md',
    boundary: cmakeBoundary });
  const manifest = {
    schemaVersion: 'ird-reuse-change/1',
    taskId: 'UI-T86',
    taskFile: 'RobWork/doc/industrial-robot-design/tasks/foundation/UI-T86.json',
    files,
  };
  saveJson(CHANGES, manifest, false);
  console.log('changes/UI-T86.json written:', files.length, 'files');
}
