# REUSE-T07：耦合矩阵复用 RobWork SVD

本能力仅替换建模侧耦合数值分解，不声称 runtime/drivetrain 的其他分解已迁移。
精确契约：[REUSE-T07](../tasks/REUSE-T07.json)。本地实施验证后仍须所有者独立会话验收，
不自签关闭、不合入、不操作远程；库存分类保持原历史事实。

| 功能/位置 | 复用来源 | 适配边界 |
| --- | --- | --- |
| `CouplingMath.hpp::couplingSingularValues` | `RobWork/src/rw/math/LinearAlgebra.hpp/.cpp::svd`，`sdurw_math` | 行主序→Eigen、既有 max 元素缩放及乘回、容器转换；删除自持 Jacobi 与两迭代常量 |
| `Parts.cpp::checkCouplingMatrix`（既有公共入口） | 上述真实生产调用链 | 保留有限性/窗口/两阈值/比较型诊断，conditionNumber 申报值不参与裁决 |
| `modeling/CMakeLists.txt` | 集成 `sdurw_math` / 独立 `find_package(RobWork CONFIG)` 的 `RW::sdurw_math` | 明确 PRIVATE 链接；不复制上游源、不另造数学门面 |
| 生产回归与复用证据 | 原 PartsTest、ReadinessTest、contract_test、testkit/GTest；直接编原 `gtest/math/LinearAlgebraTest.cpp` | 增补项目阈值/尺度/消费断开用例；保留原独立2×2解析oracle |

`rw::math` 和 Eigen 参数是任务栈私有纯值，按 SA-19 §3.6.1 使用，不构造/借持 WorkCell、State、求解器或宿主对象。
算法、收敛与奇异值排序来源为上游；不直接以 Eigen 算法绕过上游 API。阈值 1e8/1e-12 与原错误档位不变。

测试适用性限定为本能力的有限方阵、工程小维数、零/秩亏、阈值邻界、1e±200量级。
相同输入在当前固定构建中复现，不承诺跨版本/编译器逐位相等；不把“上游与自己相等”作为唯一正确性证据。
完整生产正向证据绑定本能力真实源码、CMake、上游实现及测试资源；正常/oracle/上游对照可引用一次完整执行的不同用例，避免重复跑同一冻结测试。

逐功能矩阵中的 CMake 项为项目目标接线、测试项为项目验收断言，标为 NONE/business-specific，依据为本页与真实测试源码；
这不豁免使用上游的数值函数。输入 CMake 摘要变化只同步已有相关矩阵绑定，不重新宣称未审功能已完成。

独立冒烟仍带既有 vcpkg toolchain；本机上游 RobWorkConfig 与 vcpkg find_package 宏二次解析 Windows 路径有冲突，
使用 vcpkg 已提供的 `VCPKG_OVERRIDE_FIND_PACKAGE_NAME=vcpkg_find_package`，并显式给同渠道安装前缀。
原始失败和成功命令均保留，不改框架/vcpkg源码。冒烟不是集成交付替代品。

回滚单位为本能力完整提交及匹配制品，保留失败和审查证据；不得增加运行期开关或隐藏旧算法回退。
未独验前不填 closures、不把 REUSE-T07 标为 done。验证原件位于 `../traceability/reuse/REUSE-T07/`。

既有 `ird_add_gtest` 宏只承载本项目目标的 add_test/报告目录接线；已核上游
`ADD_RW_GTEST` 定义及 `RobWorkConfig.cmake`，宏不随 CONFIG 导出，直接 include 整个
上游 gtest/CMakeLists 会注册完整上游测试树，不是可用的包接口。保留原项目薄宏并
在本包按项目接线审查，不复制测试运行器、main 或环境；跨20单元共享化留原构建治理范围。
其旧区段随新增链接行平移后须更新，不把旧 pending 记录或输入摘要刷新冒充全库构建已审。

