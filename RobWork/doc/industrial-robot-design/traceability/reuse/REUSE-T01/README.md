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
