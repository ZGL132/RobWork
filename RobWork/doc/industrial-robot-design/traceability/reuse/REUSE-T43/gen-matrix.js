// REUSE-T43 矩阵生成脚本（一次性治理数据编制工具，非产品代码）。
// selection.json 全量行编制：Backfill.cpp 全行铺面（含 eigen 换核 direct 行）、
// selection/CMakeLists.txt 全行铺面、CapabilityCurve/CombinationCheck 行收口。
// 区域以当前文件内容锚点定位，摘要口径与 validate-reuse.ps1 Digest 一致。
'use strict';
const fs = require('fs');
const crypto = require('crypto');

const ROOT = 'D:/10_Source_Repos/21_robot/RobWork';
const prod = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/';
const E = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T43';

function digestPath(p) {
  let t = fs.readFileSync(ROOT + '/' + p, 'utf8');
  t = t.replace(/^\uFEFF/, '').replace(/\r\n/g, '\n');
  return crypto.createHash('sha256').update(Buffer.from(t.replace(/\n+$/, '') + '\n', 'utf8')).digest('hex');
}
function loadLines(p) {
  return fs.readFileSync(ROOT + '/' + p, 'utf8').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').split('\n');
}
function digestRegion(lines, start, end) {
  const region = lines.slice(start - 1, end).join('\n');
  return crypto.createHash('sha256').update(Buffer.from(region.replace(/\n+$/, '') + '\n', 'utf8')).digest('hex');
}
function findLine(lines, needle, from) {
  for (let i = (from || 0); i < lines.length; ++i) {
    if (lines[i].includes(needle)) return i + 1;
  }
  throw new Error('anchor not found: ' + needle);
}

const cmakeInput = { path: prod + 'selection/CMakeLists.txt', sha256: digestPath(prod + 'selection/CMakeLists.txt') };


// —— 检索记录 ——
const SEARCHES = {
  SB: 'rg -n "class BINArchive|writeValue|readValue|doWrite|doRead" RobWork/RobWork/src/rw/common/BINArchive.hpp RobWork/RobWork/src/rw/common/BINArchive.cpp',
  SC: 'rg -n "class LinearInterpolator|interpolate|LinearInterpolator\\(" RobWork/RobWork/src/rw/trajectory/LinearInterpolator.hpp',
  SE: 'rg -n "eigenDecompositionSymmetric|SelfAdjointEigenSolver" RobWork/RobWork/src/rw/math/LinearAlgebra.hpp RobWork/RobWork/src/rw/math/LinearAlgebra.cpp',
  SF: 'rg -n "AxisBackfill|DeviceBackfill|synthesizeAxisBody|BackfillAssembly" RobWork/RobWork/src RobWork/RobWorkSim/src',
  SG: 'rg -n "little endian|little-endian|writeU32.*shift" RobWork/RobWork/src/rw/common/ RobWork/RobWorkSim/src/rwsim/',
  ST: 'rg -n "main\\(|InitGoogleTest|AddGlobalTestEnvironment|RobWork::init|sdurw-gtest-main|ADD_RW_GTEST" RobWork/RobWork/gtest/test-main.cpp RobWork/RobWork/gtest/CMakeLists.txt',
};
const SEARCH_CONCL = {
  SB: '上游 BINArchive 在位但为主机序/格式化长度容器（doWrite/doRead 格式化字段）——与产品显式小端定宽 canonical 字节身份协议不等价（F-777 位序修复承载于显式移位写）；候选评估与字节原语族处置见 byte-primitives-decision.md。',
  SC: '上游 LinearInterpolator 为 t∈[0,1] 段内插值器——无 x 域段定位/默认禁外推/拒绝轨/单位携带；产品 139 行中数值核仅 y0+t·(y1−y0) 两 flop，适配只增每查询段构造、不删重复结构，候选否决（详见行 differences）。',
  SE: '上游 eigenDecompositionSymmetric（Eigen SelfAdjointEigenSolver，升序特征值）在位——直接 API 换核落地（本批次）；解析闭式平行核删除。',
  SF: '上游无器件回填/轴合成/装配同义物（exit=1 零命中）——选型回填业务产品专属。',
  SG: '上游无显式小端定宽写原语同义物（仅 BINArchive 格式化容器，见 SB）——canonical 字节身份协议产品专属。',
  ST: '上游测试 main/环境为可选组件且非 CONFIG 导出——产品 testkit 薄入口承载，不双 main。',
};

