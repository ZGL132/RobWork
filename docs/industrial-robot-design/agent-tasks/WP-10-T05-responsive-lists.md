# WP-10-T05 响应性与大列表虚拟化

- **Task ID / 需求 ID / ADR / 阶段：**WP-10-T05；NFR-PERF-01、NFR-PERF-03、UX-02/UX-06；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/session-ui.md` v0.4 §8.2、§8.7～§8.8，`architecture/testing-contract.md` §2
- **前置任务及必需工件：**WP-10-T02（`SelectionModel`/投影工件）；WP-10-T03（`EngineeringTableView` 分页接口工件）；WP-08-T01（`IEvaluationScheduler`/`TaskSnapshot` 端口——端口契约前置，签名按 public-interfaces §4 已冻结，本卡以契约测试替身先行，集成期接 WP-08 实现）；WP-09-T04（脱敏日志公共头）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/VirtualResultModel.hpp`；`ui/src/VirtualResultModel.cpp`；`ui/test/ResponsiveListsTest.cpp`；`ui/testdata/perf/`（5,000 任务/100,000 摘要/10,000 候选生成器，不含手工巨文件）；`ui/out/test-evidence/wp-10/<run-id>/`；`ui/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**分页契约（WP-10-T03 冻结的 `setPage/window`）、`IEvaluationScheduler` 签名；禁止一次性装载全部明细、GUI 主线程执行评估/文件 IO、伪造性能数据；`architecture/`、`module-design/`、其他模块目录
- **修改前接口：**无（旧插件全量装载结果列表，主线程长阻塞）
- **修改后接口：**`VirtualResultModel`（QAbstractItemModel 子类）：`windowCount()/fetchWindow(offset,n)` 按需取数、后台加载经 queued signal 返回不可变值；规模 5,000 任务/100,000 摘要/10,000 候选下仅实例化可视窗口行
- **实施步骤：**1) 生成器产测试数据集（规模、行字段固定）；2) 虚拟模型＋窗口取数＋后台 prefetch；3) 超过 1 s 的工作转后台（经 scheduler 替身提交）；4) 计时探针采集 P50/P95/主线程阻塞窗口并写证据
- **RED 测试：**Given 100,000 行摘要，When 直接实例化旧式全量模型（对照组替身），Then 内存/首帧断言失败——证明必须虚拟化（`ResponsiveListsTest` 先行，虚拟模型未实现时 RED）
- **最小实现：**窗口取数＋选中行按需加载明细＋一处后台转移路径；不做排序/多级缓存等增强
- **正常/边界/失败测试：**
  - 正常：Given 5,000 任务/100,000 摘要/10,000 候选，When 滚动/筛选/选择，Then 只加载可视窗口与按需明细，实例化行数 ≤ 窗口×安全系数
  - 边界：Given 导航/选择/筛选/编辑操作序列和 100%/125%/150% 缩放，When 测量，Then P95 ≤ 200 ms；超过 1 s 的工作全部转后台；主线程连续阻塞 ≤ 2 s；底部表格按窗口取数且不挤压中央三维视图
  - 失败：Given 后台窗口加载失败（替身返回 `EvaluationError`），When 滚动到该窗口，Then 显示可重试占位、已加载数据不变、无崩溃
- **精确验证命令**（仓库根；GUI 测试须 Visual Studio x64 环境，`QT_QPA_PLATFORM=windows`，一次只启动一个 GUI 测试可执行文件；性能数据集与统计口径引用 `benchmark-manifest.json`）：
  ```powershell
  $env:QT_QPA_PLATFORM='windows'
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_ui_widget_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_ui_widget_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_ui_widget_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -n "evaluate|readFile|QFile" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/src/VirtualResultModel.cpp; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中（主线程无评估/IO）；`git status` 确认 testdata 生成器不产生入库巨文件
- **证据工件：**`ui/out/test-evidence/wp-10/<run-id>/`——规模与实例化行数对照、P50/P95 表、主线程阻塞窗口日志、后台转移计数、环境（CPU/内存/Qt 版本）记录与评审者签署
- **提交格式：**`WP-10-T05: 新增虚拟化响应式大列表`

  - 新增 VirtualResultModel 窗口取数与后台加载实现
  - 新增 规模/性能回归测试与数据生成器登记
  - 新增 P50/P95 与阻塞窗口证据记录
- **停止与升级条件：**性能结果不可复现（两次运行 P95 差异超统计口径）、主线程必须调用业务计算、或列表必须全量加载时暂停；WP-08 端口签名变更时升级联合评审，不修改替身语义迁就实现
