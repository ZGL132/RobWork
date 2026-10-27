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

## 2026-10-23 定向核查增量（只读调查；不回写上文历史行）

- 上文"移动关节偏置缺陷"已由 REUSE-T13（`f378f01c5`，待独验）修复：PrismaticJoint/RevoluteJoint 按上游 `getTransform(offset)/setFixedTransform` 消费 offset，q_auth=offset+q_rw、限位减 offset、默认 q_rw=0；反例已纳入 WorkCellCompilerTest/WorkCellGeometryTest。本行只登记现状，独验状态见 REUSE-T13 README 追加节。
- 上文"IKinRuntimeView 未暴露编译产物 / FrozenRuntimeView 值快照缺口"复核属实且仍开放：`KinTypes.hpp:141` IKinRuntimeView 仍仅 model()+worldToBase()（§14.4-6 表尾追加纪律在头内）；`KinematicsPanelWidget.cpp:74` FrozenRuntimeView 仍复制 CanonicalModel+T_world_base 值；`KinEvaluationChannel.cpp:61` SnapshotViewAdapter 已持 `shared_ptr<const RuntimeSnapshot>` 锚但接口未暴露。已编译 WC/Device/State 对 kinematics 的最小消费面是 runtime 下一能力包的精确缺口（migration §12.11 顺序"Device/State FK/Jacobian"的准入前置）。
- 四条数据流复核：模型链（CanonicalModel→S6 编译→RuntimeSnapshot 持 Ptr<const WorkCell>/<const DWC>→IRuntimeModelView 四入口 workCell/tryDynamicWorkCell/makeState/nameResolver）端口已足够，**runtime 侧保留、无需修改**（`workCellHandle()` 仍限 MA-02 碰撞设施借持，不扩大）；生命周期链（快照锚→借持→DWC 先于 WC 析构序）由 Snapshot 成员声明序承载；并发链的"任务冻结原锚"当前靠值复制间接成立、结构性修复依赖消费面增量。关节链 q_auth↔q_rw 的换算归属（消费方经 model zeroOffset 单次换算，不缓存）是 FK/IK 替换包的设计决策点，须所有者确认。

## 2026-10-23 消费接口设计增量（智能体A只读调查＋契约草案；未改任何生产接口）

### 一、实际实现者与接缝全量清单（rg 检索为准；不遗漏新增）

| 文件/类型 | 当前持有 | 可换绑 | 保活 | 生产 |
| --- | --- | --- | --- | --- |
| ui/plugin/KinEvaluationChannel.cpp:61 `SnapshotViewAdapter` | `shared_ptr<const RuntimeSnapshot>` 锚（model/worldToBase 纯转发） | 否（每次换绑新建实例） | 自持锚 | 是 |
| ui/plugin/KinEvaluationChannel.hpp:108 `KinSessionModelView` | `shared_ptr<const IKinRuntimeView>` 委托（rebind 仅 UI 拍；地址稳定） | 是（长命缝） | 自持委托 | 是 |
| kinematics/plugin/KinematicsPanelWidget.cpp:74 `FrozenRuntimeView` | CanonicalModel 值副本＋T_world_base 值（:76 构造复制） | 否（提交时一次性） | 任务闭包 shared_ptr（:1247 IK、:1655 workspace） | 是——**替换包删除对象** |
| kinematics/test/KinFkFixture.hpp:337 `TestView`（gui_test 同用） | 值 | — | 测试 | 否 |
| ui/test/KinEvaluationChannelTest.cpp:79 `FixtureView`、:957 `ObservedView` | 值/观测 | — | 测试 | 否 |
| kinematics/test/IkTest.cpp:241 `MidSolveDegradedView` | 双模型中途降级 | — | 测试 | 否 |
| kinematics/test/CollisionPortTest.cpp:191 `RigView` | model 引用 | — | 测试 | 否 |

接缝与入口：`KinPanelTypes.hpp:379`、`KinematicsPanelChannels.hpp:494-496` 面板缝持 `const IKinRuntimeView*`（指向地址稳定的 KinSessionModelView）；`KinEvaluationExecutor::attachSnapshot`（产品路径，UiPlugin.cpp:3663/4477 调用）同拍保留真实快照 `m_runtimeSnapshot`（MA-02 碰撞设施源）并包成 SnapshotViewAdapter；`attachView`（值视图/测试注入）置空 m_runtimeSnapshot（MA-03）、rebind 长命缝、纪元推进、账面清空、在途任务取消旗标置位（真实协作取消已存在）；12 个 kinematics 公共头以 `const IKinRuntimeView&` 收口评估入口；trajectory/KinematicsPort.hpp 仅注释引用先例形态。设计注释中的"KinRuntimeSnapshotView"命名在代码中的落点即 SnapshotViewAdapter（仅注释引用、无该名类型）。

### 二、三方案比较与推荐签名

