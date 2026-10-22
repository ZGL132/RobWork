# 既有 gtest 构建目标审计：门禁与夹具补正

本轮不改产品、上游或 vcpkg。真实 target `sdurw-gtest-main` 登记于上游 gtest 根 CMake，原 src-only 检查误拒绝，red.log/exit 保留。校验器仅额外允许两上游精确 gtest 根，不允许任意测试路径；新增真实目标正例及伪子目录拒绝例。

首轮自测暴露 sparse checkout 漏 gtest，第二轮另暴露正例未同步 build.targets、真实待审矩阵抢先拦截两个最终关闭负例；第三轮仅余目标集合问题。返工只修夹具前提：两项关闭用例临时排除无关矩阵，finally 原字节恢复；01-clean 和最终债务拒绝例始终保留真实库存。没有放宽生产门禁、消去债务或将来源格式等同算法复用。

| 运行 | executed（expected=142） | passed | failed | skipped/notRun |
| --- | --- | --- | --- | --- |
| r1-ps7 | 30 | 7 | 23 | 0/112 |
| r1-ps51 | 30 | 7 | 23 | 0/112 |
| r2-ps7 | 142 | 139 | 3 | 0/0 |
| r2-ps51 | 142 | 139 | 3 | 0/0 |
| r3-ps7 | 142 | 141 | 1 | 0/0 |
| r3-ps51 | 142 | 141 | 1 | 0/0 |
| r4-ps7 | 142 | 142 | 0 | 0/0 |
| r4-ps51 | 142 | 142 | 0 | 0/0 |

命令：分别以 pwsh / powershell.exe -NoProfile -File RobWork/scripts/industrialrobot/test-reuse-governance.ps1 -OutputDir .cache/b7-gates-<ps7或ps51>-r4 执行。各 JSON 记录真实 validator/helper/driver 摘要和全部用例输出，JUnit/驱动日志原样归档；失败不回写。完整执行 complete=true 不代表失败用例通过。

task/docs/reuse 检查通过（200 关系、197 pending、3 proposal，closedFiles=0），只证明治理登记。无本轮 C++/GUI/生产接线验证；全 T01 未退出，远程推送/CI 尚未完成。本记录是实施验证，不替代冻结 SHA 独立验收。
