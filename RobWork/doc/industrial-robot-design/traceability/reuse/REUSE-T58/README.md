# REUSE-T58 — RC-07 字节基线捕获（三文件字节原语·旧实现·52 条绑定面）

任务契约：[tasks/REUSE-T58.json](../../../tasks/REUSE-T58.json)｜变更凭证：[reuse/changes/REUSE-T58.json](../../changes/REUSE-T58.json)
权威计划：[reuse/migration.md §13.5.4 RC-07](../../reuse/migration.md)（:423，字节三件套之②——顺序敏感，必须在换核前捕获；①渠道批准已获＝byte-primitives-decision.md §6；③逐位探针＋断开红转绿属换核批）

起始 SHA：`e428cd0cf28f0927602fbeaf2db764d8deb7194c`（redesign-main）。零 .cpp/.hpp/CMake 实现改动；catalog/functions 绑定零改动（52 行仍 pending——本批只捕获基线，不作语义处置）。

## 1. 捕获对象与绑定面

以 `functions/{execution,runtime,policy}.json` 反查的 `UTIL-BINARCHIVE`（decision/status 均为 pending）绑定面为准（2026-10-28 实测）：

| 文件 | 绑定行数 | 代表符号（行号区间见 functions JSON） |
| --- | --- | --- |
| execution/src/ChannelProtocol.cpp | 36 | putU16/putU32/putU64/getU16/getU32/getU64（:22-63）、putStr/getStr/putBytes/getBytes（:65-109）、digestToHex、encodeIdentityBlock/decodeIdentityBlock、encodeFrame、FrameAssembler::feed、FrameSequencer（4 函数）、isFragmentableType/fragmentMessage、MessageReassembler::accept、manifestDigestHex/computeContentIdentity/contentIdentityMatches、八对载荷编解码（:573-818） |
| runtime/src/Codec.cpp | 10 | Writer::u16/u32/u64/f64/str（:65-107）、Reader::u16/u32/u64/f64/str（:150-201） |
| policy/src/PolicyInput.cpp | 6 | ByteWriter::u32/f64/lengthPrefixedText（:88-120）、ByteReader::u32/f64/text（:168-220） |

三文件原语均为匿名命名空间内部件——捕获一律经**真实公共入口**驱动（不复制原语实现）：
- execution：encodeIdentityBlock/decodeIdentityBlock、encodeFrame、八对 encode*/decode* 载荷编解码、fragmentMessage、MessageReassembler::accept、FrameAssembler::feed、FrameSequencer::accept/gapTimedOut（时钟注入固定纪元）、manifestDigestHex、computeContentIdentity、contentIdentityMatches；
- runtime：rtcodec::encode / encodeIdentityDomain / computeContentIdentity / parse（minimal＋rich 两模型）/ encodeNameMap / parseNameMap / computeNameMapContentIdentity（条目经 RuntimeNameMap::fromValidatedEntries 公共恢复路径）；
- policy：PolicyCodec::encode / decode / encodeSemanticProjection / contentIdentity / canonicalF64 / parseCanonicalF64。

捕获源＝主构建树 Release 静态库（`build/RobWorkStudio/libs/Release`，构建 2026-10-27 晚于三源文件最后修改 2026-09-18/10-20/10-12——现网实现核对实测）；harness 链接 `sdurws_ird_{execution,runtime,policy,core}`＋`sdurw_{math,common,core}`。

## 2. golden 落盘（三处）

| 资产 | 相对路径（testdata/golden/） | 字节 | sha256（前 16） |
| --- | --- | --- | --- |
| exec manifest | exec-channel-protocol-fixture/1.0.0/manifest.json | 4644 | `406531c16e5e1618…` |
| exec inputs | exec-channel-protocol-fixture/1.0.0/inputs/exec-channel-protocol-fixture-inputs.json | 8209 | `61049018baf627f3…` |
| exec expected | exec-channel-protocol-fixture/1.0.0/expected/exec-channel-protocol-fixture-output.json | 12020 | `83a43481e95ff89a…` |
| rt manifest | rt-codec-fixture/1.0.0/manifest.json | 4216 | `9f6b1b937a3c8c42…` |
| rt inputs | rt-codec-fixture/1.0.0/inputs/rt-codec-fixture-inputs.json | 2736 | `e2c450ee5fddb5e0…` |
| rt expected | rt-codec-fixture/1.0.0/expected/rt-codec-fixture-output.json | 21399 | `3dcce2ffa8e51f6d…` |
| pol manifest（增量） | pol-policy-codec-fixture/1.0.0/manifest.json | 5218 | `9c7250629b493081…` |
| pol inputs（追加） | pol-policy-codec-fixture/1.0.0/inputs/policy-input-rc07-baseline.json | 5475 | `f16ed63bde52d316…` |
| pol expected（追加） | pol-policy-codec-fixture/1.0.0/expected/policy-output-rc07-baseline.json | 3901 | `d83846234140243f…` |

- 家族与 kind：`exec-channel-protocol-fixture/1.0.0`、`rt-codec-fixture/1.0.0` 均为**新建**、kind=regression（referenceSource=production-snapshot、independentOfProductionImpl=false——期望字节是生产实现快照，回归身份载体、不可作正确性依据；语义等价由渠道批准与既有审计另证）；`pol-policy-codec-fixture/1.0.0` 为**既有家族纯追加**（kind 维持 contract-fixture，既有 4 资产字节零改动、integrity 原值保留，manifest 仅追加 inputs/expected/integrity/parameters.rc07ByteBaseline/history）。
- datasetId 沿既有命名风格（`pol-policy-codec-fixture` 先例）；schema=ird-golden-manifest/1；inputs 侧为用例语义描述符（caseId 与 expected 逐条对应）。
- 边界面覆盖（ask 要求逐项）：常规值、边界值、空载荷（Heartbeat 帧/ResultBatch 空字节段/空消息分片/空字节内容身份）、定宽溢出边界（u16 0xFFFF、u32 0xFFFFFFFF、u64 2^64-1、attempt 2^64-1 填满身份块 24B attempt 域——encode 成功而 decode 按"填满无终止 NUL"编码纪律拒绝，为现网既实现行为，如实捕获不修正）、浮点位模式（0.0/-0.0/1/-1/0.5/0.1/π/DBL_MIN/次正规 5e-324/DBL_MAX/1e300/-2.5/4π/0.01/2.5）、decode 读路径（8 个 decode* 往返 bytesEqual 全 true＋6 例截断/长度域越界拒绝——getStr/getBytes 失败分支）。

## 3. 确定性与校验实测（本批真实执行记录）

1. **确定性双跑**（ask 指定：同输入重跑两次逐字节一致）：
   ```
   rc07_capture.exe out1 ; rc07_capture.exe out2 ; diff -r out1 out2
   → DETERMINISM-PASS（零差异，三轮构建/迭代均复跑）
   ```
