# WP-04-T03 多文件事务与崩溃恢复

- Task ID：WP-04-T03
- 需求/阶段：NFR-REL-01、NFR-REL-04、CON-03；阶段 A / R1
- 架构契约：`architecture/persistence-schema.md`、`architecture/execution-model.md`、`architecture/public-interfaces.md`；模块方案：`module-design/persistence.md`
- 前置：WP-04-T01、WP-04-T02、WP-03 core。

## 边界与产出

允许：修改 `include/sdurws/ird/project/TransactionWriter.hpp`、`src/TransactionWriter.cpp`、`src/AtomicFile.cpp`、`test/TransactionTest.cpp`、`testdata/rwdesign/failpoints/`。
禁止：改变 manifest 字段、HEAD 格式、命令语义、结果仓库或脚本。

产出：具名事务状态机、逐文件 SHA-256 校验、同卷原子提交、failpoint 注入和启动恢复扫描。临时目录为 `.staging/<transaction-id>`，任何目录枚举不得将其视为 revision。

## 固定保存顺序

`validate input -> read expected HEAD -> create staging -> write domain JSON -> put objects -> write manifest -> re-read/hash all files -> rename revision -> replace HEAD -> fsync/close -> complete`。每一步失败均清理可安全清理的 staging，但绝不删除旧 revision 或对象。

## Given/When/Then

- Given domain 写、对象写、manifest 写、单文件哈希、manifest 校验或 HEAD 替换任一 failpoint，When commit，Then旧 HEAD、旧 revision 文件和对象哈希完全不变，返回 System 诊断。
- Given 进程在任一阶段终止，When 下次 open，Then忽略未完成 staging，产生 `IRD-PERSIST-UNCOMMITTED`，不加载半修订。
- Given 同卷合法 staging，When commit，Then revision 目录和 HEAD 切换对读者均为原子可见，不出现缺 manifest 或部分文件。
- Given 跨卷临时目录配置，When commit，Then拒绝非原子路径并返回可行动诊断，不降级为复制覆盖。

## 测试与命令

使用独立临时根目录，记录每个 failpoint、事务 ID、旧 HEAD 和全树哈希；重复 3 次验证一致性，另外验证权限不足、磁盘满模拟和重启恢复。

命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_transactions_test$'
```

证据：故障矩阵 CSV、旧/新树哈希、恢复诊断、原子切换日志和独立故障注入评审。

提交：`WP-04-T03: implement atomic multi-file transactions`。

停止：平台原子替换能力不足、发现跨卷写入或旧 HEAD 可能被覆盖时暂停，不改用非原子复制。
