# 平台服务域补全批整批验收记录（verdict：pass）

| 字段 | 值 |
| --- | --- |
| 验收对象 | 平台服务域补全批（任务 1~4：O-09 架构裁决落地／PRJ-T17／PRJ-T18／WP-24-T02／F-552 批内核查） |
| 验收分支 | 本批统一落位 `pipeline4/redesign-main`（实施工作树＝验收工作树 `build/pipeline-wt4`，跟踪面验收始末零改动）；冻结 HEAD `c42ae42a900b34182e1d926b389a2b4d40221725`（验收终测复测一致） |
| commit 范围 | `a82f58bc..c42ae42a` 恰 7 提交：00d3d13f〔ARCH/governance O-09 裁决落地〕→ 837c4752〔立卡 PRJ-T17/T18〕→ cc30a04a〔PRJ-T17〕→ e8e54523〔PRJ-T18〕→ f94830ad〔立卡 WP-24-T02〕→ ab96f101〔WP-24-T02〕→ c42ae42a〔F-552 移交登记〕；main 分支终测仍为 `49a9e81`（零触碰） |
| 验收模式 | 独立子会话验收（所有者 2026-10-12 整批指令「平台服务域补全批整批对抗式验收」；ACC §2.2 口径）——验收者与实施会话不共享会话记忆，全部结论出自验收者亲跑命令与亲读源码 |
| 验收日期 | 2026-10-12（跨零点至 10-13 凌晨，系统日期 2026-10-12） |
| 复现现场 | ①独立冒烟树 `build-acc-batch4/`（全新配置，vcpkg toolchain＋Qt 6.11.1 msvc2022_64 前缀＋`-DNOMINMAX -DWIN32_LEAN_AND_MEAN -DWIN32 -EHsc`，与实施冒烟口径同）；②集成模式 `build/` 树（缓存 `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON` 实测在位） |
| 验收者证据根 | `build-acc-batch4/_acc-evidence/`（验收用构建树内未跟踪目录——配置/构建/门禁/校验日志、双模式 40+40 套件 gtest XML 与 run log、串行复跑 7 份 XML、变异翻红/复原四日志、崩溃通道三日志、exitcodes 两份） |
| 裁决 | **pass**——验收指令六维度全过、ACC 4.1~4.11 全过、无阻断级问题；5 条建议级观察项如实列出并随治理批强制转登 findings.json（见 §六） |

---

## 一、验收请求四要素核对

- 任务 ID：平台服务域补全批（整批验收，含 3 张 canonical 契约 PRJ-T17/PRJ-T18/WP-24-T02 ＋ 3 个治理提交）✓
- 冻结 headSha：`c42ae42a`，验收始末 `git rev-parse HEAD` 双时点一致；跟踪面 `git status --porcelain` 终测仅剩验收自建构建树（未跟踪 `build-acc-batch4/`，验收指令允许的验收用构建树）✓
- 范围：`a82f58bc..HEAD` 恰 7 提交、先卡后码次序成立（837c4752/f94830ad 立卡在前、实现提交在后）✓
- 证据位置：实施方留痕 `traceability/builds/wp04-t17/`（22 件）／`wp04-t18/`（19 件）／`wp24-t02/`（20 件）全部在库且与本记录对账；本记录另立验收者独立复现证据（上表证据根）

## 二、必核项（验收者按批次风险点自设，逐项裁定）

### 必核项 1｜O-09 注入式协作：新增代码零 io 编译边 与 §3.5 补登两条协作边说明（非编译边）口径互证——通过

