# 任务状态账本（Task Status Ledger）

> 检查点：`IRD-D14-20260830`；本账本是唯一机器可检查的逐任务状态登记（validator 校验覆盖率与取值）。
> 状态定义：`Planned`（未解除前置）、`Ready`（前置已满足，可实施）、`Blocked`（存在需人工/架构裁决的阻塞，note 注明）、`Done`（已完成并提交，note 记录 commit SHA）。
> 更新规则：实施者只提交当前任务实现并报告“实现完成，待独立验证”，不得修改本账本；独立验证/治理上下文复跑验证并检查范围与证据，通过后在后续治理提交中将该行改为 `Done`，在 note 记录实现 commit SHA 与证据路径，并填写 signer/date。改为 `Ready` 必须满足 DOCUMENT-BASELINE §4 九条门禁并注明判定依据；`Blocked` 必须写明阻塞原因与负责人。
> 账本门禁（validator 强制）：`Ready` ⇒ 全部前置任务（卡内"前置任务及必需工件"）已为 `Done` 且 signer 非空；`Done` ⇒ signer 非空且 note 含 commit SHA 与 `out/test-evidence/` 证据路径。
> 初始状态：仅 `WP-00-T01 = Ready`（用户 2026-08-29 审计指示"首个可执行任务只开放 WP-00-T01"）；其余按总纲 §5.3 依赖顺序保持 `Planned`，卡内"前置任务及必需工件"字段为逐任务解锁依据。

