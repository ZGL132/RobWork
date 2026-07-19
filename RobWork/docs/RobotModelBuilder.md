# RobotModelBuilder 插件开发文档

> 适用版本:`RobWorkStudio` 插件 `rwslibs::robotmodelbuilder` (sdurws_robotmodelbuilder)
> 适用代码路径:`RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/`
> 最后更新:2026-07-03

## 0. 数据真值约定(先看)

> 本插件严格遵守**单真值**架构:
>
> 1. **`transformJoints`(SE(3) Joint+RPY+Pos)是唯一真值。**
> 2. **`dhJoints` 只是 `transformJoints` 派生出的 DH 投影视图缓存**,在 UI 中是只读表。
> 3. **默认 XML 永远输出 `<Joint>` + `<RPY>`/`<Pos>`**;`<DHJoint>` 仅在隐藏高级选项(全部 Revolute 且 SE(3)→DH 无损)下输出。
> 4. UI 视图模式(`KinematicsViewMode::JointRPYPos` / `DHProjection`)只决定哪张表可见,**不再决定建模方式**——建模方式由 `transformJoints` 唯一决定。
>
> 这意味着:用户改 DH 表的输入会被忽略;改 SE(3) 表才会真正改变模型。`<DHJoint>` 导出只用于兼容老格式,**不**是"另一种建模"。

本插件为 RobWorkStudio 提供一个**所见即所得的 6 轴机器人模型构建工具**。用户通过表单填写关节/Drawable/限位/位姿/动力学参数,插件自动生成 RobWork / RobWorkSim 可识别的三类 XML:

| XML 文件 | 用途 | 加载方 |
| --- | --- | --- |
| `<robotName>.wc.xml` | SerialDevice 主体:Base/TCP 帧、关节、Drawable、限位、预设位姿 | RobWorkStudio 主程序 |
| `<robotName>Scene.wc.xml` | WorkCell 容器:把 SerialDevice 挂到 `WORLD` 下 | RobWorkStudio 主程序 |
| `<robotName>.dwc.xml` | DynamicWorkCell:动力学参数(质量/惯量/力限) | RobWorkSim 物理仿真 |

本文面向接手本插件的开发者,从**架构 / 调用关系 / 数学公式 / 扩展点 / 测试**五个维度展开。

---

## 1. 目录与文件清单

```
RobWorkStudio/src/rwslibs/robotmodelbuilder/
├── CMakeLists.txt              # 构建脚本(插件 + 命令行测试)
├── plugin.json                 # Qt 插件元数据(name/version/description)
├── resources.qrc               # 资源(目前为空)
├── RobotModelBuilderPlugin.hpp # 插件入口声明
├── RobotModelBuilderPlugin.cpp # 插件入口实现(只做信号转发)
├── RobotModelBuilderWidget.hpp # UI 控件声明
├── RobotModelBuilderWidget.cpp # UI 实现(代码方式构建,无 .ui 文件)
├── RobotModelSpec.hpp          # 纯数据结构(无任何 Qt 依赖,UI 与 XML 共用)
├── RobotModelXmlWriter.hpp     # 序列化/校验/保存接口声明
├── RobotModelXmlWriter.cpp     # 序列化/校验/保存实现(含默认模型与几何计算)
└── RobotModelXmlWriterTest.cpp # 不依赖 GUI 的命令行回归测试
```

文件按职责可划分为 4 层:

| 层 | 文件 | 依赖 | 能否独立编译 |
| --- | --- | --- | --- |
| 数据模型 | `RobotModelSpec.hpp` | 仅 STL | ✅ 纯头文件 |
| XML 序列化 | `RobotModelXmlWriter.{hpp,cpp}` | QtCore | ✅ 可独立测试 |
| UI 交互 | `RobotModelBuilderWidget.{hpp,cpp}` | QtWidgets | ❌ 需要 sdurws |
| 插件入口 | `RobotModelBuilderPlugin.{hpp,cpp}` | sdurws | ❌ 需要 sdurws |

---

## 2. 整体架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                       RobWorkStudio 主程序                          │
└─────────────────────────────────────────────────────────────────────┘
                            │ 加载插件
                            ▼
┌─────────────────────────────────────────────────────────────────────┐
│  RobotModelBuilderPlugin                                            │
│  - initialize() 创建 Widget                                         │
│  - open/close():空实现                                              │
│  - loadSceneFile():把信号转发给 getRobWorkStudio()->setWorkcell()   │
└─────────────────────────────────────────────────────────────────────┘
                            │ 持有 + 转发信号
                            ▼
┌─────────────────────────────────────────────────────────────────────┐
│  RobotModelBuilderWidget (QWidget)                                  │
│  ┌───────────────────┐  ┌─────────────────────────────────────────┐  │
│  │ 顶部表单         │  │ QTabWidget                              │  │
│  │ - 名字/目录/模式 │  │  - Kinematics (DH 或 RPY+Pos)           │  │
│  │ - 4 个选项开关  │  │  - Drawables (几何表)                   │  │
│  └───────────────────┘  │  - Limits (关节限位表)                  │  │
│                         │  - Poses (预设位姿,可增删)              │  │
│                         │  - Dynamics (基座/link/力限)            │  │
│                         │  - XML Preview (3 个只读 QTextEdit)     │  │
│                         └─────────────────────────────────────────┘  │
│  底部:Generate Preview / Save XML / Save and Load / Reset + 状态栏  │
└─────────────────────────────────────────────────────────────────────┘
              │                            │
   fill/collectSpec                 applyLinkGeometry / validate / makeXxxXml
              ▼                            ▼
┌──────────────────────┐    ┌────────────────────────────────────────────┐
│  RobotModelSpec      │◄───┤  RobotModelXmlWriter                         │
│  (纯数据结构)        │    │  - makeDefaultSixAxisModel()                │
│  - 基本信息          │    │  - validate()                                │
│  - dhJoints          │    │  - makeSerialDeviceXml / Scene / DWC        │
│  - transformJoints   │    │  - saveFiles()                               │
│  - drawables         │    │  - computeLinkPose() / applyLinkGeometry()  │
│  - limits / poses    │    └────────────────────────────────────────────┘
│  - dynamics          │                       │
└──────────────────────┘                       ▼
                                  ┌─────────────────────────┐
                                  │ 磁盘 XML (*.wc.xml /    │
                                  │ *.dwc.xml)              │
                                  └─────────────────────────┘
