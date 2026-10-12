# WP-15-T15 验证留痕（2026-10-13，分支 wp15-t15）

任务：近似外包络估算（KIN-10，R2/D）——契约 `tasks/foundation/WP-15-T15.json`。

## 现场

- 工作树：`build/wp15-t15-wt`（基于 `origin/gov-m6-prep`＝a8adb266，三契约立卡基点）。
- 冒烟构建树：`build/wp15-t15-smoke`（独立配置：vcpkg toolchain＋CMAKE_PREFIX_PATH=Qt6 msvc2022_64；仅独立冒烟模式）。

## 已执行与通过（附证据文件）

| 项 | 命令/口径 | 结果 | 证据 |
| --- | --- | --- | --- |
| 产品库构建（冒烟） | `cmake --build build/wp15-t15-smoke --config Release --target sdurws_ird_kinematics sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test`（强制重编译 Envelope.cpp/EnvelopeTest.cpp） | 零错误 | `build-log-kinematics-t15.txt`（grep error＝0） |
| 单元测试 | `sdurws_ird_kinematics_test.exe`（全量） | **189 用例：188 通过＋1 envUnavailable**（既有 GUI 呈现登记 skip——T12 落位口径，非本批引入；含本卡 KinEnvelope 新 11 用例全过） | `gtest-kinematics-test.xml`、`ird-test-report-kinematics.json`、`run-kinematics-test.log` |
| 契约测试 | `sdurws_ird_kinematics_contract_test.exe`（全量） | **57/57 通过** | `gtest-kinematics-contract-test.xml`、`ird-test-report-contract.json`、`run-kinematics-contract-test.log` |
| ctest 标签 | `ctest --test-dir build/wp15-t15-smoke/kinematics -C Release -L "^ird$"` | 2/2 通过 | `ctest-ird-label.log` |

KinEnvelope 11 用例与契约 acceptance 对照：ACC1 默认 180 方向球壳解析对照／可取消三轨（中途/入口/求解器内部）、ACC2 近似标注、ACC3 方向集黄金锁定＋同种子重跑逐位一致、探针接线与计数守恒、设施失败非终局、平面臂近似空结论、fail-fast 矩阵、NoTcp 值面。

## 如实登记（未执行/无关失败）

1. **集成模式构建未执行**：本会话预算内未在独立 worktree 配置集成树（`build/` 集成树属主检出 redesign-main，不能承载本分支）；契约 verify 第一条（`cmake --build build --config Release --target sdurws_ird_kinematics` 集成口径）与集成 ctest 留待验收段重配执行。**未声称集成零错误。**
2. **ird_gates base..head 比对未执行**（同因，留验收段）。
3. **全量冒烟构建中一处与本任务无关的既有失败**：`sdurws_ird_ui_test` 编译错误（`ui/include/sdurws/ird/ui/View3DPreviewContract.hpp:36` 找不到 `rw/math/Transform3D.hpp`——冒烟树 ui 单元缺 rw include 路径）。该文件与本任务改动零交集（`git diff origin/gov-m6-prep -- ui` 为空，源码与基点逐字节一致），非本批引入、本批不处置；登记供治理面知悉。全量构建日志：`build-log-full-smoke.txt`（唯一 error C1083 在 line 902）。
4. **验收用例数值口径**：球壳黄金模型的数值断言基于估算预算（positionTolerance=1e-4 m、初值 24×迭代 400——黄金覆盖同款预算）；近极轴方向的 DLS 预算伪影按近似语义以计数口径断言（≥160/180 收敛），并在用例注释与单元卡 §14.6 v0.20 登记——这正是 KIN-10"近似、非精确"的语义体现。
