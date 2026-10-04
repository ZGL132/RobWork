# ui-panel-zh-labels 验证留痕

分支：`ui-panel-zh-labels`（基点 redesign-main@fef002f9）
批次：建模面板呈现中文化与复合值展开（units/modeling.md v0.32 登记行同源）

## 改动面

- `modeling/plugin/PanelModel.cpp`：Provided 态复合值展开（轴向三维／原点·安装接口·世界位姿六值 RPY 反解／惯量六分量）＋关节类型/权威模式/场景角色/几何类别值中文呈现映射（token 仍为机器权威）。
- `modeling/plugin/ModelingPanelWidget.cpp`：属性行标签 fieldKey→工程中文固定映射（chineseFieldLabel，25 键＋tcp:/pose: 前缀键；表外键回退键名原文）。
- `modeling/test/PluginPanelTest.cpp`：新增 Properties_CompoundValuesExpandedAndChineseTokenDisplay。
- `units/modeling.md`：头版本行 v0.32＋§15 修订表登记行。

## 验证结论（2026-10-04）

| 项 | 结果 |
| --- | --- |
| 集成模式全量 Release 构建 | 零错误（build/，RWS_BUILD_INDUSTRIALROBOT=ON 事前 grep 确认） |
| 冒烟模式全量 Release 构建 | 零错误（build_smoke_zhlabels，vcpkg toolchain＋Qt 6.11.1 msvc2022_64 前缀；警告全在既有他单元面） |
| sdurws_ird_modeling_test（集成） | 322/322（含新增 1 例；XML 本目录 modeling_test.xml） |
| sdurws_ird_modeling_gui_test（集成） | 17/17（XML 本目录 modeling_gui_test.xml） |
| sdurws_ird_modeling_contract_test（集成） | 通过（XML 本目录 modeling_contract_test.xml） |
| sdurws_ird_ui_test / ui_contract_test（集成） | 246/246＋32/32（装配消费面回归） |
| 冒烟三目标 | modeling_test 264/264＋gui 15/15＋contract 8/8（gated 集成专属用例按口径不计入冒烟） |
| ird_gates | 未执行——本批次纯呈现层改动（零新单元边、零新目标、零新文件域），无门禁比对基线诉求；如验收段需要可按引擎直跑补比对 |

## 呈现口径（要点）

- fieldKey 为稳定机器键（HostMigration 批量粘贴/基线注入寻址锚），键面保持英文不动；中文只是行标签呈现映射。
- 枚举值（类型/权威/角色/几何类别）中文＋英文 token 括注；编解码/校验面仍消费 token，零变化。
- "[值已提供]/[位姿已提供]"占位在面板属性区退役（ Provided 复合行一律展开精确数值）；模板级防御分支保留。
