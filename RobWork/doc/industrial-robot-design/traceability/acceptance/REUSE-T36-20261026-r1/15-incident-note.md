# REUSE-T36 独立验收现场事件记录（2026-10-26）

## 事件一：9 个送验绑定文件在工作区被外部删除（已恢复）

- 时间线：本会话于 05:27 后开始（HEAD=d8508b1cc，git status 仅 2 个未跟踪验收目录）；
  05:39:29.581/.586/.589（product/runtime 三目录 mtime）与 05:40:02.072/.074（REUSE-T33/T36 目录
  mtime）两波、毫秒级批量删除以下 9 个 git 跟踪文件（`git status` 显示 ` D` 未暂存删除）：
  1. RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/CMakeLists.txt
  2. RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/CollisionEvaluator.hpp
  3. RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/CollisionEvaluator.cpp
  4. RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/test/CollisionSessionTest.cpp
  5. RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/NameMap.cpp
  6. RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T33/final.log
  7. RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T36/collision-capability-review.json
  8. RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T36/production-RobWorkCollisionEvaluator.json
  9. RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T36/production-buildPolicyProximitySetup.json
- 该 9 文件恰为本验收正以 Digest 复核 sha256 绑定的文件集（本会话删除前已成功读取其中多个：
  `wc -l`=853、`Get-FileHash` 5 件成功）。本会话未执行任何删除/写产品文件操作（仅创建验收
  证据目录及辅助脚本）。删除者非本会话，源头不明（无进程清单留痕）。
- 处置：删除前状态快照存 `incident-git-status-before-restore.txt`；05:43:53 以
  `git restore --worktree --` 从 HEAD（=送验 d8508b1cc，含 9 文件精确内容）恢复；恢复后
  `git status` 仅剩未跟踪验收目录，`git diff HEAD` 零行。此后多次（05:44/05:57/05:59/06:10）
  复核工作区与 HEAD 零差异，无复发。
- 影响评估：零——恢复内容逐字节来自送验提交对象；后续全部 Digest 复算在恢复后完成且与
  登记 binding 全部 MATCH（10-hash-check.txt）。

## 事件二：验收员自身 PowerShell 解析瑕疵造成的两次假异常读数（非现场异动）

- `Digest [IO.File]::ReadAllText('path')`（无括号）在 PowerShell 参数模式下不执行方法调用，
  而是把方法组字符串化文本（"static string ReadAllText(string path), static string
  ReadAllText(string path, System.Text.Encoding encoding)"）作为实参传入，两次对
  RobWorkCollisionEvaluator.cpp/CollisionEvaluator.cpp 均得出同一伪值 c0e69b8f…；同式对
  reuse-contract.ps1 亦得伪值。正确形式 `Digest ([IO.File]::ReadAllText('path'))` 复算：
  RCE.cpp=9c6dac00…、CE.cpp=8ddfbb5d…、reuse-contract.ps1=e431903b…，与登记 binding 全符。
- 另首次 hash-check 脚本中函数名 `RD` 与内置别名（Remove-Item）冲突导致一轮空输出，改名后正常。
- 结论：上述均为验收工具调用瑕疵，文件本体无任何异动（RCE.cpp mtime 保持 05:09:44 实现时刻）。
  如实登记以正视听。
