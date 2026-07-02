# RobotModelBuilder — 运动学/动力学/显示数据分离约定

> 适用版本：RobWorkStudio 插件 `rwslibs::robotmodelbuilder`
> 最后更新：2026-07-02

本约定是给后续运动学 / 动力学计算插件作者的契约。`RobotModelBuilder`
输出的所有 XML 必须严格区分三类数据；任何插件只应读取它需要的类
型，不应跨界猜测。

---

## 1. 三类数据互不混淆

| 数据类型 | 存放文件 | 主要标签 | 谁来读 |
|---------|---------|---------|--------|
| **运动学 (Kinematics)** | `*.wc.xml` 的 `<SerialDevice>` 块 | `<Joint>` / `<DHJoint>` / `<PosLimit>` / `<VelLimit>` / `<AccLimit>` / `<Q>` | 任意插件（运动学、轨迹规划、可视化） |
| **显示 (Visualization)** | `*.wc.xml` 的 `<Drawable>` 子树 | `<Drawable>` / `<Cylinder>` / `<Box>` / `<Sphere>` / `<RGB>` | 渲染器、可视化工具 |
| **动力学 (Dynamics)** | `*.dwc.xml`（独立文件） | `<Mass>` / `<COG>` / `<Inertia>` / `<MaterialID>` / `<ForceLimit>` | RobWorkSim 物理引擎、动力学插件 |

### 1.1 关键原则

* **不要**从 `<Drawable>` 几何反推质量、质心、惯量。
* **不要**把动力学参数藏在 `<Property>` 里。
* `<Frame>` 名字（`Joint1`..`Joint6`、`Base`、`TCP`）必须是稳定的
  字符串，作为 `<Link object="...">` 的查找键。

---

## 2. 文件结构总览

`RobotModelBuilder` 生成三份文件：

```
<RobotName>.wc.xml       — 运动学主模型（含可选 Drawables）
<RobotName>Scene.wc.xml  — Scene wrapper（包含 Frame + Include）
<RobotName>.dwc.xml      — 动力学附加（仅当 GenerateDWC 勾选时输出）
```

`.dwc.xml` 必须用 `<IncludeData>` 包装，便于被顶层 `<DynamicWorkcell>`
通过 `<Include file="..."/>` 引用。

---

## 3. `.wc.xml` 结构

```xml
<SerialDevice name="<RobotName>">
  <!-- 6 个 Joint 或 DHJoint -->
  <Joint name="Joint1" type="Revolute">
    <RPY>0 0 0</RPY>
    <Pos>0 0 0.35</Pos>
  </Joint>
  ...
  <!-- 可选：6 个关节轴 Drawable -->
  <Drawable name="Joint1Axis" refframe="Joint1">
    <RPY>0 0 0</RPY><Pos>0 0 0</Pos><RGB>0.6 0.6 0.6</RGB>
    <Cylinder radius="0.105" z="0.18" />
  </Drawable>
  ...
  <!-- 5 段连杆圆柱（Joint1→Joint2 ... Joint5→Joint6） -->
  <Drawable name="Link1To2" refframe="Joint1">
    <RPY>0 90 0</RPY><Pos>0.06 0 0</Pos><RGB>0.35 0.45 0.65</RGB>
    <Cylinder radius="0.055" z="0.12" />
  </Drawable>
  ...
  <!-- 限位 -->
  <PosLimit refjoint="Joint1" min="-180" max="180" />
  <VelLimit refjoint="Joint1" max="120" />
  <AccLimit refjoint="Joint1" max="360" />
  ...
  <!-- 姿态库 -->
  <Q name="Zero">0 0 0 0 0 0</Q>
  <Q name="Home">0 -1.5708 1.5708 0 0 0</Q>
</SerialDevice>
```

### 3.1 关节命名约定

* 默认六轴：`Joint1`, `Joint2`, ..., `Joint6`
* 用户可改，但需保证 `.dwc.xml` 中 `<ForceLimit joint="...">` /
  `<Link object="...">` 一致。
* `Frame name="Base"` 用于 `<KinematicBase frame="Base">` 引用。

### 3.2 连杆圆柱几何

* `Link{i}To{i+1}` 的 `refframe` 是 `Joint{i}`。
* `Pos`、`RPY`、`length` 由相邻关节位置反算（JointRPYPos 模式直接用
  `Pos` 向量；DH 模式按标准 DH、与导出的 `type="schilling"` 一致，使用
  `RotZ(θ)·TransZ(d)·TransX(a)·RotX(α)`，并在预览零位时由
  `a*cos(θ)`、`a*sin(θ)`、`d` 反算相邻关节位移）。
* 显示效果不参与动力学计算；可任意修改不影响物理仿真。

---

## 4. `.dwc.xml` 结构（RobWorkSim 兼容）

```xml
<IncludeData>
  <RigidDevice device="<RobotName>">
    <ForceLimit joint="Joint1">1000</ForceLimit>
    ...
    <KinematicBase frame="Base">
      <MaterialID>Steel</MaterialID>
    </KinematicBase>
    <Link object="Joint1">
      <Mass>5.0</Mass>
      <COG>0 0 0</COG>
      <Inertia>0.01 0 0 0 0.01 0 0 0 0.01</Inertia>
      <MaterialID>Aluminum</MaterialID>
    </Link>
    ...
  </RigidDevice>
</IncludeData>
```

### 4.1 `<Inertia>` 格式（重要）

`RobWorkSim` 的 `readInertia` 只接受 **3 或 9 个数**，否则抛
`RW_THROW("Inertia needs either 3 or 9 arguments")`。

