# GOV-T21 实施验证记录（独立验收前）

范围：文档、约束、治理脚本、CI/PR及产品根CMake的ird_gates接线。产品C++、上游源码、算法和现行算法测试零修改。基点b12e8503cc1c2a1acc040f506842a02e126b9515，分支gov-t21。独立验收不采信本记录的叙述，须在冻结SHA亲手重跑。

| 项 | 命令/证据 | 真实结果 |
| --- | --- | --- |
| TDD初始红 | test-reuse-governance.ps1在validate-reuse不存在时退出1 | 预期拒绝，未假报完成 |
| 32项隔离实验 PS7 | pwsh -File RobWork/scripts/industrialrobot/test-reuse-governance.ps1 -OutputDir <本目录>/ps7-frozen | passed=32 skipped=0 failed=0；其中29项负例、3项正例；JUnit XML＋reuse-test-report.json＋逐例log |
| 32项隔离实验 PS5.1 | powershell.exe -NoProfile -File 同脚本 -OutputDir <本目录>/ps51-frozen | passed=32 skipped=0 failed=0；覆盖冻结Git对象及脏树隔离 |
| 集成重配/门禁 | VS2022 x64；cmake -S RobWork -B build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON -DCMAKE_TOOLCHAIN_FILE=<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64；cmake --build build --config Release --target ird_gates | exit=0，缓存ON；integration-final.log；原有边界自测及新复用门禁通过 |
| 集成全量构建 | VS2022 x64；cmake --build build --config Release --parallel 8 | exit=0；integration-build.log |
| 独立冒烟重配/门禁 | VS2022 x64；cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B .cache/gov-t21-smoke -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64；cmake --build .cache/gov-t21-smoke --config Release --target ird_gates | exit=0；smoke-final.log |
| 独立冒烟全量构建 | VS2022 x64；cmake --build .cache/gov-t21-smoke --config Release --parallel 8 | exit=0；smoke-build.log，既有nodiscard等警告保留 |
| 上游映射/库存 | validate-reuse.ps1；1370个基线Git文件、20单元、10类具体上游能力/构建注册 | governance PASS；DEBT=16，UNRESOLVED=1368；不代表算法合规 |
| 上下文包 | 重跑CORE-T01/CORE-T11的build-context-pack，然后validate-context-pack -All | fresh=2 stale=0 structural=0 |
| YAML | PyYAML自定义拒绝重复键loader解析5份实际/模板workflow | PASS；未把语法解析当远程运行 |
| 远程保护 | gh api repos/ZGL132/RobWork/branches/redesign-main/protection | 原404未保护；已配置strict，两个必需检查绑定GitHub Actions app_id15368，管理员同规，禁强推删除、未解决对话合入；原始/绑定后JSON见本目录 |

脚本兼容问题、夹具换行/现场变化问题的失败过程保留于ps51、ps7、*-r2、*-final；本轮最终使用冻结校验器副本的*-frozen目录。JSON/XML为真实治理脚本结果，非gtest，不生成伪ird-test-report。文档深校验、契约verify、scope/commit检查在提交前/冻结后另存日志。

## 限制与未完成事项

- 既有主线完整CI运行39724444980在治理前10/47测试目标失败（F-956）；完整包检查仍阻断合入。新复用CI须实际运行；未跑的远程项目测试不能标通过。
- 本轮未运行产品算法/GUI测试或性能基准，没有对应新行为。存量产品功能不在治理验收中被重新认证。Qt真窗口规则保持不变。
- GitLab Runner未接入，仅同步模板，不声称运行。
- 最终RequireMigrationComplete应失败；存量逐功能审计/缺口提案/适配/算法替换均为后续planned任务，治理批准不批准新核心。
- 独立验收待冻结提交后执行，实施者不自行合入redesign-main；main不变。
