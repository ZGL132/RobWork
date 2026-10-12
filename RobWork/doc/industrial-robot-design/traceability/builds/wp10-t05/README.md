# WP-10-T05（UI-T78）验证留痕——三维模型几何渲染＋ray-cast 拾取对接（阶段 B 三维交互批次 ui 侧）

任务：UI-T78（契约 tasks/foundation/UI-T78.json；F-550 剩余面收口——发布后模型渲染＋拾取可达）
分支/基线：wp10-t05（base＝redesign-main@8e494e59 立卡治理提交后；前置分支 rt-t15〔57a8b26a——RT-T15 图元几何挂接〕快进并入）
日期：2026-10-12

## 实施摘要

| 面 | 落位 | 机理（实施期实测） |
| --- | --- | --- |
| 渲染面 | ui/plugin/HostPresentationAdapters.{hpp,cpp}：`collectPublishedModelDrawableNames`（规划半区）＋`calibratePublishedModelRenderGroups`（落位半区）＋`kPublishedModelRenderGroup`（单一书写点）；`HostWorkCellPresentationObject::apply` 挂接序＝setWorkCell→分组校准→骨架呈现 | RT-T15 图元 drawable 经框架 updateSceneGraph 以 Geometry 构造缺省分组落景（**CollisionObject 组**——rw/geometry/Geometry.cpp:47 构造函数实测），而产品主相机缺省绘制掩码只含 Virtual/Physical/DrawableObject（SceneOpenGLViewer.cpp:184-192——`ShowCollisionModels` 属性缺省 false；rwsettings.xml:250 既有值 0）——不校准则发布模型几何在缺省掩码下**整体不可见**＝F-550 渲染剩余面的真实机理。校准为 Physical 组：缺省可见＋受渲染分组显隐菜单管辖；与 UI-T77 骨架组（Virtual）正交分层 |
| 拾取面 | 零产品代码改动——既有 HostView3DGateway.pickFrame 缝＋UiPlugin Ctrl+双击五拍链（UI-T45）原样覆盖发布模型命中面 | 框架 RWStudioView3D::pickFrame（RWStudioView3D.cpp:403-413）＝pickDrawable→WorkCellScene::getFrame 回映射——RT-T15 图元 drawable 入场景后命中即回映射其挂接帧（运行时映射全名）→SA-05 反解→域分发＋View3DPick 选中。gui 用例钉住可达性与 KIN-06 写回语义 |
| 九项清单 | 逐项核对登记（下表）——宿主原生项零重实现（SA-02） | 见下表框架 API 锚点 |
| 文档同步 | units/ui.md v1.97（版本头＋§13 UI-T78 行落位标记＋§14.1 补落位注＋§16.7 v1.97 行）；契约 UI-T78.json 实施段收编三处（RT-T15 卡同款先例，非语义裁决，status 仍 ready） | 见 ui.md §16.7 v1.97 |

## UX-11 九项清单逐项核对登记（acceptance 3）

