# WP-12-T03 HTML 渲染

- **Task ID / 需求 ID / ADR / 阶段：** WP-12-T03；需求 NFR-COR-04、EVI-01、REQ-06、NFR-SEC-03；ADR-004；阶段 A / R1。契约：`module-design/reporting.md` v0.3 §2/§3、`architecture/persistence-schema.md` §4。**PDF 导出不在需求基线内（D10 裁决）：本卡为纯 HTML 渲染，不创建 PDF 渲染器与接口桩。**
- **基线 commit：** 代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档：requirements v0.8、检查点 `IRD-D2-20260829`、module-design/reporting.md v0.3 §2（PDF 依赖裁决）/§3。
- **前置任务及必需工件：** WP-12-T01（`ReviewReport` 权威对象与追加协议）、WP-12-T02（内容闸）；WP-01-T03（测试入口）。
- **允许创建/修改/删除的文件**（模块根同 WP-12-T01）：创建 `include/sdurws/ird/reporting/HtmlReportRenderer.hpp`、`src/HtmlReportRenderer.cpp`、`resources/report.zh-CN.html`、`resources/report.css`、`test/HtmlRenderTest.cpp`、`out/test-evidence/wp-12/`（本卡运行证据）；修改 `CMakeLists.txt`；删除：无。
- **禁止修改的文件和公共接口：** T01/T02 冻结的权威对象与追加协议；requirements.md 与 architecture/、module-design/ 文档；**未经 WP-01 依赖门禁与 ADR 批准，禁止引入任何 HTML→PDF 渲染依赖（Qt PrintSupport 或第三方渲染库）**；WP-01 依赖清单；其他 WP 公共头。
- **修改前接口：** T01/T02 只有结构化 `ReviewReport`，无渲染器。
- **修改后接口：** `HtmlReportRenderer::render(const ReviewReport&) -> expected<Artifact, RptError>`（离线 HTML：内嵌/本地 CSS 与资源，无网络引用）；关键数值、工程状态、诊断码和快照身份在 HTML 与 JSON/CSV 数据包间逐字段一致（跨格式一致性复核归 WP-12-T06）。
- **实施步骤：** 1) 先写 HTML 渲染失败测试（资源缺失、路径越界、非有限值）构建确认失败；2) 实现模板装配（`resources/report.zh-CN.html`＋`report.css`，本地资源无外链）；3) 实现离线约束自检与 `IRD-RPT-RENDER-FAILED` 路径；4) 命令转绿，写证据并提交。
- **RED 测试：** 模板/CSS 缺失或资源路径越界 → `IRD-RPT-RENDER-FAILED` 且不写 `reports/`；渲染产物含网络引用 → 拒绝；中文内容与关键数值渲染一致。
- **最小实现：** HTML 渲染＋本地资源＋离线约束自检；无 PDF、无打印支持。
- **正常/边界/失败测试：**
  - 正常：Given 一份完整 `ReviewReport`（中文内容、关键数值、诊断码），When 渲染，Then HTML 与 JSON/CSV 关键字段逐字段一致且产物可离线打开。
  - 边界：超长表格行、附录显著标识（Quick/历史结果只入参考附录）渲染不丢字段。
  - 失败：资源缺失/路径越界/非有限值 → `IRD-RPT-RENDER-FAILED`，既有工件保留，不产出空 payload。
- **精确验证命令：**（仓库根目录、VS x64 环境；第一形式必执行，脚本不可用时按回退顺序执行原生两形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_reporting_test$'`；预期退出码 0。
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_reporting_test`；预期构建成功。
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_reporting_test$"`；预期全部通过。
- **diff 和禁止项检查：** diff 仅含允许清单；CMake 无 PDF 库/Qt PrintSupport 链接；HTML 无外链资源；渲染器不得读取当前界面或会话态；无省略号命令。
- **证据工件：** `out/test-evidence/wp-12/<run-id>/`（HTML 样例工件及其 SHA-256、一致性自检输出、离线约束检查记录）＋测试日志（命令、commit、配置）。
- **提交格式：** `WP-12-T03: 新增 ReviewReport HTML 渲染器与离线资源`

  - 新增 HtmlReportRenderer 与本地化模板/CSS 资源（离线渲染，无外链）
  - 新增 渲染失败、资源越界与格式一致性测试
  - 新增 HTML 样例工件与一致性自检证据
- **停止与升级条件：** 需要真实 PDF 输出或引入任何渲染依赖时，停止并按 WP-01 依赖门禁＋ADR 流程升级；渲染器需要读取当前界面/会话态时停止（违反 persistence-schema §4 与 CTR-DOM-002）。
