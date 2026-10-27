# CORE-T12 验证命令与重现范围

命令均从本工作树 Git 根执行。日志、XML 和原生 JSON 在同目录，退出码/计数见 README；测试不是只构建未执行。构建输出目录和二进制不提交。`$e` 为 `RobWork/doc/industrial-robot-design/traceability/core-review-r1`，`$bin` 为本工作树 `build/RobWorkStudio/bin/Release` 绝对路径。

## 配置与构建

```powershell
cmake -S RobWork -B build -G 'Visual Studio 17 2022' -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON -DCMAKE_TOOLCHAIN_FILE=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64 -DVCPKG_INSTALLED_DIR=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed -DVCPKG_MANIFEST_MODE=OFF
cmake --build build --config Release --target sdurws_ird_core_test
cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B build/core-smoke -DCMAKE_TOOLCHAIN_FILE=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64 -DVCPKG_INSTALLED_DIR=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed -DVCPKG_MANIFEST_MODE=OFF -DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake -DVCPKG_OVERRIDE_FIND_PACKAGE_NAME=vcpkg_find_package
cmake --build build/core-smoke --config Release --target sdurws_ird_core_test
cmake --build build --config Release --target sdurws_ird_project_test sdurws_ird_execution_test -- /m:6
# 上一命令D8040失败后，结束原进程才低并行重试；仅设置本次shell环境：
$env:CL='/MP1'
cmake --build build --config Release --target sdurws_ird_project_test sdurws_ird_execution_test -- /m:1
cmake --build build --config Release --target sdurws_ird_project_contract_test -- /m:1
```

新树初次缺 `RobWorkStudio/bin/RobWorkStudio.ini.template.static`，配置失败日志与两轮配置重试均保留。仅复制原现场已有忽略资产；未修改框架。集成缓存确认 ON，未配置/测试无关目标。

## 实际测试入口

```powershell
$e='RobWork/doc/industrial-robot-design/traceability/core-review-r1'
$bin=Join-Path $PWD 'build/RobWorkStudio/bin/Release'
$env:PATH='D:\10_Source_Repos\21_robot\RobWork\vcpkg\installed\x64-windows\bin;D:\software\Qt\6.11.1\msvc2022_64\bin;'+$env:PATH
# 红阶段：真实原单字播种提取为私有边界，没有修复原语义；三项回归失败。
& "$bin/sdurws_ird_core_test.exe" '--gtest_filter=*CORE_T12' "--gtest_output=xml:$e/red.xml" "--ird_report=$e/red.ird-test-report.json"
# 修复后首轮定向：
& "$bin/sdurws_ird_core_test.exe" '--gtest_filter=*CORE_T12:ReferenceBus.*' "--gtest_output=xml:$e/green.xml" "--ird_report=$e/green.ird-test-report.json"
# 补两生命周期用例及清理后，完整集仅一次：
& "$bin/sdurws_ird_core_test.exe" "--gtest_output=xml:$e/core.xml" "--ird_report=$e/core.ird-test-report.json"
& (Join-Path $PWD 'build/core-smoke/core/Release/sdurws_ird_core_test.exe') "--gtest_output=xml:$e/smoke.xml" "--ird_report=$e/smoke.ird-test-report.json"
# 最后只增强固定熵测试，构建后定向执行：
& "$bin/sdurws_ird_core_test.exe" '--gtest_filter=IdentityGenerate.EveryEntropyWordAffectsStream_CORE_T12' "--gtest_output=xml:$e/entropy-word.xml" "--ird_report=$e/entropy-word.ird-test-report.json"
& "$bin/sdurws_ird_project_test.exe" --gtest_list_tests
& "$bin/sdurws_ird_project_test.exe" '--gtest_filter=ProjectStoreTest.CreateNew_SkeletonIdentityAndReopen:ProjectStoreTest.Ppr1_CoreIdentityBaselineConsumed:ProjectStoreTest.WriteChannel_HeadAdvancesMonotonically:SaveAs*.*:CommandSubmitS6.*:ArchiveContractTest.*' "--gtest_output=xml:$e/project.xml" "--ird_report=$e/project.ird-test-report.json"
# ArchiveContractTest实际注册在以下目标，单独三项运行，非零命中：
& "$bin/sdurws_ird_project_contract_test.exe" '--gtest_filter=ArchiveContractTest.*' "--gtest_output=xml:$e/project-contract.xml" "--ird_report=$e/project-contract.ird-test-report.json"
& "$bin/sdurws_ird_execution_test.exe" '--gtest_filter=RunRegistryAdmissionTest.*' "--gtest_output=xml:$e/execution.xml" "--ird_report=$e/execution.ird-test-report.json"
```

