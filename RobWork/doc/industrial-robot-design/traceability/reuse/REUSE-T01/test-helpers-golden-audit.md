# B16：测试供数、独立黄金oracle与再生一致性

本批语义审读范围为ParetoTest.cpp、XacroExpandTest.cpp、StatisticsTest.cpp列明helper；dyn-two-link和opt-pareto两个生成器所有顶层命名函数；mdl-urdf-import顶层fixture。精确区段在batch16-definitions.json，唯一功能登记仍为functions/*.json。本批不声称完整三个测试文件、其他27个生成器或全T01语义覆盖。

## 独立测试与复用边界

Pareto测试四个helper只装DTO/目标集/ID投影，真实断言消费生产IParetoFrontBuilder；不应把固定字节测试ID改成另一套生产身份。Xacro helper模拟io已校验产物，真实复用core ContentDigester、标准容器和sort。fake路径/mtime、缺叶/环树是防御注入，不能证明真实IO沙箱/读取全链。函数默认参数includeBytes={}不是函数体，区段必须覆盖整个真实定义。

StatisticsTest的expectNearRel取max(1e-15,1e-12*abs(expected))；这属于C7独立测试容差，与core C4的absolute+relative并不等价，禁止机械替换。rowAt五分项为0，tauTotal却可非0，旧注释声称恒等式恒成立不准确；其用途是统计DTO，不是物理有效RNEA输出。FakeContext仅取消bool/no-op进度/nullopt对象。混合链经现有CanonicalModelBuilder，解析支撑力(m1+m2)g与回转力矩(Iself+m*r²)alpha各有N/N·m单位、固定轴与激励限定，不能发展为通用动力学核心。

## 两个黄金生成器

dyn-two-link的rneaReference名含RNE但实际是平面Y轴二连杆Lagrange封闭式M/C/G标量公式，非递归RNE。生产候选仍为rwsim::util::RecursiveNewtonEuler；不把独立oracle改为同源上游调用以掩盖共同错误。Node crypto负责SHA-256，fs/path/url和JSON.parse/stringify均已复用成熟工具。统计参考按时间梯形、严格大于保首峰、来源ID排序；全Ok/Complete且mandatory全在场，不能外推失败行/缺工况的覆盖。脚本“JS ===即位等值”注释不严谨：+0===-0为true，NaN===NaN为false，审计不继承逐bit承诺。消费测试DynGoldenDatasetTest.cpp另有r×F静力矩第三锚；本批未执行C++消费测试。

上游TrapMethod真实位于rwlibs/softbody/numerics/TrapMethod.hpp，其统一h、端点/中点求和序与非均匀样本逐区间梯形有差异；当前C++供给不能让Node独立oracle直接复用。Random的全局unsigned种子和boost生成器也不能直接替代脚本任务私有64位SplitMix。继续优先核上游生产适用性，不以此批准新生产核心。

opt-pareto真实复用Node crypto/Buffer（显式LE长度/双精度、-0归0）。LHS固定两维连续、基线首位；9个候选事实中6个可行，其他为限位违例/裕量缺失，零筛选设计不能证明全部优化编排。支配按C4以b为reference，分层去重首见，rank/声明序严格lex/ID排序；容差翻转检查在写出之后。该独立定义式oracle限测试使用；sandbox优化检索无Pareto/LHS命中不证明全部上游/依赖无能力，更不准把脚本转生产备用核心。

## 真实隔离执行与漂移

原testdata只读，replay-generators.py复制各dataset至.cache/b16-golden-replay后逐脚本执行。generator-replay.json记录Node版本、命令、exit/stdout/stderr及全dataset JSON对照，含未被脚本改写的输入/manifest；不能把对照文件数称作生成文件数，原始字节与规范化换行比较分列。29个进程exit0、failed/skipped/notRun=0，这只是执行状态。除mdl-urdf-import/expected/import-report.json外所有比较JSON规范化相同；该1处实质不一致，故总体再生一致性FAIL。

漂移根因已读到：make_import_expectations.mjs仍写unsupported=8和joint-velocity-limit×5，未写mappedVelocityCount；现入库expected已为unsupported=3、mappedVelocityCount=5，并有F01/{1,1}说明。GoldenImportTest.cpp明确消费mappedVelocityCount并核velocity映射。第二全新目录重跑输出与第一次完全相同，见generator-drift-confirm.json；不是换行差异或运行先后问题。未运行产品C++测试，不宣称已实测其因再生产物失败。原数据/脚本均未改，下一T05只同步过期生成脚本及注释，不能反向重产旧错误黄金值洗绿。

本批零产品/框架/vcpkg修改、零新增依赖，无C++/GUI/性能执行，无生产正向证据或grant。所有新增功能关系pending，等待冻结SHA独立语义验收及剩余全量覆盖，不授T02准入。

## 脚本的目标关系

生成器没有CMake编译归属，targets登记黄金JSON的实际消费测试目标，targetRelation显式标为generated-artifact-consumer；Node是generator-replay.json中的执行器，不能把测试目标说成编译或自动运行脚本。矩阵inputs绑定相应CMake、消费者、输入与expected数据以表达完整链。首次空targets被现有门禁拒绝，保留red-task.log/red-docs.log；仅补真实消费者，未修改校验器或虚构Node CMake目标。