```

**关键设计原则:**

1. **数据模型零 Qt 依赖** — `RobotModelSpec.hpp` 只引用 STL,使得 XML 序列化层可以脱离 UI/GUI 编译,便于 CI 无头测试。
2. **SE(3) 唯一真值,DH 只读投影视图** — `transformJoints` 是唯一可编辑的真值;`dhJoints` 由其派生,UI 上整表只读;默认 XML 永远输出 `<Joint>`;`<DHJoint>` 仅作隐藏高级选项。
3. **UI 只负责"显示 + 收集",不直接生成 XML** — 所有 XML 生成与几何计算都委托给 `RobotModelXmlWriter`,确保两份产物(预览 XML 与磁盘 XML)完全一致。
4. **Plugin 仅做信号转发** — 业务逻辑全部下沉到 Widget 与 XmlWriter,Plugin 本体非常薄,避免被宿主生命周期细节污染。

---

## 3. 调用关系详解

### 3.1 启动时序

```
RobWorkStudio 启动
        │
        ├─► 扫描并加载 sdurws_robotmodelbuilder
        │       (由 CMake 提供的 rws_plugin_load_details 注册)
        │
        ├─► new RobotModelBuilderPlugin()
        │       └─► 基类构造:记录插件名 "RobotModelBuilder"
        │
        └─► plugin->initialize()
                ├─► new RobotModelBuilderWidget(this)
                │       ├─► buildUi()        (创建 6 个 Tab + 表单 + 按钮)
                │       └─► resetDefaults()  (填入通用 6 轴出厂数据)
                │               ├─► fillFromSpec(makeDefaultSixAxisModel())
                │               └─► generatePreview()  (首次预览)
                │
                ├─► connect(_widget, loadSceneRequested → this, loadSceneFile)
                │
                └─► setWidget(_widget)   (把 Widget 嵌入 RWS 的 Dock)
```

### 3.2 "Save and Load" 流程

```
用户点击 "Save and Load"
        │
        ▼
Widget::saveAndLoad()
        │
        ├─► validateTableInput(errors)       (UI 文本格式校验)
        │       - DH/Limits/Poses/ForceLimits 必须能解析为 double
        │       - Transform/Drawables/Dynamics 中标了 "x y z"/"x1..x6"
        │         的列必须能拆成对应维度的 double
        │   若失败:showErrors(errors) + return
        │
        ├─► collectSpec()                    (UI → RobotModelSpec)
        │       - 顶部表单 → robotName/saveDirectory/mode/showFrameAxes/...
        │       - 各表格 → dhJoints/transformJoints/drawables/limits/poses/...
        │
        ├─► applyLinkGeometry(spec)          (自动重算连杆圆柱几何)
        │       对每个 autoLinkGeometry=true 且名字匹配 "Link{i}To{i+1}"
        │       的 Drawable,根据关节几何重新填 pos/rpy/length。
        │
        ├─► XmlWriter::saveFiles(spec, errors)
        │       ├─► validate(spec, errors)   (业务校验)
        │       │       - 名字非空/合法字符/不重复
        │       │       - 关节名 ≠ 空、不重复,数量=6
        │       │       - Drawable 几何合法(radius>0,length>0,RGB∈[0,1])
        │       │       - 关节限位:min<max,vel/acc>0
        │       │       - 动力学:mass>0,Ixx/Iyy/Izz>0(若未 Estimate)
        │       │       - 力限>0
        │       │
        │       ├─► serialDeviceFilePath() → 写 <robotName>.wc.xml
        │       ├─► (若 generateScene) sceneFilePath() → 写 <robotName>Scene.wc.xml
        │       └─► (若 DWC)         dwcFilePath()    → 写 <robotName>.dwc.xml
        │
        ├─► generatePreview()                (刷新 UI 上的 3 段预览)
        │
        ├─► emit loadSceneRequested(sceneFilePath(spec))
        │       └─► Plugin::loadSceneFile(filename)
        │               └─► getRobWorkStudio()->setWorkcell(filename)
        │                       └─► RobWorkStudio 重新加载场景
        │
        └─► setStatus("XML files saved. Loading scene...")
