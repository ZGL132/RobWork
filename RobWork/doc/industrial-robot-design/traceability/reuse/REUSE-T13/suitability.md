# REUSE-T13 适用性与保留边界（实施自查，未独验）

适用基线 `a045120547704d4a40752728632334f56e0b9f5c` 的只读上游；产品输入以逐功能证据的 SHA-256 绑定为准。Windows/MSVC 2022 x64、Release、同一 vcpkg 与 Qt；未验证非 Windows 网格并发。此记录不授予新所有权，不替代 ARCH §3.6 / SA-19。

## 有界发现及处理

| 分类 | 旧路径/证据 | 影响与决定 |
| --- | --- | --- |
| 存量产品缺陷 | WorkCellCompiler.cpp 旧 `jointStaticTransform` 对所有关节调用 `rotationFromCustomEaa(axis*offset)`；`offset-red.xml` 移动用例失败，旋转通过 | m 被当 rad；由实际入 WC 的 PrismaticJoint/RevoluteJoint `getTransform(offset)`、`setFixedTransform` 修正。保持 q_auth=offset+q_rw、bounds 减 offset、默认 q_rw=0；1e-9 不放宽。 |
| 复用精简机会 | 旧 axisAlignmentRotation 自持 cross/atan2/轴角与近极 snap | 改用 Transform3D::makeLookAt。下面列明手性、滚转及近极适用性；删除旧核心，不改上游 EAA。 |
| 复用精简机会 | 旧 parseMeshViaTempFile 逐 object/getTriMesh/逐 triangle 复制为 PlainTriMesh | 直接消费 Model3D::toGeometryData 的 IndexedTriMesh；原来已使用 STLFile/LoaderOBJ，继续保留，不重复造加载器。 |
| 源码证实的并发缺陷，未声称已做旧版竞态压力复现 | 旧同摘要路径 `ofstream(trunc)`，解析结束无条件 remove；两个同摘要调用可交错 truncate/read/remove | 调用独占目录，Boost unique_path 选候选、create_directory 原子取得所有权。生产 8×8 编译及成功/解析异常后的目录清理验证；不递归删目录、不共享 provider 的 mutable buffer。 |
| 源码证实的异常恢复缺口；生产回归见 semantic/runtime XML | STLFile.cpp 与 LoaderOBJ.cpp 在 load 内 setlocale(LC_ALL,"C")，恢复在正常尾部，异常可越过恢复；产品未设置线程 locale 模式 | Windows 加载窗口用 UCRT 线程模式隔离并 RAII 恢复；实际加载路径分别验证线程模式启用/禁用、有效 OBJ、坏 STL。无全局 locale 平台。非 Windows 是待验证风险，不宣称全平台修复。 |
| 缓存版本联动，不宣称发现实际用户缓存事故 | CacheKey.cpp buildKey 直接复用 Snapshot.cpp computeWorkCellCompileIdentity，编码 compilerVersion；CompilerImpl.cpp、Snapshot.cpp 两发布入口传 implementationVersion | 同模型/选项保留 /1.0 会得到旧键。实现版本 /1.1 用既有兼容性判定器确认 compiler-changed，不新增缓存。contractVersion 仍1，模型身份不变。 |
| 此次引入的测试/表示回归，已定位修正 | SnapshotTest 固定期望 PlainTriMesh；上游 toGeometryData 返回 IdxTriMesh，早期 expanded-runtime-first.xml 留 FAIL | 改验真实 TriMesh 面数；WorkCellGeometryTest 进一步验证多对象的顶点、绕序、面积、帧挂接。不把仅改枚举断言当几何准确性证明。 |
| 待归因的上游原测试失败 | upstream.xml：Model3D.constructor 期望 mask=0；同版本 Model3D.cpp 默认 Geometry::PhysicalGroup，实际值1 | 原件9项=8 passed/1 failed；未修改上游、未放宽断言。不据此认定上游 bug；夹具及默认值差异留独验核查，不能外推原套件通过。 |