function mkSources(path, lines, regions) { // regions: [[symbol, start, end]]
  return regions.map(([symbol, s, e]) => ({ path, symbol, qualifiedSymbol: symbol, startLine: s, endLine: e, sha256: digestRegion(lines, s, e) }));
}

function mkRow(fields) {
  const row = Object.assign({
    ownerUnit: 'selection',
    usageRole: 'owner',
    permissionRef: 'ARCH:3.6',
    decision: 'business-specific',
    status: 'migrated',
    inputs: [cmakeInput],
  }, fields);
  if (row.decision === 'business-specific') row.nonOverlapEvidence = { path: E + '/nonoverlap-selection.json', sha256: digestPath(E + '/nonoverlap-selection.json') };
  return row;
}

// ============ Backfill.cpp 全行铺面 ============
const bfPath = prod + 'selection/src/Backfill.cpp';
const bf = loadLines(bfPath);
const bfEnd = bf.length - 1 >= 1 && bf[bf.length - 1] === '' ? bf.length - 1 : bf.length;
// 行号整体锚点
const L = {
  isFiniteNumber: findLine(bf, 'bool isFiniteNumber(double v)'),
  isFiniteVec3: findLine(bf, 'bool isFiniteVec3(const BackfillVec3& v)'),
  isInertiaFinite: findLine(bf, 'bool isInertiaFinite(const BackfillInertiaTensor& t)'),
  eigenDoc: findLine(bf, '@brief 3×3 对称矩阵三特征值（REUSE-T43 起直接复用上游）') - 1,
  byteWriter: findLine(bf, 'class ByteWriter {'),
  byteReader: findLine(bf, 'class ByteReader {'),
  writeAxisEntry: findLine(bf, 'void writeAxisEntry(ByteWriter& w'),
  readAxisEntry: findLine(bf, 'AxisBackfillEntry readAxisEntry(ByteReader& r)'),
  checkSynthesis: findLine(bf, 'SynthesisOutcome checkSynthesisAssertions('),
  parallelAxis: findLine(bf, 'BackfillInertiaTensor parallelAxisContribution('),
  addInertia: findLine(bf, 'BackfillInertiaTensor addInertia(const BackfillInertiaTensor& a,'),
  recalcToken: findLine(bf, 'std::string_view recalcDomainToken(RecalcDomain domain)'),
  synthesize: findLine(bf, 'SynthesisOutcome synthesizeAxisBodyProperties('),
  encodePayload: findLine(bf, 'std::vector<std::uint8_t> encodeBackfillPayload('),
  decodePayload: findLine(bf, 'DecodedBackfillPayload decodeBackfillPayload('),
  encodeRecord: findLine(bf, 'std::vector<std::uint8_t> encodeBackfillRecordObject('),
  decodeRecord: findLine(bf, 'DecodedBackfillRecord decodeBackfillRecordObject('),
  findMotor: findLine(bf, 'const MotorCatalogEntry* findMotor('),
  findGearbox: findLine(bf, 'const GearboxCatalogEntry* findGearbox('),
  mountRecordExists: findLine(bf, 'bool mountRecordExists('),
  assemble: findLine(bf, 'BackfillAssemblyOutcome assembleDeviceBackfill('),
  makeEnvelope: findLine(bf, 'project::CommandEnvelope makeBackfillEnvelope('),
  commandType: findLine(bf, 'std::string DeviceBackfillCommandHandler::commandType()'),
  payloadVersion: findLine(bf, 'std::uint32_t DeviceBackfillCommandHandler::currentPayloadVersion()'),
  planFromEnvelope: findLine(bf, 'BackfillPlanOutcome DeviceBackfillCommandHandler::planFromEnvelope('),
  prepare: findLine(bf, 'project::PrepareOutcome DeviceBackfillCommandHandler::prepare('),
};
// 方法级锚点（ByteWriter 类内 / ByteReader 类内）
const mw = {
  u32w: findLine(bf, 'void u32(std::uint32_t v)', L.byteWriter),
  f64w: findLine(bf, 'void f64(double v)', L.byteWriter),
  strw: findLine(bf, 'void str(const std::string& s)', L.byteWriter),
  u32r: findLine(bf, 'std::uint32_t u32()', L.byteReader),
  f64r: findLine(bf, 'double f64()', L.byteReader),
  strr: findLine(bf, 'std::string str()', L.byteReader),
  u32At: findLine(bf, 'std::uint32_t u32At(std::size_t pos)', L.byteReader),
};

