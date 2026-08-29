# WP-04-T01 格式加载与路径安全

- Task ID：WP-04-T01
- 需求/阶段：ARC-01、CON-01、CON-03、NFR-REL-01、NFR-REL-04；阶段 A / R1
- 架构契约：`architecture/persistence-schema.md`、`architecture/public-interfaces.md`；模块方案：`module-design/persistence.md`
- 前置：WP-03 core；WP-01 的 CMake 和测试脚本。

## 边界与产出

允许：修改 `industrialrobot/project/include/sdurws/ird/project/ProjectPath.hpp`、`ProjectStore.hpp`、`src/ProjectPath.cpp`、`src/ProjectStore.cpp`、`test/PathSafetyTest.cpp` 和 `testdata/rwdesign/schema1-*`。
禁止：修改 requirements、架构契约、其他 WP 公共头、CSV 或结果目录实现。

产出：`ProjectPath::resolveRelative`、Schema 1 `ProjectStore::open/load` 和结构化诊断。解析必须先拒绝空/绝对/UNC/`..`/符号链接逃逸，再检查项目根、revision、manifest、对象引用；不得通过字符串前缀判断边界。

## 数据流与行为

`root -> normalize POSIX path -> canonical existing parent -> boundary check -> read JSON -> schema/required fields -> IDs/references -> ProjectRevision`。读取不写任何文件。项目首版必须恰好一个 `robotDesignId`；HEAD 为空、重复键、未知键、缺失修订或哈希不符均返回 `applied=false`。

## Given/When/Then

- Given 路径包含 `..`、绝对盘符、UNC、反斜杠、大小写绕过或符号链接，When resolve，Then 返回 `IRD-PERSIST-PATH-ESCAPE` 且不打开根外文件。
- Given 空 HEAD、缺失 manifest、重复 objectId 或未来 schema，When open，Then 返回对应稳定诊断且不创建 staging。
- Given合法 Schema 1 黄金包，When load revision，Then 字段、排序、哈希和 ownerScopeId 往返一致。
- Given 对象内容被修改，When load，Then 返回 `IRD-PERSIST-HASH-MISMATCH`，历史 revision 仍可通过备份字节比对。

## 测试与命令

正常/边界测试：POSIX/Windows 分隔符、大小写、超长路径、缺字段、未知字段、非法浮点、重复 ID、缺引用、旧 `.rwproj`。命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_path_safety_test$'
```

命令脚本由 WP-01 交付；若不存在必须停止并报告。证据：测试日志、夹具哈希、诊断 JSON、边界扫描报告、旧 HEAD 哈希。

提交：`WP-04-T01: implement schema loader and path safety`。

停止：契约未定义的默认值、路径平台语义无法统一、测试脚本或 WP-03 类型缺失时暂停，不自行改变语义。
