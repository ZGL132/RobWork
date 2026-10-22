# REUSE-T01 首批公共 API 探测

本目录是只读审计实验，不是生产后端。上游基点沿用 catalog 的 `b12e8503cc1c2a1acc040f506842a02e126b9515`；文件规范化摘要见 catalog，实验源码/构建文件由本次提交冻结。产品源码、上游源码和 vcpkg 均无修改。`reuse/functions/` 中首批记录为 pending，不代表文件或单元已全面审计。

`probe/` 保留首轮真实 configure/build/tests 日志、gtest XML 与既有 testkit 生成的 `ird-test-report.json`，不改写历史证据。首轮10项均运行通过，但独立验收发现 RPY 参数顺序解释错误，因此不能以首轮通过证明 Euler 掩码及不可达半周反例。修正后的重跑在 `probe-r2/`：passed=10，failed=0，skipped=0，notRun=0。既有上游完整测试集、生产迁移、性能验收及远程 CI 未执行。限制用例预期当前上游的错误/未实现行为，不能当成该功能正常或 DYN-05 已通过。

实验使用 RobWork 的 CONFIG components 和现有 RobWorkSim 导出目标；不手工拼接上游 lib。为避开当前完整 Sim 包对 Studio 的装配依赖，只导入本实验所需 `sdurwsim.cmake` 与 `sdurwsim_bullet.cmake`。GTest、Qt 配置来自同一已安装 vcpkg/Qt 环境；testkit 由现有 CMake 与 `core/test/TestMainReport.cpp` 接入，不复制报告框架。Qt Core 在 testkit 配置期需要解析，但探测无 QApplication/Widgets，不启动 GUI。

复现（PowerShell 7，VS2022 x64，Release；路径按本机既有构建给出，其他机器显式替换）：

```powershell
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cmake -S RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01 -B .cache/t01-probe-export -G 'Visual Studio 17 2022' -A x64 -DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake '-DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows;D:/software/Qt/6.11.1/msvc2022_64' -DRWSIM_TARGETS_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkSim/cmake/targets
cmake --build .cache/t01-probe-export --config Release --target ird_reuse_audit_probe
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/build/RobWorkSim/bin/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;'+$env:PATH
# 使用本工作树的绝对路径；报告目录必须存在。此处以可复现的本地输出为例。
& 'C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork/.cache/t01-probe-export/Release/ird_reuse_audit_probe.exe' --gtest_output=xml:.cache/t01-probe.xml --ird_report=.cache/t01-probe.json
```

10个用例的边界与独立解析断言见 `UpstreamSuitabilityTest.cpp`。RobWork `RPY(yaw,pitch,roll)` 与产品字段 `(roll,pitch,yaw)` 须显式换序：修正反例固定 yaw/pitch，只改 roll；不可达半周目标改成 `RPY(0,0,Pi)`（绕 X，单 Z 关节不能到达），旧 `RPY(Pi,0,0)` 实为可达绕 Z。两项修正重跑通过后才恢复相应反例结论。另修正 `runtime.rotationFromCustomEaa` 的全限定符号：它位于 runtime 命名空间，未位于匿名空间。

当前发现为 SVD/极小尺度对称特征值/主惯量可调用、同链单 leg 位置掩码可用、七次通用多项式可用、移动轴 Wrench 可投影；Euler/EAA 误差不能互换，半周局部求解成功标志须独立复验，Bullet addDevice 不能使用。它们没有证明任意机型/任意端导数/完整工况的适用性。

前期失败的环境归因：完整无 components 的 RobWork 包尝试解析未生成的 umbrella DLL；带 vcpkg toolchain 的外部配置遇上游 `C:\local` HINTS 转义；未进入 VS x64 环境时 Boost atomic 缺 synchronization 导入；MSVC 默认 GBK 不能解析本实验 UTF-8 注释。最终采用精确 components、既有安装前缀、VS x64 与 `/utf-8` 后构建通过。没有以这些环境失败为由保留自写核心。

仍未解决的能力差异集中登记于 [缺口入口](../../../reuse/extensions/required-proposals.md)，不在本目录维护另一份功能决策表。远程 push 仍因 `127.0.0.1:17890` 代理不可达失败；本地证据不能替代远程 CI 或主线合入。

