# WP-02-T02 数学断言库

- **Task ID / 需求 ID / ADR / 阶段：** WP-02-T02；NFR-COR-01（WP-02 唯一主包需求：对照与第 15.3 节默认容差的断言基础设施）；无直接关联 ADR；阶段 A 前提 / R1。契约：`module-design/testkit.md` §5、`architecture/testing-contract.md` §2（数值与性能）、`architecture/domain-model.md`。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-02-T01；工件：`sdurws_ird_testkit` 目标（loader/manifest 可用）、`testdata/manifest.json` 与 `testkit_contract_test.cpp`（本任务扩展）。
- **允许创建/修改/删除的文件：**
  - 创建：`testkit/include/sdurws/ird/testkit/ToleranceProfile.hpp`、`GeometryAssertions.hpp`、`StableSetAssertions.hpp`；`testkit/src/` 对应实现
  - 创建：`testkit/tests/assertion_library_test.cpp`
  - 修改：`testkit/CMakeLists.txt`（注册 `sdurws_ird_assertion_library_test`）、`testkit/tests/testkit_contract_test.cpp`（追加 profile 类型隔离契约）
  - 条件新增：`testdata/` 中断言库自用样本（经 manifest 登记，含 acos 近 1 与 4/5 轴子空间样本）
  - 写证据：`out/test-evidence/wp-02/<run-id>/`
- **禁止修改的文件和公共接口：** T01 交付的 manifest 字段契约与 Loader 顺序；`industrialrobot/` 既有模块；旧插件；`requirements.md`、CSV；业务模块不得自定义同名容差或混用外部容差；两层容差不得互相替换。
- **修改前接口：** `sdurws_ird_testkit` 仅有 T01 的 manifest/loader API；无容差与断言类型。
- **修改后接口：** `ToleranceProfile.hpp` 定义两个互不隐式转换的 C++ 类型 `AlgorithmTolerance`（算法级；FK 1e-9 m/rad、IK 1 mm/1 deg、Jacobian/动力学相对 1e-6 且近零下限 1e-8 N·m/N、正动力学 h/h2 收敛 1e-4 rad/1e-3 rad/s、JSON 往返 1e-12）与 `ExternalValidationTolerance`（位置 1e-6 m、姿态 1e-6 rad、力矩/力相对 1e-4 且绝对下限 1e-6），数值取自需求 §15.3 与样本 `expected`，断言库只比较不定义；所有断言函数签名只接受 profile 类型、不接受裸 `double`；`GeometryAssertions.hpp`：位置绝对/相对误差、旋转 SO(3) 测地角（忽略四元数符号）、有向轴夹角 `atan2(norm(a×b), a·b)`（dot 先 clamp）、矩阵逐元素/正交性/行列式、`J_norm=[J_v/L*; J_ω]`（L* 非正/非有限返回 DataInsufficient，4/5 轴用任务子空间）；`StableSetAssertions.hpp`：stableId 排序、集合内容与 Pareto 支配关系比较。
- **实施步骤：**
  1. 先写 `assertion_library_test.cpp` 全部 RED 断言（含朴素 acos 对照样本），构建确认失败。
  2. 实现 `ToleranceProfile.hpp` 两类型与构造校验。
  3. 实现几何断言（测地角稳定公式、有向轴、矩阵、Jacobian 归一化与子空间）。
  4. 实现稳定集合与 Pareto 断言。
  5. 在 `testkit_contract_test.cpp` 追加"签名不接受裸 double"的编译契约。
  6. 按验证命令（脚本＋原生双形式）转绿并写证据。
- **RED 测试：** 先写的失败断言：`GeodesicAngleIsStableNearDotOne`（朴素 `acos(dot)` 在近 1 样本上失败，稳定公式通过）、`OrientedAxisAngleUsesAtan2Form`、`JacobianNormalizationRejectsInvalidLStar`（L*≤0/非有限 → DataInsufficient）、`AssertionsRejectNonFiniteInputs`（NaN/Infinity）、`ProfileConstructorRejectsBareDouble`（编译期）。
- **最小实现：** 仅实现两 profile 类型与上述断言函数使 RED 转绿；不实现业务算法、不新增容差层、不改 manifest 契约。
- **正常/边界/失败测试：**
  - 正常：Given 二连杆解析 FK/Jacobian 期望值与 `AlgorithmTolerance`，When 比较，Then 位置、矩阵、旋转在容差内通过（`PositionsPassWithinAlgorithmTolerance`、`StableSetsCompareByStableId`、`ParetoRelationIsDetected`）。
  - 边界：Given 4/5 轴任务子空间样本与 dot 近 1 的旋转样本，When 断言，Then 使用子空间与稳定公式通过（`JacobianTaskSubspaceHandled`）。
  - 失败：Given NaN、Infinity、非法维度、零轴或空 stableId，When 调用断言，Then 失败并指出字段/单位/诊断；端到端输入套用算法级容差或两层混用时编译失败。
- **精确验证命令：**（仓库根目录、VS x64 环境；脚本＋原生双形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_assertion_library_test$'`；预期退出码 0。
  - 原生回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_assertion_library_test` 与 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_assertion_library_test$"`；预期构建成功、测试通过（`sdurws_ird_testkit_test` 契约测试随本次扩展同步通过）。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `testkit/` 内新增三头文件、src 实现、两测试文件与 CMake 接入，及经 manifest 登记的样本；无裸 `double` 容差参数进入公共签名；`requirements.md` 第 15.3 节数值零改动。
- **证据工件：** `out/test-evidence/wp-02/<run-id>/`：断言测试日志（实际/期望值、profile、容差、输入哈希）、acos 近 1 回归样本记录、J_norm 子空间样本记录、两层容差类型隔离编译证据。
- **提交格式：** `WP-02-T02: numerical assertion profiles`
- **停止与升级条件：** 需求 §15.3 容差与样本 `expected` 冲突、或断言规则无法从 `module-design/testkit.md` §5 推导时，停止并升级，不得自行放宽阈值或新增第三层容差。