```

### 3.3 "Generate Preview" 与 "Save XML" 的区别

| 按钮 | 写盘 | 加载场景 | 失败时表现 |
| --- | --- | --- | --- |
| **Import URDF** | ❌ | ❌ | 解析失败弹窗;成功后只填充 UI 并刷新预览 |
| **Generate Preview** | ❌ | ❌ | 仅刷新三段 XML 预览,弹窗报错 |
| **Save XML** | ✅ | ❌ | 失败弹窗,状态栏报错 |
| **Save and Load** | ✅ | ✅(自动加载场景) | 失败弹窗,不触发加载 |

### 3.4 UI 视图模式切换

UI 顶部的 "Mode" 下拉框对应 `KinematicsViewMode`,**只是 UI 视图切换器**,不再决定建模方式:

- `JointRPYPos`(默认):显示 SE(3) 真值表,隐藏 DH 投影视图表。
- `DHProjection`:显示 DH 投影视图(只读),隐藏 SE(3) 真值表。

切换时:
- `modeChanged(index)` 槽 → 隐藏/显示 `_dhTable` / `_transformTable`。
- **不影响** `spec.mode` 对 XML 输出路径的影响——`XmlWriter::makeSerialDeviceXml()` 默认永远输出 `<Joint>`,与 UI 模式无关。
- **不影响** `computeLinkPose()` 的行为——它永远从 `transformJoints[i].pos` 取位移,与 UI 模式无关。

---

## 4. 数据模型字段速查

`RobotModelSpec`(见 [RobotModelSpec.hpp](RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp)) 是整个系统的"事实来源",所有 UI 与 XML 都围绕它运转。

| 字段 | 单位 | 是否必填 | 备注 |
| --- | --- | --- | --- |
| `robotName` | — | ✅ | 经 `sanitizeFileBaseName` 清洗后作 XML 节点/文件名前缀;允许 `[A-Za-z0-9_-]` |
| `saveDirectory` | 路径 | ✅ | 必须存在(校验) |
| `mode` | enum | ✅ | `KinematicsViewMode::JointRPYPos` / `DHProjection` — **只决定 UI 上哪张表可见**,不影响 XML 输出 |
| `exportDhJointsAdvanced` | bool | 默认 false | 隐藏高级选项;true 且 `canExportDhJoints()` 通过时输出 `<DHJoint>`,否则 fallback `<Joint>` |
| `showFrameAxes` | bool | ✅ | 为 true 时所有 Frame/Joint 输出 `<Property name="ShowFrameAxis">true</Property>` |
| `generateDrawables` | bool | ✅ | 关闭时跳过所有 Drawable 校验与序列化 |
| `generateScene` | bool | ✅ | 关闭时不写 `<robotName>Scene.wc.xml` |
| `transformJoints[]` | — | 可变项 | **唯一真值**;每行对应一个 Revolute/Prismatic/FixedFrame/ToolFrame。默认模型提供 6 个 Revolute 关节,但 UI 支持增删和排序。 |
| `dhJoints[]` | — | 可变项 | **DH 投影视图缓存**(由 `transformJoints` 派生);UI 上整表只读 |
| `drawables[]` | — | 可选 | 字段见下表 |
| `limits[]` | 度 / 度·s⁻¹ / 度·s⁻² | 通常 6 项 | 写入 XML 前做度→弧度 |
| `poses[]` | 度 | 可选 | 写入 XML 前做度→弧度 |
| `dynamics.generateDynamicWorkCell` | bool | ✅ | 开启才写 .dwc.xml |
| `dynamics.baseFrame` / `baseMaterial` | — | 开启 DWC 时必填 | 用于 `<KinematicBase>` |

**`DHJointSpec`**(对应 XML `<DHJoint type="schilling">`):

| 字段 | 含义 | RobWork 标签 |
| --- | --- | --- |
| `name` | 关节名,默认 `Joint1..Joint6` | `name` |
| `alphaDeg` | 绕 X_{i-1} 旋转,直到 Z_{i-1} 与 X_i 平行的扭转角 | `alpha` |
| `a` | 沿 X_i 的连杆长度(米) | `a` |
| `d` | 沿 Z_{i-1} 的连杆偏距(米) | `d` |
| `offsetDeg` | 关节零位偏移角 θ₀ | `offset` |

**`JointTransformSpec`**(对应 XML `<Joint>`):

| 字段 | 含义 | RobWork 标签 |
| --- | --- | --- |
| `name` | 关节名 | `name` |
| `type` | `Revolute` / `Prismatic` | `type` |
| `rpyDeg` | 父系 → 本系的 Z-Y-X 欧拉角(度) | `<RPY>` |
| `pos` | 父系 → 本系的平移(米) | `<Pos>` |

**`DrawableSpec`**(对应 XML `<Drawable><Cylinder>`):

| 字段 | 含义 | RobWork 标签 |
| --- | --- | --- |
| `name` | 名称 | `name` |
| `refFrame` | 挂载点(必须已存在:Base / 关节名 / TCP) | `refframe` |
| `shape` | 固定 `"Cylinder"`(语义保留) | `<Cylinder>` |
| `radius` / `length` | 圆柱半径 / 长度(米) | `radius` / `z` |
| `rpyDeg` / `pos` | 在 `refFrame` 下的位姿 | `<RPY>` / `<Pos>` |
| `rgb` | 颜色,3 个 [0,1] 分量 | `<RGB>` |
| `collisionModel` | 是否同时作为碰撞模型 | `colmodel` |
| `autoLinkGeometry` | true 表示此 Drawable 由 `applyLinkGeometry()` 维护 | (内部标志) |

**`LinkDynamicsSpec`**(对应 XML `<Link>`):

| 字段 | 含义 | RobWork 标签 |
| --- | --- | --- |
| `linkName` | 显示名,UI/日志用 | (无) |
| `objectName` | 必须匹配 `*.wc.xml` 中的 Frame/Joint 名(如 `Joint1`) | `object` |
| `mass` | kg | `<Mass>` |
| `cog` | 质心,在 `objectName` 坐标系下(米) | `<COG>` |
| `inertia` | `(Ixx, Iyy, Izz, Ixy, Ixz, Iyz)` | `<Inertia>`(展成 3×3 矩阵) |
| `estimateInertia` | true 让 RobWorkSim 自行估算 | `<EstimateInertia />` |
| `material` | 材料名 | `<MaterialID>` |

---

## 5. DH 投影视图与 SE(3) 真值

UI 中 "Joint + RPY + Pos" 表是**唯一可编辑的真值**; "DH Parameters" 表是**只读投影视图**——由 SE(3) 真值按本节约定派生。任何在 DH 表上的编辑都会被忽略(只显示提示),不会反向污染真值。

### 5.1 约定

为使"DH 投影视图"表达的几何与真值一致,本插件采用下列映射关系。位姿的反解采用 `(r, θ) = (√(px²+py²), atan2(py,px))` 的极坐标形式,与 `dhLinkVector` / `computeLinkPose` 内部约定的"标准 DH 平移"完全一致。

| 字段 | 方向 | 公式 |
| --- | --- | --- |
| `roll` (RPY[0]) | 真值 → DH | `= offsetDeg` |
| `pitch` (RPY[1]) | — | **固定为 0**(DH 旋转 R_x(α)·R_z(θ) 在 ZYX 欧拉分解下 pitch 恒为 0) |
| `yaw` (RPY[2]) | 真值 → DH | `= alphaDeg` |
| `pos[0]` | 真值 → DH | `= a · cos(offsetDeg · π/180)` |
| `pos[1]` | 真值 → DH | `= a · sin(offsetDeg · π/180)` |
| `pos[2]` | 真值 → DH | `= d` |
| `offsetDeg` | DH → 真值 | `pos.xy ≠ 0` 时 `= atan2(pos[1], pos[0]) · 180/π`;<br>`pos.xy = 0` 时 `= roll`(见下) |
| `alphaDeg` | DH → 真值 | `= rpyDeg[2]` (yaw) |
| `a` | DH → 真值 | `= √(pos[0]² + pos[1]²)`(`pos.xy = 0` 时为 0) |
| `d` | DH → 真值 | `= pos[2]` |
| `name` | 单向 | 直接从真值复制 |
| `type` (Revolute / Prismatic) | 单向 | DH 投影视图不携带 type;UI 表格的 Status 列根据 type 显示 `Lossless` / `Projected` / `Unsupported` |

> **关于"有损"**(`真值 → DH` 方向):
> 1. **pitch 非零**:DH 旋转 R_x(α)·R_z(θ) 在 ZYX 欧拉分解下 pitch 恒为 0;用户写入的非零 pitch 无法表达,被丢弃。
> 2. **roll 与 pos 的 xy 方向不一致**:在极坐标约定下,`roll` 与 `atan2(pos[1], pos[0])` 必须描述同一个 `offset`。若用户在两个字段里写入不同的值(例如 `roll=30°` 但 `pos=(0.5, 0, 0.3)`,后者隐含 `offset=0°`),代码以 `pos` 为权威来源(`offset = atan2(py,px)`),原 `roll` 被覆盖。
> 3. 两类有损都通过 `transformJointToDh(joint, &lossy)` 的出参告知调用方;UI 拿到后通过状态栏告诉用户"实际写入的 DH 值是什么、原始的 pitch/roll 是多少",同时在 DH 表的 Status 列显示 `Projected`。
> 4. **xy 为零的特殊情况**:`pos[1]=pos[0]=0` 时,`atan2(0,0)` 数值不稳定(数学上为 0,但语义上没意义),代码保留 `roll` 作为 `offset`,`a = 0`。此时只要 `pitch=0`,转换**无损**;`roll` 描述的就是关节绕 Z 的纯旋转。
> 5. **角度比较使用 `normalizedAngleDiffDeg`**:比较 `roll` 与 `atan2(py,px)` 时,差值要先按 360° 取模到 `[-180, 180]` 再取绝对值,避免 `roll=359°` 与 `roll=1°` 之类的绕回被误判为不一致。
> 6. `offset=0` 时新约定退化为旧约定:`pos = (a, 0, d)`,默认值不变。
> 7. **高级 `<DHJoint>` 导出要求**:`exportDhJointsAdvanced=true` 时,任何一行出现上述有损情况(包括 `type != Revolute`)都会让 `canExportDhJoints()` 返回 false,`validate()` 报错,`makeSerialDeviceXml` 回退到默认 `<Joint>` 输出。

### 5.2 转换入口(模型层)

`RobotModelXmlWriter` 暴露 4 个纯静态函数,可在命令行测试中独立验证:

```cpp
// 单行转换
static JointTransformSpec dhJointToTransform(
    const DHJointSpec& dh,
    const std::string& existingType = std::string());