第二批增加53条候选关系，累计77条、涉及10单元，全部仍为pending；未覆盖文件/函数继续待审。实际定向检索的10条命令与完整输出见 [batch2-search.json](batch2-search.json)，exitCode=1只表示本次范围无命中。新增候选覆盖DH正构造、全局随机源和球面采样，并登记DH转换内的LM/差分/消元、采样/姿态小核、SHA-256、JSON/UTF-8和高阶基函数。逐功能位置与差异只维护在 `reuse/functions/`，本段不构成另一份决策台账。

本批没有运行新的适用性或生产实验。`sdurw_sandbox`有真实目标但默认关闭，优化器源存在不表示它在当前配置可用；`FirstDifferences`与示例头路径/安装列表还需核查。`generateGridPositions`是确定性体心枚举，未错误绑定随机能力。Random全局32位种子、HyperSphere按delta生成数量、DHParameterSet仅取已有属性，这些差异均不构成新核心批准。下一批继续审查其余单元、实际消费者与测试/构建工具；没有把77条当成全项目功能总数，也没有关闭库存债务。

第二批首轮独立验收FAIL（`4e2239f7672f367fff436ab83b7db56d49d19cfe`）：UTF-8 DFA误绑定资源预算需求，HyperSphere catalog检索路径通配符在Windows返回2。返工将前者追溯到IO-T04/units/io.md §5.9.1编码契约，后者显式列出.hpp/.cpp；真实重跑见 `batch2-rework-search.json`，exitCode=0。不修改权威需求、不覆盖原搜索或失败报告，不把仅修正登记声称为能力适用性验证。

第三批增加18条候选/权威边界记录，累计95条、20单元均有实际定向审读样本，但这**不等于20单元全功能审计完成**。新增日志/线程/文件/宿主场景候选的边界和8条真实检索见 `batch3-search.json`；未运行新适用性实验。ThreadPool不替代worker隔离，IOUtil不替代project不可变发布，Log不替代diagnostics稳定码，evidence聚合与workflow门控不搬入适配层。policy已有碰撞复用与宿主现有worldTframe调用也需核生命周期、权限和结果消费；预览空WorkCell锚保持pending，不用HOST标签自动洗白。新增发现 `modeling::(anonymous namespace)::zeroConfigFK`，优先核runtime上游FK端口，禁止modeling自建计算模型。

后续公共API探测留在 `probe-r3/`，passed=14、failed/skipped/notRun均0。新增4项：ParallelIK在不可达半径目标耗尽预算仍返回候选（独立圆半径残差1 m，限制证据）；标准DH与四因子定义一致、Craig约定不同；EAA反向对齐默认轴与产品固定X半周相差滚转π；七次多项式非零端导数及0.5/2 s时间缩放（物理时间系数替换后求导，对照解析端点）。日志中的ParallelIK超预算错误是该限制用例的预期输出，不是正常求解能力通过。当前仅调用公开API和既有testkit报告入口，未修改生产算法或新建数值核。

14项API探测增量已独立重建实跑通过（冻结 `4e142e485343e8bade52a8481eefbb447b017989`），原始报告在 `traceability/acceptance/REUSE-T01-api-r3-20261022/`。第三批首轮验收FAIL：日志功能误挂ERR-01、worker权限错指§3.6.3；本次改为NFR-REL-05/DIAG-T07和§3.6.4，仍待返工独立复核。原始FAIL报告在 `REUSE-T01-batch3-r1-20261022/`，不覆盖历史。只读覆盖验收 `REUSE-T01-coverage-20261022/` 另指出惯量SPD、特征值、镜像姿态、正向动力学内部RK4及路径平滑等遗漏；其结论为全功能审计NOT PASS，不是迁移批准。

第三批两项返工已在 `f6ee77c91c36f40a4cd573a24335405afc06f68b` 独立复核PASS，原件在 `REUSE-T01-batch3-r2-20261022/`；只覆盖引用修正。覆盖审查驱动的第四批新增26条关系，累计121条仍全pending，closedFiles=0；实际检索见 `batch4-search.json`。细分登记runtime惯量SPD/相对对称性、selection闭式求谱、镜像矩阵与各插件RPY、StageD随机流、轴对齐、连续性角差、平滑剔点/切线/偏差以及FD右端/RK4。RK4保留真实外层入口摘要并另列审读段，不把入口登记视为整函数结项。新增 `sdurw_pathoptimization` 真实目标与其pathPruning/gtest候选，不把边可连判定冒充产品双域偏差约束。

