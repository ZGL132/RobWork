# WP-04-T02 命令、修订与分支

- Task ID：WP-04-T02
- 需求/阶段：ARC-01、CON-01、CON-03、NFR-REL-01；阶段 A / R1
- 架构契约：`architecture/persistence-schema.md`、`architecture/public-interfaces.md`、`architecture/execution-model.md`；模块方案：`module-design/persistence.md`
- 前置：WP-04-T01、WP-03 core。

## 边界与产出

允许：修改 `include/sdurws/ird/project/ProjectCommand.hpp`、`ProjectBranch.hpp`、`IProjectCommandService.hpp`、`src/ProjectCommandService.cpp`、`src/BranchHistory.cpp`、`test/CommandRevisionTest.cpp`。
禁止：修改 WP-03 身份定义、WP-05 快照接口、GUI、CSV 和持久化格式字段含义。

产出：`apply/undo/redo` 实现、命令验证器、分支历史和 `CommandResult`。每次成功操作仅生成一个新 revision；项目和分支身份必须从 expected ref 校验，历史 payload 只读。

## 数据流与状态

`ref + DomainCommand -> load HEAD -> compare expected -> validate targets/ownerScopeId -> apply immutable patch -> TransactionWriter(T03) -> return new ref`。创建分支只记录 baseRevisionId 并共享对象；跨项目复制换新 objectId；删除 ID 永不复用。会话态变化不得调用服务。

## Given/When/Then

- Given expected revision 不是当前 HEAD，When apply，Then `IRD-PROJECT-REVISION-CONFLICT`、`applied=false`、目录字节不变。
- Given branchId 不匹配，When apply/undo/redo，Then `IRD-PROJECT-BRANCH-MISMATCH`。
- Given 合法命令，When apply，Then parentRevisionId 指向旧 revision，且只出现一个新 revision。
- Given一条历史命令，When undo then redo，Then 两次均为新 revision，原命令 payload 和旧 revision 哈希不变。
- Given无可撤销/重做历史，When调用，Then返回 `IRD-PROJECT-NOTHING-TO-UNDO` 或 `...REDO`。

## 测试与命令

覆盖并发冲突、空命令、未知 command type、重复目标、跨作用域目标、分支创建、复制/删除 ID、undo/redo 链和重启后历史读取。

命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_commands_revisions_test$'
```

证据：命令序列 JSON、旧/新 revision ref、manifest 哈希、冲突诊断、历史不变性比对和独立评审记录。

提交：`WP-04-T02: implement revision commands and branch history`。

停止：需要新增公共字段、修改 undo/redo 语义或事务写顺序时暂停并报告给 WP-04 负责人。