static DHJointSpec transformJointToDh(
    const JointTransformSpec& joint,
    bool* lossy = nullptr);   // [out] 当转换有损时被置为 true
                              // 有损 = pitch != 0,或 roll 与 pos.xy 方向不一致

// 整组维护(按行号,两侧 vector 长度不一致时不做增删)
static void refreshDhProjectionFromTransform(RobotModelSpec& spec);
    // 真值 -> DH 投影缓存:唯一允许从真值单向往 DH 写入的路径
    // UI 中"Transform 表编辑后立刻刷新 DH 表"用的就是这个

static void applyDhInputToTransform(RobotModelSpec& spec);
    // DH -> 真值:**会改写 SE(3) 真值**;仅作内部"Apply DH"工具,
    // UI 当前不调用;测试 / 批量回填场景使用
```

内部还用到 1 个**仅本文件使用**的辅助:

```cpp
// 匿名命名空间内的 normalizedAngleDiffDeg(lhs, rhs):
//   把 (lhs - rhs) 模 360° 归一化到 [-180, 180],再取绝对值。
//   用途:比较两个角度是否"在 360° 环上相等",避免 359°/1° 被误判为 358° 差异。
```

所有逻辑都在 `RobotModelXmlWriter.cpp`,**与 Qt Widget 完全解耦**,命令行测试 `sdurws_robotmodelbuilder_xmltest` 可直接覆盖。
所有来自用户输入或 URDF 的字符串在写入 XML 属性或文本节点前必须转义。新增字符串输出点应复用 `RobotModelXmlWriter.cpp` 内部的 XML escaping helper,不要直接把 `std::string` 或 `QString` 拼进 XML。

### 5.3 UI 接线(单向:真值 → 投影)

`RobotModelBuilderWidget` 通过 `QTableWidget::itemChanged` 信号维护"真值 -> DH 投影视图"单向刷新:

```
用户编辑 SE(3) Transform 表的某一行
        │
        ▼
onTransformTableCellChanged(item)
        │
        ├─► 检查 _syncingTables,若已在同步中则直接 return(防递归)
        │
        ├─► 读取整行 name/type/RPY/Pos
        │
        ├─► parseVector3 校验 RPY 和 Pos
        │   └─► 任一向量解析失败 → setStatus 报错并 return(避免把 0 写进 DH)
        │
        ├─► RobotModelXmlWriter::transformJointToDh(..., &lossy)
        │   └─► 总是把反推出的 alpha/a/d/offset 写入 DH 投影视图
        │       (即便 lossy=true 也不阻塞 UI)
        │
        ├─► 计算 Status:
        │   * type != Revolute → "Unsupported"
        │   * lossy == true   → "Projected"
        │   * 其它            → "Lossless"
        │
        ├─► 若 lossy=true,setStatus 提示用户:
        │   "Row N: RPY/Pos was projected to DH;
        │    pitch=X deg, roll=Y deg, projected offset=Z deg,
        │    alpha=A deg, a=B m, d=C m. (Status: Projected)"
        │   即明确告诉用户"哪个 pitch/roll 被丢、实际写入的 DH 值是什么"
        │   否则:仅显示成功同步的 offset/alpha/a/d + Status
        │
        ├─► _syncingTables = true
        ├─► setItem(... DH ... 5 个数值列 + 1 个 Status 列, 全部 editable=false)
        │   └─► 每个 setItem 都触发 itemChanged,但回调看到 _syncingTables=true 立即 return
        └─► _syncingTables = false
```

`onDhTableCellChanged` 不再写回 SE(3)(DH 表是只读视图):
- 整体 `setEditTriggers(QAbstractItemView::NoEditTriggers)` + 每个单元格 `setFlags(~ItemIsEditable)` 双重保护。
- 理论上不会被用户编辑;留这个槽只是为了在 setItem 误用时给出明确提示。

`_syncingTables` 是 Widget 内部的 `bool` 标志,作为重入锁使用 — 它**只**在 Widget 内部写表时短暂打开,保证不会进入"setItem 触发 itemChanged 又触发 setItem"的循环。

`fillFromSpec`(程序化回填 UI,如 resetDefaults)也会在调用前后打开/关闭 `_syncingTables`,避免被 setItem 触发的 itemChanged 错误地当作"用户编辑"。

### 5.4 测试矩阵

`RobotModelXmlWriterTest.cpp` 的"DH <-> Joint+RPY+Pos 双向转换" 一节覆盖了:

| 场景 | 断言 |
| --- | --- |
| `dhJointToTransform` 默认值 | roll=offset, pitch=0, yaw=alpha, pos=(a·cos, a·sin, d) |
| `dhJointToTransform` offset=0 | 退化为 (a, 0, d) |
| `dhJointToTransform` 保留 type | Prismatic 不被强制改回 Revolute |
| `transformJointToDh` 一致输入 | roll/yaw/a/d 与 pos 一一对应,lossy=false |
| `transformJointToDh` pitch != 0 | lossy=true,其余按 pos 反推 |
| `transformJointToDh` roll 与 pos 不一致 | lossy=true,offset 走 pos 反推 |
| `transformJointToDh` xy=0、pitch=0 | lossy=false,保留 roll 作 offset |
| `transformJointToDh` 不传 lossy | 仍能正确返回(默认 nullptr) |
| 往返无损 | DH → Transform → DH(用新约定数据)、Transform → DH → Transform(一致数据) |
| `applyDhInputToTransform` | RPY=(offset, 0, alpha),pos=(a·cos, a·sin, d),custom type 保留 |
| `refreshDhProjectionFromTransform` | 数值与 Transform 对齐(一致数据) |
| `refreshDhProjectionFromTransform` 有损 | pitch 被丢,pos 反推的值正确 |
| 长度不一致 | 不抛异常、不增删行 |

### 6.1 标准 DH 平移向量(`dhLinkVector`)

在标准 DH(RobWork `type="schilling"` 约定)下,父系到本系的位移只取决于 `a` 与 `d`,绕 X 轴旋转 `α` 后再沿新 Z 走 `d`:

```
v = (a·cos(θ₀), a·sin(θ₀), d)
   其中 θ₀ = offsetDeg · π / 180