| 格式 | 含义 |
|------|------|
| `0.01 0.01 0.01` | 主轴惯量 Ixx Iyy Izz（对角矩阵） |
| `Ixx Ixy Ixz Ixy Iyy Iyz Ixz Iyz Izz` | 完整 3×3 矩阵，行优先 |

**RobotModelBuilder 内部用 6 元紧凑格式** `Ixx Iyy Izz Ixy Ixz Iyz`，
写 XML 时自动展开成 9 元行优先格式：
```
<Ixx> <Ixy> <Ixz> <Ixy> <Iyy> <Iyz> <Ixz> <Iyz> <Izz>
```

### 4.2 `<EstimateInertia />`

如果勾选，RobWorkSim 用 `<Link object="...">` 引用的 frame 关联的
Drawable 几何来估算惯量。**没有几何时会 fallback 到微球
(`makeSolidSphereInertia(mass, 0.0001)`)，结果通常不合理。**

### 4.3 顶层 wrapper

单独的 `.dwc.xml` 用 `<IncludeData>` 包装，**不能直接**被
`DynamicWorkCellLoader::load()` 加载。需要在顶层 `<DynamicWorkcell>`
里 `<Include file="MyRobot.dwc.xml"/>` 引用：

```xml
<DynamicWorkcell workcell="MyRobot.wc.xml">
  <Gravity>0 0 -9.82</Gravity>
  <Include file="MyRobot.dwc.xml"/>
</DynamicWorkcell>
```

---

## 5. 数据流：如何由 `.wc.xml` + `.dwc.xml` 启动仿真

```cpp
#include <rw/loaders/WorkCellLoader.hpp>
#include <rwsim/loaders/DynamicWorkCellLoader.hpp>

// 1. 加载运动学 WorkCell
auto wc = rw::loaders::WorkCellLoader::Factory::load("MyRobotScene.wc.xml");

// 2. 加载动力学（注意：要么顶层 dwc，要么 wc + dwc 一起读）
auto dwc = rwsim::loaders::DynamicWorkCellLoader::load("MyRobot.dwc.xml");

// 3. 取 RigidDevice
auto rigidDev = dwc->getRigidDevices().front();  // or findDevice(name)

// 4. 递归牛顿欧拉算法需要：
auto device = rigidDev->getJointDevice();        // SerialDevice
auto base   = rigidDev->getBaseBody();           // KinematicBody
auto bodies = rigidDev->getLinks();              // vector<RigidBody::Ptr>

// 5. 调用 RNE
#include <rwsim/util/RecursiveNewtonEuler.hpp>
rwsim::util::RecursiveNewtonEuler rne(device, base, bodies);
auto torques = rne.calcTorques(state, q, qd, qdd, ftc, totalMass);
```

---

## 6. 接口稳定性保证

下游插件应假设以下名称是**稳定的契约**，不应被破坏：

* `RobotModelBuilder` 生成的 `.wc.xml` 中：
  * `<SerialDevice name="...">` 的 `name` 属性
  * 每个 `<Joint>` / `<DHJoint>` 的 `name` 属性
  * `<Frame name="Base">`（在 Scene wrapper 中）
* `.dwc.xml` 中：
  * `<RigidDevice device="...">` 的 `device` 属性
  * `<Link object="...">` 的 `object` 属性（必须能拼出
    `<RobotName>.<object>` 的 frame 路径）
  * `<KinematicBase frame="Base">`

修改这些字符串会破坏所有下游插件。修改 Drawable 几何不影响物理
仿真；修改 `<Mass>` / `<COG>` / `<Inertia>` / `<ForceLimit>` 不影响
运动学。

---

## 7. 校验规则（生成 .dwc.xml 前）

`RobotModelXmlWriter::validate()` 在勾选 `GenerateDWC` 时会检查：

* `baseFrame` 非空
* `baseMaterial` 非空
* 每个 link 的 `objectName` 非空，且不重复
* `mass > 0`
* `COG` 是有限数值
* 未勾选 `EstimateInertia` 时 `Ixx, Iyy, Izz > 0`
* 每个 `<ForceLimit>` 的 `maxForce > 0`

校验失败时 `saveFiles()` 不写盘，返回 `false`，UI 显示错误。

---

## 8. 完整数据流图

```
   UI 表单  ──────► RobotModelSpec ──────► saveFiles()
                                              │
                          ┌───────────────────┼──────────────────────┐
                          ▼                   ▼                      ▼
                 validate() 检查       *.wc.xml           *.dwc.xml (可选)
                 失败→不写盘              │                      │
                                          ▼                      ▼
                ┌────────────┐    WorkCellLoader        DynamicWorkCellLoader
                │ Drawables  │         │                       │
                └────────────┘         ▼                       ▼
                ┌────────────┐    WorkCell::Ptr         DynamicWorkCell::Ptr
                │ Joints     │    (渲染、运动学)         (RobWorkSim 物理)
                └────────────┘
                ┌────────────┐
                │ Limits/Q   │   ↓ 仅可视化/限位
                └────────────┘
                ┌────────────┐
                │ Mass/COG/  │   ↓ 仅物理
                │ Inertia/FL │
                └────────────┘
```

---

## 9. 后续工作（待办）

* [ ] `MaterialData` 自动生成（避免下游必须先定义 `<Material id="...">`）
* [ ] `<ForceLimit>` 单位区分（当前一律 Nm；移动关节应为 N）
* [ ] 支持自定义 TCP frame
* [ ] 支持 Associate（夹爪 base 几何挂到末端 link）
* [ ] 内置 6 自由度机械臂模板（UR10、UR5e、Panda 等）参数
