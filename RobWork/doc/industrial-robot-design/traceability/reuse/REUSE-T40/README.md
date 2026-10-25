# REUSE-T40 原始记录 — WP-16 project F-962 修复候选（blocked 留存）

任务契约：[tasks/REUSE-T40.json](../../../tasks/REUSE-T40.json)（status=blocked）；
起点 `d7874f57f1f0e2cde192a6c76e5b0f90a016306e`；产品树保持 HEAD 零改动
（所有者 2026-10-25 T33/T36 blocked 先例同型）。

## 1. 缺陷与修复候选

F-962（major/open，findings.json）：project 恢复扫描 `scanForRecovery`
对每个 `revisions/*/manifest.json` 仅 `codec::parseRevisionManifest`，从未将
`HEAD.manifestDigest` 与清单字节实算比对——三反例（错误 HEAD 摘要 scan true／
清单合法 committedAtUtc 字节篡改而 HEAD 摘要保持 scan true／公开
`ProjectStoreFactory::open` 接受错误摘要）穿透 §7.4④"校验 HEAD 引用闭包
完整性（对象/清单哈希）"契约（units/project.md §7.4 恢复状态机④）；
`ProjectStoreImpl.cpp:1186-1193` 仅凭 `scan.headIntegrityVerified` 拒开，
缺陷随之穿透公开工厂。B20 独立复现：acceptance
REUSE-T01-batch20-20261023（2passed/4failed）。

候选修复（`candidate/` 全件＋四份 `.diff`）：

- `project/src/TxEngine.cpp` `scanForRecovery`：HEAD 解析成功后，取
  `revisions/<head.revisionId>/manifest.json` 原始字节经
  `codec::contentVersionOf`（project/src/Codec.cpp:1510-1518，经
  `core::ContentDigester`——CR-02 唯一摘要入口）实算，`cvHex` 后与
  `head.manifestDigest` 严格等值比对；缺失/不符＝broken＋
  `PRJ-STORE-CORRUPT`（cause 定位到文件）＋`headIntegrityVerified=false`。
  口径与提交路径同源（commit 第 4 步
  `cvHex(contentVersionOf(dump(manifest)))`，提交路径自初版即实算——历史
  合法仓库比对零误伤）；不新建哈希路径、不改 Codec、不改错误码语义、
  不自写摘要。摘要供给前置说明：复用对象＝本单元既有唯一入口
  contentVersionOf（H1 直接 API，commit 路径在用），非未获批提案
  GAP-DIGEST-01（core 摘要后端替换，归 WP-15，本包零改动零替换）。
- 用户诊断 cause **首败钉扎**：闭包行走失败/闭包元数据失败/闭包对象完整性
  三处赋值加 `brokenDetail.empty()` 守卫——扫描序第一个失败（现＝HEAD 锚点
  自身）优先写入记录，后续失败仍经开发通道逐一交底（清单缺失时闭包行走必
  随之失败，修复前 cause 定位不达文件，实证见 §3 红日志）。
- `project/src/TxEngine.hpp`：scanForRecovery ④契约注释补实算比对与首败
  钉扎语义。
- `project/test/TxEngineTest.cpp`：按原夹具风格重写 B20 未合入测试树的
  ReuseProjectBoundary 探测为四用例（WrongHeadDigest／
  LegalManifestByteTamper_StaleDigest／HeadManifestMissing／
  TamperThenRestore_ScanGreen）。
- `project/test/ProjectStoreTest.cpp`：新增公开工厂反例
  Open_WrongHeadManifestDigest_Rejected；并订正裸写夹具
  `head.manifestDigest = metaCvHex`（:885 原状）——被摘要对象应为 r0 清单
  **字节**（§4.4.1），原误写为元数据对象摘要，属夹具契约偏差；修复后该形态
  必被④拒绝，订正与产品修复成对落地。

## 2. blocked 原因（门禁全文件解决要求，T36 同型）