const backfillRows = [];
function bfRow(start, end, functionId, cap, decision, symbol, reqs, skey, diffs, extra) {
  backfillRows.push(mkRow(Object.assign({
    functionId,
    requirements: reqs,
    sources: [{ path: bfPath, symbol, qualifiedSymbol: symbol, startLine: start, endLine: end, sha256: digestRegion(bf, start, end) }],
    targets: ['sdurws_ird_selection'],
    capabilityId: cap,
    decision,
    search: { command: SEARCHES[skey], conclusion: SEARCH_CONCL[skey] },
    differences: diffs,
  }, extra || {})));
}

const BYTE_FAMILY = 'canonical 字节序列化为产品数据身份契约（project.md §4.8 canonical 规则同语义）：显式小端定宽＋IEEE754 位模式（F-777），上游 BINArchive 为主机序/格式化长度容器不等价（SB 检索）；Boost.Endian 候选需所有者明确批准（T20/T21 先例），未批前按字节身份契约保留——字节原语族决策记录 byte-primitives-decision.md。';
bfRow(1, L.isFiniteNumber - 1, 'selection.Backfill.fileHead', 'NONE', 'business-specific', 'Backfill.hpp', ['SEL-10', 'NFR-COR-02'], 'SF',
  '文件头/确定性/线程约定（IEEE754 位模式进 canonical 字节、零环境量、可重入）——产品数据身份契约（SF 零命中）。');
bfRow(L.isFiniteNumber, L.isFiniteVec3 - 1, 'selection.isFiniteNumber', 'NONE', 'business-specific', 'isFiniteNumber', ['NFR-COR-03'], 'SF',
  '非有限拒绝（NaN/±Inf 一律拒绝不静默置零）——业务输入卫生面（与 REUSE-T37 core 通用 isfinite 收口不重复：此处为业务拒绝分支承载）。');