```

这个向量 `v` 就是连杆 `i`(从 Joint_{i+1} 到 Joint_{i+2})在 Joint_{i+1} 坐标系下的方向与距离。

> **注意**:`JointRPYPos` 模式下不调用此函数,直接用 `transformJoints[i].pos` 作为 `v`——因为该模式下用户已经在 Joint_{i+1} 坐标系下给出了平移。

### 6.2 自动连杆圆柱姿态(`computeLinkPose`)

**目标**:已知 `v`,求一个圆柱的中心位置 `p`、RPY 姿态 `(roll, pitch, yaw)`、长度 `L`。

**步骤 1 — 长度与中心**:
```
L = √(vₓ² + vᵧ² + v_z²)
p = v / 2
```

`L < 1e-9` 时几何退化,直接返回零姿态零长度。

**步骤 2 — 把默认 +Z 旋到 v̂**:
默认圆柱轴向是局部 `+Z`;需要构造一个旋转 `R`,使得 `R · (0,0,1)ᵀ = v̂ = v/L`。

把 `R` 表示为**轴-角**形式:
- 旋转轴 `k = (0,0,1) × v̂ = (-vᵧ, vₓ, 0) / L`
- 旋转角 `α = acos(v_z / L)`

```
‖k‖ = √(vₓ² + vᵧ²) / L
```

**情况 A:`v̂` 与 ±Z 共线**(`‖k‖ < 1e-9`,即 `vₓ = vᵧ = 0`)

- `v_z > 0`:保持单位矩阵,圆柱沿 +Z 摆放,无需旋转。
- `v_z < 0`:绕 X 轴旋转 180°:`R = diag(-1, -1, 1)`,圆柱翻向 -Z。

**情况 B:一般情况**

用 Rodrigues 公式从 `(k/‖k‖, α)` 构造旋转矩阵 `R`:

```
c = cos(α), s = sin(α), C = 1 - c
n = (nx, ny, nz) = k / ‖k‖

R = | c + nx²·C       nx·ny·C - nz·s   nx·nz·C + ny·s |
    | ny·nx·C + nz·s  c + ny²·C        ny·nz·C - nx·s |
    | nz·nx·C - ny·s  nz·ny·C + nx·s   c + nz²·C      |
```

**步骤 3 — Z-Y-X 欧拉角提取**

约定 `R = R_z(yaw) · R_y(pitch) · R_x(roll)`,展开后:

```
R[2][0] = -sin(pitch)
R[1][0] = sin(yaw)·cos(pitch)
R[0][0] = cos(yaw)·cos(pitch)
R[2][1] = cos(pitch)·sin(roll)
R[2][2] = cos(pitch)·cos(roll)
```

所以:
```
pitch = asin(-R[2][0])
yaw   = atan2(R[1][0], R[0][0])            (非奇异情况)
roll  = atan2(R[2][1], R[2][2])            (非奇异情况)
```

当 `|R[2][0]| > 0.9999`(`pitch ≈ ±90°`)进入**万向锁**,任取 `yaw = 0`,然后:
```
roll = atan2(-R[0][1], R[1][1])
```

最后把弧度转回度。

### 6.3 为什么默认 RPY 是 `(offsetDeg, 0, alphaDeg)`?

在 JointRPYPos 模式下,默认模型把 DH 参数翻译为 RobWork 的 Z-Y-X 欧拉角:

```
RPY = (roll = offset, pitch = 0, yaw = alpha)
```

直观解释:`offset` 是绕 Z 轴的初始零位旋转,`alpha` 是绕 Z 轴再绕 X 轴方向的扭转。把 `alpha` 放到 `yaw` 位置是因为 RobWork 的 Z-Y-X 顺序意味着 `yaw` 先施加、再 `pitch`、再 `roll`;若 `pitch = 0` 且 `roll = 0`,就只有 `yaw` 生效——刚好等价于绕 Z 旋转 `alpha`。

> ⚠️ **这是约定的近似**,并不严格等价于 DH 变换的完整 4×4 矩阵,仅在简单几何下能给出与 `dhLinkVector` 一致的可视化效果。当 `pitch` 不为 0 时,DH 与 RPY 之间存在非线性差异——这就是为什么 Widget 同时暴露两种模式,让用户根据自己的建模习惯选择。

### 6.4 6 元惯量 → 3×3 矩阵

`inertia[6] = (Ixx, Iyy, Izz, Ixy, Ixz, Iyz)`,对称矩阵:
```
        | Ixx  Ixy  Ixz |
I =     | Ixy  Iyy  Iyz |
        | Ixz  Iyz  Izz |
```

`makeDynamicWorkCellXml` 按**行优先**展成 9 个数写入 `<Inertia>...</Inertia>`:
```
inertia[0] inertia[3] inertia[4]   inertia[3] inertia[1] inertia[5]   inertia[4] inertia[5] inertia[2]
   Ixx       Ixy       Ixz          Ixy       Iyy       Iyz          Ixz       Iyz       Izz
```

`estimateInertia = true` 时不写矩阵,只写 `<EstimateInertia />`,RobWorkSim 会基于几何自动估算。

---

## 7. XML 输出格式速查

### 7.1 `<robotName>.wc.xml`(SerialDevice)

```xml
<SerialDevice name="GenericSixAxis">
  <Frame name="Base">
    <RPY>0 0 0</RPY>
    <Pos>0 0 0</Pos>
    <Property name="ShowFrameAxis">true</Property>   <!-- 若 showFrameAxes=true -->
  </Frame>

  <!-- 默认情况:6 个 SE(3) Joint -->
  <Joint name="Joint1" type="Revolute">
    <RPY>0 0 0</RPY>
    <Pos>0 0 0.35</Pos>
    <Property name="ShowFrameAxis">true</Property>
  </Joint>
  ... (共 6 个 Joint)

  <!-- 隐藏高级选项(exportDhJointsAdvanced=true 且 canExportDhJoints 通过)时:
       输出 6 个 <DHJoint type="schilling">,此时不再输出 <Joint> -->
  <!--
  <DHJoint name="Joint1" alpha="0" a="0" d="0.35" offset="0" type="schilling">
    <Property name="ShowFrameAxis">true</Property>
  </DHJoint>
  ... (共 6 个 DHJoint)
  -->

  <Frame name="TCP" refframe="Joint6">   <!-- 最后一个关节 -->
    <RPY>0 0 0</RPY>
    <Pos>0 0 0</Pos>
    <Property name="ShowFrameAxis">true</Property>
  </Frame>

  <!-- Drawables(若 generateDrawables=true) -->
  <Drawable name="Joint1Housing" refframe="Joint1">
    <RPY>0 0 0</RPY>
    <Pos>0 0 0</Pos>
    <RGB>0.45 0.45 0.48</RGB>
    <Cylinder radius="0.095" z="0.1" />
  </Drawable>
  <Drawable name="Link1To2" refframe="Joint1">
    <RPY>...0 90 0...</RPY>   <!-- 由 computeLinkPose 自动填 -->
    <Pos>0.15 0 0.2</Pos>
    <RGB>0.35 0.45 0.65</RGB>
    <Cylinder radius="0.055" z="0.5" />
  </Drawable>

  <PosLimit refjoint="Joint1" min="-180" max="180" />
  <VelLimit refjoint="Joint1" max="120" />
  <AccLimit refjoint="Joint1" max="360" />
  ... (三种 limit 各 6 条)

  <Q name="Zero">0 0 0 0 0 0</Q>
  <Q name="Ready">0 -1.5707963267949 1.5707963267949 0 0 0</Q>
