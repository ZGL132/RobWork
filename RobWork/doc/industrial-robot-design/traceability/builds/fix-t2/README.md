# 采样点着色功能批验证留痕（FIX-T2——F-495 收官）

| 字段 | 值 |
| --- | --- |
| 批次 | FIX-T2（采样点着色功能批——F-495 样本级定位收官；全库唯一 open blocking 的六项诉求收官批） |
| 日期 | 2026-10-12 |
| 分支 | pipeline3/redesign-main（worktree build/pipeline-wt3，集成构建树 build/——RWS_BUILD_INDUSTRIALROBOT=ON＋vcpkg toolchain＋Qt prefix，AGENTS §4.1 口径） |
| 基点 | f678682b（立卡提交；其上为实施提交） |

## 批面与交付

- **功能**：区域页「采样结果」清单（样本号/状态中文词表/世界系坐标摘要/未通过原因）点击→三维单点高亮（放大点芯＋三轴蓝描边环——F-495 建议语义「蓝框=当前选中」，复用 kRegionTintDefaultGl 零新增色值）。
- **契约追加**（View3DSampleGrid 表尾——UI-T65 追加纪律同款）：`sampleIndices`（域样本号对齐表——P-FIX-4 区域切片下标偏移防线）＋`highlightedSampleIndex`（缺省值零破坏旧渲染，operator== 同步扩展）。
- **诚实降级三态**：无对齐键（旧投影方）／键未命中（切片不同步）／无键（常态未选中）→resolveHighlightedPosition 全 nullopt 不亮不虚构——ui_test 四路径用例钉住。
- **零 Readiness 语义改动**：清单数据源＝执行器评估账面投影（UiPlugin 翻译单点 R-2），非域诊断——「诊断素材粒度」缺口的呈现侧绕行兑现。
- **F-495 账面**：进展注 append（六项诉求全闭环），status 维持 open 待宿主 GUI 复验翻牌（所有者动作，本批不代翻）。

## 改动面

ui：View3DPreviewContract.hpp（契约追加）＋plugin/SampleHighlight.hpp（新私有头纯函数）＋plugin/HostView3DPreviewBackend.cpp（渲染高亮）＋plugin/UiPlugin.{hpp,cpp}（合并段透传/翻译/清位＋双缝＋teardown 第十类收口）＋test/SampleHighlightTest.cpp（新）＋gui_test/HostView3DGatewayGuiTest.cpp（透传断言扩展）＋CMakeLists.txt（ui_test 源注册）。
requirements：plugin/RequirementsPanelWidget.{hpp,cpp}（SampleResultRow＋双缝＋区域页清单＋applySampleResults）＋plugin/RequirementsUiModule.{hpp,cpp}（转发器＋暂存形态）＋plugin/RequirementsPluginAssembly.cpp（View→Row 边界翻译）＋assembly/…/RequirementsPluginAssembly.hpp（SampleResultView 中性面＋两转发器声明）＋gui_test/RequirementsView3DFlowTest.cpp（两用例）。
文档：ui.md v1.99（§13 UI-T80 行＋§16.7）＋requirements.md v0.18（§9.8 区域面板行＋§14.6）＋kinematics.md v0.18（零触碰事实）＋DTB v0.65（WP-10-T80/WP-14-T13 两行）＋findings.json（F-495 进展注）＋FIX-T2.json。

## 受影响单元回归（fresh worktree 集成树实跑；XML 机器提取值）

| 套件 | 结果（XML `<testsuites>` 实值） | 留痕 |
| --- | --- | --- |
| sdurws_ird_modeling_test | 375/375 PASS（failures=0） | xml/modeling_test.xml＋reports/…modeling_test.json＋modeling_test.log |
| sdurws_ird_requirements_test | 214 用例（213 过＋1 环境条件 skip〔ACC1〕；failures=0） | xml/requirements_test.xml＋reports/…requirements_test.json |
| sdurws_ird_ui_test | 273 用例全过（failures=0；含 FIX-T2 新增 6 用例） | xml/ui_test.xml＋reports/…ui_test.json |
| sdurws_ird_modeling_gui_test | 42 用例（41 过＋1 环境条件 skip〔F-620 剪贴板面〕；failures=0） | xml/modeling_gui_test.xml＋reports/… |
| sdurws_ird_requirements_gui_test | 49 用例全过（failures=0；含 FIX-T2 新增 2 用例） | xml/requirements_gui_test.xml＋reports/… |
| sdurws_ird_ui_gui_test | 81 用例（80 过＋1 环境条件 skip；failures=0；含 FIX-T2 透传断言扩展） | xml/ui_gui_test.xml＋reports/… |

XML＋ird-test-report JSON 同次运行产出（Windows 风格 `--gtest_output=xml:D:/...` 路径——fix-t1 B-1 教训落实）；本表数值由 XML 机器提取（S-5 建议落实）。

## 门禁与校验

- ird_gates：exit 0 零命中（build/fixt2-gates.log 构建树内）。
- validate-docs：PASS（340 task files）；validate-task（FIX-T2.json）：PASS。
- 运行前置（DTB §5.1 fresh 行）：sdurws_ird_studio 已构建（框架 DLL）＋PATH vcpkg bin＋Miniconda3。

## 边界自查

- kinematics 零文件改动（执行器账面纯消费——kinematics.md v0.18 零触碰登记）；modeling/project/io/diagnostics/runtime/selection/trajectory/drivetrain/reporting 零 diff。
- 域 Readiness 语义零改动；R-1/R-2（requirements 零 kinematics include——翻译单点在 ui 装配层）；框架源码零触碰（高亮为 ird 预览通道自定义 Render）。
