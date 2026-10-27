# WP-12 / REUSE-T29：基座变换复用候选，性能阻断

起点 `ea8ac53baa8923d93c6233ca88db77f8e8778291`。所属 migration §13 WP-12，实际owner runtime、DTB WP-A。**blocked，生产未落地，独验NOT RUN，不计已完成迁移。** T28成果不改，无新框架/依赖/公共接口/模型owner。

## 候选、保留和来源

候选删除 BaseWorldTransform.cpp 的 makeRotation/identityRotation/multiplyRotation/rotateVector/rotateTransposed/determinant 六个helper，以及原Rodrigues、刚体逆/组合展开，生产净减88行。直接调用既有 `Rotation3D::identity/operator*`、`inverse(Rotation3D)`、`Transform3D::operator*`、`inverse(Transform3D)`、`LinearAlgebra::det`。Gram矩阵仍取RᵀR并按原逐元素最大偏差比较，不能改成一般逆或另一矩阵范数。预设精确矩阵、有限性、安装一致性、1e-9/1e-12、公共错误/返回契约、原比较素材和runtime唯一模型owner保留。没有新facade/cache/model/state；数据流仍是原运行时入口→数学值→原快照/编译/建模消费者。

上游来源：RobWork/RobWork/src/rw/math/Rotation3D.hpp、Transform3D.hpp、LinearAlgebra.hpp/.cpp（既有sdurw_math，经core PUBLIC供给）。原冒烟缺链接的历史理由已经WP-01供给和本轮实际冒烟证伪，不可作为保留重复核心的理由。

EAA边界：`EAA.hpp::axis` 的theta<1e-6分支吞掉1e-8旋转轴，`Math::eaaToQuaternion` 同用此轴。独立sin反例及真实探测见eaa-probe.*。所有者已批准 [GAP-BW-EAA-01方案A](../../../reuse/extensions/GAP-BW-EAA-01.md)：原换算点使用vcpkg既有Eigen::AngleAxisd，经RobWork Rotation3D值构造接入；不恢复EAA补丁、不推广T11局部1e-6、不自写小角算法。第三方catalog表达债务已获明确确认保留，但未伪装成RobWork EAA或NONE、未扩schema/门禁。此批准不接受性能。

## 实际执行与原始证据

| 范围 | 结果 | 原件 |
| --- | --- | --- |
| 旧生产＋新增3项用例 | PASS：45 passed/0 failed/0 skipped | baseline-build.log、baseline.xml/log/report |
| 首轮候选＋编译消费 | PASS：48 passed/0 failed/0 skipped | integration-build.log（后由更完整final原件承接） |
| 最终候选规则、原编译/安装/移动关节/快照只读消费 | PASS：53 passed/0 failed/0 skipped | final-build.log、final.xml/log/report、run-final.cmd |
| modeling安装姿态/模板/IMdl7直接消费者 | PASS：19 passed/0 failed/0 skipped | modeling.xml/log/report、run-modeling.cmd |
| EAA返回矩阵单项扰动 | 预期FAIL：0 passed/1 failed/0 skipped | eaa-disconnected.*、eaa-mutated.cpp |
| 点/逆/重力/组合真实返回单项扰动 | 预期FAIL：0 passed/1 failed/0 skipped；同一有理手算用例多条断言变红 | geometry-disconnected.*、geometry-mutated.cpp |
| Gram对角与行列式手性结果扰动 | 预期FAIL：0 passed/1 failed/0 skipped；阈值外及反射断言变红 | validation-disconnected.*、validation-mutated.cpp |
| 既有冒烟树仅runtime目标 | PASS | smoke-build.log；不重配/构建无关模块 |
| 架构检查 | PASS：11类零命中 | architecture-gates.log |
| 性能准入 | **BLOCKED：所有者“暂不接受，保留性能阻断”** | performance.log、perf*.cpp、run-performance.cmd |
| 本次文档/任务/既有复用治理检查 | PASS（仅治理；存量DEBT/UNRESOLVED未关闭，不等于算法合规） | validate-docs.log；命令为任务契约verify项 |
| 独验、整机/GUI性能、分配故障注入 | NOT RUN | 无；不把自查当独验 |

用例保留原42项并新增单轴零/微小/近π解析、重复确定性、普通角上游对照、有理非平凡点/逆/重力/非交换复合、原Gram阈值/反射。独立期望不调用被测算法产生。原始PASS只证明保存的候选字节，不能冒充恢复后的生产迁移。保存候选为 BaseWorldTransform.candidate.cpp / BaseWorldTransformTest.candidate.cpp；它们不进入任何产品目标。正常最终运行发生在三条扰动恢复正确字节并更新时间戳、重编译之后。

MSVC /O2、200000次/场景×3轮、独立TU、交替old/new：scene0复合、1含检查的逆、2重力、3普通轴角、4微小轴角、5校验。候选微小角约慢1.55–1.88倍，重力约慢1.44–1.46倍，其余也有测得退化。所有者明确暂不接受本包6场景开销；不套用T28或其他包预算，不宣称总体加速。孤立旧核只用于本包对照。

## 当前状态与下一动作

性能决定后，两个生产/测试文件恢复起点的确切内容；本提交只保留任务/提案/候选及证据。catalog、功能矩阵及permissions保持起点，不伪标migrated/verified，不新建关闭凭证。恢复后执行 `cmake --build build --config Release --target sdurws_ird_runtime_test sdurws_ird_modeling_test -- /nologo /verbosity:minimal`，退出码0，PASS，原件restore-build.log；仅恢复二进制与源码一致，不重复未变基线测试。候选自查版本由verification.json逐项绑定；以后继续实施必须以当前主线重新核适用性，不直接覆盖源码。

解除阻断需在既有上游接口内给出能满足所有者要求的性能候选，或所有者明确接受具体预算；任何语义/核心/上游扩展仍另批。不得恢复自写数学fallback。WP-12仍余本包性能、DescriptionValidator/DH等项；普通IK/RNE仍受T13模型真值独验阻断，本包未放行。下一独立直接迁移按WP路线继续定向选择，不以T编号增长记进度。回滚本治理提交只移除本包新记录，不回滚T28或他人改动。