后续探测的失败原件在 `probe-r4/`：18实跑、17 passed/1 failed，失败是本次审读把源码的r12=R(0,1)误当R(1,2)，错误预期RPY精确正奇异往返有1.8rad偏差，实际约4.2e-17rad；保存当时源码 `hypothesis-source.txt`。复核解析式后，`probe-r5/` 18 passed、`probe-r6/` 19 passed（failed/skipped/notRun均0），两份gtest XML与原生报告摘要已交叉核对。近奇异和精确±pi/2的RPY旋转往返正确，只是上游奇异时yaw=0、产品约定roll=0，规范化字段/摘要需适配。这不是上游缺陷，不据此提出patch。

SPD新探测区分整体尺度与跨轴混合尺度：上游谱解在diag(denorm_min,1,1e300)丢失小正轴，直接以谱值判严格SPD受限；同一现有Eigen依赖的LLT通过该输入、单位尺度1±nextafter边界和精确秩1十进制块拒绝。仅列明输入成立，尚未证明全部合法性入口/极值/对称化误差可替换，矩阵仍pending。构建沿用原CONFIG目标和testkit；`probe-r6/environment-artifacts.json`记录已安装数学DLL/LLT头原始摘要，不能把已有二进制摘要冒充源码可复现构建证明。第四批独立审读另发现占位圆柱误挂MDL-02，已按单元卡§2.5/§5.2改为MDL-04/WP-13-T07，待返工独立复核。

第四批登记返工和新增5项API探测已分别在 `877f6afee38446ab43c5e6a6fd9b09593925aedb` 独立验收PASS，原件见 `REUSE-T01-batch4-r1/r2-20261022/`、`REUSE-T01-api-r6-20261022/`（前者按各目录实际名称定位）；不覆盖原FAIL。19项由独立现场VS x64重建实跑，passed=19、failed/skipped/notRun=0；该结论不涉及生产迁移或全T01退出。新增两份具体提案仍未批准，3个相关功能关系如实改为proposal，其余118条pending、closedFiles=0；提案不是功能完成/源码权限许可。

第五批新增25条关系，累计146条（143 pending、3 proposal），closedFiles=0；细分三域20处低层字节原语、独立FK参照3处、回放插值和宿主采样格绘制。`batch5-search.json`保留实际检索。`UTIL-BINARCHIVE`与`HOST-LINES`仍candidate：前者主机序/格式化string长度/空白提取与冻结协议不等价，后者double→float且忽略回调alpha。测试oracle按audit-contract §6保留独立性，仅测试消费，不批准生产平行核心。最初门禁因inline方法symbol写成Class::method不在区段字面内而失败，已只修短symbol并保留qualifiedSymbol，不修改校验器或源码。

`ToolSuitabilityTest.cpp`独立扩展原探测目标，复用原CONFIG common目标/GTest/testkit，不改旧19项源码和报告；`probe-r7/`在VS x64重建后仅新增3项实跑：passed=3，failed/skipped/notRun=0，验证archive主机序、字符串空白往返差异及线性插值越界/极值溢出。原19项本轮未重复执行，不能写成22项全跑。宿主呈现未运行GUI，尚未生产接线/性能验收。两份具体提案草案在8a7174945a8f280f674a40ec46a512b5b5f1b414独立治理PASS，原件见`traceability/acceptance/REUSE-T01-proposals-20261022/`，仍未获实施批准。第五批与新3项待独立验收，全T01尚未退出。

第五批独立验收冻结9fd7256cf61a7768622810ed547617a66013949b为FAIL：仅SampleGridRender::draw误挂UX-07，已对照UI-T52 acceptance 2改为UX-02/ERR-01并保留原报告，待返工复验。新3项公共API在同冻结SHA独立VS x64重建实跑PASS（3 passed，failed/skipped/notRun=0），原件见`traceability/acceptance/REUSE-T01-api-r7-20261022/`；不证明生产适配。