| 维度 | 方案一：无新接口，装配边界直用 IRuntimeModelView | **方案二（推荐）：版本化可域能力入口** | 方案三：暴露 `shared_ptr<const RuntimeSnapshot>` 具体类型 |
| --- | --- | --- | --- |
| FK/Jacobian 能力 | 可满足（执行器已持 m_runtimeSnapshot），但任务仍需 IKinRuntimeView 值面→值/锚双源无同源绑定 | 满足：锚即 IRuntimeModelView（四入口），值面与锚同源单点 | 满足 |
| 生命周期 | 锚自执行器取得，闭包保活 | 锚随任务闭包 shared_ptr 保活；rebind/换绑不影响（真实所有权，无 no-op deleter） | 同左 |
| 具体类型耦合 | 无 | 无——返回抽象面 | kinematics 公共头绑 RuntimeSnapshot 完整定义（身份/工厂/物化编码全载入）——**被否主因** |
| 模型访问权限 | 不扩大 | 不扩大（IRuntimeModelView 只读面；workCellHandle 不经此暴露） | 面更宽（暴露快照级字段） |
| source/ABI | 评估签名全改＝最大破坏 | 表尾追加一虚＋缺省体：source 兼容（5 个替身零改动即合法）；**不声称二进制 ABI 不变**（审计原文纪律）——接口契约版本 /2 登记＋全体实现者同步＋虚表序入单元卡 | 同二 |
| 值视图/替身 | 测试需另路 | 缺省 null＝纯值视图合法如旧；错误注入替身零改动 | 同二 |
| 实施文件/下游 | 执行器＋面板两提交路径＋评估器签名族（最大） | KinTypes.hpp＋SnapshotViewAdapter＋KinSessionModelView＋面板两处提交替换 FrozenRuntimeView（净删一类）；5 替身零改动 | 同二＋多余耦合 |
| 删除项 | 无结构收益 | **删除 FrozenRuntimeView 整类**（policy §1.1.3 同包清理） | 同二 |

**推荐签名**（KinTypes.hpp 表尾追加；不重排既有成员；单元卡同步登记虚表序）：

```cpp
/**
 * @brief 已编译运行时模型视图锚（可域能力——§4.2 消费面增补行）。
 * 返回的视图（实际 owner＝RuntimeSnapshot，shared_ptr 深拷贝保活）在快照
 * 存活期内稳定；空返回＝纯值视图（无编译产物——调用方按既有两分错误轨
 * 显式处置，禁止回退旧手写链）。缺省实现保证既有实现者 source 兼容。
 */
virtual std::shared_ptr<const runtime::IRuntimeModelView> runtimeModelView() const
{
    return nullptr;
}
```

实现面：SnapshotViewAdapter 返回 m_snapshot（shared_ptr 隐式转换保持真实引用计数）；KinSessionModelView 转发委托；FrozenRuntimeView 删除后任务提交直接捕获锚（IRuntimeModelView 本身实现 model()/worldToBase()，语义逐字同——值面零损失）；5 个值替身零改动。

### 三、旧入口兼容规则

- attachSnapshot（产品）：真实编译产物＝executor 同拍保留的 m_runtimeSnapshot；包装视图锚指向同源快照（值面/锚单点同源）。
- attachView 仅值视图：现有行为全部不变（MA-03 清碰撞场景/纪元/账面/取消）；新锚入口返回 null。
- 需要编译模型能力的新功能：Device 路径 FK/Jacobian（本包）；Device IK（后续包）。
- 能力缺失契约：复用既有两分错误轨 KIN-NO-DEVICE（结构化诊断 params 标注"无编译产物视图"来源，不新增码表语义）；**禁止旧手写链 fallback**。
- 值视图测试去向：错误/降级注入替身原样保留（不达链计算）；FK 数值测试迁移为"合成模型→真实编译→快照锚"夹具（KinFkFixture 增编译路径）——此为替换包实施范围，FkTest 36 用例的迁移代价已计入契约。

### 四、四条替换后数据流（符号定位）

1. **模型**：CanonicalModel → S6 compileWorkCell（T13 已修，WorkCellCompiler.cpp:800 SerialDevice 全名经 NameMap :636）→ RuntimeSnapshot（Snapshot.hpp 成员声明序持 DWC→WC）→ IRuntimeModelView（workCell/tryDynamicWorkCell/makeState/nameResolver，§8.3）→ kinematics 经 runtimeModelView() 锚消费。唯一权威：runtime（SA-19）。
2. **状态**：每任务 `anchor->makeState()`（每线程值拷贝）；同任务串行评估可复用同一 State；跨任务禁止共享可变 State（任务闭包各自持 State）。
3. **关节**：q_auth → `q_rw = q_auth − zeroOffset`（CanonicalModel.hpp:149-151 事实契约"q_authoritative = q_zeroOffset + q_rw"）→ Device::setQ(state, q_rw)。核对结论：Revolute/Continuous 单位 rad、Prismatic m；Fixed 不占 DOF 不进向量（Fk.cpp isMovable 同口径）；Continuous＝规范侧无 bounds 的旋转关节、WC 侧无界 Revolute（WorkCellCompiler.cpp:661/675），裕量按 D-KIN-6 无工作范围=+∞ 既有口径；向量序=链序=SerialDevice DOF 序；T13 已把 WC 侧 bounds 平移 −offset（q_wc 域=q_rw 域）；TCP/设备选择经 nameResolver 全名解析（具体 TCP 帧命名规则为决策点见下）；名称映射唯一权威=快照 nameMap。检索结论：runtime 无现成 q_auth→q_rw 公开入口（编译期只平移 bounds）——最小缺口＝kinematics 单元私有 `src/JointMapping.hpp` 一个纯函数（请求局部、零缓存、FK/IK 共用，后续 dynamics 同规），不建转换平台。
4. **生命周期**：提交瞬间 runtimeModelView() 取锚 → UI rebind/attachView 推纪元置取消旗不影响旧闭包（闭包持旧锚 shared_ptr）→ 任务结束闭包释放 → 借持 Frame/Device 由快照存活期背书＋WC 析构序兜底；workCellHandle() 仍限 MA-02 碰撞设施，kinematics 不消费。

