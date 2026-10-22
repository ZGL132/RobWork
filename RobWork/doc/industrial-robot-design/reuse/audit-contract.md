# 逐功能复用审计与门禁补强契约

本文件承接所有者批准的最终方案，定义审计数据与待实现门禁；不承载第二份架构权威。权限以 [ARCHITECTURE §3.6 / SA-19](../ARCHITECTURE.md#36-上游使用权限与纯净性sa-19) 为准，执行顺序见 [policy](policy.md)，阶段见 [migration §11](migration.md#11-最终方案补充阶段门2026-10-22)。

**实施状态（GOV-T22）**：补强实现位于既有 `validate-reuse.ps1` 及其私有 `reuse-contract.ps1`，由原 `ird_gates`/CI 调用。首轮 `0d018a0ba` 独立验收 fail，见 [原始记录](../traceability/acceptance/GOV-T22-20261022.json)；当前返工验证，尚未取得 pass。下文格式的结构、摘要、权限申报及结果一致性可机器核查；真实来源、完整调用链和语义仍须独立审查。catalog 全部保持 candidate，未新增真实生产 migrated/closed 记录。补强独立验收通过前，不进入 REUSE-T02 适配实现或算法替换。

## 1. 权威与唯一数据源

| 问题 | 唯一维护处 | 下游只能引用 |
| --- | --- | --- |
| 需要什么行为、容差和验收 | REQUIREMENTS | 不借复用降低原需求 |
| 谁可持有、构造、修改、发布，进程/依赖边界 | ARCHITECTURE §3.6、§9 SA-19 | catalog、矩阵和例外均引用该处 |
| 单元内部接口和失败语义 | units/<unit>.md | 不在 reuse 重定义接口 |
| 上游有什么能力及证据 | catalog.json | 能力事实可共享，适用性按功能判断 |
| 项目具体功能如何复用 | functions/<unit>.json（后续审计产出） | 不另手写同内容 Markdown 台账 |
| 本次变更、验证与批准引用 | 既有任务/变更记录和 changes/<taskId>.json | 不为普通文档维护额外建契约 |

`inventory.json` 保留不可变文件基线；功能矩阵补充语义，不能替换或清空库存。当前 1401 个基线文件、16 个已知重复文件、2 个已观察适配文件只是已登记事实，不是范围或白名单；其余文件仍须审计。运行报告从上述数据生成。

## 2. 需求驱动的双向审计

每批执行：需求/AT 与真实入口定位 → 上游公共头、实现、测试、示例、文档、CMake 定向检索 → 阅读单位/坐标系/所有权/失败语义 → 反查产品隐藏实现与调用链 → 最小适用性探测 → 功能决策与独立审查。不设“先读完整个 RobWork 才可审计产品”的前置。

第一批校准 runtime 模型/State/名称、变换、FK/Jacobian/SVD；再按依赖推进 IK、RNE/FD、碰撞、轨迹/采样，以及加载、IO、工具、测试和构建。最终覆盖全部 20 单元及既有 adapter；计算核心、测试辅助、黄金数据生成器、inline/template/macro、可执行脚本、页面内代码与 CMake 均在范围内。纯访问器等样板可按明确语义规则成组，不能整目录认定 business-specific。

搜索保存命令、范围、版本、命中和已排除候选；无命中须继续检查相邻抽象、测试、示例和构建注册。GAP 表示待验证差异，不证明上游无能力。进度和估算以代表批次实测为准，不承诺推测工期或百分比收益。

检索校准：`JacobianCalculator.hpp` 在 `RobWork/RobWork/src/rw/models/`；`Device::baseJframe` 是 6×DOF、以基座坐标表达的帧速度 Jacobian，按实际参考点适配，不能仅因 Frame 命名排除。RNE/PhysicsEngine 的目标为 `sdurwsim`；RK4Integrator 的刚体接口不能直接充当关节 ODE 求解器。示例路径仍须按冻结版本核实。

## 3. catalog 扩展格式

保留既有能力 ID、基点、路径/符号/目标和摘要字段；以兼容升级扩充同一 `catalog.json`，不新建并行能力库。后续 schema 升级须同批更新校验器与正负夹具，不能先把未验证字段当作已审计结果。

| 新增字段 | 内容与约束 |
| --- | --- |
| `ownerUnit` | 产品权威所有者，如模型为 runtime、碰撞语义为 policy；不把所有上游能力归 runtime |
| `usages[]` | 每种消费关系分别记录 `consumerUnit`、`usageRole: owner / adapter`、`permissionRef`、允许操作；同一能力可有多种关系 |
| `operations[]` | 明确 value/query/construct/load/clone/mutate/publish 及作用对象；State/求解器会话与权威模型结构分开 |
| `tests[] / examples[] / docs[]` | 真实路径、符号/用例、版本与摘要；无资源时记录搜索与原因，不伪造路径 |
| `build` | 实际导出目标、配置选项、依赖/后端、集成/冒烟限制和版本证据 |
| `limits` | 单位、坐标系、DOF、所有权、线程、随机性/确定性、错误和许可证约束 |
| `review` | candidate/reviewed/verified、搜索证据、复核版本；verified 不代表适用于所有需求 |

`usageRole` 表示职责，**不表示 L1—L5 架构层级**；若工具保留 `layer: owner / adapter` 兼容字段，必须注明此区别且与 usageRole 单源生成。runtime 作为 owner 接受构造权审查，不能被笼统归为“适配 runtime 能力”。

## 4. 逐功能矩阵格式

后续产物路径固定为 `reuse/functions/<unit>.json`；每行对应一个可辨识语义功能与一个候选/选定能力关系。一个功能用多个能力可有多行并共享 functionId；跨多个文件必须关联完整链。新增行不得只有文件名或“复用上游”的结论。

| 字段组 | 必填内容 |
| --- | --- |
| 身份 | functionId、ownerUnit、需求/AT；无业务需求的构建/工具项引用其任务或规范 |
| 实现定位 | 文件、全限定符号、代码区段、规范化摘要；包含生产入口、跨层调用和结果消费端 |
| 权限 | consumerUnit/目标、usageRole、对象、允许操作、ARCH permissionRef/宿主例外 ID |
| 候选与检索 | catalog capabilityId、上游版本/文件/全限定符号/目标、搜索证据、未选候选及原因 |
| 决策 | direct / adapter / business-specific / pending / proposal；business-specific 也须证明不与上游重叠且不越权 |
| 差异 | 单位、参考系/参考点、offset、物性、异常/取消、生命周期、线程、确定性与兼容策略 |
| 正向证据 | 真实生产调用、返回值消费、同模型/State 上游对照、独立 oracle、断开/扰动实验及命令/结果 |
| 结项 | 替代/删除旧路径的提交、相关测试/版本摘要、批准引用（如适用）、独立验收与剩余缺口 |

审计汇总表由矩阵生成，列序统一为“功能点—现有实现位置—上游文件/函数/接口—复用方式—差异与适配点”；架构权限、证据和状态作为可追溯附列，不再维护第二份人工表。

## 5. IRD-GATE-AD 权限检查契约

在现有 `ird_gates.cmake` / 白名单与 `validate-reuse.ps1` 上扩展，权限事实来自 ARCH §3.6；不得另建规则平台。机读记录须精确绑定文件、编译目标、头/类型类别、操作、对象来源、permissionRef、例外 ID/适用期限。头分类至少区分纯值数学、模型/状态、求解器、碰撞、仿真、加载、图形/宿主；转发声明、别名、间接 include 和工厂调用同受权限约束。

默认不授权模型构造/变更。禁止只查 include：同时核查 load/clone/factory、结构写入和越界保存/返回句柄。`const`、智能指针和使用上游类名都不自动证明合规。静态结构检查辅以真实接线测试/独立语义审查，不宣称正则能完备理解 C++。发现既有违规应登记债务并阻断扩大，不用整目录白名单消音。

权限实例：业务 `Transform3D` 值运算允许；kinematics 自建 WorkCell 拒绝；通过 runtime 获得的受控只读模型和任务私有 State/IKSolver 允许；同一 State 跨任务共享写拒绝；宿主例外只授权呈现操作；worker 不能借宿主 WorkCell 指针或链接 Widgets。

## 6. direct / adapter 生产正向证据

两种方式均须证明：**业务入口 → 上游调用 → 实际消费返回值**。包含同模型、同 State 上游直接调用对照与独立解析/物理 oracle；不可只与旧核心互证。include/link/grep 命中、类名、调用百分比不构成通过条件。

在隔离测试中断开目标上游调用或扰动返回值，相关业务断言必须失败；实验绑定生产目标/入口和测试版本，不能只测试另写的演示 adapter。沿用 GTest、testkit、现有模型/夹具和报告，优先链接生产目标；不增加生产追踪框架、测试专用公共接口或运行时双路径。独立 oracle 限测试使用，不发展成第二套通用核心。

证据新鲜度同时绑定产品区段、上游能力、相关依赖/配置及测试摘要；任一相关项变化，受影响证据过期并重新验证。普通局部编辑不强制跑全量 mutation 实验，未受影响证据可引用。无法进行可判别实验时记录原因与替代检验，由独立验收确认，不可自行豁免。

## 7. 门禁实施与对抗验收矩阵

入口沿用 validate-reuse / ird_gates / 既有 CI；`REUSE-AD` 对应 IRD-GATE-AD，其他具名诊断见脚本。下表通过条件同时包含机器部分与独立语义部分，不把正则匹配或格式正例等同完整架构/生产验收。PR 引用实际日志，不新增第二套 lint 或 CI 平台。

| 规则 | 通过条件 | 必须检出的负例 |
| --- | --- | --- |
| IRD-GATE-AD | 文件/目标/类型/操作满足架构权限，例外精确匹配 | kinematics 通过工厂创建 WC；宿主例外扩散；worker 链 Widgets；借持句柄逃逸 |
| 功能覆盖 | 当前生产链、辅助脚本和生成器有矩阵，inline/template 未漏 | 改名内核藏 helper；新增脚本绕过逐文件/逐功能登记 |
| 能力适用 | catalog 已复核，差异与功能需求匹配 | 假 API/target；三次冒充五/七次；无命中直接判缺口 |
| 正向证据 | direct/adapter 均有生产入口与有效上游结果消费 | 引用上游却仍走旧内核；忽略/覆盖返回值；仅测试演示函数 |
| 新鲜度 | 相关源码/上游/依赖/测试版本匹配 | 改适配算法或测试后沿用旧绿证据 |
| 依赖复用 | 使用上游已导出目标和既有工具链，新增依赖有必要性 | 第二套引入渠道/通用后端/重复运行库；不因共享 Eigen 就误报算法重复 |
| 语义结项 | 基线不变，关闭记录证明旧核心已移除且独立验收通过 | 清库存/改基点洗白；pending/debt/audit-required 或静默 legacy fallback |

自测扩展现有 `test-reuse-governance.ps1`，在隔离夹具中真实证明允许/拒绝与测试变红；按现有 PS7/PS5.1 支持面验证，JUnit/JSON 分列 passed/skipped/failed/notRun。补强验收绑定冻结 SHA，独立会话确认；旧 GOV-T21 pass 不自动覆盖新增门禁。

### 7.1 实施格式与机器检查范围

- `catalog.json` schema 为 `ird-reuse/2`；旧 ID、上游绑定和基点保留，禁止回退旧 schema 静默跳过新字段。reviewed/verified 必须绑定复核证据，且复核版本中的上游源码/目标与现版本一致；candidate 空资源数组表示尚未复核，不表示不存在资源。
- `permissions.json` 使用 `ird-reuse-permissions/1`。`grants[]` 每项含 id、source `{path,sha256}`、targets、categories、operations、origin、lifetime、permissionRef、usageRole、build `{path,sha256}`；宿主再含 exceptionId。origin 区分 runtime-owner/runtime-borrow/task-private/host-presentation/value。模型操作、State 写权限与文件/目标匹配分别检查；四组 HOST-* 文件为封闭清单，不能用来源标签绕过。
- `functions/<unit>.json` 使用 `ird-reuse-functions/1`，顶层为 unit/functions；行字段见 §4。sources 每区段含 path/symbol/startLine/endLine/sha256；inputs 为相关源码、测试、CMake/配置的摘要绑定。targets 必须在绑定的 CMake 中有真实声明；动态 CMake 的实际编译归属仍需构建接线核查。矩阵可记录 pending/proposal 审计存量，但变动生产文件必须由已解决的 migrated/closed 功能完整覆盖，business-specific 也要 nonOverlapEvidence。新模板、脚本和 CMake 同受覆盖；仅 GOV-T21/T22 精确登记的根 CMake/门禁文件允许治理 bootstrap，不豁免目录。
- direct/adapter 的 migrated/closed 行含 production `{entry,upstream,consumer,target}` 与 evidence `{path,sha256}`。证据 JSON 使用 `ird-reuse-production/1`，含 functionId/target/bindings，以及 normal/disconnected/oracle/upstreamComparison；每次运行含 command/exitCode/xml/log，文件均绑定摘要。XML 必须有真实 testcase 节点且计数一致，无必需用例跳过或运行错误；正常/oracle/上游对照成功，断开实验非零且有失败断言。disconnected 另含 mutation `{source,originalSha256,description}` 与 mutatedSource 绑定，变异必须作用于登记生产源码并改变内容。
- `closures.json` 使用 `ird-reuse-closures/1`，files 每项含 path/baselineBlob/currentSha256/functionKeys/migrationCommit/acceptance。acceptance 为 `{commit,path,reviewedCommit}` 的 Git 对象引用，必须 REUSE-* pass 且祖先匹配。当前文件完整关联 closed 功能；已删路径 currentSha256 为 deleted，替代功能通过 `replaces[]` 明确引用旧基线路径。源码、输入、生产证据及其报告依赖须与被验版本相同；功能记录仅允许验收后的 migrated→closed 状态推进，不允许更换边界后沿用旧 pass。基线之外的新产品源码使用 baselineBlob=absent，并核对它确实不在基线且当前存在；不能仅填 closed 跳过独立结项。固定库存不改分类，最终模式对基线及当前新增源码逐项检查语义关闭。
- GOV-T22 仅有精确治理范围 bootstrap；其他进入 ready/done 的新任务必须同时引用原 governanceAcceptance 与新增 reuseGateAcceptance（GOV-T22 的真实冻结 pass），不能自填布尔值放行。

源码权限扫描支持受限头、显式类型、常见构造/工厂/加载/模型修改及 State 写路径，并区分大小写。别名、间接工厂、跨任务共享、句柄逃逸、条件编译和实际结果消费无法靠该扫描完备证明，必须逐功能记录并独立核查；既有依赖/零 Qt 门禁继续承担链接边界。格式夹具内合成的 XML、断开描述和验收记录只测试校验器，不是产品实验或真实批准。摘要防过期，不提供报告身份认证。

首轮返工口径：Qt item 的 `setData`、相机呈现的同名 `setTransform` 和文案中的类型名不自动构成上游 State 写；明确的 StateData 写与关节 setQ 仍检查权限，无法定型的别名调用由来源/生命周期审查承接。UTF-8 BOM 在工作树及 Git 对象读取时统一剥离，再按原 CRLF/LF 与末尾换行规则计算摘要，含 BOM 的产品脚本也必须在工作树和冻结树正例中通过。首轮失败证据保留，不用更新后的代码改写原探针结果。

`-Head` 冻结校验数据；验收还须使用该 SHA 的脚本执行，报告记录 validator/helper/driver 摘要。CI 的 PS7/PS5.1 自测使用独立输出目录，避免互相覆盖。未执行的远程 CI/保护设置或产品数值测试须明确保留为未执行。

## 8. 控制复杂度与收口

能直接调用就不加包装；薄适配只承担实际需要的类型/单位/坐标转换、生命周期、错误翻译与业务编排。不得建立覆盖全部 RobWork 的 facade、第二模型层、通用 backend 注册框架或每个数学运算一个接口/DTO。公共辅助只因真实共享需求提取，不能越过 owner/六类端口。

每能力批次同步移除被替代生产核心、死代码、无用依赖与隐藏旧兜底；保留测试所需独立 oracle 和真实回归。最终针对冻结版本/范围报告“全功能已审计、权限与生产复用证据有效、已知缺口/债务关闭”，不承诺静态扫描能证明一切未知语义重复不存在。风险回滚按完整能力提交或制品回退，不恢复静默旁路。
