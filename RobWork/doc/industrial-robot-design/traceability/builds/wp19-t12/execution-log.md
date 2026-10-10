# WP-19-T12 执行留痕（构建 / 门禁 / 测试——如实记录）

- 日期：2026-10-11（实施会话，分支 `wp19-t12`，工作树 `build/pipeline-wt2`）
- 契约：`tasks/foundation/WP-19-T12.json`（SEL-09-S1 直线传动目录模板与工作点映射——选型层；R2 解冻启动批）
- 本文件所有结论均为本批实际执行结果；未执行项如实标注"未执行"。

## 1. 构建

| 项 | 命令（口径） | 结果 |
| --- | --- | --- |
| 集成模式·产品库 | `cmake --build build --config Release --target sdurws_ird_selection` | 退出码 0，error 计数 0（增量；含 CatalogTypes/CatalogValidation/Screening/DiagCodes 增改与新头 LinearDrive.hpp） |
| 集成模式·测试目标 | `cmake --build build --config Release --target sdurws_ird_selection_test` / `sdurws_ird_selection_contract_test` | 退出码 0，error 计数 0 |
| 集成模式·drivetrain 回归 | `--target sdurws_ird_drivetrain_test_report` / `_contract_test_report` | 37/37 通过＋15/15 通过（本批零改动单元——回归证明） |
| 独立冒烟树 | `cmake -S …/industrialrobot -B build-smoke -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=vcpkg… -DCMAKE_PREFIX_PATH=Qt6.11.1/msvc2022_64` ＋ `--build … --config Release` | 配置与全树构建零错误；冒烟树 `_test` 282 运行＝281 通过＋1 SKIP、`_contract_test` 64/64（双侧执行） |

## 2. 门禁

| 项 | 结果 |
| --- | --- |
| ird_gates（`cmake --build build --config Release --target ird_gates`） | **零命中**——R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 全部检查通过；引擎自测 9 项按预期检出（pass_clean/pass_r4comment/pass_r4comb 干净通过；fail_r1/fail_t1/fail_r5/fail_sub/fail_r3/fail_r4/fail_t2 按预期检出） |
| gate-all.ps1 一键 | 未执行于本批（T07 批登记的 GUI FAIL 为他单元既有状态，与本批无关） |
| validate-task.ps1 | PASS（tasks/foundation/WP-19-T12.json） |
| validate-docs.ps1 | PASS（20 units / 12 trace / 313 task files） |

## 3. 测试执行（gtest XML 见 `traceability/gtest-reports/wp-19-t12/`）

| 套件 | 集成树 | 冒烟树 |
| --- | --- | --- |
| `sdurws_ird_selection_test`（_report 目标） | **282 运行＝281 通过＋1 SKIP＋0 失败**（SKIP＝SelPanelGui envUnavailable——T10 既有登记，非通过） | 282 运行＝281 通过＋1 SKIP |
| `sdurws_ird_selection_contract_test`（_report 目标） | **64/64 通过** | 64/64 通过 |

- 本批新增用例（11）：`SelLinearCatalog` 3（v2 正例装配黄金/七误例逐 issue 黄金对照/v1 面零变化与版本分派）＋`SelLinearScreening` 3（筛选黄金 8 记录逐字段对照/确定性/旋转链零回归钉扎）＋`SelLinearDriveContract` 5（零 drivetrain include/零直线映射公式词表〔含注释域〕/T12 词表·稳定码·器件类别·量纲封闭/v2 注册面/接口分派可达性）。
- 既有用例随词表/登记表扩展同步更新（追加纪律下既有行位置零变化）：DiagCodesTest 全表 58 行＋T12 批锚、FeasibleSetContractTest 58＋锚 54~57、HardScreeningContractTest 词尾锚、BackfillContractTest 58、SelGoldenDatasetContractTest goldenRefs 四→五数据集。
- 黄金数据集 `sel-linear-golden@1.0.0` 按 DatasetManifest 登记（integrity 17 条目全覆盖）；红实验证未单独执行于本批（T11 批已对 sel-* 消费链做篡改→FAILED→复绿实测；本批数据集走同一装载校验通道——`SelGoldenDatasetContract` 装载即全量校验构造性通过），如实登记不虚称已做篡改实验。
- 未执行项：GUI 测试未启动（本批零 GUI 用例——直线传动为计算库选型层，插件面板零改动）；verify-task.ps1 随验收会话执行。

## 4. 交付物清单

- 产品面：`selection/include/sdurws/ird/selection/`（CatalogTypes/Screening/DiagCodes/CatalogProvider/LinearDrive〔新〕）＋`src/`（CatalogTypes/CatalogValidation/Screening/DiagCodes/CatalogProvider）
- 测试面：`test/LinearDriveCatalogTest.cpp`、`test/LinearDriveScreeningTest.cpp`、`contract_test/LinearDriveContractTest.cpp`（新）；既有测试 5 文件同步更新
- 数据面：`testdata/golden/sel-linear-golden/1.0.0/`（inputs/expected/generate/manifest 全套）
- 文档面：`units/selection.md` v1.4（§1/§1.2/§16/§17.2/§18/§19.3/§19.5/§21）
