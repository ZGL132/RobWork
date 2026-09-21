# UI-T14（WP-10-T14）ird_gates 门禁命中集比对记录

执行方式：`cmake -DIRD_ROOT=<industrialrobot 根绝对路径>
          -DIRD_SELFTEST_DIR=<临时目录> -P cmake/ird_gates.cmake`
          脚本模式直跑（与 WP-10-T08~T13 留痕同引擎同口径）；base 基线以
          同引擎 detached worktree 方式对 base 提交
          11534184449b295252f346e460ced8306d935d0a（＝UI-T13 合入收尾状态，
          其后 6b7ffbc3/11534184 为纯文档修订不影响源码扫描面）复跑，双态
          同引擎同归一化口径，diff 零口径漂移。
          另以集成树 MSBuild 目标 `ird_gates` 与冒烟树同名目标复跑核对
          （命中集逐码一致——引擎为纯源码扫描，两树共用同一源码树）。

原始输出：builds/wp10-t14/ird_gates-integration.log（本分支集成树脚本模式
          直跑）、builds/wp10-t14/ird_gates-base.log（base worktree 基线
          脚本模式直跑）、builds/wp10-t14/ird_gates-smoke.log（冒烟树
          MSBuild 目标复跑——与集成脚本模式逐码一致）；归一化命中集见
          builds/wp10-t14/gates-hit-set.txt（逐码计数），SUB 命中逐行清单
          见 builds/wp10-t14/gates-hit-set-sub-lines.txt，逐码 diff 与新增
          命中全文见 builds/wp10-t14/gates-hit-set-diff.txt。

## 结论

与 base（11534184449b295252f346e460ced8306d935d0a）基线（逐码 LIB×8/
R-1×1/R-3×37/R-4×3/R-5×1/SUB×39/T-1×5/T-2×1）同引擎逐码比对：**七码
逐码零变化；仅 SUB 恰增 6 条原始命中（39→45）——引擎两遍扫描使每条命中
出现两次，去重后恰增 3 条唯一命中、零消失**。3 条新增命中全部为同一形态
（契约 acceptance 3 的 O-31 裁决测试面直链——units/ui.md §3.1 行 v0.4
原文"落位直链面……project/execution 直链随 UI-T14 契约测试套件落地登记；
测试目标链接面按本表承载，不属 ARCH §3.5 产品边管辖"，F-228 随之消解）：

| # | 命中码 | 链接边 | 说明 |
| --- | --- | --- | --- |
| 1 | IRD-GATE-SUB（测试目标直链他单元产品目标） | sdurws_ird_ui_contract_test → sdurws_ird_project | C-6 对端形状承载：project::ICommandInteraction 冻结点镜像（P-PR-7）的编译期捕获面——PeerBridgeContractTest 的 InteractionL5Adapter 以 override 实现对端接口＋双侧成员函数指针 static_assert |
| 2 | IRD-GATE-SUB（测试目标直链他单元产品目标） | sdurws_ird_ui_contract_test → sdurws_ird_execution | §12.1 第二层行"testkit FaultInterceptor 伪造 ITaskScheduler"的对端半区——ITaskScheduler 伪造＋TaskSnapshot→TaskViewProjection 投影桥（override＋字段访问编译期钉） |
| 3 | IRD-GATE-SUB（测试目标直链他单元产品目标） | sdurws_ird_ui_contract_test → sdurws_ird_diagnostics | 表内登记边 ui→diagnostics 值面的测试侧直链显式自证（RedactionPolicy 值面直用钉；经被测目标 PUBLIC 传染亦可达，直链为形状漂移捕获的链接期支撑） |

三条命中均属 O-31 裁决（2026-09-19，登记 DTB §4 O-31 行＋units/ui.md
§3.1/§16.7）确认的"ui 侧最小注入接口模式"配套测试面——对端类型形状漂移
由测试面捕获的机制载体；**产品面守卫不变**：`sdurws_ird_ui` 产品库链接块
仍仅 core/diagnostics＋Qt 三件套（CMake 配置期守卫硬断言＋
LinkageContractTest.ArchEdgesCoreDiagnosticsOnly_UI_CTR 链接块文本钉＋
BuildRedLineTest include 面扫描三道防线常驻，本任务对三者零改动）。
机器面回填（ird_gates SUB 规则对"ui.md §3.1 测试目标表登记的测试侧直链"
的豁免词表）责任方 WP-01-T03（F-221/F-228 同族）。

本任务其余新增/改动文件零命中：

- ui/contract_test/PeerBridgeContractTest.cpp　新契约测试 TU：测试目标
  源文件（R-3 扫描面限产品目标源集，测试目标零命中——与既有四个契约
  测试 TU 同案）；其 Qt 消费仅经 QCoreApplication（目标 main 已有，
  本 TU 零 Qt include）；
- ui/contract_test/Section12RegistryTest.cpp　新契约测试 TU：纯标准库
  ＋gtest 源码扫描用例，零 Qt 零对端类型；
- ui/test/AboutDialogModelTest.cpp / ui/test/TaskPresentationModelTest.cpp
  既有模型测试文件增用例：测试目标源文件零命中（后者新增
  QEventLoop/QTimer/thread/condition_variable 等 include——QEventLoop/
  QTimer 为 QtCore 组件，R-3 扫描面不含测试目标，同案零命中）；
- ui/CMakeLists.txt　contract_test 目标链接面增量（上表 3 条 SUB）＋
  两新 TU 注册＋注释块：产品库链接声明块逐字未动（守卫断言与
  LinkageContractTest 文本钉的扫描对象零变化）。