bfRow(L.isFiniteVec3, L.isInertiaFinite - 1, 'selection.isFiniteVec3', 'NONE', 'business-specific', 'isFiniteVec3', ['NFR-COR-03'], 'SF', '三分量位置非有限检查（业务输入卫生）。');
bfRow(L.isInertiaFinite, L.eigenDoc - 1, 'selection.isInertiaFinite', 'NONE', 'business-specific', 'isInertiaFinite', ['NFR-COR-03'], 'SF', '六分量惯量非有限检查（业务输入卫生）。');
bfRow(L.eigenDoc, L.byteWriter - 1, 'selection.symmetricEigenvalues3x3', 'MATH-EIGEN', 'direct', 'symmetricEigenvalues3x3', ['MDL-06', 'SEL-10'], 'SE',
  '已换上游：rw::math::LinearAlgebra::eigenDecompositionSymmetric<double>（Eigen SelfAdjointEigenSolver，升序特征值契约与产品一致）——保留六分量→3×3 展开薄适配；解析闭式平行核删除。求解器间为 ulp 级差异（非逐位——探针 v1 位模式断言实证），断言②③为容差语义（附录 D 第 6/7 项相对 1e-12）：SPD 严格>0、三角不等式容差、黄金容差对照全过；生产证据四面对照见 production/selection.symmetricEigenvalues3x3.json。', {
  usageRole: 'adapter',
  production: { entry: 'symmetricEigenvalues3x3', upstream: 'eigenDecompositionSymmetric', consumer: 'checkSynthesisAssertions', target: 'sdurws_ird_selection' },
  evidence: { path: E + '/production/selection.symmetricEigenvalues3x3.json', sha256: digestPath(E + '/production/selection.symmetricEigenvalues3x3.json') },
  auditEvidence: [
    { path: E + '/search-batch.txt', sha256: digestPath(E + '/search-batch.txt') },
    { path: E + '/run/upstreamComparison.xml', sha256: digestPath(E + '/run/upstreamComparison.xml') },
    { path: E + '/run/smoke-selection-build.log', sha256: digestPath(E + '/run/smoke-selection-build.log') },
  ],
});
bfRow(L.byteWriter, L.byteReader - 1, 'selection.backfillCodec.ByteWriter', 'UTIL-BINARCHIVE', 'business-specific', 'class ByteWriter', ['SEL-10', 'CON-05'], 'SB', BYTE_FAMILY + ' 类壳与 raw/id128/digest32/inertia/vec3 等声明序协议方法为本族承载面；u32/f64/str 方法另有同族行。');
bfRow(L.byteReader, L.writeAxisEntry - 1, 'selection.backfillCodec.ByteReader', 'UTIL-BINARCHIVE', 'business-specific', 'class ByteReader', ['SEL-10', 'CON-05', 'NFR-DEP-04'], 'SB', BYTE_FAMILY + ' 读侧镜像（越界/版本护栏）；u32/f64/str/u32At 方法另有同族行。');
function methodRow(fnId, sym, startAnchor, endExclusiveAnchor, capFamilyDiffs) {
  const s = startAnchor; const e = endExclusiveAnchor - 1;
  backfillRows.push(mkRow({
    functionId: fnId,
    requirements: ['CON-05'],
    sources: [{ path: bfPath, symbol: sym, qualifiedSymbol: sym, startLine: s, endLine: e, sha256: digestRegion(bf, s, e) }],
    targets: ['sdurws_ird_selection'],
    capabilityId: 'UTIL-BINARCHIVE',
    decision: 'business-specific',
    search: { command: SEARCHES.SB, conclusion: SEARCH_CONCL.SB },
    differences: capFamilyDiffs,
  }));
}
// 方法行区域：方法起点到下一方法/注释块前的近似区间——以实测锚点计算
methodRow('selection.ByteWriter.u32', 'void u32(std::uint32_t v)', mw.u32w, mw.f64w - 1, 'u32 显式小端移位写（F-777）——' + BYTE_FAMILY);
methodRow('selection.ByteWriter.f64', 'void f64(double v)', mw.f64w, mw.strw - 1, 'f64 IEEE754 位模式 memcpy 后显式小端 8 字节（F-777：原 raw(&v) 主机序跨端会静默改身份）——' + BYTE_FAMILY);
methodRow('selection.ByteWriter.str', 'void str(const std::string& s)', mw.strw, L.byteReader - 1, 'string＝u32 字节长＋UTF-8，并承载 id128/digest32/inertia/vec3 声明序原语（协议冻结面）——' + BYTE_FAMILY);
methodRow('selection.ByteReader.u32', 'std::uint32_t u32()', mw.u32r, mw.f64r - 1, 'u32 显式小端移位读——' + BYTE_FAMILY);
methodRow('selection.ByteReader.f64', 'double f64()', mw.f64r, mw.strr - 1, 'f64 显式小端读重组位模式（越界抛InvalidArgument——fail-fast）——' + BYTE_FAMILY);
methodRow('selection.ByteReader.str', 'std::string str()', mw.strr, mw.u32At - 1, 'string 长读＋UTF-8，并承载 id128/digest32/inertia/vec3 读侧原语（长度界限检查保留）——' + BYTE_FAMILY);
methodRow('selection.backfillCodec.ByteReader.u32At', 'std::uint32_t u32At(std::size_t pos)', mw.u32At, L.writeAxisEntry - 1, '定偏移 u32 读（版本头探测面）——' + BYTE_FAMILY);