| # | 契约 id | 提供方式 | 核对结论（本批实测框架锚点） |
| --- | --- | --- | --- |
| 1 | view3d.host | 宿主原生 | ✓ 在位——rws::RWStudioView3D（RWStudioView3D.hpp:69，QWidget）集成宿主中央区（O-38③ 裁决；产品装配 UiPlugin.cpp assembleView3DGateway:3645） |
| 2 | view3d.standard_camera_views | 宿主原生 | ✓ 在位——标准视图八动作 _homeView/_axometric/_front/_right/_top/_rear/_left/_bottom（RWStudioView3D.hpp:266）＋相机视图 _addCameraView/_clearCameraViews/_selectMainView（:271）＋CTRL+Left/Right/1..9 切换（:180-188 keyPressEvent 表） |
| 3 | view3d.zoom | 宿主原生 | ✓ 在位——_zoomInAction/_zoomOutAction/_zoomAutoAction（RWStudioView3D.hpp:279）＋zoomInSlot/zoomOutSlot/zoomAutoSlot（:206-208） |
| 4 | view3d.projection_toggle | 宿主原生 | ✓ 在位——_setPerspectiveViewAction/_setOrthographicViewAction（RWStudioView3D.hpp:273） |
| 5 | view3d.wireframe_transparency | 宿主原生 | ✓ 在位——setDrawType(SOLID/WIRE/BOTH)（:104）＋_showSolid/_showWire/_showOutline/_showTransparent 四动作（:257-260） |
| 6 | view3d.render_group_visibility | 宿主原生＋**本批校准落位** | ✓ 在位且对本批模型 drawable 生效——"Render groups..."菜单八组开关（RWStudioView3D.cpp:709-717；Physical/Virtual/Drawable/Collision/User1-4——hpp:275-277）＋主视图掩码合成（SceneOpenGLViewer.cpp:184-192）。本批模型 drawable 经呈现分组校准归 Physical 组——**kPublishedModelRenderGroup 钉扎用例**（ui_test PublishedModelGeometry_CalibrationGroupBindingIsPhysical_UI_T78）＝组显隐管辖的机制绑定；骨架/预览层照旧 Virtual 组不受本批影响 |
| 7 | view3d.collision_highlight_mask | 宿主原生（渲染承载在位；业务数据面归域侧——诚实边界） | ✓ 承载在位——_checkForCollision（:262）＋setCheckForCollision（:210）＋碰撞对 setHighlighted 高亮（cpp:318-321）＋_qryResult（:284）。**业务数据面（碰撞会话的计算/触发）归域侧消费任务**（卡面 acceptance 3 诚实边界登记，本批不虚构交付） |
| 8 | view3d.screenshot_png | 宿主原生 | ✓ 在位——saveBufferToFile（:166-170）＋_saveBufferToFileAction（:263）＋CTRL+G 文件对话框路径（:180 keyPressEvent 表；cpp saveBufferToFileDialog） |
| 9 | view3d.pick_ray_cast | 阶段 B 网关交付（UI-T45 既有）＋**本批命中面核对** | ✓ 可达——框架 pickFrame（:119；cpp:403-413 ray-cast→getFrame 回映射）＋网关 handleViewDoubleClick 五拍链（Ctrl+双击→反解→分发→View3DPick）。本批具名用例：UpstreamPick_PublishedModelFrame_ReachesDispatchAndSelection_UX11_AT23_UI_T78＋UpstreamPick_WriteBackStaysSessionPose_KIN06_UI_T78（ui_gui_test） |

## 测试结论

| 目标 | 结果 | 基线对照 |
| --- | --- | --- |
| sdurws_ird_ui_test | **261/261 通过**（gtest-ui.xml／ird-test-report-ui.json／ui-test-console.log） | 基线 258＋新增 UI-T78 渲染面 3 例 |
| sdurws_ird_ui_contract_test | **35/35 通过**（gtest-ui-contract.xml／ird-test-report-ui-contract.json） | 零变化（全量回归绿） |
| sdurws_ird_ui_gui_test | **80/80 通过**（gtest-ui-gui.xml／ird-test-report-ui-gui.json；§12.2 口径：QT_QPA_PLATFORM=windows＋绝对路径单实例——本机桌面会话执行，零环境障碍） | 基线 78＋新增 UI-T78 拾取面 2 例 |
| ctest --test-dir build/RobWorkStudio/src/rwslibs/industrialrobot/ui -C Release | 3/3 通过（ird 2＋ird_gui 1） | — |

新用例（acceptance 具名自证）：
- ui_test（test/HostCompilePipelineTest.cpp，真实编译链——RT-T15 增强后产物）：
  - PublishedModelGeometry_CalibrationPlanCoversCompiledPrimitives_UI_T78（acceptance 1 渲染面：双关节本地设计真实十段链编译→校准清单恰好覆盖发布模型 drawable 全集＋圆柱/盒两族各至少其一在册＋图元帧绑定）
  - PublishedModelGeometry_CalibrationDeterministicAcrossRecompile_UI_T78（acceptance 1 生命周期：同模型两次编译清单逐元素相等〔NFR-COR-01〕＋重编译产物 WC 新实例＝框架 setWorkcell 换 WC 整树重建零残留的机制自证）
  - PublishedModelGeometry_CalibrationGroupBindingIsPhysical_UI_T78（acceptance 3 清单边界：渲染分组显隐对本批 drawable 生效的机制绑定钉扎）
- ui_gui_test（gui_test/HostView3DGatewayGuiTest.cpp）：
  - UpstreamPick_PublishedModelFrame_ReachesDispatchAndSelection_UX11_AT23_UI_T78（acceptance 2 拾取面：发布模型帧 pickFrame 命中→SA-05 反解→两域分发→View3DPick 选中——F-550 step6 收口自证）
  - UpstreamPick_WriteBackStaysSessionPose_KIN06_UI_T78（acceptance 2 写回语义：view3DSessionPoseContract 四布尔逐位＋链路副作用仅会话级选中/域分发）