### 五、FK/Jacobian 替换准备清单（后续包；本轮仅准备）

| 旧符号（Fk.cpp，507 行） | 业务契约 | 上游符号 | 数据适配 | 删除项 | 保留项 | 验证 |
| --- | --- | --- | --- | --- | --- | --- |
| 链式累乘 RAcc/pAcc（:283-321） | tcpInBase 1e-9 黄金 | `SerialDevice::baseTframe(tcpFrame, state)`（Device.hpp:207） | JointMapping q 换算＋TCP 帧 nameResolver 解析 | 自建链累乘、toEigen/toRw 往返（:124-136） | PoseMetrics 装配、IRDPM01 编码 | FkTest 36 用例迁移编译夹具＋解析黄金对照 |
| 几何雅可比源点采集与构造（:242-344） | 基础雅可比 6×n 行优先、D-KIN-2 单位口径 | `Device::baseJframe`（Device.hpp:326，返回 rw::math::Jacobian） | 行优先展平逻辑复用；行序/列序与上游约定核对（JacobianCalculator） | axisPointInBase/axisDirInBase 全套 | 展平与指标链 | 独立有限差分＋解析双杆正交列 |
| Eigen JacobiSVD（:349） | 奇异值/条件数/可操作度 | 已是 Eigen（O-40 唯一 SVD 点）——**非 RobWork 但为既定复用决策** | 无 | — | 全部 | Σσ²=‖J‖F² 恒等式既有用例 |
| 裕量归一化（:397-430） | D-KIN-6 黄金锁定 | 无上游对应（产品口径） | q_auth 域 bounds 保持模型承载 | — | 全部 | 既有用例 |
| Ik.cpp poseError/DLS（:150/:336，有限差分雅可比 :361） | IK 数值路径 | （IK 替换归后续独立包；届时 JacobianCalculator 消 FD） | — | — | 本轮不动 | — |
| FrozenRuntimeView（面板） | 提交冻结语义 | 锚即冻结（IRuntimeModelView 不可变） | 闭包捕获 shared_ptr | **整类删除**（:74-~90＋两构造点） | 提交前置空态判定 | 面板提交路径测试 |
| 独立 oracle | 解析黄金生成器/独立 Rodrigues 参考 | 不适用 | — | — | **保持独立**：不因"重复数学"删除、不改调被测上游、不重生成黄金值充当证明 | GoldenKinTest 黄金数据集原值不动 |

无自建缓存/模型副本（FkEvaluator 无状态）✓；死依赖＝FrozenRuntimeView 一项。

### 六、可执行验证清单（接口实施获批后执行；目标名取自实际 CMake）

接口/生命周期包（构建树沿用 `.cache/integrated` 惯例或按根 AGENTS 新配）：
- `sdurws_ird_kinematics_test`：全部实现者/替身编译＋值视图兼容路径＋既有断言不变；
- `sdurws_ird_ui_test`（ird_add_gtest，ui/CMakeLists.txt:824）：SnapshotViewAdapter/KinSessionModelView 锚转发＋attachView 值视图行为不变＋提交后换绑旧任务用原模型（executor 级并发用例，非 GUI）＋任务结束锚可释放＋两任务 State 互不污染＋缺编译产物两分错误；
- `sdurws_ird_kinematics_contract_test`：对端契约回归；
- 真实编译快照消费：KinFkFixture 增编译夹具（合成模型→runtime 工厂→锚）冒烟进 kinematics_test；
- GUI 最小集：仅面板提交/换绑必须经 Widgets 生产入口证明的部分——`sdurws_ird_kinematics_gui_test`（CMakeLists.txt:524）之 KinematicsPanelSessionGuiTest 增"提交后换绑"单例；按 VS x64、QT_QPA_PLATFORM=windows、单 exe 规则；不跑其余 GUI。

FK/Jacobian 包（后续实施轮）：解析单/双杆、非 Z 轴、移动/旋转/固定、正负 offset 与安装旋转、多 TCP 与基座/世界系、Jacobian 有限差分与单位一致性、生产入口消费上游结果、隔离断开（Device 结果弃用→1 失败）、性能/确定性/并发——过滤条件与输出路径（gtest XML＋ird-test-report）随实施契约冻结于 `.cache/integrated` 同构树。

