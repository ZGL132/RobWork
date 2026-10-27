# TK-T13：testkit 第一轮评审整改实施记录

本记录是实施自查和送验入口，不是独立验收。任务保持 `ready`；F-015～F-018 保持原记录 `open`，不在此建立重复缺陷台账。

## 现场与授权

- 工作树：`C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork`；分支 `codex/core-review-fixes`。
- 起始提交：`6dc1141ae1e4d0db61baf1e7ab8d2ed414669c9f`；起始工作区干净。当前实施内容以本包本地提交为准，后续独验须冻结具体 SHA。
- 所有者明确指定本工作树/分支，并要求“按照testkit 第一轮逐单元代码评审进行修改”，覆盖早期只读评审与 redesign-main 现场要求。没有新建分支、工作树、子智能体、远程同步、合并或修改历史。
- R03/R06 格式方案的询问为：“建议保留种子为 JSON 整数并支持完整 uint64；失败报告的 NaN/±Inf 用明确字符串表示，长度缺失用 null，正常有限值仍为 number。是否按此方案修复？”所有者答复：**“采用上述方案”**。这不是 Boost.JSON 迁移、新依赖或容差变更批准。
- 未改 core、框架、vcpkg、其他产品单元实现、正式 testdata/expected、权限、门禁或批准状态。公共布局变化的实际测试消费者必须重编；不声明旧二进制兼容。
- 原第一轮评审以本起始提交源码为对象，归档于 `review.md`；本记录按原 R01～R12 定位整改，不重新编号成全局 findings。

## 保留、删除、适配及职责

| 对应项与位置 | 实施内容及保留的唯一职责 | 消费者与接口影响 |
| --- | --- | --- |
| R01/R11，SetCheck.hpp | 有身份未配对元素只登记 missing/extra，不进入数值回退；同身份值失配不重复计数；最近数值详情也限无身份元素。长度不同仍定位更早的顺序差异。保留 Boost.Graph 匹配接缝、traits、独立穷举 oracle、规模与歧义规则 | optimization/policy/ReproBenchmark 等带身份测试；无身份数值匹配算法未变。错误输入判定恢复既有契约 |
| R02，AssertMacros.hpp | 有限性表达式捕获一次再交 `std::isfinite`；保留 GoogleTest 原生非致命失败和 listener 结果接线 | 宏消费者；不修改 core 比较公式或容差 |
| R04/R05，ToleranceProfile.cpp | 强制既有 schema；保存已校验的 `kind`。删重复的 14 单位探测及反向量纲表；保留既有 token→kind 和 core 单位表交叉校验 | 黄金档案/GoldenFixture；不改附录 D 数值、单位或 EAA 批准范围 |
| F-015/F-017/F-018，Dataset.cpp | parameters 缺省 `{}`，显式错类型拒绝；整数字段先校验再转换；版本使用标准 `from_chars` 并归 TestKitError；generator.script 和 generate 下来源均纳入现完整性链；删重复前缀、重复 include/恒真条件 | 数据装载与 lint；不生成或修改正式 expected，摘要继续唯一复用 core |
| R03/R06/R07，JsonLite/Fixture/Report/ReproBenchmark | 现有 JSON 接缝的数值载荷用 `variant<double,uint64_t>`，整数 token 及种子写出走精确轨；没有并存易失效的数字缓存。一般 double 读/写保留；显式 C locale 转换保留次正规、下溢和负零。writer 拒绝非有限 number，报告边界按授权映射；CompareDetail 显式标记缺失侧 | manifest、复现记录、报告和公开头消费者需重编；只改变已批准表示，未引入第二解析器或生产持久化平台 |
| R08，ProcessRunner.cpp | Windows 环境键用 `CompareStringOrdinal(...,TRUE)` 合并；保留 Win32 Job/CreateProcess/现有引号及树回收语义。仅本 TU 提升 Windows API 声明级别至 Vista，未传播构建宏 | project 真实子进程契约与 listener helper；非零退出仍为冻结的 Crashed/exitCode=0，真实 CLI 退出码另由独立驱动核对 |
| R09/F-016，tools/testdata | 缺省字典改为 root/requirements-ids.json；检查字典结构/ID；实际装载容差档案并核 coveredAt；分类失败退出。CMake 对齐现 helper 输出目录并绑定测试依赖 | lint CLI 与正式元测试。读取失败为非零；工具不是 expected 数学正确性的 oracle |
| R10，install-scan | 按相对路径组件扫描 testkit 库/测试头/testdata；缺根与扫描异常稳定 exit2，只读、不删除文件 | 安装扫描仍是建议稿，未自行接入正式门禁或宣称真实产品安装包验收 |
| R12，PerfBenchmark.hpp | q 先检查有限性，再按原范围与线性插值公式求值 | 性能测试；不新增性能阈值，不擅自裁决预热计数含义 |