## 上游能力、语义与生产数据流

- **模型与关节**：CanonicalModel → compileWorkCell → WorkCell/FixedFrame/PrismaticJoint/RevoluteJoint/SerialDevice → 原有受控视图与任务私有 State。WorkCell 接管帧树；runtime 是唯一模型 owner，发布后借持只读；不增加 DTO、缓存、门面或第二条运动链。`Joint::setBounds/setMaxVelocity/setMaxAcceleration` 仍逐字段显式设值。上游 Joint/PrismaticJoint 原测试直接编译复用；数值对照保留产品测试中的独立 Rodrigues/解析平移 oracle。
- **轴适配**：Transform3D.hpp makeLookAt 定义相机 −Z 朝 center。eye=0、center=−axis，结果列向量为 s、u、axis，s×u=axis，故右手且 R*Z=axis。up 取不平行坐标轴；精确 ±Z 保留 I/Rx(pi)。一般轴滚转由 up 决定，并不保持旧最短弧的内部关节帧基底；这是内部表示变化，不能说所有 Frame 矩阵逐元素不变。关节轴/原点与权威一致，子连杆用同一 R 的逆补偿，平移及 R_axis(offset+q) 的物理语义不变；圆柱绕轴滚转无物理差异。原设计 v0.8 是实现历史，未冻结所有一般轴的完整关节帧基底。独验仍须核对依赖借持 Joint Frame 的消费者；若发现依赖旧滚转，不能私自变更该消费契约。近 ±Z、非零安装/关节 origin 旋转、正负 offset、右手/正交及 world/base 数值直接验证，1e-9。
- **网格适配**：provider 字节 → 现有摘要校验/格式选择 → 本次独占临时文件 → 原 STLFile 或 LoaderOBJ → Model3D::toGeometryData → Geometry/RigidObject → WorkCell。Model3D.cpp 复制原 face 索引并应用 object 层级变换；本路径 LoaderOBJ 生成的 object 变换是单位阵，产品不另注入 Model3D 全局变换。多 g 对象的真实生产回归逐面比较解析期望，不依赖输出对象/面的遍历顺序。并未验证任意手工 Model3D 层级、大网格索引阈值或非 OBJ 资源，catalog 限定于此。
- **消费属性**：GeometryData/TriMesh 是此路径既有消费抽象；材质、纹理、独立顶点法线原先的逐三角复制也未保留，本包不宣称新增保真能力。geometry 的 SI scale=1、恒等帧内变换、挂接帧、名字/visual-collision facet 保持；真实资源随 link/TCP/scene 帧运动，图元仍为呈现代理。STL 仍 Plain，OBJ 改 Indexed；不能把具体 Plain 的外部强转视为稳定接口。UI 的 Plain 条件目前定位为 T30 生成 STL 的 smoke 分支，未证实 OBJ 生产显示故障，不擅改 UI。

## business-specific 的非重叠证据

检索命令（按需定位，不全库重读）：

```text
rg -n 'makeLookAt|toGeometryData|getTransform|setFixedTransform' RobWork/RobWork/src/rw/{math,models,geometry}
rg -n 'load|setlocale|parse_g|_transform' RobWork/RobWork/src/rw/loaders/model3d/{STLFile,LoaderOBJ}.cpp
rg -n 'rollback|runStagesOneToFive|implementationVersion|recheckConsumedResources' RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/CompilerImpl.*
rg -n 'CanonicalModel|CompileRequest|RuntimeNameMap|ResourceRef|CompileStage' RobWork/RobWork/src RobWork/RobWorkSim/src
```

上述花括号是命令示意；PowerShell 实际逐路径传参。上游没有本产品 CanonicalModel/修订闭包/摘要身份/诊断协议；这不等于上游缺少数学或机器人能力。