bfRow(L.writeAxisEntry, L.readAxisEntry - 1, 'selection.writeAxisEntry', 'NONE', 'business-specific', 'writeAxisEntry', ['CON-05'], 'SF', '轴条目编码（声明序协议面——SF 零命中）。');
bfRow(L.readAxisEntry, L.checkSynthesis - 1, 'selection.readAxisEntry', 'NONE', 'business-specific', 'readAxisEntry', ['CON-05', 'NFR-DEP-04'], 'SF', '轴条目解码（越界/尾栏护栏）。');
bfRow(L.checkSynthesis, L.parallelAxis - 1, 'selection.checkSynthesisAssertions', 'NONE', 'business-specific', 'checkSynthesisAssertions', ['MDL-06'], 'SE',
  'MDL-06①②③合成断言（质量>0/SPD/三角不等式相对 1e-12 容差）——特征值经上游求解器（MATH-EIGEN direct 行）；断言容差语义与分类轨道为产品契约，非上游面。');
bfRow(L.parallelAxis, L.addInertia - 1, 'selection.parallelAxisContribution', 'NONE', 'business-specific', 'parallelAxisContribution', ['MDL-05'], 'SF', '平行轴秩一修正 m·(|d|²E₃−d·dᵀ)（MDL-05 业务规则——SF 零命中；上游无该物性合成规则）。');
bfRow(L.addInertia, L.recalcToken - 1, 'selection.addInertia', 'NONE', 'business-specific', 'addInertia', ['MDL-16'], 'SF', '六分量张量加法（合成业务承载）。');
bfRow(L.recalcToken, L.synthesize - 1, 'selection.recalcDomainToken', 'NONE', 'business-specific', 'recalcDomainToken', ['SEL-10'], 'SF', '复算提示域令牌（AT-30 四域产品契约）。');
bfRow(L.synthesize, L.encodePayload - 1, 'selection.synthesizeAxisBodyProperties', 'NONE', 'business-specific', 'synthesizeAxisBodyProperties', ['SEL-10', 'MDL-16'], 'SF', '五步物性合成业务（输入卫生→平行轴→求和→回迁→断言——黄金双实现容差对照 sel-golden 附录 D 第 9 项；SF 零命中）。');
bfRow(L.encodePayload, L.decodePayload - 1, 'selection.encodeBackfillPayload', 'NONE', 'business-specific', 'encodeBackfillPayload', ['CON-05'], 'SF', '请求载荷编码（版本头＋条目序列——canonical 字节身份）。');
bfRow(L.decodePayload, L.encodeRecord - 1, 'selection.decodeBackfillPayload', 'NONE', 'business-specific', 'decodeBackfillPayload', ['CON-05', 'NFR-DEP-04'], 'SF', '载荷解码（外来版本/截断拒绝）。');
bfRow(L.encodeRecord, L.decodeRecord - 1, 'selection.encodeBackfillRecordObject', 'NONE', 'business-specific', 'encodeBackfillRecordObject', ['AT-30', 'CON-05'], 'SF', '记录对象编码（project 修订载荷）。');
bfRow(L.decodeRecord, L.findMotor - 1, 'selection.decodeBackfillRecordObject', 'NONE', 'business-specific', 'decodeBackfillRecordObject', ['AT-30', 'CON-05'], 'SF', '记录对象解码。');
bfRow(L.findMotor, L.findGearbox - 1, 'selection.findMotor', 'NONE', 'business-specific', 'findMotor', ['SEL-10'], 'SF', '目录包内电机查找辅助（内部 ns 块）。');
bfRow(L.findGearbox, L.mountRecordExists - 1, 'selection.findGearbox', 'NONE', 'business-specific', 'findGearbox', ['SEL-10'], 'SF', '减速器查找辅助。');
bfRow(L.mountRecordExists, L.assemble - 1, 'selection.mountRecordExists', 'NONE', 'business-specific', 'mountRecordExists', ['SEL-10'], 'SF', '安装关系存在性核对（SEL-BACKFILL-MOUNT-MISMATCH 承载）。');
bfRow(L.assemble, L.makeEnvelope - 1, 'selection.assembleDeviceBackfill', 'NONE', 'business-specific', 'assembleDeviceBackfill', ['SEL-10', 'P-SEL-7'], 'SF', '组装业务（目录值→来源装配、缺席原子失败——SEL-10 业务编排；SF 零命中）。');
bfRow(L.makeEnvelope, L.commandType - 1, 'selection.makeBackfillEnvelope', 'NONE', 'business-specific', 'makeBackfillEnvelope', ['SEL-10'], 'SF', 'project 命令信封装配（经 project 端口——零直接写权）。');
bfRow(L.commandType, L.payloadVersion - 1, 'selection.commandType', 'NONE', 'business-specific', 'commandType', ['SEL-10'], 'SF', '命令类型标识。');
bfRow(L.payloadVersion, L.planFromEnvelope - 1, 'selection.currentPayloadVersion', 'NONE', 'business-specific', 'currentPayloadVersion', ['CON-05'], 'SF', '载荷版本常量。');
bfRow(L.planFromEnvelope, L.prepare - 1, 'selection.planFromEnvelope', 'NONE', 'business-specific', 'planFromEnvelope', ['SEL-10', 'MDL-06'], 'SF', '信封→计划（解码＋合成＋断言编排——原子面）。');
bfRow(L.prepare, bfEnd, 'selection.prepare', 'NONE', 'business-specific', 'prepare', ['SEL-10'], 'SF', '壳体准备（对内核零逻辑直通转发——BackfillTest.cpp:26 注）。');