第六批新增28条关系，累计174条（171 pending、3 proposal），closedFiles=0。runtime rtcodec/snapshotcodec与外层快照大端原语、selection组合校核及回填版本字段、时间加权RMS与单位转换已定向审读；`batch6-search.json`保留实际检索。两个selection u32At通过memcpy读取版本头，与其普通u32显式小端路径不同，跨端行为待生产回归，不改产品来绕过T01阶段门。Statistics样本均值/方差不能代替DYN-03时间梯形RMS，DOM getUnit缺产品量纲契约且依赖过重；Constants角度因子仍优先复用。Boost原依赖可提供进一步字节工具候选，未新增渠道或通用codec平台；不因BINArchive不适用即申请新核心。本批无产品/上游/vcpkg变动，无新增C++探测；全部登记仍待独立审查和全功能审计退出。

第五批返工和第六批登记在冻结 ee9cc79bd68ec27b8b2f95901dddd78d6c57d5d2 独立复验 PASS，原件归档于 `traceability/acceptance/REUSE-T01-batch5-r2-20261022/`、`REUSE-T01-batch6-20261022/`；结论仅限对应登记。

第七批新增26条关系，累计200条（197 pending、3 proposal），closedFiles=0。evidence快照/切片/清单低层编码原语与core/evidence测试入口细分登记；magic/版本/错误/身份权威仍归原owner。上游gtest入口附带全局初始化与TestSuiteConfig，不能盲链纯产品测试或造成双main。真实搜索见 `batch7-search.json`。额外支持上游精确gtest构建目标的门禁修正及双运行时142项真实自测（各142 passed、failed/skipped/notRun=0）见 `gates/gtest-target/`，保留所有失败轮次。无新增C++探测、未修改产品/上游，待本批冻结SHA独立审查，全T01未退出。

第八批新增20条关系，累计220条（217 pending、3 proposal），closedFiles=0。细分testkit/IO的JSON递归解析、序列化、Unicode编码与CSV流式解码，另将已用std::errc的两处错误映射登记为NONE候选，不建诊断平台。真实检索及11个安装头/CONFIG原始摘要见 `batch8-search.json`；上游默认XML/字符串工具限制不证明全部插件或同渠道库都无能力。已安装Boost1.92.0的公开Endian及PropertyTree由现有探测目标构建，新3项实跑全部passed，failed/skipped/notRun=0，见 `probe-r8/`。原19项和r7三项本轮未运行，不称25项全跑；没有产品依赖/算法、上游或vcpkg修改。testkit/IO单元卡补正旧自持解析理由，保持profile、位置/预算/取消/身份权威差异，成熟同渠道候选仍须适用性与供给确认，不批准新核。

第八批首轮独立语义验收冻结bb997f1为FAIL（B8-P2-01）：readHex4误绑定调用分支644–649；已返工为真正定义723–747并重算摘要，其余19条独立实读无同类问题，待冻结返工复验。原FAIL原件保留在 `traceability/acceptance/REUSE-T01-batch8-r1-20261022/`。同冻结SHA新增3项依赖API独立重建实跑PASS（3 passed，failed/skipped/notRun=0），见 `REUSE-T01-dependency-r8-20261022/`。7af7e86门禁补正亦已独立双运行时各142 passed及8项边界探测PASS，见 `REUSE-T01-gtest-target-20261022/`；这些结论均不构成全T01退出。原始验收日志保留末尾空白，不为diff清洗历史。

第九批新增19条runtime/运动学/UI视图与调用链关系，累计239条（236 pending、3 proposal），closedFiles=0。真实边界、五个测试替身和T02最小接口方向见 `runtime-boundary-audit.md`；不把model/worldToBase值冻结当成WC构造许可。新增runtime-probe沿用产品原runtime CMake、原夹具、GTest/testkit，VS x64配置/构建exit0；2项真实执行1 passed、1 failed，skipped/notRun=0：旋转控制组正确，移动offset=0.25m误编码旋转且缺平移，三坐标轴均检出。原FAIL见 `runtime-probe/red/`，未改产品/上游/vcpkg或放宽规范。该发现阻断相关T02适配验收，待按阶段修复runtime owner映射；不是上游能力缺失。全T01仍未退出。

第八批readHex4返工在冻结6d4c254548d2784aa25a2b87978e3d7fc0ab60af独立复验PASS；原件见 `traceability/acceptance/REUSE-T01-batch8-r2-20261022/`，原首轮FAIL保留，仅限登记返工。