2. **Test-DeclaredGoldenJson**（ask 指定；golden-assets.ps1 同一实现、真实树 stub，check_golden.ps1）：
   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File check_golden.ps1
   → 12/12 passed（新 2 家族 manifest/inputs/expected×2＋pol 增量 3 处＋pol 既有 4 处回归复核）
   ```
3. **C++ 装载全量链**（testkit::GoldenDataset::load＝解析→schema→完整性→交叉校验，rc07_dataset_check.exe）：
   ```
   PASS: exec-channel-protocol-fixture@1.0.0 kind=regression inputs=1 expected=1 integrity=2
   PASS: rt-codec-fixture@1.0.0 kind=regression inputs=1 expected=1 integrity=2
   PASS: pol-policy-codec-fixture@1.0.0 kind=contract-fixture inputs=3 expected=2 integrity=6
   CPP-DATASET-CHECK-PASS
   ```
4. **既有消费者回归**（pol 增量不破坏）：
   ```
   cmake --build build --config Release --target sdurws_ird_policy_test
   sdurws_ird_policy_contract_test.exe --gtest_filter='*Codec*Fixture*'
   → 5 tests from PolPolicyCodecFixture PASSED
   ```
5. **健全性核对**（捕获值对照已知编码事实）：
   - 帧/编码头逐字节：IRDCHN1 帧头 `49524443484e31 0100 00 00 <seq LE>`（小端——与 IRDCANO `49524443414e4f 0001 0003 0000` 大端、IRDNAME、IRDPOL1 相互区分）；
   - f64 位模式对照标准值：0→`0000000000000000`、-0→`8000000000000000`、0.1→`3fb999999999999a`、DBL_MAX→`7fefffffffffffff`、5e-324→`0000000000000001`，15 值 parse 位往返全 true；
   - 空字节 SHA-256＝`e3b0c442…`（标准空串摘要——computeContentIdentity(空) 与 node:crypto 一致）；
   - 行为面：fragmentMulti flags=1,1,0、reassemble payload=`a0a1a2a3a4a5` flags=0、assembler 三段喂入 roundtrip equal、sequencer 1→Delivered/3→Buffered/2→Delivered[2,3]（首帧即基线——序控不依赖发送方从 1 起）、gap-timeout 999ms false/1000ms true/failed true（注入时钟）；
   - 舍入面：pol-full-normal roundtrip valueEqual=false 为现网既实现行为（未规范化输入编码侧规范化——承载序比较不等，契约原文"未规范化输入的往返以规范化后相等成立"），字节基线以 encode 输出 hex 为准。
6. **读侧覆盖核对**（getStr/getBytes 绑定的全部调用点在 decode* 体内——独验核对面）：8 个 decode*（decodeHello/HelloAck/Progress/ResultBatch/CheckpointBatch/DispatchRequest/FinalOutput/ErrorReport）全部实际执行且 decode(encode(x)) 再编码逐字节相等（bytesEqual 全 true）＋6 例截断/长度域越界拒绝边界（:586/:613/:679/:718/:805 定宽不足＋getStr :81/getBytes :103 长度域越出剩余字节）——functions/execution.json 36 条绑定无一遗漏；
7. **rt 身份域严格重演**：identityMatchesIdentityDomainDigest（minimal/rich）＝SHA-256(encodeIdentityDomain(model)) 与 computeContentIdentity 逐一相等（core ContentDigester 同源重算——§4.3.5）、identityMatchesEncodedDigest（namemap）＝SHA-256(IRDNAME 编码自身) 与 computeNameMapContentIdentity 相等（§7.6）——三布尔捕获值均 true。

## 4. 复核命令（换核批与独验复用）

harness 重建→捕获→与入库 expected 逐字节比对（完整命令表＝下方代码块＋两新家族 manifest generator.invocation 内联登记；generate/ 无落盘文件——避免新产品树文件触发复用门禁 UNDECLARED，manifest 声明 committed=false 与之自洽）：

```bash
# 1) 构建（批处理）: .zcode/workflow-drafts/rc07-baseline/build_capture.bat
# 2) 运行两次 + 一致性
PATH="$PWD/build/RobWork/bin/Release:$PWD/vcpkg/installed/x64-windows/bin:$PATH"   .zcode/workflow-drafts/rc07-baseline/rc07_capture.exe out-verify
cmp <(node -e "process.stdout.write(require('fs').readFileSync('out-verify/exec-channel-protocol-captured.json'))")     RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/exec-channel-protocol-fixture/1.0.0/expected/exec-channel-protocol-fixture-output.json
# （rt/policy 同法比对 rt-codec-captured.json→rt-codec-fixture-output.json、
#   policy-codec-captured.json→policy-output-rc07-baseline.json）
```

换核批接续清单（三件套③前置盘点）：
1. 渠道落地后同命令重跑三捕获，与入库 expected **逐位探针**（字节不一致处逐条归因：渠道行为差异 vs 捕获误差）；
2. 断开旧原语（红）→ 渠道实现（绿）——红转绿以本基线为 oracle；
3. ReportCodec.cpp 64 行（reporting）缺：新建 golden 家族（report-* 命名风格）＋State 借用面解阻（RC-09）＋同法捕获（其 State 借用使 harness 需构造 reporting 输入对象，工作量最大——故留换核批前置追加，本批不动）；
4. runtime/src/Snapshot.cpp 8 条字节行：待 T56 裁决（受控视图/任务私有 State 转发形态）后另批追加捕获，本批不代裁；
5. toleranceProfile 说明：schema 必填字段——execution 域无既有档案，exec 家族引用 ev-slice（零容差字节等值语义最接近）并在 parameters 明示"字节等值断言不经容差通道"；rt 家族引用 rt-runtime（runtime 域既有档案，rt-namemap-roundtrip 先例）。如所有者要求专属档案，随换核批在 testdata/tolerance/ 另立。

## 5. 未触面（如实登记）

- functions 52 行保持 pending（本批只捕获，不做 decision/status 处置——RC-07 完成判据的语义处置随换核批逐位探针后进行）；
- ReportCodec.cpp 64 行、Snapshot.cpp 8 行（见 §4）；
- catalog.json/permissions.json/closures.json 零改动；
- harness 为会话工具（.zcode/workflow-drafts/rc07-baseline/，git 忽略目录）不入库——可复现性由本 README 附录 A 源码全文＋§4 重建命令＋两家族 manifest generator.invocation 内联命令保证（golden 家族 generate/ 不落盘、不引用——manifest committed=false 自洽，防 UNDECLARED 误伤）；
- NOT RUN：独立验收（待所有者安排）、逐位探针＋红转绿（换核批）、commit-lint/verify-task-scope（随提交批显式 -Base e428cd0cf -Head <送验SHA>）。

## 附录 A — 捕获 harness 源码（rc07_capture.cpp 全文）

```cpp
/**
 * @file   rc07_capture.cpp
 * @brief  RC-07 字节基线捕获 harness（会话工具——.zcode 工作草稿，不入库）。
 *
 * 目的：对三文件字节原语（execution/src/ChannelProtocol.cpp 36 行、
 *   runtime/src/Codec.cpp 10 行、policy/src/PolicyInput.cpp 6 行——functions
 *   JSON UTIL-BINARCHIVE pending 绑定）经真实公共入口捕获期望字节。
 *
 * 捕获纪律（REUSE-T58）：
 *   - 只消费产品 STATIC 库（sdurws_ird_execution/runtime/policy/core——
 *     主构建树 build/RobWorkStudio/libs/Release，构建晚于源文件）；
 *   - 输出纯确定性：无时钟/环境/地址量；进度/序控时钟全部注入固定点；
 *   - 确定性验证：同输出目录参数连跑两次逐字节一致（run-diff）。
 *
 * 用法：rc07_capture.exe <outDir>   （写出 exec/rt/pol 三个 JSON）
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>

#include <sdurws/ird/execution/ChannelProtocol.hpp>
#include <sdurws/ird/execution/Errors.hpp>

#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Codec.hpp>
#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/runtime/NameMap.hpp>
#include <sdurws/ird/runtime/Resource.hpp>

#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

namespace fs = std::filesystem;
namespace core = sdurws::ird::core;
using core::ContentDigester;
using core::Digest256;

// =====================================================================
// 通用助手（确定性：id/摘要全部由固定种子经 SHA-256 派生——同
// runtime/test/CanonicalModelFixture.hpp 口径，非产品路径）。
// =====================================================================

static Digest256 digestOf(const std::string& seed)
{
    ContentDigester d;
    d.update(seed.data(), seed.size());
    return d.finalize();
}

template <typename Id>
static Id idFrom(const std::string& seed)
{
    const Digest256 d = digestOf(seed);
    Id id;
    std::memcpy(id.bytes.data(), d.data(), 16);
    return id;
}

static std::string hex(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char kH[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (std::uint8_t b : bytes) {
        out.push_back(kH[b >> 4]);
        out.push_back(kH[b & 0x0F]);
    }
    return out;
}

static std::string hexDigest(const Digest256& d) { return hex({d.begin(), d.end()}); }

static std::string jstr(const std::string& s)  // 简易 JSON 串转义（ASCII 输入）
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { out.push_back('\\'); }
        if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

static std::vector<std::uint8_t> bytePattern(std::initializer_list<std::uint8_t> init)
{
    return std::vector<std::uint8_t>(init);
}

/// 256 字节 0x00..0xFF 全值域模式（浮点/定宽位模式的承载样本）。
static std::vector<std::uint8_t> fullBytePattern()
{
    std::vector<std::uint8_t> v(256);
    for (int i = 0; i < 256; ++i) { v[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i); }
    return v;
}

static core::TaskIdentity fixedIdentity(std::uint64_t attempt)
{
    core::TaskIdentity id;
    id.project = idFrom<core::ProjectId>("prj-A");
    id.branch = idFrom<core::BranchId>("brn-A");
    id.revision = idFrom<core::RevisionId>("rev-1");
    id.run = idFrom<core::RunId>("run-1");
    id.attempt = core::AttemptId{attempt};
    return id;
}

// =====================================================================
// EXEC——ChannelProtocol.cpp 字节面（36 条 UTIL-BINARCHIVE 绑定经公共入口）。
// =====================================================================

static std::string captureExec()
{
    using namespace sdurws::ird::execution;
    namespace core = sdurws::ird::core;
    std::ostringstream o;
    o << "{\"unit\":\"execution\",\"file\":\"execution/src/ChannelProtocol.cpp\","
      << "\"capturedVia\":[\"encodeIdentityBlock\",\"decodeIdentityBlock\",\"encodeFrame\","
      << "\"encodeHello/decodeHello\",\"encodeHelloAck/decodeHelloAck\","
      << "\"encodeProgress/decodeProgress\",\"encodeResultBatch/decodeResultBatch\","
      << "\"encodeCheckpointBatch/decodeCheckpointBatch\","
      << "\"encodeDispatchRequest/decodeDispatchRequest\",\"encodeFinalOutput/decodeFinalOutput\","
      << "\"encodeErrorReport/decodeErrorReport\",\"fragmentMessage\",\"MessageReassembler::accept\","
      << "\"FrameAssembler::feed\",\"FrameSequencer::accept\",\"manifestDigestHex\","
      << "\"computeContentIdentity\",\"contentIdentityMatches\"],\"cases\":[";

    const core::TaskIdentity id1 = fixedIdentity(1);
    const core::TaskIdentity idMax = fixedIdentity(0xFFFFFFFFFFFFFFFFull);

    // ---- 身份块（encodeIdentityBlock/decodeIdentityBlock）----
    {
        std::array<std::uint8_t, kIdentityBlockSize> block{};
        encodeIdentityBlock(id1, block);
        o << "{\"caseId\":\"id-block-normal\",\"hex\":\"" << hex({block.begin(), block.end()})
          << "\",\"decodeRoundtrip\":" << (decodeIdentityBlock(block.data(), block.size()).has_value() ? "true" : "false") << "}";
    }
    {
        std::array<std::uint8_t, kIdentityBlockSize> block{};
        encodeIdentityBlock(idMax, block);
        o << ",{\"caseId\":\"id-block-attempt-max\",\"hex\":\"" << hex({block.begin(), block.end()})
          << "\",\"decodeRoundtrip\":" << (decodeIdentityBlock(block.data(), block.size()).has_value() ? "true" : "false") << "}";
    }

    // ---- 帧编码（encodeFrame：帧头 23B＋身份块 168B＋载荷）----
    const std::string digestHex = manifestDigestHex("evaluator|1|1\nworkers|1|1\n");

    auto frameBytes = [](ChannelFrame f) {
        return hex(encodeFrame(f));
    };

    {
        ChannelFrame f;
        f.type = ChannelMsgType::Hello;
        f.seq = 1;
        f.identity = id1;
        f.payload = encodeHello({kChannelProtocolVersion, digestHex});
        o << ",{\"caseId\":\"frame-hello-normal\",\"hex\":\"" << frameBytes(std::move(f)) << "\"}";
    }
    {
        ChannelFrame f;
        f.type = ChannelMsgType::Heartbeat;   // 空载荷类型（无载荷编解码对）
        f.seq = 2;
        f.identity = id1;
        o << ",{\"caseId\":\"frame-empty-payload\",\"hex\":\"" << frameBytes(std::move(f)) << "\"}";
    }
    {
        ChannelFrame f;
        f.type = ChannelMsgType::ResultBatch;
        f.seq = 0xFFFFFFFFFFFFFFFFull;        // u64 定宽上边界
        f.identity = id1;
        f.payload = encodeResultBatch({0xFFFFFFFFFFFFFFFFull, bytePattern({0x00, 0xFF, 0x7F, 0x80})});
        o << ",{\"caseId\":\"frame-seq-max\",\"hex\":\"" << frameBytes(std::move(f)) << "\"}";
    }
    {
        ChannelFrame f;
        f.type = ChannelMsgType::DispatchRequest;
        f.flags = kFlagFragmentMore;          // flags 保留位面
        f.seq = 3;
        f.identity = id1;
        f.payload = bytePattern({0xDE, 0xAD, 0xBE, 0xEF});
        o << ",{\"caseId\":\"frame-flags-more\",\"hex\":\"" << frameBytes(std::move(f)) << "\"}";
    }

    // ---- 各载荷编解码（putU16/U32/U64/getStr/getBytes 全值域面）----
    o << ",{\"caseId\":\"hello-normal\",\"hex\":\""
      << hex(encodeHello({1, digestHex})) << "\"}";
    o << ",{\"caseId\":\"hello-empty-digest\",\"hex\":\""
      << hex(encodeHello({1, std::string{}})) << "\"}";
    o << ",{\"caseId\":\"hello-version-max\",\"hex\":\""
      << hex(encodeHello({0xFFFF, std::string("ab")})) << "\"}";

    o << ",{\"caseId\":\"helloack-accept\",\"hex\":\""
      << hex(encodeHelloAck({true, 5000, std::string{}})) << "\"}";
    o << ",{\"caseId\":\"helloack-reject-max\",\"hex\":\""
      << hex(encodeHelloAck({false, 0xFFFFFFFFu, std::string("evaluator manifest digest mismatch")})) << "\"}";

    o << ",{\"caseId\":\"progress-zero\",\"hex\":\""
      << hex(encodeProgress(ProgressReport::make(0, "compile", 0, 0))) << "\"}";
    o << ",{\"caseId\":\"progress-max\",\"hex\":\""
      << hex(encodeProgress(ProgressReport::make(100, "optimize",
                                                 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull))) << "\"}";

    o << ",{\"caseId\":\"resultbatch-empty\",\"hex\":\""
      << hex(encodeResultBatch({1, {}})) << "\"}";
    o << ",{\"caseId\":\"resultbatch-max\",\"hex\":\""
      << hex(encodeResultBatch({0xFFFFFFFFFFFFFFFFull, bytePattern({0x00, 0xFF})})) << "\"}";

    o << ",{\"caseId\":\"checkpointbatch-normal\",\"hex\":\""
      << hex(encodeCheckpointBatch({1, bytePattern({0xDE, 0xAD, 0xBE, 0xEF})})) << "\"}";
    o << ",{\"caseId\":\"checkpointbatch-empty\",\"hex\":\""
      << hex(encodeCheckpointBatch({0xFFFFFFFFFFFFFFFFull, {}})) << "\"}";

    // DispatchRequest/FinalOutput——ContentIdentity 身份＋双字节段
    const core::ContentIdentity cidA = computeContentIdentity(bytePattern({0x01, 0x02, 0x03}));
    const core::ContentIdentity cidEmpty = computeContentIdentity({});
    {
        DispatchRequestPayload p;
        p.snapshotIdentity = cidA;
        p.modelIdentity = cidEmpty;
        p.snapshotBytes = fullBytePattern();
        p.modelBytes = {};
        o << ",{\"caseId\":\"dispatchrequest-normal\",\"hex\":\"" << hex(encodeDispatchRequest(p)) << "\"}";
    }
    {
        FinalOutputPayload p;
        p.snapshotIdentity = cidEmpty;
        p.modelIdentity = cidA;
        p.outputCanon = bytePattern({0x00, 0xFF, 0x00, 0xFF});
        o << ",{\"caseId\":\"finaloutput-normal\",\"hex\":\"" << hex(encodeFinalOutput(p)) << "\"}";
    }

    o << ",{\"caseId\":\"errorreport-dev\",\"hex\":\""
      << hex(encodeErrorReport({true, std::string("dev-channel"), std::string("assert failed")})) << "\"}";
    o << ",{\"caseId\":\"errorreport-stable\",\"hex\":\""
      << hex(encodeErrorReport({false, std::string("EX-CHANNEL-PROTOCOL-ERROR"),
                                std::string("magic mismatch")})) << "\"}";

    // ---- decode* 读路径（getStr/getBytes 绑定的全部调用点在 decode 体内——
    // 换核后读侧字节 oracle：decode(encode(x)) 再编码逐字节相等＋截断边界）----
    auto decodeCase = [&](const char* caseId, const std::vector<std::uint8_t>& wire,
                          auto decodeFn, auto encodeFn) {
        const auto back = decodeFn(wire);
        o << ",{\"caseId\":\"" << caseId << "\",\"decodeOk\":"
          << (back.has_value() ? "true" : "false");
        if (back.has_value()) {
            const std::vector<std::uint8_t> re = encodeFn(*back);
            o << ",\"reencodedHex\":\"" << hex(re)
              << "\",\"bytesEqual\":" << (re == wire ? "true" : "false");
        }
        o << "}";
    };
    {
        const std::vector<std::uint8_t> wire = encodeHello({1, digestHex});
        decodeCase("decode-hello", wire,
                   [](const std::vector<std::uint8_t>& b) { return decodeHello(b); },
                   [](const HelloPayload& p) { return encodeHello(p); });
        const std::vector<std::uint8_t> wire2 = encodeHelloAck({false, 0xFFFFFFFFu, std::string("evaluator manifest digest mismatch")});
        decodeCase("decode-helloack", wire2,
                   [](const std::vector<std::uint8_t>& b) { return decodeHelloAck(b); },
                   [](const HelloAckPayload& p) { return encodeHelloAck(p); });
        const std::vector<std::uint8_t> wire3 = encodeProgress(ProgressReport::make(42, "compile", 3, 8));
        decodeCase("decode-progress", wire3,
                   [](const std::vector<std::uint8_t>& b) { return decodeProgress(b); },
                   [](const ProgressReport& p) { return encodeProgress(p); });
        const std::vector<std::uint8_t> wire4 = encodeResultBatch({0xFFFFFFFFFFFFFFFFull, bytePattern({0x00, 0xFF})});
        decodeCase("decode-resultbatch", wire4,
                   [](const std::vector<std::uint8_t>& b) { return decodeResultBatch(b); },
                   [](const ResultBatchPayload& p) { return encodeResultBatch(p); });
        const std::vector<std::uint8_t> wire5 = encodeCheckpointBatch({1, bytePattern({0xDE, 0xAD, 0xBE, 0xEF})});
        decodeCase("decode-checkpointbatch", wire5,
                   [](const std::vector<std::uint8_t>& b) { return decodeCheckpointBatch(b); },
                   [](const CheckpointBatchPayload& p) { return encodeCheckpointBatch(p); });
    }
    {
        DispatchRequestPayload p;
        p.snapshotIdentity = cidA;
        p.modelIdentity = cidEmpty;
        p.snapshotBytes = fullBytePattern();
        p.modelBytes = {};
        const std::vector<std::uint8_t> wire = encodeDispatchRequest(p);
        decodeCase("decode-dispatchrequest", wire,
                   [](const std::vector<std::uint8_t>& b) { return decodeDispatchRequest(b); },
                   [](const DispatchRequestPayload& q) { return encodeDispatchRequest(q); });
    }
    {
        FinalOutputPayload p;
        p.snapshotIdentity = cidEmpty;
        p.modelIdentity = cidA;
        p.outputCanon = bytePattern({0x00, 0xFF, 0x00, 0xFF});
        const std::vector<std::uint8_t> wire = encodeFinalOutput(p);
        decodeCase("decode-finaloutput", wire,
                   [](const std::vector<std::uint8_t>& b) { return decodeFinalOutput(b); },
                   [](const FinalOutputPayload& q) { return encodeFinalOutput(q); });
    }
    {
        const std::vector<std::uint8_t> wire = encodeErrorReport({true, std::string("dev-channel"), std::string("assert failed")});
        decodeCase("decode-errorreport", wire,
                   [](const std::vector<std::uint8_t>& b) { return decodeErrorReport(b); },
                   [](const ErrorReportPayload& q) { return encodeErrorReport(q); });
    }
    // 截断/长度域越界边界（getStr/getBytes :72-87/:95-109 的失败分支面）
    o << ",{\"caseId\":\"decode-hello-empty\",\"decodeOk\":"
      << (decodeHello({}).has_value() ? "true" : "false") << "}";                       // end-cursor<2（:586）
    o << ",{\"caseId\":\"decode-helloack-short\",\"decodeOk\":"
      << (decodeHelloAck(bytePattern({1, 0x00, 0x00, 0x00})).has_value() ? "true" : "false") << "}";  // <5B（:613）
    o << ",{\"caseId\":\"decode-progress-short\",\"decodeOk\":"
      << (decodeProgress(bytePattern({42, 0x00, 0x00})).has_value() ? "true" : "false") << "}";       // <4B（:679）
    o << ",{\"caseId\":\"decode-resultbatch-short\",\"decodeOk\":"
      << (decodeResultBatch(bytePattern({1, 2, 3, 4, 5, 6, 7})).has_value() ? "true" : "false") << "}"; // <8B（:718）
    o << ",{\"caseId\":\"decode-errorreport-empty\",\"decodeOk\":"
      << (decodeErrorReport({}).has_value() ? "true" : "false") << "}";                  // <1B（:805）
    {   // 长度域声明越出剩余字节——getStr :81 / getBytes :103 的上限分支
        std::vector<std::uint8_t> bad = {0x01, 0x00};                     // protoVersion=1
        bad.push_back(0xFF); bad.push_back(0xFF); bad.push_back(0xFF); bad.push_back(0xFF); // len=0xFFFFFFFF > 剩余
        o << ",{\"caseId\":\"decode-hello-len-overflow\",\"decodeOk\":"
          << (decodeHello(bad).has_value() ? "true" : "false") << "}";
    }

    // ---- 摘要与身份 helper ----
    o << ",{\"caseId\":\"manifest-digest\",\"digestHex\":" << jstr(digestHex) << "}";
    o << ",{\"caseId\":\"content-identity-empty\",\"digestHex\":\"" << hexDigest(cidEmpty.bytes) << "\"}";
    o << ",{\"caseId\":\"content-identity-fullpattern\",\"digestHex\":\""
      << hexDigest(cidA.bytes) << "\"}";
    o << ",{\"caseId\":\"content-identity-match\",\"matchTrue\":"
      << (contentIdentityMatches(bytePattern({0x01, 0x02, 0x03}), cidA) ? "true" : "false")
      << ",\"matchFalse\":"
      << (contentIdentityMatches(bytePattern({0x01, 0x02}), cidA) ? "true" : "false") << "}";

    // ---- 分帧续传（fragmentMessage/MessageReassembler）----
    auto fragCase = [&](const char* caseId, ChannelMsgType type,
                        const std::vector<std::uint8_t>& message, std::size_t fragSize) {
        std::vector<ChannelFrame> frags;
        std::uint64_t seq = 10;
        const bool ok = fragmentMessage(type, id1, seq, message, fragSize, frags);
        o << ",{\"caseId\":\"" << caseId << "\",\"accepted\":" << (ok ? "true" : "false")
          << ",\"nextSeq\":" << seq << ",\"frames\":[";
        for (std::size_t i = 0; i < frags.size(); ++i) {
            if (i) { o << ","; }
            o << "{\"seq\":" << frags[i].seq << ",\"flags\":" << static_cast<unsigned>(frags[i].flags)
              << ",\"hex\":\"" << hex(encodeFrame(frags[i])) << "\"}";
        }
        o << "]}";
    };
    fragCase("fragment-single", ChannelMsgType::DispatchRequest, bytePattern({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}), 16);
    fragCase("fragment-multi", ChannelMsgType::DispatchRequest, bytePattern({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}), 4);
    fragCase("fragment-empty-message", ChannelMsgType::FinalOutput, {}, 4);
    fragCase("fragment-type-reject", ChannelMsgType::Heartbeat, bytePattern({1}), 4);

    {
        // 重组：两片按序入 → 完整消息（收侧重组器字节面）
        std::vector<ChannelFrame> frags;
        std::uint64_t seq = 100;
        const std::vector<std::uint8_t> message = bytePattern({0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5});
        (void)fragmentMessage(ChannelMsgType::FinalOutput, id1, seq, message, 4, frags);
        MessageReassembler re;
        std::vector<ChannelFrame> done;
        for (auto& f : frags) {
            auto out = re.accept(std::move(f));
            done.insert(done.end(), std::make_move_iterator(out.begin()), std::make_move_iterator(out.end()));
        }
        o << ",{\"caseId\":\"reassemble-two-frag\",\"completedCount\":" << done.size();
        if (!done.empty()) {
            o << ",\"payloadHex\":\"" << hex(done.front().payload)
              << "\",\"flags\":" << static_cast<unsigned>(done.front().flags);
        }
        o << "}";
    }

    {
        // 流装配：单帧字节按 7/100/其余 三段喂入（自定界增量解析）
        ChannelFrame f;
        f.type = ChannelMsgType::Progress;
        f.seq = 7;
        f.identity = id1;
        f.payload = encodeProgress(ProgressReport::make(42, "compile", 3, 8));
        const std::vector<std::uint8_t> wire = encodeFrame(f);
        FrameAssembler asm_;
        std::vector<ChannelFrame> got;
        std::size_t off = 0;
        const std::size_t cuts[3] = {7, 107, wire.size()};
        for (const std::size_t c : cuts) {
            const std::size_t n = (c > wire.size() ? wire.size() : c) - off;
            (void)asm_.feed(wire.data() + off, n, got);
            off += n;
            if (off >= wire.size()) { break; }
        }
        o << ",{\"caseId\":\"assembler-incremental\",\"frameCount\":" << got.size();
        if (!got.empty()) {
            o << ",\"roundtripHex\":\"" << hex(encodeFrame(got.front()))
              << "\",\"equalOriginal\":" << (encodeFrame(got.front()) == wire ? "true" : "false");
        }
        o << "}";
    }

    {
        // seq 序控：基线 1 → 乱序 3 入窗口 → 补缺 2 连带吐出（确定性行为面；
        // 首帧 seq 即基线——序控不依赖发送方从 1 起的约定）
        FrameSequencer::Config cfg;
        cfg.windowCapacity = 4;
        FrameSequencer seqr(cfg, nullptr);
        auto mk = [&](std::uint64_t s) {
            ChannelFrame f;
            f.type = ChannelMsgType::ResultBatch;
            f.seq = s;
            f.identity = id1;
            f.payload = encodeResultBatch({s, {}});
            return f;
        };
        auto seqsOf = [](const std::vector<ChannelFrame>& v) {
            std::string s = "[";
            for (std::size_t i = 0; i < v.size(); ++i) {
                if (i) { s += ","; }
                s += std::to_string(v[i].seq);
            }
            return s + "]";
        };
        auto outcomeName = [](FrameSequencer::Outcome oc) {
            switch (oc) {
            case FrameSequencer::Outcome::Delivered: return "Delivered";
            case FrameSequencer::Outcome::Buffered: return "Buffered";
            case FrameSequencer::Outcome::DuplicateDropped: return "DuplicateDropped";
            case FrameSequencer::Outcome::WindowOverflow: return "WindowOverflow";
            }
            return "?";
        };
        FrameSequencer::Outcome o1 = FrameSequencer::Outcome::Delivered;
        auto d1 = seqr.accept(mk(1), &o1);          // 基线＝1，直接交付
        FrameSequencer::Outcome o2 = FrameSequencer::Outcome::Delivered;
        auto d2 = seqr.accept(mk(3), &o2);          // 越前 1 → 入窗口缓冲
        FrameSequencer::Outcome o3 = FrameSequencer::Outcome::Delivered;
        auto d3 = seqr.accept(mk(2), &o3);          // 补缺 → 连带吐 2,3
        o << ",{\"caseId\":\"sequencer-reorder\",\"outcome1\":\"" << outcomeName(o1)
          << "\",\"outcome2\":\"" << outcomeName(o2) << "\",\"outcome3\":\"" << outcomeName(o3)
          << "\",\"delivered1\":" << seqsOf(d1) << ",\"delivered2\":" << seqsOf(d2)
          << ",\"delivered3\":" << seqsOf(d3) << "}";
    }
    {
        // seq 序控：注入时钟下的缺口断裂（gapTimedOut 确定性；缺口＝基线后越前）
        using namespace std::chrono;
        FrameSequencer::Config cfg;
        cfg.windowCapacity = 4;
        cfg.gapTimeout = milliseconds{1000};
        steady_clock::time_point t0{};  // 固定纪元——无真实时钟
        steady_clock::time_point now = t0;
        FrameSequencer seqr(cfg, [&now] { return now; });
        auto mk = [&](std::uint64_t s) {
            ChannelFrame f;
            f.type = ChannelMsgType::ResultBatch;
            f.seq = s;
            f.identity = id1;
            f.payload = encodeResultBatch({s, {}});
            return f;
        };
        FrameSequencer::Outcome oc = FrameSequencer::Outcome::Delivered;
        (void)seqr.accept(mk(1), &oc);   // 基线＝1
        (void)seqr.accept(mk(3), &oc);   // 缺 2——缺口打开（计时起点 t0）
        now += milliseconds{999};
        const bool notYet = seqr.gapTimedOut();
        now += milliseconds{1};
        const bool timedOut = seqr.gapTimedOut();
        o << ",{\"caseId\":\"sequencer-gap-timeout\",\"gapTimedOutBefore\":" << (notYet ? "true" : "false")
          << ",\"gapTimedOutAfter\":" << (timedOut ? "true" : "false")
          << ",\"failed\":" << (seqr.failed() ? "true" : "false") << "}";
    }

    o << "]}";
    return o.str();
}

// =====================================================================
// RT——Codec.cpp 字节面（10 条 Writer/Reader 原语绑定经三形态入口）。
// =====================================================================

namespace {

using namespace sdurws::ird::runtime;
namespace rtcore = sdurws::ird::core;
namespace rtc = sdurws::ird::runtime::rtcodec;

using rtcore::ContentDigester;

static rtcore::ValueProvenance userProv()
{
    return rtcore::ValueProvenance::make(rtcore::ProvenanceKind::UserProvided);
}

static rtcore::SourcedValue<double> val(double v)
{
    return rtcore::SourcedValue<double>::provided(v, userProv());
}

static rtcore::SourcedValue<rw::math::Vector3D<double>> val3(double x, double y, double z)
{
    return rtcore::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(x, y, z), userProv());
}

static rw::math::InertiaMatrix<double> diagI(double i1, double i2, double i3)
{
    return rw::math::InertiaMatrix<double>(i1, 0.0, 0.0, 0.0, i2, 0.0, 0.0, 0.0, i3);
}

static rtcore::ContentVersion cvFrom(const std::string& seed)
{
    rtcore::ContentVersion cv;
    cv.bytes = digestOf(seed);
    return cv;
}

/// minimal 模型（2 旋转关节——镜像 runtime/test/CanonicalModelFixture.hpp minimalFixture）。
static CanonicalModel minimalModel()
{
    CanonicalModelHeader h;
    h.project = idFrom<rtcore::ProjectId>("prj-A");
    h.branch = idFrom<rtcore::BranchId>("brn-A");
    h.revision = idFrom<rtcore::RevisionId>("rev-1");
    h.revisionSeq = 7;
    h.descriptionContractVersion = 1;
    h.compilerContractVersion = 1;
    h.builtFrom = digestOf("description-bytes");

    const rtcore::ObjectId robot = idFrom<rtcore::ObjectId>("robot");
    const rtcore::ObjectId j1 = idFrom<rtcore::ObjectId>("j1");
    const rtcore::ObjectId j2 = idFrom<rtcore::ObjectId>("j2");
    const rtcore::ObjectId l0 = idFrom<rtcore::ObjectId>("l0");
    const rtcore::ObjectId l1 = idFrom<rtcore::ObjectId>("l1");
    const rtcore::ObjectId l2 = idFrom<rtcore::ObjectId>("l2");
    auto addRef = [&h](const rtcore::ObjectId& oid, const char* seed, const char* token) {
        ObjectRefEntry e;
        e.objectId = oid;
        e.contentVersion = cvFrom(seed);
        e.objectTypeToken = token;
        e.digest = digestOf(std::string{seed} + "-bytes");
        h.objectRefs.push_back(e);
    };
    addRef(robot, "robot", "robot-design");
    addRef(j1, "j1", "joint");
    addRef(j2, "j2", "joint");
    addRef(l0, "l0", "link");
    addRef(l1, "l1", "link");
    addRef(l2, "l2", "link");

    RobotChain c;
    c.robotObjectId = robot;
    c.robotLocalName = "IRB_T";
    c.deviceName = "IRB_T";
    CanonicalJoint a;
    a.objectId = j1;
    a.localName = "joint_1";
    a.type = JointType::Revolute;
    a.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);
    a.bounds = JointBounds{-2.97, 2.97};
    a.maxVelocity = val(2.5);
    CanonicalJoint b;
    b.objectId = j2;
    b.localName = "joint_2";
    b.type = JointType::Revolute;
    b.axis = rw::math::Vector3D<double>(0.0, 1.0, 0.0);
    b.bounds = JointBounds{-3.14159265358979323846, 3.14159265358979323846};
    b.maxVelocity = val(2.0);
    c.joints = {a, b};
    CanonicalLink base;
    base.objectId = l0;
    base.localName = "base_link";
    CanonicalLink la;
    la.objectId = l1;
    la.localName = "link_1";
    CanonicalLink lb;
    lb.objectId = l2;
    lb.localName = "link_2";
    c.links = {base, la, lb};

    return CanonicalModelBuilder()
        .setHeader(h)
        .setWorld(WorldPlacement{})
        .setChain(c)
        .build();
}

/// rich 模型（全物性/摩擦/工具命名 TCP/场景几何/耦合矩阵/诊断警告——镜像
/// richFixture 的合法值；阈值面加有限大/极小 f64 位模式值）。
static CanonicalModel richModel()
{
    CanonicalModel m = minimalModel();
    CanonicalModelHeader h = m.header();
    const rtcore::ObjectId t1 = idFrom<rtcore::ObjectId>("t1");
    const rtcore::ObjectId s1 = idFrom<rtcore::ObjectId>("s1");
    const rtcore::ObjectId r2 = idFrom<rtcore::ObjectId>("res-2");
    {
        ObjectRefEntry e;
        e.objectId = t1;
        e.contentVersion = cvFrom("t1");
        e.objectTypeToken = "tool";
        e.digest = digestOf("t1-bytes");
        h.objectRefs.push_back(e);
        e.objectId = s1;
        e.contentVersion = cvFrom("s1");
        e.objectTypeToken = "scene";
        e.digest = digestOf("s1-bytes");
        h.objectRefs.push_back(e);
    }

    ResourceRef mesh1;
    mesh1.resourceId = idFrom<rtcore::ObjectId>("res-1");
    mesh1.contentDigest = digestOf("mesh-1-bytes");
    mesh1.state = ResourceState::Recorded;
    mesh1.accessVersion = 1;
    ResourceRef mesh2;
    mesh2.resourceId = r2;
    mesh2.contentDigest = digestOf("mesh-2-bytes");
    mesh2.sourcePathHint = std::string{"assets/mesh2.stl"};
    mesh2.state = ResourceState::Solidified;
    mesh2.accessVersion = 2;

    RobotChain chain = m.chain();
    const double masses[3] = {5.0, 4.0, 3.0};
    for (std::size_t i = 0; i < chain.links.size(); ++i) {
        CanonicalLink& l = chain.links[i];
        l.mass = val(masses[i]);
        l.centerOfMass = val3(0.0, 0.0, static_cast<double>(i) * 0.1);
        l.inertia = rtcore::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
            diagI(0.01, 0.02, 0.03), userProv());
        if (i == 1) { l.visual = mesh1; }
        if (i == 2) { l.collision = mesh2; }
    }
    for (CanonicalJoint& j : chain.joints) {
        j.friction.viscous = val(0.1);
        j.friction.coulomb = val(0.2);
        j.friction.bias = val(0.01);
    }
    chain.joints[0].maxAcceleration = val(1.0e-300);   // 次正规量级有限位模式
    chain.joints[1].maxAcceleration = val(1.0e300);    // 大量级有限位模式

    CanonicalTool tool;
    tool.objectId = t1;
    tool.localName = "tool_1";
    tool.geometry = mesh1;
    tool.mass = val(1.2);
    tool.centerOfMass = val3(0.0, 0.0, 0.05);
    tool.inertia = rtcore::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
        diagI(0.001, 0.001, 0.002), userProv());
    tool.tcpOffset = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 0.1),
        rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    tool.namedTcpOffsets.emplace_back(
        "tcp", rw::math::Transform3D<double>(
                   rw::math::Vector3D<double>(0.0, 0.0, 0.1),
                   rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1)));

    CanonicalSceneObject obstacle;
    obstacle.objectId = s1;
    obstacle.localName = "obstacle_1";
    obstacle.worldPose = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(1.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    obstacle.geometry = mesh2;

    CanonicalDrivetrain dt;
    dt.ratioPerJoint = {val(1.0), val(100.0)};
    CouplingMatrix coupling;
    coupling.rows = 2;
    coupling.cols = 2;
    coupling.c = {1.0, 0.0, 0.0, 1.0};
    coupling.jointRange = JointIndexRange{0u, 2u};
    coupling.conditionNumber = 1.0;
    dt.coupling = coupling;

    rtcore::DiagnosticRecord warn = rtcore::DiagnosticRecord::make(
        std::string{"RT-CAPABILITY-MISSING"}, std::nullopt, std::string{"joint_1"},
        std::string{}, std::string{"能力缺失警告（RC-07 捕获夹具）"},
        std::string{"maxAcceleration 曾未提供"}, std::string{"建模侧补全后重编译"},
        std::nullopt, rtcore::DiagnosticLevel::Warning);

    return CanonicalModelBuilder()
        .setHeader(h)
        .setWorld(m.world())
        .setChain(chain)
        .setTools({tool})
        .setDefaultTcpIndex(std::uint32_t{0})
        .setScene({obstacle})
        .setDrivetrain(dt)
        .setResourceManifest({mesh1, mesh2})
        .setDiagnostics({warn})
        .build();
}

}  // namespace

static std::string captureRt()
{
    std::ostringstream o;
    o << "{\"unit\":\"runtime\",\"file\":\"runtime/src/Codec.cpp\","
      << "\"capturedVia\":[\"rtcodec::encode\",\"rtcodec::encodeIdentityDomain\","
      << "\"rtcodec::computeContentIdentity\",\"rtcodec::parse\",\"rtcodec::encodeNameMap\","
      << "\"rtcodec::parseNameMap\",\"rtcodec::computeNameMapContentIdentity\"],\"cases\":[";

    const CanonicalModel minimal = minimalModel();
    const std::vector<std::uint8_t> minEnc = rtc::encode(minimal);
    o << "{\"caseId\":\"rt-minimal-encode\",\"hex\":\"" << hex(minEnc) << "\"}";
    const std::vector<std::uint8_t> minIdDom = rtc::encodeIdentityDomain(minimal);
    o << ",{\"caseId\":\"rt-minimal-identity-domain\",\"hex\":\"" << hex(minIdDom) << "\"}";
    {
        // 严格重演：SHA-256 over 身份域编码 == computeContentIdentity（§4.3.5）
        const auto identity = rtc::computeContentIdentity(minimal);
        ContentDigester d;
        d.update(minIdDom.data(), minIdDom.size());
        o << ",{\"caseId\":\"rt-minimal-content-identity\",\"digestHex\":\""
          << hexDigest(identity.bytes)
          << "\",\"identityMatchesIdentityDomainDigest\":"
          << (d.finalize() == identity.bytes ? "true" : "false") << "}";
    }

    {
        const auto parsed = rtc::parse(minEnc);
        const bool ok = parsed.ok();
        std::string reHex;
        bool roundTripBytes = false;
        if (ok) {
            const std::vector<std::uint8_t> re = rtc::encode(parsed.get());
            reHex = hex(re);
            roundTripBytes = (re == minEnc);
        }
        o << ",{\"caseId\":\"rt-minimal-parse-roundtrip\",\"parseOk\":" << (ok ? "true" : "false")
          << ",\"reencodedHex\":\"" << reHex << "\",\"bytesEqual\":" << (roundTripBytes ? "true" : "false") << "}";
    }

    const CanonicalModel rich = richModel();
    o << ",{\"caseId\":\"rt-rich-encode\",\"hex\":\"" << hex(rtc::encode(rich)) << "\"}";
    const std::vector<std::uint8_t> richIdDom = rtc::encodeIdentityDomain(rich);
    o << ",{\"caseId\":\"rt-rich-identity-domain\",\"hex\":\"" << hex(richIdDom) << "\"}";
    {
        const auto identity = rtc::computeContentIdentity(rich);
        ContentDigester d;
        d.update(richIdDom.data(), richIdDom.size());
        o << ",{\"caseId\":\"rt-rich-content-identity\",\"digestHex\":\""
          << hexDigest(identity.bytes)
          << "\",\"identityMatchesIdentityDomainDigest\":"
          << (d.finalize() == identity.bytes ? "true" : "false") << "}";
    }

    // IRDNAME——NameMap 子形态（条目经 fromValidatedEntries 公共恢复路径）
    {
        namespace nm = sdurws::ird::runtime;
        std::vector<nm::RuntimeNameMap::Entry> entries;
        auto mk = [&](rtcore::ObjectId oid, nm::NameScope scope, const char* token,
                      const char* local, const char* full, const char* auth) {
            nm::RuntimeNameMap::Entry e;
            e.objectId = oid;
            e.scope = scope;
            e.scopeToken = token;
            e.localName = local;
            e.fullName = full;
            e.authoritativeLocalName = auth;
            entries.push_back(e);
        };
        mk(idFrom<rtcore::ObjectId>("robot"), nm::NameScope::Device, "IRB_T", "IRB_T", "IRB_T", "IRB_T");
        mk(idFrom<rtcore::ObjectId>("j1"), nm::NameScope::Joint, "IRB_T", "joint_1", "IRB_T.joint_1", "joint_1");
        mk(idFrom<rtcore::ObjectId>("s1"), nm::NameScope::SceneObject, "Scene", "obstacle_1", "Scene.obstacle_1", "obstacle_1");
        rtcore::ContentIdentity ident;   // encode/身份不依赖该占位值——fromValidatedEntries 必填参数
        ident.bytes = digestOf("rc07-namemap-placeholder");
        const nm::RuntimeNameMap map = nm::RuntimeNameMap::fromValidatedEntries(
            std::move(entries), ident, 1);
        const std::vector<std::uint8_t> enc = rtc::encodeNameMap(map);
        o << ",{\"caseId\":\"rt-namemap-encode\",\"hex\":\"" << hex(enc) << "\"}";
        {
            // 严格重演：SHA-256 over IRDNAME 编码自身 == computeNameMapContentIdentity（§7.6）
            const auto identity = rtc::computeNameMapContentIdentity(map);
            ContentDigester d;
            d.update(enc.data(), enc.size());
            o << ",{\"caseId\":\"rt-namemap-content-identity\",\"digestHex\":\""
              << hexDigest(identity.bytes)
              << "\",\"identityMatchesEncodedDigest\":"
              << (d.finalize() == identity.bytes ? "true" : "false") << "}";
        }
        const auto parsed = rtc::parseNameMap(enc);
        bool ok = parsed.ok();
        bool bytesEqual = false;
        unsigned ruleVersionOut = 0;
        if (ok) {
            bytesEqual = (rtc::encodeNameMap(parsed.get()) == enc);
            ruleVersionOut = parsed.get().ruleVersion();
        }
        o << ",{\"caseId\":\"rt-namemap-parse-roundtrip\",\"parseOk\":" << (ok ? "true" : "false")
          << ",\"ruleVersion\":" << ruleVersionOut
          << ",\"bytesEqual\":" << (bytesEqual ? "true" : "false") << "}";
    }

    o << "]}";
    return o.str();
}

// =====================================================================
// POL——PolicyInput.cpp 字节面（6 条 ByteWriter/ByteReader 绑定经
// PolicyCodec 编码面＋canonicalF64 浮点原语公共入口）。
// =====================================================================

static std::string capturePol()
{
    using namespace sdurws::ird::policy;
    namespace core = sdurws::ird::core;
    std::ostringstream o;
    o << "{\"unit\":\"policy\",\"file\":\"policy/src/PolicyInput.cpp\","
      << "\"capturedVia\":[\"PolicyCodec::encode\",\"PolicyCodec::decode\","
      << "\"PolicyCodec::encodeSemanticProjection\",\"PolicyCodec::contentIdentity\","
      << "\"PolicyCodec::canonicalF64\",\"PolicyCodec::parseCanonicalF64\"],\"cases\":[";

    // ---- canonicalF64 位模式（唯一浮点通道——定宽/浮点位模式捕获）----
    const double f64Cases[] = {
        0.0, -0.0, 1.0, -1.0, 0.5, 0.1, 3.141592653589793,
        2.2250738585072014e-308, 5e-324, 1.7976931348623157e308, 1.0e300,
        -2.5, 12.566370614359172, 0.01, 2.5,
    };
    bool first = true;
    int f64Idx = 0;
    for (const double v : f64Cases) {
        if (!first) { o << ","; }
        first = false;
        const std::array<std::uint8_t, 8> bits = PolicyCodec::canonicalF64(v);
        const double back = PolicyCodec::parseCanonicalF64(bits.data());
        const std::array<std::uint8_t, 8> bitsBack = PolicyCodec::canonicalF64(back);
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        char idBuf[64];
        std::snprintf(idBuf, sizeof(idBuf), "f64-bits-%02d", f64Idx++);
        o << "{\"caseId\":\"" << idBuf << "\",\"value\":" << buf
          << ",\"bitsHex\":\"" << hex({bits.begin(), bits.end()})
          << "\",\"roundtripBitsEqual\":" << (bits == bitsBack ? "true" : "false") << "}";
    }

    // ---- full 形态编码（u32/长度前缀/presence/f64 载荷）----
    const core::ObjectId polObj = idFrom<core::ObjectId>("pol-obj-1");
    const core::ObjectId objB = idFrom<core::ObjectId>("pol-obj-2");
    const core::ObjectId objC = idFrom<core::ObjectId>("pol-obj-3");

    auto makeInput = [&](bool rich) {
        RawPolicyInput in;
        in.policyObject = polObj;
        if (!rich) {
            // 全缺省最小面：presence=0/空集/空串长度前缀
            in.origin.kind = PolicyOriginKind::Template;
            in.collision.enabled = false;   // 关闭态——无安全间距（可缺省）
            in.collision.enabledDomains = {CollisionDomain::Self};
            in.jointThresholds.travelLimitCheckEnabled = false;
            in.numericContractAnchor = "appendixD@v1.16";
            return in;
        }
        in.origin.kind = PolicyOriginKind::Imported;
        in.origin.sourceObject = objC;
        in.origin.note = std::string{"imported from baseline policy"};
        in.compatibilityNotes = std::string{"schema 1 migration note"};
        in.collision.enabled = true;
        in.collision.enabledDomains = {CollisionDomain::Tool, CollisionDomain::Self};  // 乱序入——规范化
        in.collision.safetyClearance = RawThresholdInput{0.01, "mm"};
        in.collision.excludeAdjacentLinksByDefault = false;
        in.collision.mandatoryPairs = {
            PairRule::make(ScopeTarget::makeRole("RobotLink"), ScopeTarget::makeObject(objB),
                           PolicyRuleLevel::Must, std::string("must-check pair")),
        };
        in.collision.excludedPairs = {
            PairRule::make(ScopeTarget::makeGroup("sensors"), ScopeTarget::makeObject(objB),
                           PolicyRuleLevel::Should, std::string("filtered pair")),
        };
        in.jointThresholds.nearLimitRatio = RawThresholdInput{0.9, std::string{}};
        in.jointThresholds.conditionNumberWarning = RawThresholdInput{1.7976931348623157e308, "1"};
        in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{5e-324, "rad"};
        in.jointThresholds.travelLimitCheckEnabled = true;
        in.applicability.modes = {core::EvaluationMode::Verified, core::EvaluationMode::Preview};
        in.applicability.modelObjects = {objB, objC};      // 乱序入——bytes 字典序规范化
        in.applicability.taskObjects = {};
        in.applicability.caseObjects = {objC};
        in.numericContractAnchor =
            "appendixD@v1.16.rc07-baseline-capture-with-a-longer-anchor-payload-0123456789";
        return in;
    };

    {
        const RawPolicyInput in = makeInput(false);
        const std::vector<std::uint8_t> enc = PolicyCodec::encode(in);
        o << ",{\"caseId\":\"pol-full-minimal\",\"hex\":\"" << hex(enc) << "\"}";
        const bool equal = (PolicyCodec::decode(enc) == in);
        o << ",{\"caseId\":\"pol-full-minimal-roundtrip\",\"valueEqual\":"
          << (equal ? "true" : "false") << "}";
    }
    {
        const RawPolicyInput in = makeInput(true);
        const std::vector<std::uint8_t> enc = PolicyCodec::encode(in);
        o << ",{\"caseId\":\"pol-full-normal\",\"hex\":\"" << hex(enc) << "\"}";
        o << ",{\"caseId\":\"pol-full-normal-roundtrip\",\"valueEqual\":"
          << (PolicyCodec::decode(enc) == in ? "true" : "false") << "}";
    }

    // ---- 语义闭包投影＋内容身份（SI 真值位模式；CR-02 唯一摘要点）----
    {
        CollisionRules rules = CollisionRules::make(
            true, {CollisionDomain::Environment, CollisionDomain::Self, CollisionDomain::Tool},
            PolicyThreshold::make(0.01, PolicyValueOrigin::Explicit,
                                  PolicyThresholdDomain::SafetyClearance),
            true,
            {PairRule::make(ScopeTarget::makeRole("RobotLink"), ScopeTarget::makeObject(objB),
                            PolicyRuleLevel::Must, std::string("must-check pair"))},
            {PairRule::make(ScopeTarget::makeGroup("sensors"), ScopeTarget::makeObject(objB),
                            PolicyRuleLevel::Should, std::string("filtered pair"))});
        JointThresholds jt;
        jt.nearLimitRatio = PolicyThreshold::make(0.9, PolicyValueOrigin::Explicit,
                                                  PolicyThresholdDomain::NearLimitRatio);
        jt.conditionNumberWarning = PolicyThreshold::make(10.0, PolicyValueOrigin::Explicit,
                                                          PolicyThresholdDomain::ConditionNumberWarning);
        jt.finiteRotationTravelLimit = PolicyThreshold::make(
            12.566370614359172, PolicyValueOrigin::Explicit,
            PolicyThresholdDomain::FiniteRotationTravelLimit);
        jt.travelLimitCheckEnabled = true;
        PolicyApplicability app;
        app.modes = {core::EvaluationMode::Quick};
        const std::string anchor = "appendixD@v1.16";
        const std::vector<std::uint8_t> proj =
            PolicyCodec::encodeSemanticProjection(PolicySchema::currentVersion, rules, jt, app, anchor);
        o << ",{\"caseId\":\"pol-projection-normal\",\"hex\":\"" << hex(proj) << "\"}";
        o << ",{\"caseId\":\"pol-projection-content-identity\",\"digestHex\":\""
          << hexDigest(PolicyCodec::contentIdentity(PolicySchema::currentVersion, rules, jt, app, anchor).bytes)
          << "\"}";
    }
    {
        // 缺省面：碰撞关闭（无间距）、阈值槽位全空、行程开关关——presence=0 面
        CollisionRules rules = CollisionRules::make(
            false, {CollisionDomain::Scene}, std::nullopt, true, {}, {});
        JointThresholds jt;
        jt.finiteRotationTravelLimit = JointThresholds::defaultFiniteRotationTravelLimit();
        jt.travelLimitCheckEnabled = false;
        PolicyApplicability app;   // 全空集
        const std::string anchor = "appendixD@v1.16";
        const std::vector<std::uint8_t> proj =
            PolicyCodec::encodeSemanticProjection(PolicySchema::currentVersion, rules, jt, app, anchor);
        o << ",{\"caseId\":\"pol-projection-absent\",\"hex\":\"" << hex(proj) << "\"}";
        o << ",{\"caseId\":\"pol-projection-absent-content-identity\",\"digestHex\":\""
          << hexDigest(PolicyCodec::contentIdentity(PolicySchema::currentVersion, rules, jt, app, anchor).bytes)
          << "\"}";
    }

    o << "]}";
    return o.str();
}

// =====================================================================
// main——写三个 JSON 到 argv[1]。
// =====================================================================

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: rc07_capture.exe <outDir>\n");
        return 2;
    }
    const fs::path outDir = argv[1];
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        std::fprintf(stderr, "cannot create outDir: %s\n", outDir.string().c_str());
        return 2;
    }
    const std::pair<const char*, std::string> outputs[] = {
        {"exec-channel-protocol-captured.json", captureExec()},
        {"rt-codec-captured.json", captureRt()},
        {"policy-codec-captured.json", capturePol()},
    };
    for (const auto& [name, content] : outputs) {
        std::ofstream f(outDir / name, std::ios::binary);
        f << content;
        if (!f) {
            std::fprintf(stderr, "write failed: %s\n", name);
            return 3;
        }
    }
    std::printf("captured 3 files into %s\n", outDir.string().c_str());
    return 0;
}

```
