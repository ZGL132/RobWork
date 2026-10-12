# ui-t77——三维预览专项（F-550 骨架呈现＋F-555 标记位置＋F-556 中文标签）验证留痕

日期：2026-10-08
分支：`ui-t77`（基点 redesign-main@a30f0a94）
范围：findings F-550（发布后模型三维可见）/ F-555（工位标记 position 字段）/ F-556（标签 CJK 乱码）集中修改。

## 改动清单

| 层 | 文件 | 内容 |
| --- | --- | --- |
| 契约 | `ui/include/sdurws/ird/ui/View3DPreviewContract.hpp` | `View3DFrameMarker` 表尾追加 `optional<Vector3D> position`（世界系 m；nullopt＝挂帧旧语义——UI-T65 tint 同款追加纪律） |
| requirements 面板 | `requirements/plugin/RequirementsPanelWidget.hpp/.cpp` | `StationMarkerProjection` 追加 position（`TaskPoint.pose.position` 四态 tryValue——缺失不转零） |
| requirements 门面 | `requirements/assembly/.../RequirementsPluginAssembly.hpp`、`requirements/plugin/RequirementsPluginAssembly.cpp` | `StationMarkerView` 表尾追加＋同构直转 |
| ui 投影绑定 | `ui/plugin/UiPlugin.cpp` | 标记 sink：World 恒等直投／对象引用系 `findFrame`＋`worldTframe` 变换；帧不可解析＝position 置空诚实降级 |
| ui 文本承载 | `ui/plugin/QtTextBillboard.hpp/.cpp`（新增） | Qt 离屏排版→GL 纹理（进程级缓存＋上下文更换防御）→billboard 四边形；F-556 方案②替代 RenderText 用法 |
| ui 渲染后端 | `ui/plugin/HostView3DPreviewBackend.hpp/.cpp` | 标记双形态：position 有值＝`StationMarkerRender`（三轴＋标签一体）挂 WORLD；无值＝既有挂帧路径（标签换 QtTextBillboardRender） |
| ui 发布呈现 | `ui/plugin/HostPresentationAdapters.hpp/.cpp` | `apply()` 内 setWorkCell 后挂发布模型骨架（`ird-model-skeleton-` 组）：全帧小轴阵＋SerialDevice 连杆链线（实时 State 跟随）＋设备基座/末端标签 |
| ui 主题词表 | `ui/include/sdurws/ird/ui/UiTheme.hpp` | 追加 kMarkerLabel（标签字色）＋kAxisX/Y/Z（坐标轴惯例色）词表 |
| CMake | `ui/CMakeLists.txt` | QtTextBillboard.cpp 编入 ui_plugin／ui_studio／sdurws_ird_ui_test 三目标 |
| 测试 | `requirements/gui_test/RequirementsView3DFlowTest.cpp`、`ui/test/View3DPreviewMarkerPositionTest.cpp`（新增，集成树专属门控块编入） | position 追加纪律钉住＋投影链（Provided 透传/NotProvided nullopt/禁用过滤）用例。注：契约值面测试独立 TU 且集成树专属——View3DPreviewContract 携带 rw/math 头依赖，ui_test 主列表是模型层测试（冒烟模式无框架目标、core 冒烟分支不链 sdurw_math——双模式既有设计），HostCompilePipelineTest 门控块同款形态 |

## 验证（How）

- 集成模式：`RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`（构建前 grep 确认）；受影响目标编译链接零错误
  ——`sdurws_ird_requirements_plugin`、`sdurws_ird_ui_plugin`、`sdurws_ird_ui_test`、
  `sdurws_ird_requirements_gui_test`、`sdurws_ird_requirements_test`、`sdurws_ird_requirements_contract_test`。