红阶段私有模板仅 `return std::mt19937_64{entropy()};`，原生成函数委托它；三项测试失败后才改四字 seed_seq、variant 守卫和 RAII。红阶段源码未接入新生产依赖。先红后绿原用例只改末字；最后测试改为四字逐一扰动，再定向通过，不能将最终增强的四字断言全称为当时红阶段已执行。

## 隔离探针与计时

`build-real-identity.cmd` 在 VS x64 环境以 `/O2 /MD /std:c++17` 编译原探针和既有 TestMainReport，链接本树真实 core/testkit 与现有 GTest。源码副本与原评审源一致；未执行其余旧探针或把它们算通过。

```powershell
cmd /d /c "$e/build-real-identity.cmd"
& (Join-Path $PWD "$e/review-probe.exe") '--gtest_filter=ReviewIdentity.FirstTwoValuesInFreshThreadsAreDistinct' "--gtest_output=xml:$e/real-identity.xml" "--ird_report=$e/real-identity.ird-test-report.json"
cmd /d /c "$e/build-seed-bench.cmd"
& (Join-Path $PWD "$e/seed-bench.exe")
```

线程探针原样执行、报告完成后才复制源码进本记录，并将构建脚本改为同内容本地副本路径；没有重跑统计来选择有利结果。微基准固定源验证初始化分支成本，不代表 random_device/新线程的完整生产延迟；无性能预算判定。

## 检索、凭证及检查

```powershell
rg -n 'generator|void Random::seed|generator.seed|class Event|void add|void remove|fire\(' RobWork/RobWork/src/rw/math/Random.cpp RobWork/RobWork/src/rw/core/Event.hpp
rg -n 'seed_seq|random_device|thread_local' RobWork/RobWork/src/rw RobWork/RobWorkSim/src -g '*Random*' -g '*Event*'
rg -n ReferenceEventBus RobWork/RobWorkStudio/src/rwslibs/industrialrobot -g '*.hpp' -g '*.cpp' -g '!**/test/**' -g '!**/core/**'
# 固定起点重新生成实际变动的绑定（不是门禁脚本）：
& 'C:/Users/zgl18/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' "$e/sync-bindings.py"
cmake "-DIRD_ROOT=$PWD/RobWork/RobWorkStudio/src/rwslibs/industrialrobot" "-DIRD_SELFTEST_DIR=$PWD/build/core-review-gates" -P RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates.cmake
pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/CORE-T12.json
```

第二条检索无命中 exit=1，不能据此概括上游无随机能力，仍核对 Random/Event 的具体限制。首次文档检查因新树缺两个登记的 Eigen 安装头失败；从现有供给原样复制后运行通过，未改 gate。validate-docs 已包含 validate-task/validate-reuse，没有再重复被包含入口；全库深证据检查 NOT RUN。ird_gates 通过记录使用实际引擎入口（参见执行日志），不以 target 名替代运行。

提交后范围检查必须显式 `-Base cdd0eae9bdba3d5c7acd049d5ad405f4446f0ceb -Head <实际提交>`；commit-lint 只检查该范围。本地提交不代表独立验收或远程交付。

空白自查：完整 `git diff --cached --check` exit=2，共68条提示，均为编译器原始 .log 的行末空白，保留真实输出，不清洗证据；`git diff --cached --check -- . ':(exclude)**/*.log'` exit=0，产品源码、测试、CMake、文档及凭证无空白错误。原提示见 staged-whitespace.log。不是关闭门禁或改写原日志。