</SerialDevice>
```

> ⚠️ **`<DHJoint>` 仅在隐藏高级选项通过时输出**:
> 1. UI 上 `_exportDhAdvanced` 默认 `setVisible(false)`,用户看不到这个开关。
> 2. 即使用户(或代码)显式打开 `exportDhJointsAdvanced=true`,`canExportDhJoints()` 也会要求所有 6 行都是 `Revolute` 且 `transformJointToDh` 全部无损,否则 `validate()` 报错,`saveFiles` 拒绝落盘。
> 3. `makeSerialDeviceXml()` 内有 fallback 逻辑——若 `canExportDhJoints` 失败,自动回退到默认 `<Joint>` 输出(防御性编程)。

> ⚠️ 关节限位用**度**(RobWork 内部会再换算),但 `<Q>` 中的位姿用**弧度**(`-90° → -π/2 ≈ -1.5707963267949`)。

### 7.2 `<robotName>Scene.wc.xml`(WorkCell 容器)

```xml
<WorkCell name="GenericSixAxisScene">
  <Frame name="RobotBase" refframe="WORLD">
    <RPY>0 0 0</RPY>
    <Pos>0 0 0</Pos>
    <Property name="ShowFrameAxis">true</Property>
  </Frame>

  <Include file="GenericSixAxis.wc.xml" />
</WorkCell>
```

注意 `<Include>` 引用的是 **不**带 `Scene` 后缀的 SerialDevice 文件——这是 RobWork 加载链约定:Scene 文件是用户打开的入口,内部通过 `Include` 串到真正的机器人定义。

### 7.3 `<robotName>.dwc.xml`(DynamicWorkCell)

```xml
<DynamicWorkCell workcell="GenericSixAxisScene.wc.xml">
  <RigidDevice device="GenericSixAxis">
    <ForceLimit joint="Joint1">1000</ForceLimit>
    ... (共 6 个 ForceLimit)

    <KinematicBase frame="Base">
      <MaterialID>Steel</MaterialID>
    </KinematicBase>

    <Link object="Joint1">
      <Mass>5</Mass>
      <COG>0 0 0</COG>
      <Inertia>0.01 0 0 0 0.01 0 0 0 0.01</Inertia>
      <MaterialID>Steel</MaterialID>
    </Link>
    ... (共 6 个 Link)

    <!-- estimateInertia=true 的 Link 会输出 -->
    <!-- <EstimateInertia /> -->
  </RigidDevice>
