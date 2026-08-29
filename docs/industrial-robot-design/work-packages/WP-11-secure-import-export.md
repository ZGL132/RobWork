# WP-11 安全导入导出实施计划

> 阶段/发布：阶段 A / R1；安全 I/O 公共接口所有者：WP-11。实现者、验证者和评审者必须是不同执行上下文。

**目标：** 为 CSV、JSON、URDF、网格和目录包提供统一的路径、字节、编码和资源边界，防止路径穿越、资源耗尽、公式注入和业务插件重复解析。

## 1. 目标与非目标

通用 I/O 层负责安全读取、预算、编码、CSV 语法、目录包哈希和安全导出；业务 WP-13/14/19 负责已验证记录到领域对象的语义转换。解析器不执行宏、公式、命令、网络 URL、外部实体或资源区外引用。

不实现机器人模型语义、需求校验、器件筛选、项目 revision 提交或第三方工作簿引擎。

## 2. 需求与契约

- 需求：REQ-05、SEL-01、SEL-02、NFR-COR-03、NFR-REL-04、NFR-SEC-01～03、AT-02、AT-08。
- 架构契约：`architecture/persistence-schema.md`、`architecture/testing-contract.md`、`architecture/public-interfaces.md`。
- 模块方案：`module-design/secure-io.md`。
- 阶段/发布：阶段 A / R1；导入失败不得改变项目旧 revision，成功记录由后续业务 WP 显式应用。

## 3. 文件所有权与依赖

拥有目录：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/`，含 `include/sdurws/ird/io/`、`src/`、`test/`、`testdata/`、`out/test-evidence/wp-xx/<run-id>/`（AGENTS §3）。允许 WP-03 core、WP-09 诊断（代码前置 WP-03、09，module-design/secure-io.md 裁决）和 Qt Core/标准库；资源副本写入经 WP-04 内容对象端口（契约引用，集成期交付，本模块不直接写 `objects/`）；禁止 Qt Widgets、未登记解析库、业务插件第二套路径/CSV/JSON 读取和直接写 revision。

目标：`sdurws_ird_io`、`sdurws_ird_io_test`、`sdurws_ird_io_contract_test`。

## 4. 安全预算和路径规则

`ImportBudget` 必须显式包含单文件字节数、总字节数、JSON/XML 最大深度、最大记录/字段数、最大字符串长度、网格三角形/顶点数、压缩展开比和目录文件数。预算在读取前检查，超限立即停止，不保留部分领域对象。路径统一 POSIX `/`，拒绝空段、`.`、`..`、绝对/UNC、符号链接逃逸、资源目录外引用和大小写绕过。

## 5. CSV、JSON、URDF 和目录包

CSV 仅接受 UTF-8/UTF-8 BOM，支持 RFC 4180 引号、逗号和换行；每行输出源行号、字段名、原值、规范值和诊断。NaN/Infinity、非法单位和重复表头不转零。导出时以 `=`,`+`,`-`,`@` 开头的文本统一加安全前缀；数值类型保持数值，JSON 证据保存未转义原值。

目录包固定为 `catalog_manifest.json`、`motors.csv`、`reducers.csv`、`motor_curves.csv`、`reducer_curves.csv`、`compatibility.csv`。manifest 记录 schema、目录 ID/版本、来源、文件名和 SHA-256；缺文件、多余未声明文件、哈希/列/单位错误、曲线无序或跨表悬空引用阻止形成 `CatalogVersion`。

URDF/网格/JSON 通用层只读安全字节，不执行外部实体、网络、命令或宏；损坏文件、资源缺失和预算超限返回可定位诊断，项目旧状态保持。

## 任务

| 任务 | 独立产出 | 任务卡 |
| --- | --- | --- |
| WP-11-T01 | 路径规范化和资源预算 | [T01](../agent-tasks/WP-11-T01-path-budget.md) |
| WP-11-T02 | RFC 4180 CSV 安全读取 | [T02](../agent-tasks/WP-11-T02-csv-reader.md) |
| WP-11-T03 | CSV 公式注入安全写出 | [T03](../agent-tasks/WP-11-T03-csv-writer.md) |
| WP-11-T04 | 目录包、哈希和跨表引用 | [T04](../agent-tasks/WP-11-T04-catalog-import.md) |
| WP-11-T05 | URDF、网格和 JSON 安全边界 | [T05](../agent-tasks/WP-11-T05-resource-boundary.md) |

依赖：T01 → T02/T03/T04/T05；T04 依赖 T02；T05 依赖 T01。每张卡一个 worktree、分支和提交。

## 6. 失败分类与数据流

数据流固定为 `safe path/bytes -> budget -> encoding/syntax -> normalized record -> business adapter -> explicit command`。输入错误（路径、编码、CSV 字段）为 Input；资源缺失/跨表悬空为 Engineering/DataInsufficient；磁盘、解析库或权限故障为 System。任何失败都不创建部分项目 revision 或正式 CatalogVersion。

## 验证

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug -Target sdurws_ird_io_test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_io(_contract)?_test$'
```

## 7. 证据、迁移和退出条件

证据必须含输入文件哈希、预算、路径规范化结果、源行号/字段诊断、manifest 哈希、跨表引用图、导出前后文本和命令日志。旧插件 I/O 先只读接入，无法证明安全边界的标 Rewrite/EvidenceOnly。

## 退出条件

恶意路径、超预算、损坏编码、公式样式字段、外部实体和悬空引用均被安全拒绝且可诊断；业务插件无第二套路径/CSV/JSON 解析；AT-02、AT-08 输入侧断言和独立安全评审通过。