- **代码面（验收者亲 grep，非引申报）**：project 全单元（含本批新增 12 文件）`sdurws/ird/io` include 零命中——唯一出现处为 `project/test/LinkageContractTest.cpp:248/253/267` 的**钉住断言文本自身**（双向扫描 include 面与链接面的守卫用例，非消费）；新增源码 include 面仅 project 自身公共头＋core（Events/Digest/DiagData）＋testkit（测试侧）——R-1/R-2 干净；`project/CMakeLists.txt` 本批 diff **零 target_link_libraries 变化**（零新链接边）。
- **文档面（五点互证）**：governance-log P-PR-5（:90）/P-IO-1（:128）/O-09（:179）三案 **closed** 均载 2026-10-12 裁决与「ARCH §3.5 补登两条协作边说明（非编译边）」口径；ARCHITECTURE §3.5 `io ↔ project`/`io ↔ runtime` 两行（:226/:227）＋门禁实施行（:230，明文「只登记协作语义、不构成编译链接白名单：任一侧引入对侧头文件或链接目标，仍按表外同层边构建失败」）＋变更记录 v0.14 行（:787）在案；units/io.md v0.12（§2 上游表 :34、§8 两行 :217/:218、§9.11 :239 均带〔2026-10-12 O-09 裁决〕消账标注）；units/project.md v0.25（§2.1 :100、§10.4 :1188 注入形态确认）；DTB §4.2 O-09 行（:703）处置翻『已裁决』。
- **互证结论**：「零 io 编译边（LinkageContractTest 钉＋ird_gates R-1/R-2 零命中互证）」与「两行协作边说明仅登记注入语义、不入编译白名单」两侧口径一致，注入缝 fake 自证成立（SolidifyStubs/PackageStubs fake 源/codec 零 io 头，CMake 注释明载「注入缝自证」）。

### 必核项 2｜PRJ-TX-11 转真实、project 测试目标零跳过——通过

- 验收者亲跑集成模式 `sdurws_ird_project_test`：**277/277 通过、disabled=0、零跳过**；`Tx11CopyConsistency.SaveAsAndPackageImport_ReferenceWalkConsistent` 以真实用例执行通过（run log 实录 RUN→OK 269 ms）——GTEST_SKIP 阻塞登记已翻转，与 T18 申报一致。冒烟侧串行复跑同 277/277 零跳过。

### 必核项 3｜真实失败能力抽测（三项全做，证据留档）——通过（3/3）

| # | 抽测 | 结果 | 证据（`build-acc-batch4/_acc-evidence/`） |
| --- | --- | --- | --- |
| 3a | 变异翻红：`ResourceRefs.hpp:494` 钉住常量 `kSolidifyCommandType` 由 `"solidify-external-resource"` 改坏为 `"solidify-external-resourcX"` → 重建 → 钉住用例 | **翻红**：`SolidifyServiceTest.RegistrationProtocol_TokensRegisteredDuplicateFailsFast` FAILED、exe EXIT=1（acc-mutation-run.log） | `git checkout` 复原→重建 EXIT=0→复跑 2/2 PASSED EXIT=0（acc-restore-*.log）；复原后 `git status` 跟踪面净、HEAD 仍 `c42ae42a` |
| 3b | `sdurws_ird_studio.exe --ird-crash-test bogus-mode`（非法模式） | **exit=2**＋stderr「unknown --ird-crash-test mode (expected: av\|abort)」（acc-crash-invalid-mode.log） | 防误触闸实测在位 |
| 3c | 真机崩溃链复验：`--ird-crash-test av` | **SEH 访问违例崩溃**（bash 观测 139＝0xC0000005 族）＋崩溃诊断文件**新鲜落盘** `%LOCALAPPDATA%\sdurws-ird\...\crash\crash-*.log`（acc-crash-av.log＋目录时间戳） | AT-11 观测点真机在位；另有 StudioCrashContractTest 3 进程级用例（真子进程 SEH→脱敏落盘／CRT abort／hang→看门狗 830 ms 自我了断）在验收者亲跑 ui_contract_test 38/38 中全绿 |

## 三、验收清单 4.1~4.11 逐项证据

