# WP-10-T01 会话状态、草稿与阶段模型

- Task ID：WP-10-T01
- 需求/阶段：UX-01～UX-08、KIN-06、NFR-PERF-03；阶段 A / R1
- 架构契约：`architecture/execution-model.md`、`architecture/public-interfaces.md`；模块方案：`module-design/session-ui.md`
- 前置：WP-03 core、WP-04 command/draft、WP-05 currentness。
- 允许：修改 `ui/include/.../SessionState.hpp`、`EditDraft.hpp`、`StageStatusModel.hpp`、`src/SessionState.cpp`、`src/DraftController.cpp`、`test/SessionStateTest.cpp`。
- 禁止：Widget 依赖、直接写 revision、显示状态进入 snapshot、修改 WP-04/05 接口。
- 产出：三层状态模型、应用单修订和阶段状态映射。

## Given/When/Then

- Given Jog、IK 双击、播放、筛选、着色或候选预览，When更新，Then仅改变 SessionState，不创建 revision/snapshot/cache。
- Given EditDraft，When保存，Then写草稿但不入计算；点击应用才调用命令并最多产生一个 revision。
- Given base revision 过期，When apply，Then显示冲突诊断，草稿和项目保持不变。

## 测试、证据与提交

命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_session_state_test$'`。证据：状态转移、revision 计数、草稿 JSON 和诊断。提交：`WP-10-T01: implement session and draft state model`。

停止：会话态与项目态隐式互转或需要 Widget 才能测试时暂停。