本包 owner 为 testkit 维护者；数据与数学期望权威仍归各域数据 owner，core 仍拥有数值比较/单位/摘要，GoogleTest 拥有真实测试裁决，产品进程调度归 execution。没有新增通用 facade、匹配核心、报告平台或缓存。

## 格式兼容边界

- JSON 非负整数字面量在 uint64 范围逐位保存。`JsonValue.number` 保留 double 读/赋值形式，精确整数字段使用 `setUnsigned/tryUnsigned`。单一载荷保证调用方覆盖 double 后不会留下陈旧整数。
- uint64 字段接受完整整数 token；小数/指数记法仅兼容 `[0,2^53)` 内的整数值，大数必须用整数 token，防止 `9007199254740993.0` 先舍入再冒充精确种子。溢出、负数、非整数和超界线程数明确拒绝，不执行无定义转换。
- 失败详情：有限实际值/期望值/diff 仍为 number，非有限分别为 `"NaN"`、`"+Inf"`、`"-Inf"`；长度缺失侧和其 diff 为 null。真实 NaN 与缺失分别表示，失败不会变成成功。
- `JsonNumber` 与 CompareDetail 的布局已变化；集成、独立冒烟和实际消费者均应以源码增量重建。外部归档消费者须按上述字段类型读报告，并继续检查退出码、XML、实际命中和新鲜报告。

## 复用核查与简洁性

定向检索原始输出见 `reuse-search.log`、`reuse-string-search.log`。RobWork `rw/core/DOMParser.cpp` 的默认工厂是 BoostXMLParser，只提供 XML；`StringUtil.hpp::toDouble/toX` 的 istringstream 路径宽松接受 nan/inf，不能直接替换本受限 JSON 的文法、locale 和极小数契约。数值转换采用 CRT/POSIX 显式 locale 公开原语；`std::from_chars/to_chars` 负责整数输入输出，没有自写整数算术。Boost.JSON 渠道已有批准，但本轮未实施单元迁移，原提案与独验前置不变。

SetCheck 的相容性继续委托 core，Boost.Graph 返回结果仍决定匹配；未恢复旧 Kuhn 核心、未改变 T20 已接受性能范围。T49 的正式生成器与输入未变，原独验按其适用范围复用，没有重跑 29 生成器或改写正式 manifest。core 起始修复集中于身份播种、事件错误轨/句柄及空 Core.cpp，Compare/Units/Digest 未变；本轮无额外 core 补偿或重审。

删除的是错误身份回退、重复量纲探测/反向表、重复校验和陈旧说明；新增量主要是边界校验、必要载荷表示与回归。实现、正式测试、文档/证据分别按本地 diff 审阅，不以行数减少或“调用了上游”代替正确性。

## 实际验证与原始证据

构建树的 `CMAKE_HOME_DIRECTORY`、`CMAKE_CACHEFILE_DIR` 已核对属于本工作树；集成 `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`。使用现有 Release 树增量构建，没有重新全库配置。日志、XML、原生 JSON 与独立驱动置于本目录；具体命令见 `commands.json` 及各独立驱动的结果 JSON。