| task_id | state | blocked_by | signer | date | note |
| --- | --- | --- | --- | --- | --- |
| WP-00-T01 | Done | - | 独立验证者/治理协调（ZCode 治理会话，2026-08-30 复跑验证） | 2026-08-30 | 实现提交 00379c0991099812d3e3857218e1cbd49c68791a；证据 out/test-evidence/wp-00/20260830-4d14959-impl/t01-requirements-review.md；2026-08-30 独立复跑基线断言 t01-req-count（128==128）与 t01-at-count（19）、生成器（128 行、退出码 0、CSV 零字节变化）、文档门禁（退出码 0）均通过；提交范围与格式符合卡内要求；2 项疑似不一致（README 检查点滞后、AT-14 未入 §16）非阻断，待需求维护者裁决 |
| WP-00-T02 | Done | - | 独立验证者/治理协调（ZCode 治理会话，2026-08-30 复跑验证） | 2026-08-30 | 实现提交 b7c3d6b70d6085f1e3c10187e34e09b2bb84d48f；证据 out/test-evidence/wp-00/20260830-da8ec71-impl/t02-generation-log.md；2026-08-30 独立复跑四条断言（rows/columns 13 列/encoding BOM+CRLF/byte-stable 双跑哈希 0725D6EE 与证据一致）、两条逐字命令（生成器 128 行退出码 0、门禁退出码 0）通过；副本注入复验缺锚点与反向范围两失败场景均退出码 1、诊断一致、正式双 CSV 字节不变、零临时残留；提交范围与 param 契约符合卡内要求 |
| WP-00-T03 | Done | - | 独立验证者/治理协调（ZCode 治理会话，2026-08-30 复跑验证） | 2026-08-30 | 实现提交 2aa63eb827f77381e33d03e3b6d88a391ff1013c；证据 out/test-evidence/wp-00/20260830-ecf6505-impl/t03-gate-and-fixtures.md；2026-08-30 独立复跑两条逐字命令通过：门禁退出码 0 与成功行、run-fixtures.ps1 执行器退出码 0 且 8 夹具全部"非零＋§6 关键词＋修复动作＋干净树通过"；独立官方树哈希（SHA-256 全树）前后一致且与证据记录一致；夹具仅含注入脚本零完整拷贝；提交范围与接口保持符合卡内要求；实施者 RED 迭代在系统 TEMP 遗留 4 个中断夹具副本目录已由本验证清理 |
| WP-00-T04 | Done | - | 独立验证者/治理协调（ZCode 治理会话，2026-08-30 复跑验证） | 2026-08-30 | 实现提交 553c9b5535b78a957eef98d2ce8560bcac1754fd；证据 out/test-evidence/wp-00/20260830-ef23c93-impl/t04-independent-review.md（双 shell 对照 t04-dual-shell-byte-compare.md 同目录）；2026-08-30 独立复跑：门禁 powershell.exe 5.1 退出码 0、pwsh.exe 7.6.5（本机已可用，Ready 行环境注意解除）退出码 0、成功行逐字一致；run-fixtures.ps1 执行器退出码 0 且 8 夹具全部非零＋§6 关键词命中＋官方树哈希运行前后一致；requirement-traceability.csv SHA-256 0725D6EE05C5D4A5B9A9AD73E5D2499D2A0FB98FD41E4F957D18B1255F848C88 与证据及 T02 入库哈希一致；实现提交仅含两份新增证据文件、CSV 零变化、git diff --check 干净、提交格式符合卡内要求；抽样表机器复核 47 条、21 前缀全覆盖、每前缀 ≥2 项（ERR/EVI 为单项前缀已在证据注明）；断点 0 项，观察 5 项（含继承 3 项）移交需求维护者/后续治理任务，均不阻断 |
| WP-01-T01 | Done | - | 独立验证者/治理协调（ZCode 治理会话，2026-08-30 复跑验证） | 2026-08-30 | 实现提交 c7c8b4126a0bb76b08edcf96fe2ff4e1142d8762；实现证据 out/logs/industrial-robot/20260830-193442/（卡内指定 boundary-fixtures.log、boundary-clean-tree.log、boundary-rule-fixture-map.md）；独立复跑证据 out/test-evidence/wp-01/20260830-c7c8b41-verify/t01-independent-verification.md；2026-08-30 于实现 SHA 隔离 worktree 复跑：正式树缺省扫描退出码 0（骨架目录未建属预期）、4 夹具全部非零且规则/文件/行/关键词与证据逐字一致、IO 失败场景非零＋R0 诊断、文档门禁退出码 0；扫描器无 offscreen/并行 GUI 字面量、无自动修复；提交仅 9 个允许文件、CSV 与禁改文件零变化、git diff --check 干净；RED 骨架断言由夹具构造（每夹具单违规）与 GREEN 复跑共同佐证；观察（非阻断）：卡内证据根 out/logs/industrial-robot/<timestamp>/ 与 AGENTS §5.3／账本 Done 规则的 out/test-evidence/wp-xx/<run-id>/ 口径不一致，本验证已按规范根补立独立复跑记录桥接，建议卡片所有者后续修订对齐 WP-01 各卡证据路径 |
| WP-01-T02 | Done | - | 独立验证者/治理协调（ZCode 治理会话，2026-08-31 复跑验证） | 2026-08-31 | 实现提交 2443d996b246192b3869c97d1ceffa5471c74d70；治理授权结构修订 2287b55a0974cb6d8eab82efb4140346a7f69c16（用户 2026-08-31 选择阻塞报告方案 1，注册移至顶层 WITH_RWS 门控外并修订卡文边界预期）；实现证据 out/logs/industrial-robot/20260830-201938/（8 件）；独立复跑证据 out/test-evidence/wp-01/20260831-2287b55-verify/t02-independent-verification.md；2026-08-31 于 2287b55 复跑：默认配置退出码 0、sdurws_ird_core 与 smoke 构建退出码 0、CTest 1/1 通过、optoff（WITH_RWS=OFF）18 目标无 UI 无半目标、边界补验（WITH_RWS=ON＋UI=OFF）18 目标、反向依赖注入配置期失败退出码 1、边界扫描 32 文件退出码 0；范围：rwslibs/CMakeLists.txt 与实现前零差异、顶层仅治理授权 5 行、无 QWidget/QApplication；观察非阻断 3 项（证据根口径沿用 T01 观察、08-31 裁决建议补 D15 检查点、CMake 4.3 FindBoost 依赖环境前缀由 T03 入口脚本固化） |
| WP-01-T03 | Done | - | 独立验证者/治理协调（ZCode 治理会话，2026-08-31 复跑验证） | 2026-08-31 | 实现提交 c161d6d27b362027ea9dbb7d2219b51504561f1a（main 已快进至该提交）；实现证据 out/logs/industrial-robot/20260831-062118/（red/green/failpath/boundary/native 分目录＋red-green-summary.md 前后结果表，主树副本与 worktree 原件 diff -rq 全等，本次治理提交入库）；独立复跑证据 out/test-evidence/wp-01/20260831-c161d6d-verify/t03-independent-verification.md；2026-08-31 于实现 SHA 新建隔离 worktree 复跑：卡内三脚本命令（configure/build/run-tests -Regex '^sdurws_ird_core_test$'）退出码 0、CTest 1/1 通过（QT_QPA_PLATFORM=windows 强制、ctest -j 1）；原生回退双命令（VsDevCmd x64 包装逐字执行）退出码 0、结果与脚本形式一致；四条失败断言均退出码 1 且诊断稳定（no-source 路径诊断且不创建构建目录、no-vs 以验证者自编译 stub vswhere 零实例证实不回退、regex 零匹配报告、offscreen 冲突先报告停止且未执行 ctest）；-NoConfigure 确证跳过配置步骤、8 次调用日志时间戳全部分离；check-boundaries 32 文件退出码 0；提交范围仅 4 允许文件、common.ps1 追加 183 行零删除（T01 函数逐字未动）、industrialrobot/ 与旧插件零变化、git show --check 干净、提交标题与卡一致、offscreen/并行 GUI 禁词零命中、四脚本 PS 5.1 解析器零错误；环境前置（非代码）：新 worktree 需复制 gitignored 的 RobWorkStudio.ini 双模板、操作员 CMAKE_PREFIX_PATH（QT 6.11.1＋vcpkg Boost）由脚本仅记录不设置；观察非阻断移交：T02"前缀固化"建议未被本卡契约吸收、待工作包所有者裁决，证据根口径沿用 T01 观察 |
| WP-01-T04 | Ready | - | 独立验证者/治理协调（ZCode 治理会话，2026-08-31 解阻塞门禁判定） | 2026-08-31 | 解阻塞登记（治理提交在所有者裁决 319f9e9 之后）：前次实现 40dc8e877ea43748917e2869e174c54390c430db（基点 e239e77，早于所有者修订 35acbd7，未签署）。独立复跑证据 out/test-evidence/wp-01/20260831-40dc8e8-verify/t04-independent-verification.md。静态面合规：仅 2 允许文件、T01/T03 脚本零改动、禁项/缓存白名单通过、package.ps1 PS5.1 解析通过、提交标题与卡一致。阻塞四项：①卡内必执行命令结构性不可满足——模型测试通配 regex `^sdurws_ird_.*_test$` 匹配 10 个已注册测试但 T03 冻结 build.ps1 仅构建 2 目标（独立复跑退出码 8，9/10 Not Run），package 的 cmake --install 需全树工件而两目标构建缺 pqp.lib（独立复跑退出码 1）；修复路径（改 build.ps1/CMake/缩窄 regex）均越出本卡允许范围或违反逐字符一致契约，须所有者修订卡/计划或另立卡；②remote 为 GitHub、无 GitLab Windows Runner，卡内步骤 6 与 Runner 证据不可产出，卡内停止条件已触发而未升级；③修订后卡要求未实现：yml 无 CMAKE_PREFIX_PATH 流水线变量、一致性表无前缀来源行、证据未登记两级证据根（计划 §5.4/§5.5）；④实现者证据无一必执行命令通过（configure 前缀未设失败、Debug 零匹配、Release 退出码 8 且跨 worktree 消费主树构建目录违反 §5.6.4、package 退出码 1、build/边界无日志），RED t04-fail-blocks 仅叙述清单无执行记录。返工条件见验证报告 §5（所有者裁决上游冲突、集成负责人 Runner 裁决、实现者按修订卡于干净 worktree 重做并以 §5.6 准备环境）。解阻塞依据：blocked_by 两项均已裁决——上游契约冲突与 Runner 豁免经所有者修订 319f9e9 冻结（四项裁决：模型测试正则收敛 ^sdurws_ird_core_test$、package.ps1 改已构建目标集收集、GitLab yml 契约定义＋GitHub Actions 执行通道双文件豁免、GUI job 阶段 A 通道预检；返工指引 agent-tasks/rework/WP-01-T04-rework-guide.md）。DOCUMENT-BASELINE §4 九项门禁于 319f9e9 复判通过：①需求 v0.8 Accepted；②testing-contract（CTR-TST-001/002）Accepted（IRD-D14），无直接关联 ADR；③前置 WP-01-T03 Done（c161d6d）且四入口脚本＋check-boundaries.ps1 在库，main 产品代码自 c161d6d 起零变化（35acbd7/248a373/319f9e9 仅文档与证据）；④允许/禁止文件明确（白名单：yml 修改＋workflow 新建＋package.ps1 修改，禁改 industrialrobot/、旧插件与 T01/T03 脚本）；⑤流水线步骤顺序、模型/GUI 分 job、缓存白名单与前缀双声明由修订卡与计划 §9 唯一冻结；⑥验证命令完整且结构性可满足（原阻塞①②经裁决消除），逐字命令与预期退出码（0、1/1 通过）冻结于卡；⑦NFR-DEP-01/02 追踪链路经 T03 验证确认闭合；⑧文档门禁双 PowerShell 退出码 0（2026-08-31 复跑，证据 out/test-evidence/wp-01/20260831-248a373-owner-rev2/t04-unblock-ruling.md）；⑨无遗留跨模块语义决定（原阻塞③④已并入返工指引为实施者硬性要求）。实施者按返工指引于新分支 codex/wp-01-t04-gitlab-gate-r2（基点 ≥248a373，禁止复用/amend 40dc8e8）执行并产出新实现提交；本卡 Done 签署留待独立复跑验证；WP-01-T05 维持 Planned 至本卡 Done |
| WP-01-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3）；前置 WP-01-T04 当前 Ready 未 Done | - | - | - |
| WP-02-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-02-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-02-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-02-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-03-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-03-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-03-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-03-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-03-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-04-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-04-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-04-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-04-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-04-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-05-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-05-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-05-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-05-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-05-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-06-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-06-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-06-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-06-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-06-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-07-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-07-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-07-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-07-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-07-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-08-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-08-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-08-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-08-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-08-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-09-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-09-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-09-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-09-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-09-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-10-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-10-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-10-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-10-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-10-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-10-T06 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-11-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-11-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-11-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-11-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-11-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-12-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-12-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-12-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-12-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-12-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-12-T06 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-13-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-13-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-13-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-13-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-13-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-13-T06 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-13-T07 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-13-T08 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-14-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-14-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-14-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-14-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-14-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-14-T06 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-14-T07 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-15-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-15-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-15-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-15-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-15-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-15-T06 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-15-T07 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-15-T08 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-16-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-16-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-16-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-16-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-16-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-16-T06 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-16-T07 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-17-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-17-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-17-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-17-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-17-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-17-T06 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-17-T07 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-18-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-18-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-18-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-18-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-18-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-19-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-19-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-19-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-19-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-19-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-19-T06 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-19-T07 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-20-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-20-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-20-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-20-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-20-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-20-T06 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-20-T07 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-20-T08 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-21-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-21-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-21-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-21-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-21-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-21-T06 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-21-T07 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-22-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-22-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-22-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-22-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-22-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-22-T06 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-23-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-23-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-23-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-23-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-23-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-24-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-24-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-24-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-24-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-24-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-25-T01 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-25-T02 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-25-T03 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-25-T04 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
| WP-25-T05 | Planned | 前置 WP 与卡内前置字段（总纲 §5.3） | - | - | - |