- 测试执行（gtest XML＋控制台输出在本目录）：
  - `sdurws_ird_ui_test`：255/255 通过（基线 254；撤 View3DContract 临时挂载后新增
    View3DPreviewMarkerPosition.FrameMarkerPositionAppendOnly_UI_T77——一减一加持平）；
  - `sdurws_ird_requirements_gui_test`：46/46 通过（含新增 StationMarkersCarryPositionWhenProvided_UI_T77）；
  - `sdurws_ird_requirements_test`：200 通过＋1 SKIPPED（GuiRegistration.V22RegisteredNotExecuted——§10.2 登记不执行，既有设计）；
  - `sdurws_ird_requirements_contract_test`：16/16 通过。
- 门禁（ird_gates / gate-all.ps1）：**本轮未全绿——如实登记**：所有者的产品实例
  `sdurws_ird_studio.exe`（PID 47992）正在运行，构建树 `build/RobWorkStudio/bin/Release`
  下框架 DLL 的 POST_BUILD 部署（copy_if_different/applocal）被进程锁住（Permission denied），
  导致全目标「构建」步骤以非零退出码被门禁判 FAIL——编译与链接本体均零错误（测试 exe 已产出并全绿）。
  所有者关闭产品实例后重跑 `gate-all.ps1` 即可复核；`sdurws_ird_studio` 完整链接同因待重试。
- 独立冒烟模式：临时目录独立配置（vcpkg toolchain 绝对路径＋Qt 前缀）**全量构建零错误**
  （含修复后的 ui_test——契约值面测试 TU 已归入集成树专属门控块，冒烟编译不再触 rw 头）。

## 边界与诚实声明

- F-550 本轮交付＝**发布后模型骨架呈现**（帧结构/运动链可视化）；真实 mesh 几何渲染
  仍归 WP-10-T05 资源链（四段缺口：mesh 解析加载器/GeometryRef.localTransform 编译域
  透传/S6 挂接/L5 装配；当前模板模型 resourceManifest 为空）——findings F-550 owner 行已登记。
- F-556 裁决＝登记三选一中的**方案②**（Qt 纹理化文本替代件）；标签世界尺寸固定 0.06 m
  （自适应尺寸随文本渲染基建任务演进）。
- 宿主端到端复验（增工位→标记渲染于坐标点；中文标签正常显示；应用草稿→三维出现模型骨架）
  待所有者换装新 exe 后执行，findings 翻转 fixed 以复验为准。

## 合流复验（2026-10-10，merge aa9f7aa3 后）

- 显式重配（F-623：防 VS 增量构建新目标静默缺位）`RWS_BUILD_INDUSTRIALROBOT:BOOL=ON` → 全量构建零错误；
- 七套件直跑全过（ui/requirements×3/modeling×2/kinematics——exit=0）；
- gate-all **85/85**（`-QtPrefix D:/software/Qt/6.11.1/msvc2022_64`，日志 logs/gate-post-merge-85of85.log）；
- findings.json F-555/F-556 按所有者预裁决（「状态翻转随 ui-t77 合流批次办理」）翻 fixed（fixedAt=2026-10-10）；
- 验收记录：traceability/acceptance/UI-T77-20261010.md（acc/ui-t77/1@3b5ffa97，verdict=pass——独立子会话对抗式验收）。

## 第二批：F-551/F-553 呈现缺陷修复批（2026-10-12，基点 redesign-main@38af404e）

范围：findings F-551（需求域 apply 对象树显示名为空）/ F-553（命令置灰禁用原因无 tooltip），
均在分支 `ui-t77` 上实施（本节与其上两节三维预览批次同分支异批——两批提交以 commit 为界）。

### F-551——需求域对象显示名（呈现层最小改法，owner 出路 2「树模型名称解析覆盖需求域对象」）