| 验证 | 结果与适用范围 |
| --- | --- |
| 断言、IO、数据与格式反例先红 | 原始 `assertions-red` 4 FAIL、`io-red` 2 FAIL、`data-red` 3 FAIL、`format-red` 4 FAIL；这些是原缺陷复现，不是验收 PASS |
| 早期相关回归 | `group-green3` 54 PASS，0 FAIL/SKIP |
| 本轮唯一一次完整集成基线 | `testkit.xml`：133 执行，132 PASS，1 FAIL，0 SKIP；失败为新增极小数用例揭示的转换原语不兼容，已撤销 stream 方案并修正，不改写此失败记录 |
| 极小值修复后的相关验证 | `json-final.xml`：35 PASS，0 FAIL/SKIP；保留次正规、下溢、负零及 DBL_MAX 边界 |
| 精确表示首次转绿 | `format-green.xml`：4 PASS，0 FAIL/SKIP |
| 独立冒烟最终相关用例 | `smoke-final.xml`：32 PASS，0 FAIL/SKIP；不重复完整套件 |
| 实际 listener 子进程独立交叉核对 | `report-smoke/results.json`：7 PASS；包括受控失败/跳过和精确最大种子。子进程本身为 2 passed、4 failed、1 skipped，退出码/XML/JSON 一致；驱动 PASS 表示符合预期，绝非把失败子进程记成验收成功 |
| 变动公共头自包含 | `headers/results.json`：6 PASS；MSVC x64，单头语法编译 |
| lint/扫描独立 CLI 正负例 | `tools/tool-results.json`：14 PASS；真实退出码，专用数据副本与临时安装树，不是正式产品安装包验收 |
| 最终集成 testkit 相关用例 | `integration/testkit-final.xml`：106 PASS，0 FAIL/SKIP；独立驱动同时检查退出码、非零命中、XML及原生JSON |
| 直接消费者 | `integration/optimization.xml` 2 PASS；`integration/project.xml` 1 PASS（真实子进程锁）；`policy-final/policy.xml` 2 PASS；均0 FAIL/SKIP，不运行GUI用例 |
| policy 首次目标选择 | `integration/policy.xml`：0命中，NOT RUN，不能算PASS。独立驱动如实返回校验FAIL；查明用例属于 `_contract_test`，补建正确目标后仅跑该两项，原零命中记录保留 |
| 稳定载荷后的真实集成 helper/lint | `report-integration/results.json` 7 PASS；`lint-final/tool-results.json` 6 PASS；用例状态和具体CLI退出码保持真实 |
| ird_gates 与文档/复用检查 | `ird-gates.log`：引擎及正负自测PASS、零红线命中；`validate-docs.log`：task/reuse/docs PASS。复用输出仅为治理检查通过，仍列有既有DEBT/UNRESOLVED及他单元待审权限项；全库证据索引深查NOT RUN，不冒充算法合规或独验 |

`data-red` 的早期输入包含旧代码的超界浮点到整数路径；这类旧转换不能视为有定义的动态行为。缺陷依据是静态无范围检查及修复后的明确拒绝断言，原日志仅保留事实。本包不再执行旧 UB 分支，也不运行 lsnchild 的 legacy 非法 delete 模式。

`commands.json` 保存最终独立驱动的实际子进程参数、退出码及构建命令；早期红转绿的过滤参数能从原日志恢复，但此前没有单列保存完整 shell 调用，不补造缺失参数。`implementation-inputs.json` 绑定工作区源码摘要、已执行二进制摘要及构建缓存；送验版本另由显式 Base/Head 的提交范围检查确定。公共 `.number` 的实际消费者检索见 `number-consumers.log`；未变消费者中现有 double 读取、赋值、容器追加与显式转换仍适用，不据此承诺所有外部 C++ 用法源码/ABI 兼容。

未执行（NOT RUN）：全库测试、GUI、core 全套、正式安装包扫描、全部历史黄金生成器，以及原评审未决的竞态/资源耗尽探针。没有用过滤零命中或模型实验冒充真实接口通过。

## 历史证据影响与未决边界

R01 影响带身份但只靠 SetCheck 判等的证据；已读 optimization 黄金测试原有精确 CandidateId 二次断言仍有价值。R02 历史已定位调用为常量，未证明副作用消费者曾触发。R03 影响非有限/长度失配的失败 JSON，不使已有有效成功 JSON、XML或进程退出码整体失效。R06 只定向核查超 double 精确范围的种子；现有小种子记录不因本修复自动失效。R08 只在继承环境存在异大小写同名键时触发。lint/scan 旧 PASS 不能证明其原来漏检的对象，但不能据此宣布全部算法/生成器独验无效。

原评审中的并发 ReportStore 生命周期、suite/global setup 的进程级报告、repeat/disabled/零命中规划集、预热错误计数、路径/链接边界、TempDir 碰撞所有权、异常分配与极大等待、未规定 JSON/统计极值等仍需各自契约裁决或具体探针。本包未扩大修改；lint 字典健壮性已在 R09 同包补充类型与重复校验。原生逐例报告仍不能独立证明整个进程成功。

## 送验、回滚及后续 project 限制

1. 本包是 testkit 局部整改，可按本地提交整体回滚；不需要迁移正式数据或回退 core。若仅回滚表示层，须与 JsonLite/Fixture/Report/ReproBenchmark/CompareDetail 同步，不能混用新旧载荷 ABI。
2. 所有者另开独立会话冻结送验 SHA，核对本任务 acceptance、真实证据及批准格式；本实施者不签独验、不改 done、不关闭 F-015～F-018 或迁移债务。
3. project 评审可继续，但必须先检查 ProcessOutcome.kind，不能把 Crashed.exitCode=0 当成功；保留真实子进程/文件锁/崩溃接线证据，不用替身替代。
4. project 和后续归档消费必须接受明确非有限/缺失表示与精确种子整数；JSON 可解析、decisive=true 或固定种子记录本身都不是执行成功/确定性的证明。继续核对非零命中、exit、XML和真实来源绑定。

在所列版本、范围和已执行验证内，未发现其他问题。
