# WP-10-T02 只读场景投影与预览恢复

- Task ID：WP-10-T02
- 需求/阶段：UX-01～UX-08、KIN-06、AT-04、AT-05；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/execution-model.md`；模块方案：`module-design/session-ui.md`
- 前置：WP-10-T01、WP-06 resolver/runtime、WP-04 query、WP-05 snapshot。
- 允许：修改 `ui/include/.../ISceneProjection.hpp`、`SceneProjection.hpp`、`SelectionModel.hpp`、`src/SceneProjection.cpp`、`test/SceneProjectionTest.cpp`。
- 禁止：反写领域对象、按显示名称查找、跨线程操作 QWidget 或修改 RobWork 编译工件。
- 产出：当前/候选/历史投影、objectId 联动和姿态恢复。

## Given/When/Then

- Given current revision、candidate 或历史 snapshot，When project，Then节点只读且以 objectId 绑定。
- Given进入候选预览，When退出/取消/投影失败，Then恢复原姿态、选择和当前方案，不写项目。
- Given列表、场景和详情交互，When选择对象，Then三者同步使用同一 objectId。

## 测试、证据与提交

命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_scene_projection_test$'`。证据：投影节点表、恢复前后姿态、objectId 联动日志。提交：`WP-10-T02: implement read-only scene projection`。

停止：场景 API 要求写回领域对象或名称无法反解时暂停。