第九批首轮独立语义验收冻结dd27799为FAIL：B9-P2-01三处qualifiedSymbol作用域错误、B9-P2-02 findDevice遗漏完整返回类型；已分别修正为真实匿名/全局/测试局部域和997–1003行并重算摘要，待返工独立复验。原件在 `traceability/acceptance/REUSE-T01-batch9-r1-20261023/`。同冻结SHA移动offset反例经独立VS x64重建实跑确认1 passed、1 failed、exit1，反例真实性PASS而产品缺陷OPEN，见 `REUSE-T01-runtime-counterexample-20261023/`，不授予T01/T02整体通过。

第十批新增56条测试/构建关系，累计295条（292 pending、3 proposal），closedFiles=0。36个main分7种初始化形态、20个同形ird_add_gtest宏及两项现有监听适配见 `test-build-audit.md` / `test-build-inventory.json`，不为最薄入口新增平台。新增1项GTest公开Append析构探测passed（failed/skipped/notRun=0），原25项本轮未重跑；实测契约揭示产品静态监听器所有权错配，登记F-958，尚未宣称已复现所有退出崩溃。F-957另登记真实移动偏置缺陷；两项保持open并分别作为T02/T05修复前置。无产品/上游/vcpkg修改，未全T01退出。

B9返工独立验收PASS见 `../../acceptance/REUSE-T01-batch9-r2-20261023/`（冻结a717113，限原两项登记问题）。全覆盖定向审查NOT PASS见 `../../acceptance/REUSE-T01-coverage-20261023/coverage.md`（冻结bb997f1，16组遗漏是下限，后续批次逐项补齐）；该结论不撤销局部验收，也不批准T02准入。

第十一批补规划/优化/包络/功率统计33条功能关系（本批pending），见`planning-computation-audit.md`。新增4项公开API实跑passed，failed/skipped/notRun=0；原26项NOT RUN。新发现PathAnalyzer+Manhattan、Linf与TrapMethod候选，保留全局随机/浮点顺序/供给与真实接线边界，不改产品、不批准新核心，不构成T01退出。

B10首轮语义验收FAIL（B10-P2-01）原档见`../../acceptance/REUSE-T01-batch10-r1-20261023/`：10单元宏条目多列13个未调用宏的目标。现按真实ird_add_gtest调用修正20宏消费者，functions与inventory一致，见`batch10-rework.json`，待独立复验；未改门禁或产品。新增GTest探测独立重建1 passed验收PASS见`../../acceptance/REUSE-T01-gtest-listener-20261023/`。单次git -c http.proxy= push已成功同步到e0bee299，未改持久代理；原代理失败记录保留。

B10-P2-01返工独立复验PASS仅关闭宏消费者登记问题，归档`../../acceptance/REUSE-T01-batch10-r2-20261023/`。B11新增4项公开API独立重建实跑4 passed、failed/skipped/notRun=0，见`../../acceptance/REUSE-T01-planning-api-20261023/`；旧26项NOT RUN，非全T01验收。
第十二批补JSON骨架/数值断言/Win32进程工具/关闭监督34条关系，见`test-tools-lifecycle-audit.md`；本批仅只读语义审计与文档门禁，无产品修改或C++实跑。

B11语义独立验收PASS见`../../acceptance/REUSE-T01-batch11-20261023/`（冻结a3cb5648，仅33关系/候选及证据边界）；不是全T01退出。

第十三批补Xacro/通道协议54关系：已用pugixml与core摘要的路径按实际记录，输出转义/固定LE及业务协议分开审；见`xacro-channel-audit.md`。本批无C++实跑/产品改动，不关闭库存或T01。

B12首轮独立FAIL（B12-P2-01）归档`../../acceptance/REUSE-T01-batch12-r1-20261023/`：原说明错误地把running也称为历史位；已修正当前运行态与其余三个历史观测位之分并更新关联摘要，产品未改，待独立复验。

B13独立语义验收PASS（冻结63e86ba5，限54关系）见`../../acceptance/REUSE-T01-batch13-20261023/`；B12返工复验PASS仅关闭B12-P2-01，见`../../acceptance/REUSE-T01-batch12-r2-20261023/`，原FAIL保留。