</DynamicWorkCell>
```

---

## 8. 扩展点

### 8.1 增加新的 Drawable 形状

当前 `DrawableSpec::shape` 已支持 Box / Cylinder / Sphere / Cone / Plane / STL / Mesh / Polytope。若继续扩展新的几何类型:

1. 在 `RobotModelSpec.hpp` 的 `GeometryKind` 与字符串转换 helper 中增加枚举值。
2. 在 `RobotModelXmlWriter.cpp` 的 Drawable / SceneGeometry / CollisionModel 输出块按新类型分发子标签。
3. 在 `RobotModelBuilderWidget` 的 Shape combo、列可编辑规则、输入校验中补齐新类型。
4. 在 `RobotModelXmlWriterTest.cpp` 增加 validate、XML 输出、WorkCellLoader 加载相关回归测试。

### 8.2 增加新的关节运动学表达(在 SE(3) 框架内)

由于 `transformJoints`(SE(3) Joint+RPY+Pos)已经是唯一真值,**新运动学表达(Modified DH / URDF / Product of Exponentials 等)只应作为"输入辅助 / 投影视图"加入**,不应再成为并行的真值。

推荐做法:

1. 在 `XmlWriter` 增加新的"SE(3) → 新表达"投影函数,如 `transformJointToModifiedDh(...)`,把 `transformJoints` 派生为新表达(类似现有 `transformJointToDh`)。
2. 若需要 UI 输入:在 `Widget` 中加一张新表,**只读**显示新表达;若用户希望"从新表达回填真值",提供显式 "Apply XXX" 按钮调用 `applyXxxInputToTransform(spec)`,并在按钮上明确标注"会覆盖 SE(3) 真值"。
3. 在 `XmlWriter::makeSerialDeviceXml` 中:
   * 默认仍输出 `<Joint>`(SE(3) 真值);
   * 仅在隐藏高级选项 `exportModifiedDhJointsAdvanced = true` 且 `canExportModifiedDhJoints()` 通过时输出 `<ModifiedDHJoint>`。
4. 在 `validate()` / `canExportModifiedDhJoints()` 中校验:是否所有行都满足该表达(例如 Modified DH 要求 pitch=0)。
5. 在 `RobotModelXmlWriterTest.cpp` 中补回归测试,覆盖:
   * 投影一致性;
   * 回填按钮会改写真值(并验证改写后真值正确);
   * 高级导出在非全 Revolute 或有损时拒绝;
   * fallback 到 `<Joint>` 仍正确。

### 8.3 把模型导入(反向解析)

当前只支持"导出"。若需要把已有 SerialDevice XML 反向解析成 `RobotModelSpec` 以便二次编辑:

1. 在 `RobotModelXmlWriter` 中增加 `static RobotModelSpec parseSerialDeviceXml(const QString& path)`,用 `QXmlStreamReader` 或 `rw::loaders::XML` 读取。
2. 在 `Widget::buildUi` 增加 "Load XML" 按钮,在 Widget 上加私有槽函数 `loadXml()` 调用上述方法,然后 `fillFromSpec(parsed)` + `generatePreview()`。
3. 在 `Plugin::loadSceneFile` 之外再加一个信号/槽,允许 Widget 主动通知 Plugin 把当前 WorkCell 缓存到 spec(用于 "Edit Current" 场景)。

### 8.4 添加自定义默认模型

`makeDefaultSixAxisModel` 提供了出厂默认。要增加"UR5"、"Panda"等预设:

1. 在匿名命名空间增加新函数 `appendUR5Defaults(RobotModelSpec&)`、`appendPandaDefaults(...)`。
2. 在 `makeDefaultSixAxisModel` 中并列分支,按 `spec.robotName` 或新增的 `enum class RobotPreset` 路由。
3. 在 Widget 顶部加 "Preset" 下拉框,触发 `resetDefaults(preset)` 时传入不同 preset。

---

## 9. 测试与调试

### 9.1 命令行测试

```bash
cd RobWork/build
cmake --build . --target sdurws_robotmodelbuilder_xmltest
./bin/sdurws_robotmodelbuilder_xmltest
```

`sdurws_robotmodelbuilder_xmltest` 已注册为 CTest 用例。常用验证命令:

```powershell
cmake --build . --target sdurws_robotmodelbuilder_xmltest --config Release
ctest -C Release -R sdurws_robotmodelbuilder_xmltest --output-on-failure
```

退出码 `0` = 全部通过,`1` = 有断言失败。测试覆盖:

| 类别 | 用例 |
| --- | --- |
| **基础结构** | 根节点 / Base / TCP / 6 Joint / 默认 Drawable / 限位 / Ready 位姿弧度 |
| **真值优先** | UI 模式 = DHProjection 时,默认仍输出 6 个 `<Joint>`(不是 `<DHJoint>`) |
| **DH 投影视图不改真值** | 编辑 `dhJoints` 后 `applyLinkGeometry()` 仍按 `transformJoints` 计算 |
| **高级 `<DHJoint>` 导出** | 全部 Revolute + 无损时输出 6 个 `<DHJoint>`;有 Prismatic / 有损时 `canExportDhJoints` 拒绝,`makeSerialDeviceXml` fallback `<Joint>` |
| **DWC** | DynamicWorkCell / RigidDevice / KinematicBase / 6 Link / 6 ForceLimit |
| **斜向几何** | Link1To2 长度 = √(0.3²+0.4²)、中心 = (0.15, 0, 0.20)、RPY 非硬编码 |
| **Y 分量** | 长度 / 中心 / RPY 旋转都正确跟随 Pos 向量 |
| **标准 DH** | Link4To5 长 0.38、中心 (0, 0, 0.19) |
| **DH 偏移** | a=0.4 d=0.2 offset=90° → 长度 √0.20、中心 (0, 0.2, 0.1) |
| **自动重算** | 用户改 length/rpy/pos → 调用 applyLinkGeometry 后被覆写 |
| **`applyDhInputToTransform`** | RPY/Pos 跟随 DH,custom type 保留 |
| **`refreshDhProjectionFromTransform`** | 数值与真值一致;有损时 pos 反推正确 |
| **往返** | DH → Transform → DH 无损(新约定数据);Transform → DH → Transform 无损(一致数据) |
| **校验拒绝** | 零质量 / 零力限 / 含空格名 / 重复关节名 / 零半径 / lossy DH 高级导出 |

### 9.2 落盘调试

测试退出前会把默认模型与"斜向模型"分别写到:

- `<系统临时目录>/robotmodelbuilder_dump/` — 默认 6 轴 + DWC
- `<系统临时目录>/robotmodelbuilder_skewed/` — 斜向 Link1To2

可以直接用 RobWorkStudio 打开 `<robotName>Scene.wc.xml` 验证三维显示。

### 9.3 常见坑

| 现象 | 原因 | 排查 |
| --- | --- | --- |
| 加载场景后看不到关节坐标轴 | `showFrameAxes=false`,或没勾选 "Show axes" | UI 顶部勾选,或 XML 中确认 `<Property name="ShowFrameAxis">true</Property>` |
| 圆柱与关节位置不对齐 | 修改了 DH 但没调 `applyLinkGeometry` | 务必在 `saveFiles` 前调用;Widget 已自动处理 |
| DWC 加载后物理行为异常 | `inertia` 顺序错(只填了 Ixx Iyy Izz 缺 Ixy/Ixz/Iyz) | 校验会拦截,确认每行填了 6 个数 |
| 万向锁方向反了 | `pitch ≈ ±90°` 时代码强制 `yaw=0` | 改 JointRPYPos 模式重新组织关节顺序,避免接近奇异 |
| RobotStudio 找不到插件 | 插件未编译或 `rws_plugin_load_details` 未注册 | 检查 `CMakeLists.txt` 中 `rws_add_plugin` + `rws_plugin_load_details` 都已调用 |
| "Save and Load" 后场景不刷新 | `setWorkcell` 之前的 XML 写盘失败 | 检查 status bar 与弹窗错误,确认 `saveDirectory` 存在且可写 |

---

## 10. 数据分离约定(交叉引用)

本插件严格区分**运动学 / 显示 / 动力学**三类数据:

| 数据 | 文件 | 谁来读 | 本插件的输出 |
| --- | --- | --- | --- |
| 运动学 | `*.wc.xml` 的 `<SerialDevice>` | 任意运动学/规划插件 | `makeSerialDeviceXml` |
| 显示 | `*.wc.xml` 的 `<Drawable>` 子树 | 渲染器 | `makeSerialDeviceXml`(含 Drawable) |
| 动力学 | `*.dwc.xml`(独立) | RobWorkSim | `makeDynamicWorkCellXml` |

完整约定见 [RobotModelBuilderDynamics.md](RobotModelBuilderDynamics.md)。任何下游插件只读它需要的类型,**不应跨界猜测**——例如动力学插件不应从 `<Drawable>` 反推质量。

---

## 11. 版本演进路线(建议)

> 仅作展望,不是当前已实现的功能。

1. **导入已有 SerialDevice**(7.3 节)
2. **支持 Box/Sphere/Cone 等更多 Drawable 形状**(7.1 节)
3. **预设库**(UR5、Panda、KR 6 R900 等)
4. **CSV/Excel 批量导入关节参数**
5. **基于 WorkCell 反向生成 Drawable**(自动从已有 `.wc.xml` 生成可视化的关节外壳)
6. **支持 Modified DH / URDF**(7.2 节)
7. **拖拽式编辑 Drawable**(用 RobWorkStudio 的 3D 视图直接拖动几何)

任何演进都应保持 **RobotModelSpec 零 Qt 依赖** 这一约束,这样命令行测试可以继续覆盖核心逻辑。

---

## 附录 A:头文件包含关系

```
RobotModelBuilderPlugin.hpp
    └── rws/RobWorkStudioPlugin.hpp
RobotModelBuilderPlugin.cpp
    ├── RobotModelBuilderPlugin.hpp
    ├── RobotModelBuilderWidget.hpp
    └── rws/RobWorkStudio.hpp

RobotModelBuilderWidget.hpp
    └── RobotModelSpec.hpp
RobotModelBuilderWidget.cpp
    ├── RobotModelBuilderWidget.hpp
    ├── RobotModelXmlWriter.hpp
    └── Qt 控件/工具头