## 构建结论（双模式）

| 模式 | 命令面 | 结论 |
| --- | --- | --- |
| 集成模式 | `cmake --build build --config Release --target sdurws_ird_ui_plugin sdurws_ird_ui_test sdurws_ird_ui_contract_test sdurws_ird_ui_gui_test`（缓存 RWS_BUILD_INDUSTRIALROBOT:BOOL=ON 构建前 grep 确认） | 零错误 |
| 独立冒烟 | 全树 `C:/Users/zgl18/AppData/Local/Temp/build_smoke_t78-ui`（cmake -S industrialrobot＋vcpkg toolchain 绝对路径＋Qt prefix D:/software/Qt/6.11.1/msvc2022_64；全新目录） | 见 smoke-build.log |

## ird_gates 结论（base..head 归一化比对）

- 引擎直跑：head（wp10-t05 工作树）与 base（8e494e59，detached worktree C:/Users/zgl18/AppData/Local/Temp/t05_base_wt，跑后已清理）各自 **exit 0＋"全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中"**；IRD-GATE-\* 归一化直方图完全一致＝**零新增零消除**。
- 比对件：ird-gates-{base,head}.log＋ird-gates-hist-{base,head}.txt（同目录 logs/）。

## 治理校验

- validate-docs：PASS（20 units, 12 trace entries, 315 task files）——logs/validate-docs.log
- validate-task（UI-T78）：PASS——logs/validate-task.log。★ 实施段卡面同步三处（卡面 note 已登记，均非语义裁决、RT-T15 卡同款先例，status 仍 ready）：①dependsOn 悬空收编——UI-T77 系已合入授权批次无 canonical 契约文件（tasks/foundation 无 UI-T77.json 实测），依赖事实由 note/outputs/designRefs 承载；②knownPitfalls findings 系 id（F-550）按 O-nn/CR-nn/P-XX-nn 句法规则收编为 O-31（F-550 指向由 designRefs findings.json#F-550 承载）；③branch ui-t78→wp10-t05（本批按 WP-10-T05 阶段 B 三维交互批次任务分支实施——与卡面原拟的差异如实登记）。
- 契约 verify 五条全部执行：①ui_plugin 目标构建零错误 ②ctest ui 目录 3/3（ui_test/ui_contract_test 全绿；ui_gui_test 按 §12.2 Windows GUI 口径真实执行、零环境障碍）③ird_gates 零新增（本文件口径）④validate-docs PASS ⑤validate-task PASS。

## 诚实边界登记

- **mesh 保真度升级（真实 visual/collision mesh 渲染——io 解析加载器／GeometryRef.localTransform 编译域透传／IRuntimeResourceProvider 资源字节消费）＝待所有者裁决**，本批渲染消费 RT-T15 基本图元（连杆圆柱/标记盒），不私裁（卡面 acceptance 4 原文）。
- **碰撞高亮与碰撞组掩码＝渲染承载在位、业务数据面（碰撞会话的计算与触发）归域侧消费任务**（卡面 acceptance 3 诚实边界）——本批不虚构碰撞会话交付。
- **渲染分组校准为呈现层落位**：分组值（Physical）与校准时机（挂接半区）系本批呈现侧实现决策——依据＝acceptance 1"发布后三维视图呈现模型几何"在缺省掩码下的可达性＋acceptance 3"渲染分组显隐对本批 drawable 生效"；卡面/单元卡已随批登记机理（ui.md §16.7 v1.97）。若所有者对呈现分组语义另有裁决（例如要求模型图元留在 Collision 组并改主视图掩码缺省），属增量修订事项——本批未私裁需求语义。
- 宿主实机点验（应用草稿后三维可见模型图元、Ctrl+双击拾取高亮）＝所有者人工流程（与 UI-T77 同款登记）——无人值守面以具名用例＋框架 API 证据为限。
- 留痕目录口径：任务分支名 wp10-t05（ask 指定）；卡面 branch 字段已同步 wp10-t05。本目录即卡面 outputs 所指"留痕 traceability/builds/ui-t78/"的承载（分支口径命名，README 交叉登记）。