随本包实际构建修复 IRD-GATE-AD 的CMake注释误报：`CheckSourcePermission` 仅对
CMake文件先剥离 `#` 行注释，避免注释中的 WorkCell 被解释成模型操作；不扩大权限、
不伪造 grant。先红（3个误报/5个正确），再在PS7与PS5.1各8项通过；真实C++头/State写、
CMake非注释类型仍拒绝。回归脚本已写入任务verify，独验必须连同这一精确修复核对。

## 局部设计判断与四维审查（policy §1.1）

保留现有函数签名和公共 `checkCouplingMatrix`，因为编辑/就绪/命令均依赖其结果与稳定错误档位；
不维持旧扫描循环、迭代上限或旋转状态。CouplingMath 为单一私有边界，不增类、DTO、backend、缓存或第二套模型。
行主序→Eigen 转换是上游真实参数差异，max缩放由1e±200合法值的原契约要求，二者保留；
U/V是上游现有svd接口的必填输出，只在本调用内存活，返回后释放。条件数首尾比与两阈值是项目判档语义，
不引入逆矩阵或额外谱缓存。Parts/Readiness继续编排，不复制其业务逻辑到适配。

被否候选：直接 `Eigen::JacobiSVD` 绕过已有RobWork API；保留旧Jacobi作fallback违反H2；
把上游完整gtest构建文件include进产品会注册无关目标，不能当包导出；
新数学facade/跨单元私有头共享无必要，并增加权威和依赖问题。分解只归上游，阈值跟随现有P-MDL-7治理，不在本包擅自改归属。

| 维度 | 本包结果与必要保留 | 仍需审查 |
| --- | --- | --- |
| 准确性 | 原Parts/Readiness/命令及新阈值/独立解析/上游重构对照通过，真实生产丢弃结果被断言拒绝；容差未放宽 | 独验重核冻结证据；未覆盖维度不宣称全库正确 |
| 架构 | 栈上私有纯值、零Qt计算、无模型/State所有权；runtime仍为模型owner；明确上游target；现有业务入口消费结果 | 核对CMake项目注册/报告宏与数值适配的边界，以及随包CMake注释解析修复 |
| 简洁性 | 删除自持Jacobi列对统计/旋转/迭代与两常量、排序和functional依赖；242→153行；保留真实数据/尺度转换与阈值 | 减行数不是通过结论；同项目其他重复核未迁移、不以此关闭 |
| 运行效果 | 原有限性拒绝/诊断与异常传播不变，无共享可变状态；同构建复现；微基准与校验和已记录 | 分解约4.662倍耗时；未找到本操作既有预算，性能可接受性待所有者/独验确认，**不得自判已接受或关闭** |

全套治理夹具的首次扩展回归因隔离sparse树缺 `units/core.md`，在预期规则之前报 `REUSE-MISSING`，已中断并保留原件；不是数值失败，也不把该轮算通过。
驱动改为已独验GOV-T22实现提交 `e6f06e5b9451357b10001a4677b10b4ad9e454a6` 的固定数据夹具，
保留全部152正反例，实际执行本工作树validator/helper，不叠加未物化依赖的在途矩阵。
修复后PS7完整152通过、0失败/跳过/notRun；报告记录fixtureCommit及真实脚本原始字节SHA。
规则自测与实际工作树适用性独立：后者仍由validate-docs、validate-reuse及产品ird_gates检查，不能以旧夹具代替新产品验证。
AD局部回归PS7/PS5.1各8项通过；完整152仅在PS7执行，未声称PS5.1完整自测执行。

本能力实施验证：集成 474 通过、0 失败、0 跳过；契约17通过；独立冒烟372通过、契约8通过（均0失败/跳过）。
TDD基线37项中36通过/1预期接线失败；真实结果断开1项实际失败。恢复原件后因旧时间戳留下的中间制品474项中9失败原件保留；更新时间戳强制重编译后最终474通过，不把中间制品当最终结果。
微基准3×3 SVD中位数：旧版0.0917 μs/次，迁移0.4277 μs/次（比值4.662）；每版7组×10000次、100次预热，校验和一致。该值仅限同机本次测量，最终性能接受由独验结合应用预算判断；没有伪造新的性能阈值。
