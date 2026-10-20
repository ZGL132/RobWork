# 合入后主线需求/建模复验入口

产品冻结基线：redesign-main@5fd9a3d192ea1f67484bed7aec089ec10719c51a。

结论：**不通过**。入口[REPORT.md](REPORT.md)，逐功能见[需求120项](REQUIREMENTS-MATRIX.md)、[建模47项](MODELING-MATRIX.md)。

本轮双模式重新构建，41套件5401条：5393通过、1失败、7跳过；完整JSON见verification-summary.json。重复专项不累计。
正式宿主235＋361＋67断言、auto 8步通过，主流程71张PNG；视觉复跑31张原图仍出现滚动到底空白，测试却通过。完整问题与未执行边界见报告，不以这些计数替代全功能验收。

run.ps1/batch.ps1是本轮实际入口；硬编码输出仍指向原workspace的build/main-functional-audit-20261021。再次运行应先更换输出目录，避免覆盖已完成的工作现场。独立树与可执行文件未入证据库，二进制摘要已记录。

41份主XML和36份原生ird报告如实保存；缺失的5份不伪造，原因/目标见MA-09。9页联系表仅用于审阅索引，原PNG保留。

SHA256SUMS采用文本换行规范化LF后字节哈希（.md/.json/.xml/.log/.ps1/.py/.txt），PNG/JPG按原始字节；自身不入哈希。日志含失败与跳过，不清理成全绿。findings中的本轮新增条目在证据外登记；文档验证/索引命令日志保留在原build目录，避免自引用改写已封存证据。
