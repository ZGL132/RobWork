# REUSE-T07 本地实施证据（待独立验收）

产品、接口、适配边界和四维审查见 `../../../reuse/coupling-svd-migration.md`；本文件只定位复现入口。
`results.json` 区分正常、预期断开失败、中间制品失败及独验未执行；未关闭库存或债务。

- 集成树 `.cache/t07-integrated`：配置源 `RobWork`，VS2022 x64、Release、RWS_BUILD_INDUSTRIALROBOT=ON；vcpkg toolchain 为 `D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake`；Qt 前缀 `D:/software/Qt/6.11.1/msvc2022_64`。
- 独立树 `.cache/t07-smoke`：源为 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot`，相同toolchain与生成器；前缀为同一vcpkg的installed/x64-windows及Qt；RobWork_DIR为原检出的 `RobWork/RobWork/cmake`，VCPKG_OVERRIDE_FIND_PACKAGE_NAME=vcpkg_find_package。原解析失败和成功配置均在build/保留。
- `cmake --build <对应树> --config Release --target sdurws_ird_modeling_test sdurws_ird_modeling_contract_test`；最终字面sdurw_math链接配置/构建日志见build/t07-*-literal*及t07-literal-link-*。
- 测试在VS x64环境，PATH前置集成树RobWork/bin/Release、RobWorkStudio/bin/Release、RobWorkSim/bin/Release，以及同一vcpkg/bin、Qt/bin、D:/software/Miniconda3。模型测试使用QCoreApplication，无Widgets平台依赖。
- 集成exe为 `<集成树绝对路径>/RobWorkStudio/bin/Release/sdurws_ird_modeling_test.exe`（契约另行单独运行）；独立exe为 `<独立树绝对路径>/modeling/Release/<目标名>.exe`。参数 `--gtest_output=xml:<目录>/gtest.xml --ird_report=<目录>/ird-test-report.json`；原生main沿用项目testkit。
- `normal/`、`contract/`、`smoke/`、`smoke-contract/`：正常真实运行。`red/`为旧实现接线预期失败；`disconnected/`是实际生产头丢弃sigma、重建产品/测试后的单case失败；mutation源码、命令与原始摘要在production.json。
- 恢复mutation时必须更新时间戳再重建，不能以源文件恢复假定制品已恢复。`stale-restoration/`保留该真实失败，最终normal/已由强制重编后的完整运行产出。
- `benchmark/`：旧头只从实施基线c27349提取到.cache中作测量，未保留产品fallback；原样测量JSON与比较见comparison.json。耗时增至约4.662倍，未获性能接受，不判验收通过。
- `gates/docs-final.log`与`ird_gates-final.log`针对实际工作树；`TestCMakeComments.ps1`直接加载真实helper，PS7/PS5.1各8正反项。`t07-governance-suite/`是缺依赖夹具的中断运行，未生成完整报告；`t07-governance-pinned/`为修复后完整152例，固定夹具SHA和实际脚本原始字节SHA均在真实报告中。

独立验收须核对完整冻结提交及性能预算；本包不自行合入、不标done、不填closures、不操作远程。全库未审项与16项历史债务不因本包消失。