对改动产品文件，复用门禁要求全文件解决（reuse-contract.ps1:271-279：
changedSources 每文件 COVERAGE 全行铺面＋全部覆盖行 resolved）：

- `TxEngine.cpp` 22 个功能行全部 pending/pending（functions/project.json），
  须全部 resolved＋逐行区段铺面（现差 81 个非空行——文件头/注释带/横幅）；
- `TxEngine.hpp`／`TxEngineTest.cpp`（1422 行）／`ProjectStoreTest.cpp`
  （1119 行）零覆盖行，自零建铺面；
- validate-reuse 实测：候选在场（工作区改动未提交）即首败
  `REUSE-UNDECLARED : ...TxEngine.cpp : 29540740...`（改动文件无逐文件
  凭证即拒），见 `reuse-gate-coverage-evidence.log`（exit=1）。

该要求属 project 单元迁移包（T05-project 父任务）的逐行语义审计与
capability/凭证批次，超出 migration §13.3 WP-16 修复包批准边界；按所有者
2026-10-25 T33/T36 escalate 裁决先例：候选留存、产品树 HEAD 零改动、
blocked 登记，解阻待项目单元迁移包批准后按 `candidate/*.diff` 低成本落地。

## 3. 验证证据（真实执行）

| 文件 | 内容 |
| --- | --- |
| `f962-red.log` / `red-failed.xml` / `red-failed-report.json` | 未修复码（产品 src 回退 HEAD、仅候选测试在场）新 5 用例 **5/5 failed（exit=1）**：错误 HEAD 摘要/合法时间字节篡改/公开 open 三反例全放行；HeadManifestMissing 红因＝cause 定位不达文件（"闭包行走失败 detail=…未注册修订"，损坏结论已对）；TamperThenRestore 红因＝篡改腿 scan true |
| `green-sdurws_ird_project_test.log` / `.xml` / `.ird-test-report.json` | 修复码全量 **287/287 passed（exit=0）**：既有 282 用例零回归＋候选 5 用例全绿（历史兼容＝篡改/缺失/合法恢复/既有全套） |
| `green-sdurws_ird_project_contract_test.log` / `.xml` / `.ird-test-report.json` | contract 目标全量 **32/32 passed（exit=0）**：含 ProcessCrashContractTest 崩溃重开断言（历史兼容半区） |
| `wp16-t40-ird-gates.log` | `cmake -DIRD_ROOT=<industrialrobot> -P cmake/ird_gates.cmake` exit 0：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中（候选码无红线命中） |
| `reuse-gate-coverage-evidence.log` | 候选在场实跑 validate-reuse.ps1 exit=1 首败 REUSE-UNDECLARED——改动文件进入门禁累计改动集的实证；COVERAGE 全文件解决要求见 reuse-contract.ps1:271-279 与 §2 量化 |
| `candidate/*.cpp/.hpp` | 四件候选全件（与 §3 红/绿实测所构建字节一致；TxEngineTest 为会话内订正 sink 隔离后的终版） |
| `candidate/*.diff` | 对起点 d7874f57f 的四份逐文件 diff |

运行方式：VS x64 Release 集成树（`RWS_BUILD_INDUSTRIALROBOT=ON`），
`cmake --build build --config Release --target sdurws_ird_project_test
--target sdurws_ird_project_contract_test`；exe 于
`build/RobWorkStudio/bin/Release/` 直跑，`--ird_report`／`--gtest_output=xml`
落本目录。

## 4. 解阻与落地面

所有者批准 project 单元迁移包（T05-project 父任务）时：应用
`candidate/*.diff` → 同批完成 TxEngine.cpp 22 行语义解决与四文件全行铺面、
changes/ 逐文件凭证、permissions 绑定 → 复跑本目录绿命令复验 → 独立验收
（本修复属 §6.4 身份/持久化类，实施提交不等于独验通过）。F-962 在候选落地
前保持 open：现 HEAD 公开 open 仍接受错误 HEAD 摘要（B20 三反例在案）。