// direct 行生产消费者绑定（audit-contract §6）：checkSynthesisAssertions 为
// symmetricEigenvalues3x3 的唯一生产调用点，区域同时归属自身 business-specific 行。
{
  const d=backfillRows.find(r=>r.functionId==='selection.symmetricEigenvalues3x3');
  const s=d.sources[0];
  const csStart=L.checkSynthesis, csEnd=L.parallelAxis-1;
  d.sources.push({ path: bfPath, symbol:'checkSynthesisAssertions', qualifiedSymbol:'checkSynthesisAssertions', startLine:csStart, endLine:csEnd, sha256:digestRegion(bf,csStart,csEnd) });
}

// ============ selection/CMakeLists.txt 全行铺面 ============
const cmPath = prod + 'selection/CMakeLists.txt';
const cm = loadLines(cmPath);
const cmEnd = cm[cm.length - 1] === '' ? cm.length - 1 : cm.length;
const cL = {
  gtestBanner: findLine(cm, '# ---- gtest 接入'),
  libBanner: findLine(cm, '# ---- 产品库目标'),
  pluginBanner: findLine(cm, '# ---- 插件界面目标'),
  testBanner: findLine(cm, '# ---- 测试目标 sdurws_ird_selection_test'),
  macroBanner: findLine(cm, '# ---- ird_add_gtest：'),
  contractBanner: findLine(cm, '# ---- 契约测试目标'),
  appBanner: findLine(cm, '# ---- 开发验证 harness'),
};
const cmakeRows = [];
function cmRow(start, end, functionId, symbol, reqs, diffs, targets) {
  cmakeRows.push(mkRow({
    functionId,
    requirements: reqs,
    sources: [{ path: cmPath, symbol, qualifiedSymbol: symbol, startLine: start, endLine: end, sha256: digestRegion(cm, start, end) }],
    targets: (targets && targets.length) ? targets : ['sdurws_ird_selection'],
    capabilityId: 'NONE',
    decision: 'business-specific',
    search: { command: SEARCHES.SF, conclusion: SEARCH_CONCL.SF },
    differences: diffs,
  }));
}
cmRow(1, cL.gtestBanner - 1, 'selection.CMake.header', 'selection', ['NFR-COR-01'], '单元构建头（目标注册/依赖纪律声明——构建面产品专属）。');
cmRow(cL.gtestBanner, cL.libBanner - 1, 'selection.CMake.gtestQt', 'GTest', ['NFR-COR-01'], 'gtest/Qt6 接入（DTB §5.5 唯一机制＋R-3 Qt 唯一例外面）。', ['sdurws_ird_selection_plugin']);
cmRow(cL.libBanner, cL.pluginBanner - 1, 'selection.CMake.libTarget', 'sdurws_ird_selection', ['NFR-COR-01'], '产品库目标（REUSE-T43 增 sdurw_math PRIVATE 库边：MATH-EIGEN 直接消费——drivetrain REUSE-T12 同款条件别名分支，冒烟经 find_package(RobWork) 导出包）。', ['sdurws_ird_selection']);
cmRow(cL.pluginBanner, cL.testBanner - 1, 'selection.CMake.pluginTarget', 'sdurws_ird_selection_plugin', ['NFR-COR-01'], '插件界面目标（WP-19-T02 落位）。', ['sdurws_ird_selection_plugin']);
cmRow(cL.testBanner, cL.macroBanner - 1, 'selection.CMake.testTarget', 'sdurws_ird_selection_test', ['NFR-COR-01'], '单元测试目标。', ['sdurws_ird_selection_test']);
cmRow(cL.macroBanner, cL.contractBanner - 1, 'selection.CMake.ird_add_gtest', 'ird_add_gtest', ['NFR-COR-01'], '20 单元同形薄宏（DTB §5.5；REUSE-T32 定案 business-specific 保留——上游宏非 CONFIG 导出，不另建测试构建平台）。', ['sdurws_ird_selection_test', 'sdurws_ird_selection_contract_test']);
cmRow(cL.contractBanner, cL.appBanner - 1, 'selection.CMake.contractTarget', 'sdurws_ird_selection_contract_test', ['NFR-COR-01'], '契约测试目标。', ['sdurws_ird_selection_contract_test']);
cmRow(cL.appBanner, cmEnd, 'selection.CMake.appTarget', 'sdurws_ird_selection_app', ['NFR-COR-01'], '开发验证 harness 目标＋两面读取门禁块（LINK_LIBRARIES/INTERFACE 静态核查）。', ['sdurws_ird_selection_app']);

