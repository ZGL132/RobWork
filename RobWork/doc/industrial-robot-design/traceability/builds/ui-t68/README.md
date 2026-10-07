# UI-T68 构建与测试留痕（需求巡检取证通道清理批次）

分支 `ui-t68`（基点 redesign-main@48251bc5）；任务契约
tasks/foundation/UI-T68.json；DTB §2.11 WP-10-T68 行；units/ui.md v1.79
§13 UI-T68 行。批次性质＝acc/ui-t67/1 建议级 E-2~E-5＋F-537 随手 closure。

## 交付物

- **F-537 根因修复**（UiPlugin.cpp maybeRunRequirementsTour）：需求树发现
  锚全等「需求树」→前缀匹配——树头自 F-499 起更名「需求树（当前草稿）」，
  全等失配致 tree=nullptr→stationEntryCount 恒 -1（ui-t67 实录
  tree-child=-1 的真因＝锚过时，非时序竞态——本批 3s 有界轮询仍不现身即
  实证）；step2 工位新增改有界轮询（100ms×30 拍）。
- **E-5 名实相符**（拍 7.5 第二半重做）：按表头首列『级别』锚定逐项表
  （首版 findChild 误取层汇总表〔聚合行无锚〕）＋两半点击协议
  （setCurrentItem＋itemClicked emit）＋后置条件断言（点击行＝当前行＋锚
  列良构）＋断言更名 validation-row-click-protocol（ok(true) 恒真面退役）。
- **E-4 取证前提**：宿主窗体最大化＋需求 Dock show/raise＋step1
  panel-visible 断言——七帧 PNG 逐字节异哈希（ui-t67 五帧同哈希静窗闭合）；
  tour-4b 484B 空条→120KB 真实渲染。
- **E-3 判定行入档**：双驱动 driver-verdict.log（断言摘录＋exit＋判定行）。
- **E-2 补正**：builds/ui-t67/console-tour-75.log 按
  smoke-tour/console-tour.log:69~74 真实摘录填充＋ui-t67 README 补正注。
- **F-539 新登**（suggestion）：生产装配 locateSink 缺席——校验行定位链
  LocateTarget 产出即弃（setLocateSink 全仓零生产调用点），落点行为断言归
  gui 钉扎；7.5 拍断言面如实收窄为调用半区。
- **F-540 新登**（major，基线既有）：WP-22-T02 返工 3fdafa36 把元测试钉扎
  短语「ui 单元界面目标」拆行——ui_test 恒红（冒烟 238/239／集成
  253/254）；修复权归门禁机器面所有者，本批零触碰该面。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 受影响目标集成构建（ui_plugin＋studio——UiPlugin.cpp 仅此两目标编译） | 零错误 | 会话执行记录 |
| 独立冒烟（全树配置＋构建） | error C/LNK 计数＝0 | smoke-build.log |
| 冒烟 ctest ui 单元 | ui_test 238/239（1＝F-540 基线既有）＋ui_contract＋ui_gui PASS | smoke-build.log |
| 冒烟 ctest requirements 单元 | 3/3 全绿 | smoke-build.log |
| 集成相邻套件实跑 | ui_contract 32/32＋ui_gui 78/78＋requirements_gui 44/44；ui_test 253/254（1＝F-540） | smoke-build.log（口径行） |
| 真机 requirements-tour | TOUR_PASS（exit=0；step2 tree-child=1＋7.5 三断言＋panel-visible） | smoke-tour/console-tour.log＋driver-verdict.log＋7 PNG |
| 真机 modeling-tour（回归） | MTOUR_PASS（exit=0；F-536 gap 断言保持〔登记号更正 2026-10-07——原稿沿旧号 F-524，登记库该号＝ui-t64 编码损坏；本缺口＝F-536（F-543 传证）〕） | smoke-tour-modeling/console-mtour.log＋driver-verdict.log＋6 PNG＋irdbundle |
| 截图唯一性（E-4） | 七帧逐字节异哈希 | smoke-tour/*.png（md5 七值全异） |
| ird_gates | 零命中 exit 0＋引擎自测 9 项符合预期 | ird-gates.log |
| validate-docs／validate-task（UI-T68.json） | PASS（20 units/308 task files）／PASS | 会话执行记录（显式核验输出行） |

## 诚实边界

- **ui_test 1 例红（F-540）＝基线既有**，引入点 3fdafa36（WP-22-T02 返工，
  并行工作流批次）先于本批分支基点 48251bc5；本批零触碰白名单/元测试文件，
  修复权归门禁机器面所有者（findings F-540 owner 行二选一口径）。
- 7.5 拍定位链落点行为（树滚动＋三维高亮）生产不可达（F-539）——本拍断言
  面＝点击协议落地＋锚列良构；落点读回断言待 F-539 修复后升级。
- modeling/kinematics 等非相邻套件未在本批实跑（本批 diff 不触其编译面
  ——UiPlugin.cpp 仅 plugin/studio 两目标编译，CMake 实证；归验收复现口径）。
- 集成 ui_test 的 F-540 一例使「全套件全绿」口径不成立——各套件计数如实
  分列，不带病前进。