| # | 检查项 | 结论 | 证据 |
| --- | --- | --- | --- |
| 4.1 | 改动范围 | ✅ | `git diff --name-status a82f58bc..HEAD`＝101 文件（+76263/−86），逐一落两类允许根：`industrialrobot/{project,ui}/**`＋`doc/industrial-robot-design/**`；**范围外路径零命中**（grep 反证 EXIT=1）；patches/ 无新文件（SA-02 零登记需求）；三契约 forbiddenFiles（REQUIREMENTS/ARCHITECTURE/io/**）逐卡核对零触碰（ARCHITECTURE 改动出自治理提交 00d3d13f——O-09 裁决落地，批次任务 1 本职，不属 T17/T18/T02 卡范围） |
| 4.2 | 可复现构建 | ✅ | **冒烟全新配置**：configure EXIT=0（acc-smoke-configure.log）→ 全量构建 78 个链接产物 **EXIT=0、error 行零条**（acc-smoke-build.log）；**集成模式**：增量 `cmake --build build --config Release` **EXIT=0 零错误**（acc-integration-build.log）＋抽 3 目标**强制重链**（删除 `libs/Release/sdurws_ird_project.lib`、`sdurws_ird_ui.lib`、`bin/Release/sdurws_ird_studio.exe` 后重建）EXIT=0、三产物时间戳全部刷新（acc-integration-relink3*.log） |
| 4.3 | 契约 verify 命令 | ✅（6/7 原形成功；1 条形态缺陷见 §六-3，语义面 PASS） | PRJ-T17/T18 verify 五条逐条原形执行全成功（三 cmake target 构建＋validate-task＋validate-docs）；WP-24-T02 verify：`--target sdurws_ird_studio` 构建成功（强制重链同一目标）；validate-task 第 2 条 **DTB 相对＋位置参数原形执行失败**（「task file not found」实测）——以同批 T17/T18 已登记订正的仓库根相对形态复跑 **PASS**，validate-docs **PASS (20 units, 12 trace entries, 319 task files)**（acc-validate.log） |
| 4.4 | acceptance 条目 | ✅ | 三契约 acceptance 逐条核对均有验收者亲跑证据：T17 ①~③（§5.7 固化落位/重关联新修订 AT-21/ResourceRefs.hpp 冻结签名＋零 io＋双模式构建＋单元卡 v0.24＋留痕）；T18 ①~④（§8.8 三处改写/§8.9 选择性复制/§8.10 包通道注入式/TX-11 转真实——必核项 2）；T02 ①~③（崩溃链路＝必核项 3b/3c 真机实证＋PM-17 参数面 --no-splash/--ird-crash-test/设置加载/闪屏＝StudioMain.cpp 启动序 ⓪~⑩ 亲读＋测试留痕）——未执行项零标注 |
| 4.5 | 测试真实失败能力 | ✅ | 必核项 3 变异翻红 1/1 翻红＋复原复绿；另 3b/3c 双通道真机实证 |
| 4.6 | 架构红线 | ✅ | AGENTS §5 八条逐条：①L2 零 Qt——project 新增源码 `#include <Q` 零命中（唯一命中为 BuildRedLineTest 断言文本），CrashCapture 零 Qt（include 面亲读：仅 std＋windows.h＋core/diagnostics 公共头）；②R-1 互链——必核项 1（零 io 编译边；ird_gates R-1 零命中互证）；③R-2 私有头——新增实现头全在 src/（SolidifyServiceImpl/SaveAsServiceImpl/PackageServiceImpl/StoreClone）与 ui/studio（CrashCapture），跨单元消费零命中（ird_gates R-2/SUB/LIB 零命中互证）；④SA-02——框架零触碰（4.1）；⑤R-4——ird_gates R-4 零命中；⑥PA-1——修订写路径全在 project（其权威域）、崩溃报告写出＝应用壳装配面（diagnostics §7.6 分工冻结兑现、diagnostics 源码零改动亲证——本批 diff 无 diagnostics/ 源文件）；⑦PA-2——固化/重关联产生新修订、替换版本同 oid 新 cv（SolidifyServiceTest 钉住）；⑧old/ 零引用 |
| 4.7 | 注释规范 | ✅ | 抽查 8 文件**全部合格**：ResourceRefs.hpp/PackageService.hpp（公共头全查——设计依据锚点 units/project.md §5.7/§5.8/§8.8~§8.10＋需求 CON-03/PM-09/NFR-REL-04/PM-05/NFR-SEC-01/02＋O-09 口径＋背景说明齐备）、SolidifyServiceImpl.hpp、StoreClone.hpp（D-17 快照锚定机理注）、PackageServiceImpl.hpp（导入/另存为机制同源差异注）、SaveAsServiceImpl.hpp（错误二分语义注）、CrashCapture.hpp（崩溃路径纪律五要素注）、StudioCrashChild.hpp（参数传递纪律/模式白名单注）——文件头均含设计依据锚点＋中文背景；函数级 Doxygen 抽查 3 处全合格（createSolidifyService：@param 注入面/@return 独占持有/@throws 两路/线程安全装配期纪律；StoreClone.hpp captureStoreSnapshot/copySnapshotListToStaging：@param [in] 逐参＋@throws StoreError 分码；CrashCapture.hpp emitCrashReport/installCrashCapture：@param 单位与方向/@return 四态发射状态/@throws 无 noexcept） |
| 4.8 | 文档同步 | ✅ | 单元卡三份随批落位且与代码一致：project.md v0.24/v0.25（§12 PRJ-T17/T18 行测试计数与留痕目录与实况一致、§13.1 状态、§11 增补组、§15.4 链式登记）、ui.md v1.98（§13/§16.7）、diagnostics.md v0.15（§12.1 协作行消账——Writer 沿用零改动如实登记）；偏差登记逐条核对在案（T17 五项/T18 两项/T02 四项，DTB §5.4 口径）——**唯一漏登项见 §六-3**（WP-24-T02 verify 路径形态未随同批先例订正登记）；findings F-552 注记与代码实况一致（见 §四.3）；WP-22-T07 note 消账挂合入收尾批（如实登记不越批） |
| 4.9 | commit 质量 | ✅ | 7 提交标题均任务前缀＋动词开头中文摘要；正文四段（What/Why/验证/影响面）逐提交核验齐备；验证段结论与验收者亲跑同口径（267/268＋1 skip→277/277 零跳过→272/272＋38/38 逐位一致；T02 诚实边界如实登记「正常无参 GUI 全程启动未在本树点验——归所有者实机点验」，非粉饰） |
| 4.10 | 留痕完整 | ✅ | 三留痕目录 22/19/20 件全部在库（git 追踪）：gtest XML＋ird-test-report.json＋构建/门禁/校验/测试 run log 四件套齐；任务卡 note 实施登记回填（三卡均在案）；XML 计数与申报逐位一致（§四.1） |
| 4.11 | 偷懒/缩水扫描 | ✅ | ①diff 新增行 `TODO\|FIXME\|stub\|placeholder\|未实现\|not implemented` 扫描——命中仅为测试替身命名 `PackageStubs.hpp/SolidifyStubs.hpp/stub::`（项目既有测试替身惯例，SolidifyStubs 先例同款，非缩水标记），真实偷懒标记零命中；②空实现零（新增服务实体均实质编排，StoreClone.cpp 694 行/SolidifyServiceImpl.cpp 676 行/CrashCapture.cpp 494 行亲读抽验）；③acceptance 逐条证据表见 4.4；④测试强度：变异翻红实证非恒真（4.5），断言校验行为与边界（三处改写逐字节核验/预算超限中止/损坏包穿越样例/脱敏零命中断言等亲读抽验） |

## 四、测试全量复现结果（验收者亲跑，双模式）

1. **规模**：集成模式 40 套件＋冒烟模式 40 套件全部执行（exitcodes 两份各 40 行；gtest XML＋run log 逐套件落证据根）。
2. **执行形态如实注记（重要）**：首轮双模式跑批**并行**执行，出现 7 套次非零退出（集成 io_test/io_contract_test/workflow_contract_test；冒烟 io_test/io_contract_test/project_test/workflow_contract_test）——验收者就地**隔离串行复跑全部失败套件（7 份 serial XML 留档）：7/7 EXIT=0 全绿零跳过**。失败形态（libzip `zip_close=-1`、原子替换「系统找不到指定的文件」）为两批并发进程对 libzip/原子替换路径的文件锁竞争＝**验收者执行方式的自干扰，非产品缺陷**（实施申报与留痕为串行口径，其全绿结论在串行口径下完全复现）。该现象作为并发隔离观察项转登（§六-5）。
3. **关键计数（验收者亲跑，与实施申报逐位一致）**：
   - project_test **277/277 零跳过**（集成＋冒烟串行双确认；TX-11 真实用例在列）；
   - project_contract_test **32/32**（集成；decisive 口径零 skip）；
   - ui_test **272/272**（集成）／**252/252**（冒烟——studio 目标冒烟不注册既定口径下差 20 用例，与申报口径一致）；ui_contract_test **38/38**（集成）／**25/25**（冒烟）；新增 StudioCrashReport 3 进程级用例＋CrashCaptureTest 5 用例在亲跑中全绿；
   - io 回归零劣化：io_test 127/127、io_contract_test 9/9（串行双模式）；workflow_contract_test 77/77；diagnostics_test 117/117、diagnostics_contract_test 26/26；
   - 实施留痕 XML 对账：wp04-t17 双模式各 268（267 过＋1 既有登记 skip＝TX-11，run log 实录）／契约 32；wp04-t18 双模式各 277 零跳过／契约 32；wp24-t02 集成 272＋38／冒烟 252＋25——全部与申报一致。
4. **F-552 注记对账（对抗读法）**：findings.json F-552 owner 注记 ↔ `UiPlugin.cpp:2238 orchestrateNewProject / :2306 openViaSessionController / :2312-2320 F-552 防线注释（hasOpenSession→beginSwitch）` ↔ `SessionControllerModelTest.cpp:701/742` 两钉住用例 ↔ 「静默退出观测缺口随 WP-24-T02 崩溃捕获链闭合」↔ 状态维持 open 待所有者宿主复验——五点亲证一致，纯注记零字段翻改（diff 1 行）；宿主级 GUI 断言用例移交路线 A（本批边界红线禁碰 ui 单元——注记与范围红线一致，非偷懒）。

## 五、验收过程如实注记

1. **验收者并行跑批自干扰**（§四.2）——首轮 7 套次非零退出全部由串行复跑翻绿定性；结论以串行口径为准。
2. **DTB 文档头双版本行并存（基线已存异常，本批未察觉延续）**：基线 a82f58bc 头部即有 v0.63（TK-T12 立项）＋v0.61（legacy state.json）两行（并行批次插入所致的既有异常）；本批 00d3d13f 将 v0.61 行机械替换为 v0.62（O-09）行——提交正文「文档版本 v0.61→v0.62」对该行替换机械属实，但头部最新行实为 v0.63，且 v0.63 链中「前一版本 v0.62」＝WP-10-T05 立项（与新插入行 v0.62＝O-09 为**两个不同 v0.62 语义**）——版本链语义冲突延续未修复。§4.2 O-09 处置行本身更新正确。建议级（§六-1）。
3. **ARCHITECTURE.md 头部版本字段未链式 bump**：变更记录已追加 v0.14 行，头部字段仍 v0.13（io/project/DTB/ui/diagnostics 均头部链式 bump，本档漏）——建议级（§六-2）。
4. **集成树 lib 目录笔误自纠正**：验收者首次强制重链误删 `lib/`（实为 `libs/`），已用正确路径重做并确认三产物时间戳刷新——全程留痕（relink3/relink3b 两日志），不影响结论。
5. **分支形态**：本工作树分支 `pipeline4/redesign-main` 与 `origin/redesign-main` 分叉（本地领先 7 提交即本批、远端另 22 提交属其他并行批次）——送验对象＝本地 HEAD c42ae42a 本身，冻结 SHA 口径不受分支引用影响；合入决策归所有者。

## 六、建议级问题转登清单（不阻塞；随治理批逐条落 findings.json，编号顺延）

| # | 内容 | 建议 |
| --- | --- | --- |
| S1 | DTB 文档头双版本行并存且 v0.62 语义冲突（v0.63 链内「前一版本 v0.62」＝WP-10-T05 立项 vs 独立行 v0.62＝O-09 裁决；基线已存异常、本批延续） | 治理批合并版本链：头部收敛为单行最新版（v0.64），将 O-09 行并入变更序，消除双 v0.62 |
| S2 | ARCHITECTURE.md 头部版本字段仍 v0.13，变更记录已有 v0.14 行——头部未链式 bump（同批其余五文档均 bump） | 治理批头部字段补 bump v0.14（一行修订） |
| S3 | WP-24-T02.json verify 第 2 条（`validate-task.ps1 tasks/foundation/WP-24-T02.json`——DTB 相对＋位置参数）机械不可执行（实测 task file not found）；同批 PRJ-T17/T18 已按 DTB §5.4 登记订正为仓库根相对形态，本卡未订正且 note 未登记该偏差（其实施留痕 validate-task.log 仅 PASS 一行未载命令形态；校验语义以正确路径复跑 PASS 无碍） | 合入收尾批随手续订正该 verify 条目形态＋note 补一句偏差登记（与 T17/T18 同款） |
| S4 | WP-24-T02.json forbiddenFiles 条目 `RobWork/RobWork/RobWorkStudio/src/**` 双 `RobWork/` 笔误（意向为框架 src 面；现形态永不命中＝守护失效一格）；与 audit-r2d O2（designRefs 散文式）同族契约卫生 | 随 S3 同批订正为 `RobWork/RobWorkStudio/src/**`（industrialrobot 例外由 allowedFiles 语义承载） |
| S5 | 测试套件并发不隔离：同机并行执行两套全量测试批时，libzip 写盘与原子替换类用例（io/io_contract/workflow_contract/project 部分用例）出现文件竞争假失败（验收者实测 7 套次，串行全绿）——流水线 ctest 串行口径无影响 | testkit/TK-T12 面登记一句「套件并发执行不受支持」口径注记即可，不要求修复 |

## 七、验收者环境说明

- 验收工作树：`D:\10_Source_Repos\21_robot\RobWork\build\pipeline-wt4`（分支 pipeline4/redesign-main @ c42ae42a；跟踪面全程零改动——变异为用后即 git checkout 还原的单文件临时改动，还原后 porcelain 净、HEAD 双时点不变）。
- 构建口径：VS2022 x64 Release；冒烟全新树 `build-acc-batch4`（vcpkg toolchain 主仓绝对路径＋Qt 6.11.1 msvc2022_64＋`-DNOMINMAX -DWIN32_LEAN_AND_MEAN -DWIN32 -EHsc`——与验收指令同、含 T17 note ② 的 -EHsc 实测教训）；集成树增量＋三目标强制重链。
- 测试口径：直跑 exe＋`--gtest_output=xml:`；PATH 携 vcpkg installed/x64-windows/bin＋Qt bin＋Miniconda3（python313.dll——F-715 口径）；run 形态沿 traceability/builds/wp04-t17/integration-test-run.log 先例。
- 工具链：cmake＋MSBuild 17、pwsh 7（validate-task/-docs）、Miniconda python（XML 聚合）、Git Bash。
- 记录落位：本记录按验收指令写入实施工作树 `traceability/acceptance/platform-batch4-20261012.md`（整批指令显式指定路径；未提交未推送——分支状态零触碰，提交/回传动作归编排者）；验收者全部复现证据在 `build-acc-batch4/_acc-evidence/`（验收用构建树内，用后可弃）。

## 八、裁决

**verdict＝pass**：验收指令六维度（构建复验/测试复验/真实失败能力/红线与纪律/文档链一致性/申报对账）全部通过，ACC 4.1~4.11 全过，无阻断级问题；5 条建议级观察项（§六）如实列出并强制转登，不阻塞合入。合入决策归所有者；S1~S4 建议随合入收尾批/治理批一并处置。
