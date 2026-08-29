# WP-02-T01 数据清单与完整性

- **Task ID / 需求 ID / ADR / 阶段：** WP-02-T01；NFR-COR-01（核心算法须有解析算例或独立参考对照——本任务交付其数据基础设施，WP-02 在 CSV 中的唯一主包需求）；无直接关联 ADR；阶段 A 前提 / R1。契约：`module-design/testkit.md` §2/§4、`architecture/persistence-schema.md` §3（JSON 规则）、`architecture/testing-contract.md` §2。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-01-T03；工件：`RobWork/scripts/industrial-robot/run-tests.ps1`（统一测试入口可用）、`out\build\industrial-robot` 可构建（含 `industrialrobot/CMakeLists.txt` 选项集）。
- **允许创建/修改/删除的文件：**
  - 创建：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt`（注册 `sdurws_ird_testkit`、`sdurws_ird_testkit_test`、`sdurws_ird_manifest_integrity_test`，仅 `BUILD_TESTING=ON` 时）
  - 修改：`industrialrobot/CMakeLists.txt`（仅新增 testkit 子目录接入，不动既有模块块）
  - 创建：`testkit/include/sdurws/ird/testkit/DataManifest.hpp`、`DataManifestLoader.hpp`、`GoldenDataId.hpp`；`testkit/src/`（manifest 解析与 SHA-256 校验实现）
  - 创建：`testkit/tests/testkit_contract_test.cpp`、`testkit/tests/manifest_integrity_test.cpp`
  - 创建：`testdata/manifest.json` 及完整性检查所需的最小样本集（全量数据集在 WP-02-T03 交付）
  - 写证据：`out/test-evidence/wp-02/<run-id>/`
- **禁止修改的文件和公共接口：** `industrialrobot/` 既有模块骨架与锚点源文件；旧插件；`requirements.md`、CSV、文档门禁脚本；`module-design/testkit.md` 冻结的 manifest 字段与 Loader 顺序；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（新增）。
- **修改后接口：** `sdurws_ird_testkit`（STATIC，允许依赖：C++ 标准库、Qt Core JSON、WP-01 批准的 SHA-256；禁止业务插件与 GUI）；公共头三个，Loader 行为按 `module-design/testkit.md` §4 固定：路径规范化 → 存在性 → JSON 解析 → schema/version → units/有限数 → SHA-256 → expected 完整性 → 返回不可变对象，任一步失败返回 Input/Integrity 诊断、无部分样本；manifest 顶层 `schemaVersion、datasetVersion（ird-golden-0.7.1）、generatedBy、generatedAt、samples`，样本 11 字段 `id/path/kind/version/purpose/units/source/generationMethod/expected/sha256/consumers`；重复 id 拒绝、consumers 至少一项且指向存在的任务、未知 kind/units 拒绝。
- **实施步骤：**
  1. 先写 `manifest_integrity_test.cpp` 全部 RED 断言并注册目标，构建确认失败（类型与 loader 未实现）。
  2. 实现 `DataManifest.hpp` 数据模型与 `GoldenDataId.hpp` 标识类型。
  3. 实现 `DataManifestLoader` 按固定顺序校验，诊断区分 Input/Integrity。
  4. 建 `testdata/manifest.json` 最小样本集（含一个合法样本与供失败路径使用的字段变体，失败样本以运行期临时副本注入，不落 `testdata/`）。
  5. 写 `testkit_contract_test.cpp` 冻结 manifest 字段契约。
  6. 按验证命令（脚本＋原生双形式）转绿并写证据。
- **RED 测试：** 先写的失败断言（`manifest_integrity_test.cpp`）：`LoaderRejectsMissingSampleFile`、`LoaderRejectsSha256Mismatch`、`LoaderRejectsDuplicateSampleId`、`LoaderRejectsUnknownUnits`、`LoaderRejectsMissingSource`、`LoaderRejectsIncompatibleSchemaVersion`；契约测试 `ManifestSchemaFieldsAreFrozen`（`testkit_contract_test.cpp`）。
- **最小实现：** 仅实现 loader/manifest 数据模型与三目标 CMake 接入，使上述断言转绿；断言库、全量数据集与迁移协议分别在 T02～T04。
- **正常/边界/失败测试：**
  - 正常：Given 合法 manifest 与不可变样本，When 加载，Then 返回按 id 排序的只读样本，字段、版本、consumers 与哈希完整。
  - 边界：Given 失败路径运行前后，When 对 `testdata/` 求 `Get-FileHash`，Then 哈希不变（加载失败不修改 testdata、不产生正式结果）。
  - 失败：Given 缺文件、哈希不符、重复 id、未知单位、空 source 或 schema/version 不兼容，When 加载，Then Input/Integrity 诊断、无样本对象、测试非零退出。
- **精确验证命令：**（仓库根目录、VS x64 环境；WP-01-T03 已交付入口，脚本＋原生双形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_manifest_integrity_test$'`；预期退出码 0。
  - 原生回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_manifest_integrity_test` 与 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_manifest_integrity_test$"`；预期构建成功、测试通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `testkit/`、`testdata/`（manifest 与最小样本）与 `industrialrobot/CMakeLists.txt` 的一处子目录接入；既有模块零变化；无手写期望值进入 manifest（每个样本均有 source 与 generationMethod）。
- **证据工件：** `out/test-evidence/wp-02/<run-id>/`：测试日志（datasetVersion、manifest SHA-256、命令、提交 SHA）、失败路径前后 `testdata/` 哈希对照表、最小样本清单。
- **提交格式：** `WP-02-T01: 新增 manifest 加载器与完整性校验`

  - 新增 manifest 数据模型、加载器与 SHA-256 完整性校验实现
  - 新增 manifest 完整性与契约测试目标登记
  - 新增 manifest 最小样本集与完整性证据记录
- **停止与升级条件：** manifest 字段、单位表或 Loader 顺序无法从 `module-design/testkit.md` §4 与 `architecture/persistence-schema.md` §3 推导，或 SHA-256 实现不在 WP-01 批准依赖内时，停止并升级给工作包所有者；数据生产者不得批准自己的样本字段。
