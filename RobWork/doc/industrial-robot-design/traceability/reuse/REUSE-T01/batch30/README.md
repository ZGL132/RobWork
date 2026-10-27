# B30 重现与证据边界

独立目标复用父审计配置、原core/testkit报告器及原DigestTest七例；新增六例不修改产品代码。Windows SDK CNG仅作审计参照，零依赖安装。GTest imported目标需在最外层find_package，首次配置失败原日志保留。

在仓库根，以现有VS x64环境执行（以下为本机实际路径；独立clone需将源码和构建目录换为自身绝对路径，框架/依赖路径保持实际供给）：

```powershell
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cmake -S RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch30 -B .cache/b30-build -G 'Visual Studio 17 2022' -A x64 '-DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake' '-DRWSIM_TARGETS_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkSim/cmake/targets' '-DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows;D:/software/Qt/6.11.1/msvc2022_64'
cmake --build .cache/b30-build --config Release --target ird_digest_audit
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWork/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;'+$env:PATH
# 先创建输出目录；exe和两个报告参数使用本次现场的绝对路径。
& 'C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork/.cache/b30-build/Release/ird_digest_audit.exe' '--gtest_output=xml:C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork/.cache/b30-final-run/gtest.xml' '--ird_report=C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork/.cache/b30-final-run/ird-test-report.json'
```

真实结果13跑11passed/2failed/0skipped，exit 1；失败为F-965两个公开hash，不能将其改成EXPECT_NE洗绿。XML中14个cng_sha256_<长度>属性可与oracle-vectors.json的Python hashlib结果独立比对。oracle.py只生成审计固定向量，不链接生产，不能成为新哈希核心。

搜索命令/输出/外部候选摘要见search.json；产品源/原测试/构建的规范化摘要绑定于core功能矩阵。verification.json明确未执行项；artifact-manifest.json保存本批原件raw SHA-256，不用换行转换重写原日志。仅独立冻结SHA验收可判本批局部通过；全T01及T02未获准入。