| 层 | 文件 | 内容 |
| --- | --- | --- |
| requirements 域 | `requirements/plugin/HostMigrationProviders.hpp/.cpp` | 新增 `RequirementsNameResolver`（`ui::IUiNameResolver` 实现）：工作集现取语义名（根→`RequirementSet.name`；点/区域/工况→条目 `name`；计划→「计划 i」沿用 PanelTreeModel 既有口径）；闭包外/空名/无会话→nullopt（UX-02 占位兜底不破坏） |
| requirements 模块 | `requirements/plugin/RequirementsUiModule.hpp/.cpp` | 新增 `sharedNameResolverFrontend()`（惰性构造＋缓存；deps 与三接入面同源绑定） |
| requirements 门面 | `requirements/plugin/RequirementsPluginAssembly.cpp`、`requirements/assembly/.../RequirementsPluginAssembly.hpp` | 转发出口（宿主消费面；失败隔离缺席＝空指针） |
| ui 宿主 | `ui/plugin/UiPlugin.hpp/.cpp` | `m_nameResolver` 成员类型改接口（`shared_ptr<ui::IUiNameResolver>`）；`assembleSharedSurfaces` 需求域可用时升级端口（三个消费面同持 shared_ptr 一处升级全覆盖；桩语义＝前端未命中 nullopt，runtime 真值链将来装配点已注释登记） |
| 测试 | `requirements/test/HostMigrationTest.cpp` | 2 用例：`NameResolver_ClosureObjectsAnswerWorkingSetNames_F551`（五对象语义名）、`NameResolver_ForeignOrSessionlessAnswersNothing_F551`（闭包外/无效身份/无会话→nullopt） |

### F-553——禁用原因 tooltip 传递链（断点＝refreshHostMenuActions 丢弃 reasonKey）

| 层 | 文件 | 内容 |
| --- | --- | --- |
| ui 文案 | `ui/include/sdurws/ird/ui/UiText.hpp`、`ui/src/UiText.cpp` | 新增 `disableReasonToolTip(enabled, reasonKey, baseToolTip)`：禁用→统一原因解析；可用→还原基础文案；空键→F-557 同面守卫（单一书写点，防消费点分岔漂移） |
| ui 宿主 | `ui/plugin/UiPlugin.hpp/.cpp` | 菜单动作登记结构体化（`HostMenuCommandAction`——装配拍快照 baseToolTip）；`addCommandAction` 增基础文案参（F-501 三条长说明迁移）；`refreshHostMenuActions` 补 `setToolTip(disableReasonToolTip(...))`——与顶栏按钮先例（WorkbenchContent refreshCommandStates）同口径 |
| 测试 | `ui/test/ShellModelTest.cpp` | 2 用例：`DisableReasonToolTip_DecisionMatrix_F553`（决策矩阵）、`DisableReasonToolTip_AllRegistryReasonKeysRegistered_F553`（六原因键词表完备性——防菜单刷新遇未登记键 fail-fast） |

### 验证（How）

- 集成模式全量构建：`RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`（构建前 grep 确认）→
  `cmake --build build --config Release` 退出码 0、错误扫描零命中（logs/f551-f553-integration-build.md）。
- 测试执行（gtest XML＋控制台输出在本目录 logs/，前缀 `f551-f553-`）：
  - `sdurws_ird_ui_test`：260/260 通过（基线 258＋本批 2）；
  - `sdurws_ird_ui_contract_test`：35/35 通过；
  - `sdurws_ird_ui_gui_test`：78/78 通过；
  - `sdurws_ird_requirements_test`：213 通过＋1 SKIPPED（GuiRegistration.V22RegisteredNotExecuted_ACC1——§10.2 登记不执行，既有设计；含本批 2 用例）。
- 门禁：`cmake --build build --config Release --target ird_gates` →
  R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 全部检查通过零命中（logs/f551-f553-ird-gates.log）。

### 边界与诚实声明

- F-551 修复为呈现层（名称解析覆盖），apply 载荷/对象名称语义零触碰；两处名称口径来源
  已在代码注释登记（根＝对象本体 `RequirementSet.name`；计划＝PanelTreeModel「计划 i」既有口径）。
  若所有者对需求集根/计划的树显示名词形另有裁决，属后续单元卡增量修订，不阻塞本呈现修复。
- F-553 的宿主端到端复验（置灰「应用草稿」悬停应显示「存在未就绪阻断项——先处理校验页阻断」等
  原因文案）待所有者换装新 exe 复验后由所有者翻 findings 状态（本实施会话不自行翻转）。
