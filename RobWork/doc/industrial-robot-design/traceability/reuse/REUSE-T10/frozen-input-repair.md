# 冻结输入修正（REUSE-T10）

冻结 e453006 的 validate-reuse -Head HEAD 真实失败，退出码 1：
`REUSE-MISSING : RobWork/RobWork/src/rw/common/BinArchive.hpp`。
原失败原件见 frozen-validation-initial.log / frozen-validation-initial.exit.txt。

根因：Windows 文件系统大小写不敏感，但 Git 对象路径大小写敏感。
当前 diagnostics 功能矩阵的 BinArchive.hpp/.cpp 应为 Git 已跟踪的 BINArchive.hpp/.cpp；
内容摘要相同，不修改上游文件，也不改历史探测/验收原件。
逐个核对全部当前 functions 输入路径，还发现 modeling.CMake.ird_add_gtest
引用本地生成的 RobWorkConfig.cmake，冻结树不存在。改绑已经入库的
RobWorkConfig.cmake.in 模板并更新摘要；模板实际控制 CONFIG 包导出，
包含 RobWorkConfigMacros.cmake 引用，不导出只在 gtest/CMakeLists.txt 定义的 ADD_RW_GTEST。
生成包的运行证据仍在原 probe-r10/environment-artifacts.json，不能把本地生成物当 Git 输入。

这是证据输入事实修正；门禁严格性、算法、批准范围与性能未接受状态均不改变。
需重跑冻结检查后才能记录修复成功；完整能力仍待独立验收。
