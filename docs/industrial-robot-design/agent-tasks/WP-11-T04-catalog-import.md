# WP-11-T04 目录包与跨表引用

- **Task ID / 需求 ID / ADR / 阶段：**WP-11-T04；REQ-05、SEL-01～02、NFR-REL-04、NFR-SEC-01～03、AT-08；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/secure-io.md` v0.3、`schemas/catalog/catalog-manifest.schema.json`、`schemas/catalog/column-dictionary.schema.json`
- **前置任务及必需工件：**WP-11-T01（路径/预算工件）；WP-11-T02（`CsvReader` 记录流工件）；WP-04-T04（内容对象端口——`CatalogVersion` 提交归业务命令，本卡不写 revision/对象库）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/CatalogPackageReader.hpp`；`io/src/CatalogPackageReader.cpp`；`io/test/CatalogImportTest.cpp`；`io/test/IoContractFixture.cpp`（追加本端口三例）；`io/testdata/io/catalog/`（合法包＋六类损坏包样本）；`io/out/test-evidence/wp-11/<run-id>/`；`io/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`schemas/catalog/*.schema.json`（D3 拥有，只读消费）；WP-11-T01/T02 已合入接口；`architecture/`、`module-design/`；禁止实现 WP-19 筛选/淘汰规则、忽略 manifest、接受多余文件或悬空引用
- **修改前接口：**无（目录包校验不存在；旧目录导入无哈希/外键验证）
- **修改后接口：**`CatalogPackageReader::import(SafePathHandle,ImportBudget)->expected<ValidatedCatalogPackage,IoError>`：manifest 按 `catalog-manifest.schema.json`（固定六文件集合 `catalog_manifest.json`、`motors.csv`、`reducers.csv`、`motor_curves.csv`、`reducer_curves.csv`、`compatibility.csv`、`catalogId`/版本、来源、SHA-256、行数预算、`declaredUnits`）＋各表按 `column-dictionary.schema.json`（列名大小写敏感、类型/单位/必填/范围、`primaryKeys` 唯一、`foreignKeys` 无悬空）＋曲线点序与覆盖校验；全部通过才产出不可变记录（供业务命令形成 `CatalogVersion`）
- **实施步骤：**1) manifest 解析与哈希校验（额外未声明文件拒绝）；2) 六文件逐表读取（经 `CsvReader`）与列字典校验；3) motor/reducer 唯一性、曲线点序/覆盖、compatibility 外键图构建；4) 任意失败→`IRD-IO-CATALOG-INVALID` 且零部分产出；5) 三例入契约夹具
- **RED 测试：**Given `compatibility.csv` 引用不存在的电机型号（悬空外键），When import，Then `IRD-IO-CATALOG-INVALID`（Input/Error）定位到行/列，不形成 `CatalogVersion`（`CatalogImportTest` 先行）
- **最小实现：**manifest＋列字典＋外键三类校验；曲线覆盖检查按 schema 登记的规则子集实现并注明
- **正常/边界/失败测试：**
  - 正常：Given 固定六文件合法目录包，When import，Then 全部校验通过，产出内容身份稳定的不可变记录
  - 边界：Given 同一包重复导入两次，When import，Then 校验结果与内容身份逐字节一致（幂等）；行数恰等于预算上限时通过
  - 失败：Given 缺文件/多余文件/哈希不符/重复型号/曲线无序，When import，Then 拒绝、零部分目录版本、旧项目状态不变、诊断可定位
- **精确验证命令**（仓库根）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_io(_contract)?_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_io_test sdurws_ird_io_contract_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_io(_contract)?_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -n "rank|score|select|filter" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/src/CatalogPackageReader.cpp` 零命中（无筛选语义）；`rg -n "revision|commit" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/src/CatalogPackageReader.cpp` 零命中（不写修订）
- **证据工件：**`io/out/test-evidence/wp-11/<run-id>/`——manifest/哈希清单、跨表引用图、六类失败包诊断样本、重复导入一致性记录
- **提交格式：**`WP-11-T04: 新增目录包校验与跨表引用`

  - 新增 manifest 哈希、列字典与外键图校验实现
  - 新增 六类损坏包失败测试及目标登记
  - 新增 引用图与失败包诊断证据记录
- **停止与升级条件：**目录字段或跨表语义未冻结（schema 与模块详设不一致）时暂停，不替业务层补默认值；曲线覆盖规则超出 schema 登记范围时上报 WP-11/WP-19 联合裁决