// ============ 其余行（CapabilityCurve/CombinationCheck——文件零变更） ============
function untouchedRow(functionId, cap, decision, reqs, skey, diffs, srcPath, symbol, s, e, targets, extra) {
  const lines = loadLines(srcPath);
  return mkRow(Object.assign({
    functionId,
    requirements: reqs,
    sources: [{ path: srcPath, symbol, qualifiedSymbol: symbol, startLine: s, endLine: e, sha256: digestRegion(lines, s, e) }],
    targets,
    capabilityId: cap,
    decision,
    search: { command: SEARCHES[skey], conclusion: SEARCH_CONCL[skey] },
    differences: diffs,
  }, extra || {}));
}
const ccPath = prod + 'selection/src/CapabilityCurve.cpp';
const comboPath = prod + 'selection/src/CombinationCheck.cpp';
const otherRows = [
  untouchedRow('selection.LinearCurveEvaluator.evaluate', 'TRAJ-LINEAR', 'business-specific', ['SEL-02', 'NFR-COR-03', 'NFR-COR-04'], 'SC',
    'TRAJ-LINEAR 候选否决（证据定案）：上游 LinearInterpolator 为 t∈[0,1] 段内插值器，不含产品 §6.2 四条插值规则（x 域闭区间判定/默认禁外推拒绝轨/端点闭区间/区间-版本-单位携带）与 §14.3 fail-fast/拒绝分轨——本函数 139 行中数值核仅 y0+t·(y1−y0) 两 flop（上游同为该式），逐段构造 LinearInterpolator 只增每查询对象构造、零重复结构可删；x 非时间量、外推禁令为产品目录语义。可复用通用段插值已在 trajectory CartesianLine（REUSE-T26）真实消费，本处不接。catalog TRAJ-LINEAR 的 selection:adapter usage 随本判定移除。',
    ccPath, 'LinearCurveEvaluator::evaluate', 52, 137, ['sdurws_ird_selection']),
  untouchedRow('selection.combinationCodec.ByteWriter.u32', 'UTIL-BINARCHIVE', 'business-specific', ['CON-05'], 'SB', 'u32 显式小端移位写（与 Backfill.cpp 同款协议、各自冻结——Backfill.cpp:109-110「复制而非共享」协议面决策）——' + BYTE_FAMILY, comboPath, 'void u32(std::uint32_t v)', 177, 183, ['sdurws_ird_selection']),
  untouchedRow('selection.combinationCodec.ByteWriter.f64', 'UTIL-BINARCHIVE', 'business-specific', ['CON-05'], 'SB', 'f64 位模式显式小端写——' + BYTE_FAMILY, comboPath, 'void f64(double v)', 190, 199, ['sdurws_ird_selection']),
  untouchedRow('selection.combinationCodec.ByteWriter.str', 'UTIL-BINARCHIVE', 'business-specific', ['CON-05'], 'SB', 'string＝u32 长＋UTF-8——' + BYTE_FAMILY, comboPath, 'void str(const std::string& s)', 201, 205, ['sdurws_ird_selection']),
  untouchedRow('selection.combinationCodec.ByteReader.u32', 'UTIL-BINARCHIVE', 'business-specific', ['CON-05'], 'SB', 'u32 小端读——' + BYTE_FAMILY, comboPath, 'std::uint32_t u32()', 273, 282, ['sdurws_ird_selection']),
  untouchedRow('selection.combinationCodec.ByteReader.f64', 'UTIL-BINARCHIVE', 'business-specific', ['CON-05'], 'SB', 'f64 小端读重组——' + BYTE_FAMILY, comboPath, 'double f64()', 290, 305, ['sdurws_ird_selection']),
  untouchedRow('selection.combinationCodec.ByteReader.str', 'UTIL-BINARCHIVE', 'business-specific', ['CON-05'], 'SB', 'string 读——' + BYTE_FAMILY, comboPath, 'std::string str()', 306, 313, ['sdurws_ird_selection']),
  untouchedRow('selection.combinationCodec.ByteReader.u32At', 'UTIL-BINARCHIVE', 'business-specific', ['CON-05'], 'SB', '定偏移 u32 读（版本探测）——' + BYTE_FAMILY, comboPath, 'std::uint32_t u32At(std::size_t pos)', 367, 372, ['sdurws_ird_selection']),
];

