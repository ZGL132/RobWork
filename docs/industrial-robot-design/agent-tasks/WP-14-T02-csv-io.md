# WP-14-T02 CSV 导入导出

- **Task ID / 需求 ID / ADR / 阶段：**WP-14-T02；REQ-05、REQ-07（模板）、AT-02、NFR-SEC-03；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/requirements-definition.md` v0.3 §3/§5、`module-design/secure-io.md` v0.3
- **前置任务及必需工件：**WP-14-T01（冻结字段模型工件）；WP-11-T02（`CsvReader` 安全读取端口——`sourceLine/fieldName/rawText/normalizedValue` 逐行诊断）；WP-11-T03（`CsvWriter` 安全写出端口——公式样式统一转义）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/requirements/include/sdurws/ird/requirements/RequirementsCsvAdapter.hpp`；`requirements/src/RequirementsCsvAdapter.cpp`；`requirements/resources/requirements/task-points.column-dictionary.json`、`regions.column-dictionary.json`、`load-cases.column-dictionary.json`、`templates/`（三表模板 CSV）；`requirements/test/CsvIoTest.cpp`；`requirements/testdata/requirements/{csv-valid,csv-malicious}/`；`requirements/evidence/WP-14/T02/`；`requirements/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**WP-11 reader/writer 接口与转义规则（单一实现）；`schemas/catalog/column-dictionary.schema.json`（风格对齐、只读消费）；T01 冻结字段；`architecture/`、`module-design/`；禁止插件内第二套 CSV/路径解析、直接执行公式
- **修改前接口：**无（旧插件自有 CSV/路径解析待删除，§13.3 消除项）
- **修改后接口：**`RequirementsCsvAdapter::importTaskPoints(SafePathHandle,budget)->expected<vector<TaskPoint>,Diagnostics>`（同型 `importRegions`/`importLoadCases`）；`export*(rows)`（经 `CsvWriter`）；三份列字典（风格对齐 `column-dictionary.schema.json`）：任务点表列 `task_id(string,必填)`、`task_name(string,必填)`、`priority(Must|Should,必填)`、`frame_id(string,必填)`、`tcp_ref(string,可选→defaultTcp)`、`pos_x/pos_y/pos_z(decimal,m,必填)`、`quat_x/quat_y/quat_z/quat_w(decimal,无量纲,必填)`、`position_tolerance(decimal,m,必填,>0)`、`orientation_tolerance(decimal,rad,必填,>0)`、`constrained_components(string,逗号分隔,可选→全集)`、`approach_*/retract_*`（可选列组）；区域表与负载表各持独立列字典
- **实施步骤：**1) 冻结三份列字典 JSON；2) 导入：WP-11 reader 记录→列映射→T01 校验链（错误行定位、正确行保留）；3) 导出：数值列保型、文本经 WP-11 转义；4) `templates/` 三表模板与往返；5) 恶意样本矩阵
- **RED 测试：**Given 含公式注入文本（`=1+1` 等）与错误行的 CSV，When import，Then 公式仅作文本不执行、错误行按 `sourceLine/fieldName` 定位（`IRD-REQ-ROW-INVALID`）、正确行正常导入（`CsvIoTest` 先行）
- **最小实现：**任务点表导入/导出＋列字典；区域/负载表同构复用同一映射器
- **正常/边界/失败测试：**
  - 正常：Given 模板生成的合法 CSV，When 导入→导出往返，Then 值、顺序与来源稳定（AT-02）
  - 边界：Given `tcp_ref` 留空与 `constrained_components` 留空，When import，Then 分别回退 `defaultTcp` 与全集；表头列序变化（列名不变）时按列名映射
  - 失败：Given 单位非法/枚举外/容差≤0 行，When import，Then 该行拒绝＋逐行逐列定位、其余行不受影响、零修订、零部分写入
- **精确验证命令**（仓库根；含消费 WP-11 端口的契约测试）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_requirements(_contract)?_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_requirements_test sdurws_ird_requirements_contract_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_requirements(_contract)?_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`grep -rn "QFile\|ifstream\|ofstream\|getline" requirements/src/RequirementsCsvAdapter.cpp` 零命中（一律经 WP-11）；`grep -rn "eval\|system(\|=" requirements/src/RequirementsCsvAdapter.cpp` 无公式求值分支；两次导出哈希一致
- **证据工件：**`requirements/evidence/WP-14/T02/`——三列字典、恶意 CSV 样本与处置、错误行定位报告、往返对照与哈希
- **提交格式：**`WP-14-T02: implement requirements csv io`
- **停止与升级条件：**列字典字段未冻结、或与 T01 字段模型不一致时暂停（先回 T01 修订）；需要第二套转义/解析实现时立即停止并上报（§13.3 消除项）
