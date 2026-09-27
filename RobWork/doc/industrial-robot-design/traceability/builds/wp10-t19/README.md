# WP-10-T19（≙UI-T19）验证留痕

任务：宿主控制器抽取（方案 B.1 迁移链首任务；契约 tasks/foundation/UI-T19.json）
分支/基线：ui-t19（base＝redesign-main@be3bdbbf——B.1 批次放行合入后）；流水线 tick#394 领取
日期：2026-09-27

## 构建结论（双模式）

| 模式 | 命令面 | 结论 |
| --- | --- | --- |
| 集成模式 | `cmake --build build --config Release --target sdurws_ird_ui sdurws_ird_ui_test sdurws_ird_ui_contract_test sdurws_ird_ui_gui_test sdurws_ird_ui_app sdurws_ird_ui_plugin`（缓存 RWS_BUILD_INDUSTRIALROBOT:BOOL=ON 已 grep 确认） | 零错误（error 计数 0） |
| 独立冒烟 | 全树构建于 build_smoke_t19（`cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B build_smoke_t19 -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/QT/6.11.1/msvc2022_64`） | 零错误 |

## 测试结论（gtest XML＋ird-test-report.json 双份留痕，本目录）

| 目标 | 集成树计数 | 冒烟树计数 | 基线对照 |
| --- | --- | --- | --- |
| sdurws_ird_ui_test | 158/158 PASS | 158/158 PASS | 154→158（＋HostControllerModel 4 例） |
| sdurws_ird_ui_contract_test | 22/22 PASS | 22/22 PASS | 22→22（零变化） |
| sdurws_ird_ui_gui_test | 36/36 PASS | 36/36 PASS | 35→36（＋WorkbenchContentGuiTest 访问器在位性 1 例） |

新用例（acceptance 1 具名自证）：HostControllerModel.ProductMainWindowVocabularyClosed / AggregatesControllerFamiliesByIdentity / DraftFamilyOptionalWhenNotAssembled / StatusProjectionObserversForwardToContent（均 _B1_UI_T19 追溯后缀）；WorkbenchContentGuiTest.ContentCommandRegistryAccessorServesLiveRegistry_B1_UI_T19。
GUI 运行环境：QT_QPA_PLATFORM=windows＋QT_PLUGIN_PATH=<Qt>/plugins（冒烟树 platforms 插件依赖）。

## ird_gates 结论（base..head 归一化比对）

- 引擎直跑：head（ui-t19 工作树）与 base（be3bdbbf，detached worktree）各 151 条 IRD-GATE-* 归一化直方图行，**逐行完全一致——零新增零消除**（本任务仅源文件表尾追加＋测试增列，零链接语义变更）。
- 比对件：ird-gates-hist-base.txt ↔ ird-gates-hist-head.txt（双端引擎原始日志 ird-gates-base.log / ird-gates-head.log 同目录）。
- 如实登记：构建期 ird_gates 自定义目标即红（72 处全仓命中判失败）为 **F-019 登记的存量事实**（TK-T03 起），base 端同样即红——非本任务引入。

## 治理校验

- validate-docs：PASS（20 units, 12 trace, 201 task files）——validate-docs.log
- 契约 verify 四条：构建目标/ctest 标签/ird_gates（本文件口径）/validate-docs——全部执行，未执行项无。

## 交付物清单

- 新增：include/sdurws/ird/ui/IHostController.hpp、src/HostController.cpp、test/HostControllerModelTest.cpp
- 增量：WorkbenchContent.hpp（commandRegistry() 访问器＋WorkbenchHostKind 通道地位注释）、WorkbenchContent_p.hpp/.cpp（访问器实现）、gui_test/WorkbenchContentGuiTest.cpp（＋1 例）、CMakeLists.txt（源文件表尾追加×2）
- 文档同步：units/ui.md v1.17（§3.3/§4.1/§10.1 v1.17 增量注⑨⑩⑪/§13 UI-T19 落位登记注/§16.7 v1.17 行）
