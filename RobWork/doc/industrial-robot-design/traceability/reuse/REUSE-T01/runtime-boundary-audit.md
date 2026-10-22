# REUSE-T01：runtime 与运动学消费边界审计

本记录只审本批接口/调用链，不宣告全功能覆盖或阶段退出。固定产品源码未改；原库存分类不变。逐区段摘要在 functions/runtime、kinematics、ui.json；8条实际搜索在 batch9-search.json。

## 实际接线与最小改动方向

| 位置/功能 | 已有复用来源 | T02/T03边界与决定 |
| --- | --- | --- |
| runtime/Adapter.hpp；WorkCellCompiler.cpp 的 WorkCellConstView | rw::models::WorkCell/SerialDevice 与 State | findFrame/findDevice/defaultState已是薄转发，不另造WorkCell访问平台。const Ptr保活对象；返回Frame裸指针仍不得逃逸生命周期；只读模型不等于指针可随意跨worker。 |
| runtime/Snapshot.hpp/.cpp 的 IRuntimeModelView、RuntimeSnapshot | workCell/tryDynamicWorkCell/makeState/nameResolver现有入口 | 先复用这四入口。State任务私有，DWC缺失保持显式错误；不把算法搬到runtime，不建新Snapshot或规范模型owner。 |
| kinematics/KinTypes.hpp 的 IKinRuntimeView | 当前仅model/worldToBase | 未暴露编译产物，不能仅把FK函数改成Device调用。T02需最小增量消费面与版本/全部消费者同步；旧注释“尾部追加不破坏ABI”不能当ABI证明。 |
| ui/KinEvaluationChannel.cpp 的 SnapshotViewAdapter、attachSnapshot | shared_ptr<const RuntimeSnapshot>锚 | 已能保活编译产物，优先薄转发；attachView只携数值视图会清除碰撞场景，不能复用旧场景兜底。 |
| ui/KinEvaluationChannel.hpp 的 KinSessionModelView | rebind共享委托 | UI换绑后后台不得读取同一可换绑委托；任务提交必须冻结原快照锚，不把旧任务接到新项目。 |
| kinematics/plugin/KinematicsPanelWidget.cpp 的 FrozenRuntimeView | 当前只复制CanonicalModel和T_world_base；用于IK与workspace提交 | 值快照只能满足旧手写计算路径，不能提供已编译WC。T02须保留原runtime快照/受控视图，不允许在kinematics构造WC来补缺。此处是适配准入缺口，不把现有值冻结误判成创建上游模型违规。 |
| 五个测试视图替身 | KinFkFixture::TestView、MidSolveDegradedView、RigView、FixtureView、ObservedView | 逐一同步新接口；纯数值错误/降级注入保留，生产正向测试另用真实runtime快照。禁止拿test-only模型重建许可推广到生产。 |
| runtime/WorkCellCompiler.cpp 的 jointStaticTransform/compileWorkCell | 原Joint/Device构造与公开运动学API | 映射修复只在runtime owner；运动学以q_auth输入，旋转映射q_rw=q_auth-offset，不能重复offset。移动分支必须先修以下已证实缺陷。 |

## 已证实产品缺陷：移动关节偏置

CanonicalJoint明示 q_authoritative=zeroOffset+q_rw，移动偏置单位m。现有compileWorkCell对移动/旋转共用jointStaticTransform，把offset送入R_axis，再构造PrismaticJoint，子连杆仅补偿轴对齐旋转。于是移动偏置变成旋转，静态平移缺失；这不是RobWork PrismaticJoint缺陷。

runtime-probe复用产品原runtime CMake（包括真实WorkCellCompiler.cpp）、core/testkit、既有CanonicalModelFixture及GTest报告；只通过CONFIG目标别名衔接既有源码内目标名，不复制产品构建源清单。VS x64真实配置/构建exit0。规范期望未放宽：offset=0.25m、q_rw=0.125m、三坐标轴分别测试，权威位移应0.375m且旋转恒等；实际位移0.125m并带0.25rad旋转。旋转控制组通过；移动测试失败。red/原生JSON与XML为total=2、passed=1、failed=1、skipped/notRun=0、进程exit1。原runtime FK等价用例主要是旋转三关节，不能据历史绿证明移动覆盖。

处理：记录为T02 runtime映射修复前置，使用上游Transform3D/关节类型最薄适配，不新增求解核；届时把本规范反例纳入原runtime测试并补非Z轴/Fixed/原点旋转/多TCP/限位及下游FK契约，回归通过后才允许相关迁移验收。T01仅登记发现，不带失败进入T02已完成状态，不用业务层补偿掩盖owner错误。

## 未执行与后续验收

未运行本批GUI或全量产品回归，未实施接口/算法替换；没有生产正向证据，因此全部新增关系保持pending。独立验收需核真实源边界、生命周期/权限及缺陷重现，不能以门禁结构PASS替代语义结论。下一步按全T01覆盖缺口继续审计，再提交T02精确契约与回归方案。
