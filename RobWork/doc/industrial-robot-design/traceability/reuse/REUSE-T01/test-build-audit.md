# REUSE-T01：测试入口与构建复用审计

本批覆盖精确文件名的36个测试main、20个ird_add_gtest和testkit监听器两项。test-build-inventory.json绑定每个main完整定义、真实CMake目标、七组同形态代码及宏；不以本批代替夹具、黄金生成器、断言、构建红线和全部测试审计。主分组辅助去注释/空白，不把文本相似当语义相等或运行证明。

| 入口组 | 文件数 | 保留边界与复用方向 |
| --- | --- | --- |
| InitGoogleTest→现有RecordListener→RUN_ALL_TESTS | 24 | 已是GTest/testkit的最薄入口；无需为三行包装发明启动框架。若T05合并测试源，只共享一个已有形态的源文件，不新增产品公共API/运行库。 |
| drivetrain Debug CRT通道 | 1 | 仅MSVC Debug重定向，不能因Release相同就删除；该设置不修复堆/所有权缺陷。 |
| execution恢复子进程分派 | 1 | tryParseChildArgs/runRecoveryParkChild先于GTest；ThreadPool或通用main不代替进程崩溃现场。 |
| QApplication GUI | 4 | Qt GUI生命周期必须保留；VS x64、QT_QPA_PLATFORM=windows、绝对路径一次一个exe，禁止offscreen。 |
| QCoreApplication模型 | 4 | 无GUI平台需求，不改成Widgets，不把它与GUI入口盲合并。 |
| testkit自身入口 | 1 | 先保存g_irdTestArgv供ProcessRunnerTest定位，再装监听器摘参数，再InitGoogleTest；初始化顺序不能抹平。 |
| UI崩溃子进程/QCore入口 | 1 | runCrashTestChild先于Qt/GTest；保留白名单分派与原退出语义。 |

上游sdurw-gtest-main确有可复用初始化基础，但附加RobWork::init/TestSuiteConfig.xml且自带main，不适用于所有纯产品/Qt/子进程入口，不可与自有main双链接。原ADD_RW_GTEST位于上游gtest根，非CONFIG导出工具模块；不能为取宏而include注册整套上游测试的CMakeLists。产品20个ird_add_gtest经逐段比较为同一语义：add_test、报告目录/执行目标、排除默认构建。T05可将这一个现存薄宏原样移到产品cmake/ird_tests.cmake再include，保留调用目录语义和真实目标，不新增平台/参数矩阵或复制上游测试系统；目录依赖/上游供给与报告验收沿原入口。

## 已证实适配契约问题：监听器所有权

RecordListener.hpp把static TestRecordListener地址交给TestEventListeners::Append，且头/多个main注释声称GTest不接管所有权。已安装gtest/gtest.h公开Append契约恰相反：集合会delete该对象；Release才返还所有权。probe-r10仅使用公开API及heap对象，观察到集合析构时真实调用一次监听器析构（新增1项passed，failed/skipped/notRun=0）；没有执行静态对象非法delete，也不宣称已复现所有产品退出崩溃。该契约错配独立于已有F-795编解码修复，不能把CRT重定向视为修好所有权。

T05在现有installTestRecordListener内按公开Append所有权传递堆对象，保持每进程一次/参数摘除/输出路径/原生JSON与GTest XML；同步所有权注释。测试用原testkit/GTest验证单次析构、正常/失败/skip报告及独立子进程退出，必要Debug CRT；不增加注册平台或改变GTest退出码。产品本轮未修改。

## 具体绑定

- `core/test/TestMainReport.cpp::main` → `sdurws_ird_core_test`。
- `drivetrain/contract_test/ContractTestMain.cpp::main` → `sdurws_ird_drivetrain_contract_test`。
- `drivetrain/test/TestMainReport.cpp::main` → `sdurws_ird_drivetrain_test`。
- `dynamics/contract_test/ContractTestMain.cpp::main` → `sdurws_ird_dynamics_contract_test`。
- `dynamics/test/TestMainReport.cpp::main` → `sdurws_ird_dynamics_test`。
- `evidence/test/TestMainReport.cpp::main` → `sdurws_ird_evidence_test, sdurws_ird_evidence_contract_test`。
- `execution/contract_test/ContractTestMain.cpp::main` → `sdurws_ird_execution_contract_test`。
- `execution/test/TestMainReport.cpp::main` → `sdurws_ird_execution_test`。
- `io/contract_test/ContractTestMain.cpp::main` → `sdurws_ird_io_contract_test`。
- `io/test/TestMainReport.cpp::main` → `sdurws_ird_io_test`。
- `kinematics/contract_test/ContractTestMain.cpp::main` → `sdurws_ird_kinematics_contract_test`。
- `kinematics/gui_test/TestMainReport.cpp::main` → `sdurws_ird_kinematics_gui_test`。
- `kinematics/test/TestMainReport.cpp::main` → `sdurws_ird_kinematics_test`。
- `modeling/contract_test/ContractTestMain.cpp::main` → `sdurws_ird_modeling_contract_test`。
- `modeling/gui_test/TestMainReport.cpp::main` → `sdurws_ird_modeling_gui_test`。
- `modeling/test/TestMainReport.cpp::main` → `sdurws_ird_modeling_test`。
- `optimization/contract_test/ContractTestMain.cpp::main` → `sdurws_ird_optimization_contract_test`。
- `optimization/test/TestMainReport.cpp::main` → `sdurws_ird_optimization_test`。
- `policy/test/TestMainReport.cpp::main` → `sdurws_ird_policy_test, sdurws_ird_policy_contract_test`。
- `project/test/TestMainReport.cpp::main` → `sdurws_ird_project_test`。
- `reporting/contract_test/TestMainReport.cpp::main` → `sdurws_ird_reporting_contract_test`。
- `reporting/test/TestMainReport.cpp::main` → `sdurws_ird_reporting_test`。
- `requirements/contract_test/ContractTestMain.cpp::main` → `sdurws_ird_requirements_contract_test`。
- `requirements/gui_test/TestMainReport.cpp::main` → `sdurws_ird_requirements_gui_test`。
- `requirements/test/TestMainReport.cpp::main` → `sdurws_ird_requirements_test`。
- `runtime/test/TestMainReport.cpp::main` → `sdurws_ird_runtime_test`。
- `selection/contract_test/ContractTestMain.cpp::main` → `sdurws_ird_selection_contract_test`。
- `selection/test/TestMainReport.cpp::main` → `sdurws_ird_selection_test`。
- `testkit/test/TestMain.cpp::main` → `sdurws_ird_testkit_test`。
- `trajectory/contract_test/ContractTestMain.cpp::main` → `sdurws_ird_trajectory_contract_test`。
- `trajectory/test/TestMainReport.cpp::main` → `sdurws_ird_trajectory_test`。
- `ui/contract_test/TestMainReport.cpp::main` → `sdurws_ird_ui_contract_test`。
- `ui/gui_test/TestMainReport.cpp::main` → `sdurws_ird_ui_gui_test`。
- `ui/test/TestMainReport.cpp::main` → `sdurws_ird_ui_test`。
- `workflow/contract_test/ContractTestMain.cpp::main` → `sdurws_ird_workflow_contract_test`。
- `workflow/test/TestMainReport.cpp::main` → `sdurws_ird_workflow_test`。

本轮无GUI或全产品回归；已有25项API探测本轮未重跑。所有新增关系保持pending，未宣称生产迁移/文件关闭；原有库存不变。
