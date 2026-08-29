# WP-13-T05 物性与工具

- **Task ID / 需求 ID / ADR / 阶段：**WP-13-T05；MDL-05、MDL-07、MDL-13、需求 §7.1/§15.3；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/robot-modeling.md` v0.3 §5（公式表＝全产品解析估算唯一语义源）
- **前置任务及必需工件：**WP-13-T02（草稿/命令工件）；WP-11-T01（`SafeProjectPath`/`ImportBudget` 路径与资源校验工件）；WP-13-T01 的 `materials/` 夹具
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/include/sdurws/ird/modeling/MaterialInertiaEstimator.hpp`、`ToolEnvironmentEditor.hpp`；`modeling/src/MaterialInertiaEstimator.cpp`、`ToolEnvironmentEditor.cpp`；`modeling/test/MaterialToolTest.cpp`；扩充 `modeling/testdata/modeling/materials/`（登记哈希）；`modeling/out/test-evidence/wp-13/<run-id>/`；`modeling/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`ToolDefinition`（SYM-DOM-004）权威类型、`schemas/robot-design.schema.json` 的 `section.dimensions`（直径制持久化语义）；WP-11 接口；`architecture/`、`module-design/`（公式表引用不复述）；禁止直接写项目文件、第二套持久化截面语义
- **修改前接口：**无（估算器与工具编辑器不存在）
- **修改后接口：**`MaterialInertiaEstimator::estimate(section,material)->expected<SectionEstimate,Diagnostics>`：`SectionEstimate`＝截面类型与参数、mass、COM、inertia（参考点＝质心、参考姿态＝连杆坐标系 C，显式）、`ValueProvenance=Estimated`；`ToolEnvironmentEditor::applyToolDraft(EditDraft,ToolDefinitionEdit)->expected<RobotDesignDraft,Diagnostics>`、`applyEnvironmentDraft(EditDraft,EnvironmentModelEdit)->expected<RobotDesignDraft,Diagnostics>`（工具安装接口/TCP/几何/质量/质心/惯量与环境编辑，经草稿）
- **实施步骤：**1) 实现公式表：实心圆 `m=ρπr²L`、`Izz=mr²/2`、`Ixx=Iyy=m(3r²+L²)/12`；空心圆（ro/ri）`m=ρπ(ro²−ri²)L`、`Izz=m(ro²+ri²)/2`、`Ixx=Iyy=m[3(ro²+ri²)+L²]/12`；矩形（b×h×L）`m=ρbhL`、`Ixx=m(h²+L²)/12`、`Iyy=m(b²+L²)/12`、`Izz=m(b²+h²)/12`；2) 口径转换固定在估算器边界：持久化直径制 → 公式半径制 `r=d/2`，不产生第二套语义；3) 每个估算张量执行正定性与三角不等式校验；4) 权威覆盖保留原来源与备注；5) 工具/TCP 引用编辑（不复制几何）
- **RED 测试：**Given 物性/材料缺失或惯量张量非法（非正定/违反三角不等式），When estimate，Then `IRD-MDL-PROPERTIES-INSUFFICIENT`（Engineering/Warning）降级证据等级并显示来源，不伪装精确（`MaterialToolTest` 先行）
- **最小实现：**三类截面解析公式＋r=d/2 转换＋张量校验；权威覆盖与工具编辑各一条路径
- **正常/边界/失败测试：**
  - 正常：Given 三类截面解析算例（手算对照值），When estimate，Then mass/COM/inertia 满足 §15.3 动力学相对容差（1e-6），参考系显式
  - 边界：Given 直径制持久化输入 d，When estimate，Then 内部按 `r=d/2` 计算且输出 `section.dimensions` 仍为直径制（往返不变）；权威覆盖后 `ValueProvenance` 更新且原 Estimated 来源与备注保留
  - 失败：Given 工具/TCP/网格引用不可解析，When 应用工具草稿，Then `IRD-MDL-TOOL-REF-UNRESOLVED`（Input/Error）、零修订、恢复动作＝补齐引用
- **精确验证命令**（仓库根）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_modeling_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_modeling_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -n "d */ *2|0\.5 *\* *d" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/src/MaterialInertiaEstimator.cpp` 命中处唯一（单一 r=d/2 转换点）；`rg -n "geometry.*copy|cloneGeometry" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/src/ToolEnvironmentEditor.cpp` 零命中（TCP 不复制几何，MDL-13）；`rg -n "ofstream|QFile" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/src/MaterialInertiaEstimator.cpp RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/src/ToolEnvironmentEditor.cpp` 零命中
- **证据工件：**`modeling/out/test-evidence/wp-13/<run-id>/`——三类截面解析算例对照表（公式值 vs 实现值）、口径转换记录、覆盖前后来源快照、诊断样本
- **提交格式：**`WP-13-T05: 新增物性估算与工具环境编辑`

  - 新增 三类截面解析公式估算与 r=d/2 口径转换实现
  - 新增 张量校验与工具/环境草稿测试及目标登记
  - 新增 解析算例对照与来源快照证据记录
- **停止与升级条件：**单位或参考坐标系不明确、公式表与 §15.3 容差不闭合时暂停；WP-20 派生重算（optimization.md §5）需要新截面类型时升级公式表版本评审，不得在本卡内私扩公式
