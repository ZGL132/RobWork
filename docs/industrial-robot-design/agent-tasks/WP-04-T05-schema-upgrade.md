# WP-04-T05 Schema 升级与外部源重新关联

- Task ID：WP-04-T05
- 需求/阶段：ARC-01、CON-01、NFR-REL-04、NFR-DEP-04；阶段 A / R1
- 架构契约：`architecture/persistence-schema.md`、`architecture/public-interfaces.md`；模块方案：`module-design/persistence.md`
- 前置：WP-04-T01、WP-04-T03、WP-03 core。

## 边界与产出

允许：修改 `include/sdurws/ird/project/ProjectUpgradeRegistry.hpp`、`ProjectSourceRelinker.hpp`、`src/ProjectUpgradeRegistry.cpp`、`src/ProjectSourceRelinker.cpp`、`test/SchemaUpgradeTest.cpp`、`testdata/rwdesign/schema1-*`。
禁止：修改当前需求语义、manifest 基线字段、旧目录原文件、WP-05 快照接口或手工 CSV。

产出：Schema 1 注册/读取基线、逐版本前向升级框架、未来版本拒绝、旧 `.rwproj` 诊断和外部源重新关联命令。升级目标目录必须是新 staging，原目录只读保留。

## 数据流与版本策略

`read project.json -> detect format/schema -> registry.find(current, target) -> apply one-step upgrade -> validate -> write staging -> hash/manifest -> atomic commit`。不允许从 1 直接猜测 3 的字段含义；未知未来版本返回 `IRD-PERSIST-FUTURE-SCHEMA`。旧 `.rwproj` 返回 `IRD-PERSIST-LEGACY-FORMAT`，不自动转换。

重新关联流程为 `sourceUri -> safe canonical path -> current hash -> compare recorded hash -> explicit user confirmation -> new command/revision`。缺失返回 `IRD-PERSIST-SOURCE-MISSING`，变化返回 `IRD-PERSIST-SOURCE-CHANGED`；历史 revision 仍引用旧 content object。

## Given/When/Then

- Given 合法 Schema 1，When open，Then加载成功，字段和 manifest 哈希稳定。
- Given 注册的相邻升级器，When upgrade，Then每一步均校验输入输出并在最终阶段原子提交。
- Given 未知未来 schema 或升级器缺失，When open/upgrade，Then拒绝读取且原目录字节不变。
- Given 升级过程任一 failpoint，When restart，Then旧 HEAD 可读，无半升级 revision，产生 System 诊断。
- Given 外部源缺失/哈希变化，When relink 未确认，Then不创建 revision；确认后仅生成新 command，历史对象不变。

## 测试与命令

覆盖 Schema 1 黄金包、未来版本、跳级、升级器异常、旧格式、源移动、源篡改、确认取消和历史读取。

命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_schema_upgrade_test$'
```

证据：升级前后 JSON/哈希、版本链日志、失败恢复比对、重新关联命令审计和独立评审记录。

提交：`WP-04-T05: implement schema upgrades and source relinking`。

停止：升级需要隐式默认值、无法保留历史对象或需求没有定义新字段语义时暂停并提交 ADR 请求。