第十四批补project/reporting codec 132条关系，精确区段见`batch14-definitions.json`，边界见`persistence-report-codec-audit.md`；无产品/上游/依赖修改，无本批C++实跑，未关闭库存/全T01。最近本地提交因GitHub443不可达未全部远程交付，不推进T02。

第十五批补宿主预览/网关/文本21条关系并细化3条既有关系，见`host-preview-audit.md`；实际studio同源目标、QtText受限操作与四项HOST例外需精确核权，pending不扩大权限。未跑GUI/GL，不关闭T01。

B14首轮独立FAIL（B14-P2-01）见`../../acceptance/REUSE-T01-batch14-r1-20261023/`：requirePathField错误地写成ASCII路径；现按实际非空/4096字节/C0及DEL限制澄清解码后允许UTF-8中文与空格，磁盘JSON ASCII要求独立。仅修登记，待冻结返工复验。

B15首轮独立FAIL（B15-P2-01）见`../../acceptance/REUSE-T01-batch15-r1-20261023/`：五条QtText关系漏列集成条件下的ui_test消费者，现补齐实际target与说明，权限仍待核、无grant变化，待冻结复验。远程已恢复并同步到a78b41ff2，历史网络失败保留。

B16补测试helper/黄金oracle共43关系，见`test-helpers-golden-audit.md`；29个生成器隔离执行均exit0，但JSON产物对照发现URDF导入1处实质漂移，第二独立目录重复确认。未修改原黄金数据/产品/依赖，未运行本批C++测试，脚本exit0不冒充数据一致/数学PASS，全T01仍未退出。

B14/B15返工各自独立复验PASS，仅关闭B14-P2-01/B15-P2-01；原件见`../../acceptance/REUSE-T01-batch14-r2-20261023/`与`REUSE-T01-batch15-r2-20261023/`，未授整批或T01退出。F-959登记黄金生成器漂移，保持open。

全覆盖第二次独立复核NOT PASS（冻结b83fa7a3）见`../../acceptance/REUSE-T01-full-coverage-20261023/coverage.md`：20组具体遗漏与文件内部漏段是继续补审入口，不是穷尽范围；不撤销局部PASS、不授T02。B16仅补其中部分测试/生成器项，不能据此关闭其余候选。

B17补core容差、testkit集合模板与数据装载39条关系，见`compare-set-dataset-audit.md`；既有Boost最大匹配2项API实跑passed，原core C4极值反例1项failed。旧30项NOT RUN，不改产品、全T01未退出。

B16语义独立首验FAIL（B16-P2-01），原件见`../../acceptance/REUSE-T01-batch16-r1-20261023/`。返工为22条C++ helper补来源全文inputs，待冻结独立复验；保留原FAIL。生成器独立复跑见`../../acceptance/REUSE-T01-batch16-generators-20261023/`：29进程passed，91 JSON对照90一致/1不一致，F-959仍open，未跑C++消费者，不授全T01/T02。

B18补策略解析、限位、作用域和后端构建46条关系，见`policy-boundary-audit.md`。原policy目标实跑新增2探测1passed/1failed，F-961登记open；全policy回归未跑，T01未退出。B16返工独验PASS仅关闭B16-P2-01，原件见`../../acceptance/REUSE-T01-batch16-r2-20261023/`。

B20补project事务/Win32原语/恢复/查询/归档47条关系，见`project-transaction-audit.md`；原目标新增6项2passed/4failed，F-962公共打开摘要漏检与F-963磁盘满误归类保持open。无产品变更、全T01未退出。

B19为19个治理脚本的后续逐功能审计补最小待审格式，未登记/关闭这些脚本。旧helper新增8夹具红跑8failed，最终当前152夹具PS7与PS5.1各152passed、0failed/skipped/notRun，见`governance-script-format-audit.md`和`batch19/verification.json`；独立验收待冻结。原样归档B17/B18局部PASS，不授全T01/T02。

B21审4治理脚本14条pending，修原lite夹具漏带catalog的gtest检出面；PS7/PS5.1红跑各6p1f/1未执行，修后各8p0f/0skip；分支负例两入口exit1。其余脚本和全T01未完成，见governance-entry-audit.md。

2026-10-23按所有者新指示持续本地执行，不同步远程；B19/B20局部验收原件分别归档于acceptance/REUSE-T01-batch19-20261023与batch20-20261023；两者均不授全T01/T02。
