# POL-T13 实施验证记录

本记录属于实施者自查，不是独立验收。原专项评审见 [POLICY-REVIEW.md](POLICY-REVIEW.md)，取舍、12项整改与未决边界见 [IMPLEMENTATION.md](IMPLEMENTATION.md)。机器记录含每次测试命令、退出码、XML/日志/原生报告链接，见 [verification.json](verification.json)。

## 环境与范围

工作树为 `C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork`，分支 `codex/core-review-fixes`。实施源码起点 `59af1f9b449ee23fb78dfe547b3a7339df9d325d`；本包提交范围基点 `fd198f54c0b66bc61dd64b29a04e2b1675f42dbc`，其间两提交仅为另一会话的 PRJ-T19 验收登记。

集成构建使用本 C 盘工作树源码、Release/MSVC x64，`RWS_BUILD_INDUSTRIALROBOT=ON`；D 盘 vcpkg 只作只读依赖。独立冒烟使用 `build/core-smoke`。没有运行 D 盘源码构建，没有执行远程命令。

此次修改碰撞公共头、宿主装配和消费者覆盖判定，因此验证扩大至 policy、kinematics、diagnostics、runtime 名称/快照相关套件、UI 运动学通道，以及插件和 Studio 构建。UI 为 QCoreApplication 模型测试，未声称执行真实 Widgets 人工点验。

## 最终构建与测试

集成构建成功：policy_test、policy_contract_test、diagnostics_test、runtime_test、kinematics_test、ui_test、ui_plugin、studio。原始日志为 [green-build-1.log](raw/green-build-1.log)、[consumer-build.log](raw/consumer-build.log)、[final-consumer-build.log](raw/final-consumer-build.log)。最后一份覆盖最终首末帧别名修正及夹具修正。

独立冒烟 policy_test、policy_contract_test、diagnostics_test 构建成功，见 [smoke-build.log](raw/smoke-build.log)。日志保存原始构建输出，未重新配置无关目标。

| 模式/套件 | passed | skipped | failed | 原始证据前缀 |
| --- | ---: | ---: | ---: | --- |
| 集成 policy 全套 | 228 | 0 | 0 | raw/green-policy |
| 集成 policy contract 全套 | 24 | 0 | 0 | raw/green-contract |
| 集成 diagnostics 全套 | 123 | 0 | 0 | raw/green-diagnostics |
| 集成 runtime 名称/快照/WorkCell/HostPresentation | 97 | 0 | 0 | raw/green-runtime |
| 集成 kinematics 全套 | 297 | 1 | 0 | raw/final-kinematics |
| 集成 UI 通道/锚/会话视图 | 30 | 0 | 0 | raw/final-ui |
| 独立冒烟 policy | 141 | 0 | 0 | raw/smoke-policy |
| 独立冒烟 policy contract | 9 | 0 | 0 | raw/smoke-policy_contract |
| 独立冒烟 diagnostics | 123 | 0 | 0 | raw/smoke-diagnostics |

每个前缀对应 `.log`、`.xml` 和同名目录下 `ird-test-report.json`。kinematics 的既有 `PluginPanelLinks.KinGuiPresentationRegisteredNotExecuted_ACC1` 在 gtest 中 skipped=1，原生报告记 envUnavailable=1、skipped=0、decisive=false；不能计为全项 PASS。

## 红例及中途失败

- 首批 policy 回归：1 passed / 0 skipped / 12 failed；正式诊断注册契约：0 / 0 / 1，均真实执行，见 raw/red-policy、raw/red-registry 对应输出。
- 诊断测试最初使用不存在字段导致编译错误，随后改为 `entry.record.code`，成功构建后才运行红例。编译错误不计有效红例。
- 首次 kinematics 全套 295 passed / 1 skipped / 2 failed；修正缺几何旧预期与配置快照夹具闭包后，最终完整套件结果见上表。未改变 evidence 产品规则。
- 首次可启动的 UI 通道回归 29 passed / 0 skipped / 1 failed；定位为逻辑 LinkFrame 与首末物理 BaseFrame/Flange 别名不一致，改为精确名称表范围选择，最终30项通过。
- 更早一次 UI 启动退出码 `-1073741515`，未进入 gtest，状态 **NOT RUN**。按 `ird-python313-dll` 技能核查既有 `SDURWS_IRD_PYTHON_DIR=D:/software/Miniconda3`，仅在当前进程 PATH 前置该目录后重跑。未复制 DLL、未修改持久环境。

## 数值与运行测量

真实上游盒几何覆盖两独立 WorkCell 的解析分离/相交判断；新正式回归另覆盖空/部分拒收 mesh、非有限 q/path 参数、失效、非法枚举、过滤与距离摘要、非有限/极宽/次正规限位。脚本距离替身验证协议，不作为真实距离后端正确性的证据。

[微基准源码](raw/PolicySessionBenchmark.cpp)与[编译脚本](raw/build-benchmark.ps1)可重放，Release 下1000次交替场景会话创建与求值、5000次单会话求值，解析判断 failed=0。创建并求值 p50=124.6 μs、p95=201.2 μs，单会话求值均值2.37718 μs，见 [benchmark.json](raw/benchmark.json)。没有冻结性能预算与旧版本对照，此数据不等于正式性能独验通过。

## 门禁与送验边界

依赖红线 R-1～R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 扫描及全部引擎正负例自测通过。撤销 runtime proximity 豁免，仅精确登记 policy_contract_test→diagnostics 测试边；没有修改门禁引擎或扩大通用准入。

完整 `ird_gates` **FAIL**，见 [ird-gates-final.log](raw/ird-gates-final.log)：依赖扫描和引擎自测通过，随后复用检查报 `REUSE-FRESHNESS : runtime/CMakeLists.txt`。文档联合入口 `pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/POL-T13.json` 同样 **FAIL**，见 [validate-docs-4.log](raw/validate-docs-4.log)。早期新任务缺锚点、既有授权/矩阵摘要未同步的问题已修正，原失败日志保留。

当前阻断来自本包删除重复设施后变更的 runtime CMake：REUSE-T13/T30/T52/T53 既有生产报告仍冻结绑定旧 CMake 摘要 `5ef2e38b…`，当前为 `23b072f9…`。当前功能矩阵及 permissions 只刷新活跃来源/构建绑定；历史报告不覆盖、不改写，不以新摘要冒充旧生产实验重新验收。相关生产证据适用性与重新绑定需在本包独立验收时核对。这是本轮构建清理触发的绑定影响，不能称为不相关全局失败；也不能据此声称依赖能力本身已错误。

既有 pending/proposal 保留；[binding-audit.json](binding-audit.json)记录受影响来源中的59项未决功能与16个文件的非空行覆盖缺口。此为结构筛查，未决项未被新业务行覆盖以规避准入；机器检查停在上述生产证据绑定，未证明后续全部准入项通过。摘要/行号刷新只恢复可追溯性，不代表新增权限或关闭语义审计。

Group schema、真实距离后端、完整 Workpiece/Payload 角色供给、bool 严格编码仍待原裁决；同帧多 Object 的资源/呈现几何供给未在本包扩大。宿主回归用缺几何发布快照验证规则接线与缺证，不能替代完整生产场景碰撞/GUI验收。

独立验收 **NOT RUN**，由所有者另开会话针对送验 SHA 执行；不自签 pass、不推进 execution。回滚应整体撤销本包生产/测试/构建登记和对应文档/绑定，保留历史验证与他会话改动。