RobotModelSpec.hpp
    └── STL only(array/string/vector)

RobotModelXmlWriter.hpp
    ├── RobotModelSpec.hpp
    └── Qt(QString/QStringList) + STL(array)
RobotModelXmlWriter.cpp
    ├── RobotModelXmlWriter.hpp
    └── Qt(QDir/QFile/QTextStream/QRegularExpression) + STL(cmath/set)

RobotModelXmlWriterTest.cpp
    ├── RobotModelXmlWriter.hpp
    └── Qt(QCoreApplication/QDir) + STL(iostream/cmath)
```

## 附录 B:Qt 版本兼容

`CMakeLists.txt` 中:

```cmake
if(DEFINED Qt6Core_VERSION)
    qt_add_resources(RccSrcFiles resources.qrc)
else()
    qt5_add_resources(RccSrcFiles resources.qrc)
endif()
```

两条分支处理 Qt5 → Qt6 的 `qt_add_resources` API 差异。`RW::sdurw_core` 等依赖项会自动选择对应版本的 Qt。

## 附录 C:插件加载机制

`rws_add_plugin(...)` + `rws_plugin_load_details(${SUBSYS_NAME} 2 RobotModelBuilder false)` 一起完成:

1. 生成 Qt 插件元数据 `plugin.json`(对应 `Q_PLUGIN_METADATA(IID ... FILE "plugin.json")`)。
2. 在 RWS 启动时把 `RobotModelBuilder` 注册到插件列表(显示在菜单/工具栏)。
3. 静态链接模式下 (`RWS_DEFAULT_LIB_TYPE == "STATIC"`),把插件追加到 `RWS_PLUGIN_LIBRARIES` 让主程序在链接期可见。

如果修改了插件名/版本,**必须同步修改 `plugin.json` 与 `rws_plugin_load_details` 调用**,否则 RWS 找不到这个插件。

---

## URDF Import

RobotModelBuilder 可以把 `.urdf` 文件作为输入格式,在已有的关节/几何/动力学/限位表里继续编辑。导入逻辑没有新建任何独立插件或独立模型——它把 URDF 解析进现成的 `RobotModelSpec`;导入完成后:

- `transformJoints` 仍是唯一真值;UI 的 Joint+RPY+Pos 真值表全部可编辑。
- `dhJoints` 仅作为投影视图缓存,导入后 `refreshDhProjectionFromTransform(spec)` 会重算一次。
- 所有保存继续走 `RobotModelXmlWriter`:`<SerialDevice>.wc.xml`、场景 `.wc.xml`、`CollisionSetup.xml`、`ProximitySetup.xml`(若启用)、`*.dwc.xml`。
- UI 上的 `Import URDF` 按钮弹出 `QFileDialog::getOpenFileName(*.urdf *.xml)`;选完文件后立即 `fillFromSpec` + `generatePreview`,任何 `warnings` 用 `QMessageBox::information` 单独弹给用户。
- URDF 导入不会自动覆盖磁盘文件,也不会自动切换当前 WorkCell;导入成功后用户需要显式点击 **Save XML** 或 **Save and Load**。

### 支持的 URDF 内容

- `robot/@name` → `spec.robotName`(同时作为 `sanitizeFileBaseName` 之后的文件名前缀)。
- `joint type`:`revolute` / `continuous` → `Revolute`;`prismatic` → `Prismatic`;`fixed` → `FixedFrame`;`planar` / `floating` / 其它 → 退化到 `FixedFrame`。
- `origin xyz/rpy`:URDF 是米 + 弧度;插件内部用米 + 度。RPY 语义从 URDF 的 X-Y-Z(rpyRad[i])重排到插件的 Z-Y-X(rpyDeg[i])。
- `<axis xyz="...">`:URDF 默认轴 `(0,0,1)` 直接导入；非默认轴会通过旋转 RobWork joint frame 对齐到本地 Z 轴来表示，不再生成 `*_axis_compensation` 辅助帧。导入器会把 child link 的 visual / collision / inertial 局部位姿转换到新的 joint frame 下，因此可动关节数量和 URDF 保持一致，自动相邻碰撞排除也只基于真实关节。
- `<limit lower/upper/velocity/effort>`:`revolute/continuous` 的 `lower/upper/velocity` 由弧度转度;`effort` 直接灌进 `JointForceLimitSpec.maxForce`;`prismatic` 保留米。
- `<visual>` / `<collision>`:`<box>/<cylinder>/<sphere>/<mesh>` 转 `DrawableSpec` / `CollisionModelSpec`;`<material><color rgba>` 取前三通道 `RGB`。
- `<inertial>`:`<mass value="..."/>` 与 `<inertia ixx ixy ixz iyy iyz izz/>` 转 `LinkDynamicsSpec.mass / cog / inertia`,`material="Imported"`;`objectName` 取产生该 link 的 joint 名(URDF 概念上的"动态对象"绑在子 link 上)。

### 限制与冲突处理

- 第一版只支持单条串联链 + fixed frame。如果 URDF 有分支,导入器选择一条 root-to-tip 链(每个 link 取字典序最小的子关节),对跳过的兄弟给 `"URDF branch at link ..."` 警告。
- 不支持 `planar` / `floating` 等 URDF 关节类型:这些固定为 `FixedFrame`,自由度被忽略。
- 链上的非默认 `<axis>` 会重定向 joint frame 使其 Z 轴对齐 URDF axis,同时折叠 child link 的 visual / collision / inertial 位姿到 joint frame 下。
- `package://<pkg>/<path>` 在用户提供的 `UrdfImportOptions.packageRoots` 列表里逐一尝试,首个实际存在的文件返回规范化路径;无法解析的 mesh 路径保留原文并写一条 warning。
- 没有 `<inertial>` 的 link 不会出现在 `LinkDynamicsSpec`;若 URDF 整体缺 dynamic 数据,导入器为每个 movable joint 兜底填一份默认(`mass=1.0`,`Ixx=Iyy=Izz=0.01`)。
- 导入完成后立即调用 `RobotModelXmlWriter::validate` 兜底。如果 URDF 合法但产生的 spec 不合法,导入报错并把 validate 错误一同写进 `errors`。

### 已知路径处理约定

- `package://` 解析默认把 URDF 所在目录作为第一个 `packageRoots` 候选项;UI 暴露给用户的 `packageRoots` 当前不进 UI(只走默认行为),后续 Milestone 任务再按需补成可编辑。
- mesh 路径写进 `DrawableSpec.filePath` / `CollisionModelSpec.filePath` 时,`RobotModelXmlWriter::relativeGeometryPath` 会再次规范化为相对 `saveDirectory` 的相对路径,所以 Scene XML 不会有绝对路径泄漏。

*如发现文档与代码不一致,以代码为准并请更新本文档。*