| 保留功能 | 为何保留、依赖与边界 |
| --- | --- |
| 文件声明、常量、MeshReferral、事务槽位/类声明 | 构建依赖声明或产品数据/生命周期定义，不是运算核心；权限按精确文件登记。常量是已有呈现比例、截断长度与退化规则，不扩大数学工具。 |
| linkCylinderRadius | 产品呈现代理半径规则 clamp(0.15L,5mm,80mm)，标准 min/max 足够；上游 Cylinder 只承载尺寸，不决定产品观感策略。 |
| meshFormatOf / meshContentFormat | 选择已有加载器、拒绝不支持格式/危险未知记录；不解码顶点/面，不造解析器。无路径仍走真实 LoaderOBJ/STLFile，DAE/WRL/IV 仍未支持。 |
| MeshLocaleScope / TempFileRemover | 原加载器路径接口和 Windows UCRT 环境的局部 RAII 接缝；无机器人核心，无全局状态台账，目录/locale 不入身份。异常析构不抛；OS 拒绝删除仍可能留文件，不能承诺任意磁盘故障下绝无残留。 |
| throwIfError / scopedFullName / translateRobWorkError | 原产品名称唯一源和已登记 RT 稳定码/错误载体的边界适配；不造第二名称规则或诊断码系统。文案职责本包维持既有分工，未把 runtime 变 diagnostics authority。 |
| CompilerImpl sha256 / oidText / warningRecord / assembleJointBounds / stageName | 直接委托既有 core 摘要、格式化、诊断载体及产品可选字段语义，不复制核心。通用上游工具不能替代产品 OID、NotProvided、限位量纲/发布策略。 |
| CompilerImpl contractVersion / implementationVersion | 原编译器版本契约，/1.1 联动旧键，不扩大公共接口；由现有 CacheKey/Snapshot 机制消费。 |
| runStagesOneToFive / recheckConsumedResources / compile / buildCanonicalModel / createCanonicalModelCompiler | 产品十段事务、修订闭包、资源复查、取消、错误及原子发布；真实运动学/几何仍委托 compileWorkCell 与既有上游。不是可用 WorkCellLoader 替换的自定义机器人算法，不搬入 core/policy、不扩大模型 owner。 |
| CompileTransaction 的构造/生命周期、stage/advance/rollback | 单次产品编译事务状态机与 RAII 回滚，槽位持有上游产物；非 WorkCell/StateStructure 的平行实现，不跨进程。 |
| 四个测试文件的 fixture/provider/helper/TEST | 原测试输入装配、端口替身、独立 oracle 与业务断言；直接复用现有 TestMainReport/GTest/CanonicalModelFixture，新增用例沿用原目标。不为复用破坏独立 oracle。测试内模型由测试调用独占，只服务同包验证，不授予业务层所有权。 |

矩阵 migrated 仅表示本包实施与自查完成（含实审后保留的正确实现）；不是 closed，也不代表上述文件或整个 runtime 已经独立验收。其余基础单元待审计/缺口继续保留。

## 证据适用范围

`production/*.json` 绑定当前源区段、构建定义、上游实现及原始 XML/log。normal、oracle、upstreamComparison 可引用同次真实 runtime 执行：其中独立解析 FK/几何期望与原生 Prismatic/WorkCell 查询对照各有具体用例；不冒称每个 helper 都有独立上游微基准。disconnected 是一次聚合真实生产返回值断开，不是逐函数隔离因果实验；矩阵记录受影响的入口和消费者。原上游测试9项8过1失败仅作原件，不拿失败报告充当绿色正向证据。

性能沿用 `runs.json` 及 perf-* 原件：共同的结构重复编译/STL生产场景，前后各两轮200次；candidate 3.486/3.707s，baseline 3.724/3.274s，区间重叠。含断言/IO/报告的场景时间不能当内核微基准，也不足以认定稳定性能变化。后补断言没有改变这些产品路径；不重复无差别性能/对端/冒烟。