// ============ 保留既有 resolved 行并输出 ============
const old = JSON.parse(fs.readFileSync(ROOT + '/' + 'RobWork/doc/industrial-robot-design/reuse/functions/selection.json', 'utf8'));
const keep = old.functions.filter(r => r.status === 'migrated' && (r.functionId === 'selection.contract_test.ContractTestMain.main' || r.functionId === 'selection.test.TestMainReport.main'));
if (keep.length !== 2) throw new Error('resolved anchors not found: ' + keep.length);

// 保留行的 inputs 绑定按当前文件刷新（本批 CMakeLists 变更使旧摘要过期）
for (const r of keep) {
  for (const i of (r.inputs||[])) { i.sha256 = digestPath(i.path); }
  for (const b of (r.auditEvidence||[])) { b.sha256 = digestPath(b.path); }
  if (r.evidence && r.evidence.path) r.evidence.sha256 = digestPath(r.evidence.path);
}

const out = { schemaVersion: old.schemaVersion, unit: old.unit, functions: [...backfillRows, ...cmakeRows, ...otherRows, ...keep] };
fs.writeFileSync(ROOT + '/' + 'RobWork/doc/industrial-robot-design/reuse/functions/selection.json', JSON.stringify(out, null, 1) + '\n');
console.log('selection.json written:', out.functions.length, 'rows');
