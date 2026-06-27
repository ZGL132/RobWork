# RobWorkStudio 学习路线图（先掌握界面 → 再做二次开发）

## Context

你刚拿到 `d:\10_Source_Repos\21_robot\RobWork-master` 这套代码，已能启动 RobWorkStudio，准备分阶段先把"操作 GUI → 读懂代码 → 写第一个插件 → 用脚本快速验证"走一遍，再进行"机械臂设计 / 运动学 / 动力学 / 电机减速器选型 / 规划验证"的二次开发。

- 基础水平：**学过 C++ 与机器人基础理论**（不再赘述 DH/Jacobian/RRT 等概念定义）
- 构建状态：**已编译、能启动 RobWorkStudio**
- 目标：精通 RobWorkStudio 各官方插件、读懂 GUI 源码、能写自己的插件、能用 Lua 在 GUI 内快速做实验

> 学完后回到主线任务，再开始"电机减速器选型"等定制开发。

---

## 阶段 0 · 环境自检（10 分钟）

**目标**：确认 RobWorkStudio 启动正常、插件齐全、自带示例可加载。

**操作清单**：
1. 启动 `RobWorkStudio.exe`，窗口标题应类似 `RobWorkStudio v<ver>`。
2. `File → Open...`，加载 [RobWork/gtest/testfiles/workcells/simple_wc/SimpleWorkcell.wc.xml](RobWork/gtest/testfiles/workcells/simple_wc/SimpleWorkcell.wc.xml)，左侧 `TreeView` 应出现 `PA10`、`Cyl`、`Wall*` 等 frame。
3. 左侧应出现 `Jog / TreeView / PropertyView / WorkcellEditorPlugin`，底部应出现 `Log / PlayBack`，右侧应出现 `Sensors`；如果 `Planning` 不在默认停靠，从 `Plugins → Load plugin` 加载 `Planning.dll`。
4. `Tools → Print Colliding Frames` 验证碰撞检测跑通。

**完成标志**：能看到 PA10 机械臂 + 圆柱 + 墙的 3D 场景，Log 面板有日志输出。

**对应源码**（事后看一眼即可）：
- 启动顺序：[RobWorkStudio/src/rwslibs/rwstudioapp/RobWorkStudioApp.cpp](RobWorkStudio/src/rwslibs/rwstudioapp/RobWorkStudioApp.cpp)
- 主窗口：[RobWorkStudio/src/rws/RobWorkStudio.cpp](RobWorkStudio/src/rws/RobWorkStudio.cpp)

---

## 阶段 1 · GUI 总览与主窗口结构（半天）

**目标**：知道 RobWorkStudio 的菜单、工具栏、dock 区域、3D 视图的整体布局。

**必读文档**：
- [doc/sphinx/manual/robworkstudio.rst](doc/sphinx/manual/robworkstudio.rst) —— **整章读完**，介绍 9 个默认插件 + Extra / User Plugins
- [doc/sphinx/manual/introduction.rst](doc/sphinx/manual/introduction.rst)

**对照源码**（理解"菜单是怎么拼起来的"）：
- `RobWorkStudio::setupFileActions / setupToolActions / setupPluginsMenu / setupHelpMenu` 在 [RobWorkStudio.cpp](RobWorkStudio/src/rws/RobWorkStudio.cpp)
- 菜单顺序固定为 `File → Tools → Plugins → View3D → Help`

**练习**：在 `Tools → Preferences`（即 PropertyViewEditor）里改 `WindowWidth/Height/WindowX/WindowY`，关闭 RWS，再打开验证设置被保存到 `~/.RobWorkStudio.ini`。

**完成标志**：能默写 5 个菜单、4 条工具栏、central widget 与 5 个默认 dock 区域。

---

## 阶段 2 · 3D 视图交互（半天）

**目标**：掌握鼠标/键盘对 3D 视图的全部操作，包括 ArcBall 平移旋转、滚轮缩放、双击选点/选 frame、Standard Views、视图保存。

**必读文档**：
- [RobWorkStudio/src/rws/RWStudioView3D.hpp](RobWorkStudio/src/rws/RWStudioView3D.hpp) 的 `keyPressEvent` 注释段（`Ctrl+G/H/A/F/R/T/E/L/B`、`Ctrl+1..9`、`Ctrl+Left/Right`、`Ctrl++/-/*`）
- 鼠标交互细节直接读源码：[SceneOpenGLViewer.cpp:630-700](RobWorkStudio/src/rws/SceneOpenGLViewer.cpp) 的 `mousePressEvent / mouseDoubleClickEvent / wheelEvent`，[ArcBallController.cpp](RobWorkStudio/src/rws/ArcBallController.cpp) 的 `handleEvent`

**练习（用 `SimpleWorkcell.wc.xml`）**：
1. 左键拖 = 旋转；Ctrl+左拖 = 缩放；右键拖 = 平移；滚轮 = 以光标为中心缩放。
2. 双击 = 把 pivot point（小红球）移到该点；Shift+双击 = 触发 `positionSelectedEvent`（Log 面板会刷事件）；Ctrl+双击 = 拾取 frame（TreeView 会高亮）。
3. 切 `Standard views`：Front/Right/Top/Rear/Left/Bottom/Axiometric/Home。
4. `Ctrl+1..9` 切相机；`View3D → Save view...` 保存自定义视角。
5. `View3D → Render groups` 切换 Physical/Virtual/Drawable/Collision/User1..4 渲染掩码。

**完成标志**：能用键盘快捷键把视角精确切到任意标准视角，并知道双击三种修饰键分别做什么。

---

## 阶段 3 · WorkCell 与 TreeView（半天）

**目标**：理解 WorkCell 的 frame 树、设备、对象如何在 TreeView 里显示，并能用 PropertyView 改属性。

**必读文档**：
- [doc/sphinx/manual/workcells.rst](doc/sphinx/manual/workcells.rst) —— WorkCell/Frame/State/Stateless/DAF 等概念
- [doc/sphinx/file_formats/workcell.rst](doc/sphinx/file_formats/workcell.rst) —— `.wc.xml` 字段含义

**对照源码**：
- 树渲染：[RobWorkStudio/src/rwslibs/treeview/TreeView.cpp](RobWorkStudio/src/rwslibs/treeview/TreeView.cpp)
- 底层：[rw/kinematics/StateStructure.hpp](RobWork/src/rw/kinematics/StateStructure.hpp)

**练习**：
1. 加载 [RobWork/example/ModelData/XMLScenes/RobotOnTable/Scene.xml](RobWork/example/ModelData/XMLScenes/RobotOnTable/Scene.xml)，TreeView 里展开 `UR → base → shoulder → ... → wrist3 → TCP`，对照 [UR.wc.xml](RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR.wc.xml) 看关节对应关系。
2. 在 PropertyView 里改 `Drawables` 的颜色、透明度，3D 视图实时刷新。
3. 在 TreeView 上 **Ctrl+左键双击 frame**，验证事件回调。

**完成标志**：能在 TreeView 里定位任一 frame 并知道它对应 XML 中的哪个节点。

---

## 阶段 4 · WorkcellEditor 编辑 XML（1 天）

**目标**：能在 GUI 里直接增删改 frame、drawable，并保存为新的 `.wc.xml`。

**必读文档**：
- [doc/sphinx/manual/robworkstudio_plugins/workcell_editor.rst](doc/sphinx/manual/robworkstudio_plugins/workcell_editor.rst) —— 快捷键 `Ctrl+f / Ctrl+d / Ctrl+s / Ctrl+e`

**对照源码**：
- [RobWorkStudio/src/rwslibs/workcelleditorplugin/](RobWorkStudio/src/rwslibs/workcelleditorplugin/)（目录里是编辑器核心代码 + 语法高亮实现）

**练习（基于 `SimpleWorkcell.wc.xml`）**：
1. 用 WorkcellEditor 给场景加一个 Box，命名 `MyBox`，保存为 `MyScene.wc.xml`，重新打开验证。
2. 在 PropertyView 改 Box 的尺寸、颜色，再次保存。
3. 比较自己生成的 `MyScene.wc.xml` 与 [rwxml_workcell.xsd](RobWork/xml-schemas/rwxml_workcell.xsd) schema 是否合法。

**完成标志**：能"不写代码"地构造一个新工作单元并保存。

---

## 阶段 5 · Jog 示教（半天）

**目标**：能用 Jog 滑块把机械臂拖到任意姿态，并理解 Jog 如何触达每个关节 vs 笛卡尔末端。

**对照源码**：
- [RobWorkStudio/src/rwslibs/jog/Jog.cpp](RobWorkStudio/src/rwslibs/jog/Jog.cpp)
- [RobWorkStudio/src/rwslibs/jog/SliderTab.cpp](RobWorkStudio/src/rwslibs/jog/SliderTab.cpp)（含 reduction 计算，**注意这里的 reduction 是 UI 滑块缩放比，不是机械减速比**）

**练习**：
1. 加载 `RobotOnTable/Scene.xml`，在 Jog 的 Joint 模式下把 UR 拖到几个不同姿态。
2. 切到 Cartesian 模式，拖动末端位姿。
3. 打开 PropertyView 看 `state` 如何被 `setState` 实时改写。
4. 在 3D 视图里拾取一个 frame，看 Jog 是否能选中并拖动。

**完成标志**：能用 Jog 完成 5 次"从任意姿态拖回 home"。

---

## 阶段 6 · 路径规划（Planning 插件 + RRT/PRM）（1 天）

**目标**：能在 GUI 里手动设置 start/goal，跑规划，看路径、做优化、保存。

**必读文档**：
- [doc/sphinx/manual/motionplanning.rst](doc/sphinx/manual/motionplanning.rst) —— 末尾"RobWorkStudio Planning Plugin"段 + `ex-motionplanning.cpp/lua/python` 四语言示例

**对照源码**：
- 插件：[RobWorkStudio/src/rwslibs/planning/Planning.cpp](RobWorkStudio/src/rwslibs/planning/Planning.cpp)
- 后端：[RobWork/src/rw/pathplanning/](RobWork/src/rw/pathplanning/)（接口）+ [RobWork/src/rwlibs/pathplanners/](RobWork/src/rwlibs/pathplanners/)（RRT/PRM/SBL/Z3/ARW 实现）

**练习（基于 `SimpleWorkcell.wc.xml`）**：
1. 打开 `Planning / Jog / Log` 三个插件。
2. Jog 设 start，移到 goal，点 `Plan` → 默认用 RRT，观察 Log 中的采样次数/成功率。
3. 切到 PRM，重新规划，比较时间。
4. 跑 `Optimize`（净空/路径长度优化），对比前后路径。
5. `Save path...` 存为 `.path` 文件，关闭 RWS 再打开能 `Load path` 复现。
6. 打开 [RobWork/example/lua/ex-motionplanning.lua](RobWork/example/lua/ex-motionplanning.lua) 看一下，对照 `motionplanning.rst` 末尾的说明逐行读懂。

**完成标志**：能从 GUI 跑出至少一条无碰撞路径并保存，再重新打开路径能复现。

---

## 阶段 7 · 轨迹与回放（PlayBack）（半天）

**目标**：理解 path → Trajectory → TimedStatePath 的转换，能在 GUI 里播放、保存回放、配合 Jog 录动作。

**必读文档**：
- [doc/sphinx/manual/trajectories.rst](doc/sphinx/manual/trajectories.rst)

**对照源码**：
- 插件：[RobWorkStudio/src/rwslibs/playback/PlayBack.cpp](RobWorkStudio/src/rwslibs/playback/PlayBack.cpp)、[Player.cpp](RobWorkStudio/src/rwslibs/playback/Player.cpp)、[StateDraw.cpp](RobWorkStudio/src/rwslibs/playback/StateDraw.cpp)
- 时间参数化：[RobWork/src/rw/trajectory/TrajectoryFactory.hpp](RobWork/src/rw/trajectory/TrajectoryFactory.hpp)

**练习**：
1. 在阶段 6 跑出的路径上，`PlayBack → Convert path → makeLinearTrajectory` / `makeParabolicBlend`，比较速度曲线。
2. `Save TimedStatePath` 存 `.rwplay`，再 `Load` 播放。
3. 参照 [RobWorkStudio/example/cpp/ex-play-timedstatepath.cpp](RobWorkStudio/example/cpp/ex-play-timedstatepath.cpp) 在命令行版本里跑一遍同样的播放。
4. 在 Jog 里录一段手写轨迹（拖动 → Record → 回放）。

**完成标志**：能解释 `Path / Interpolator / Trajectory / TimedStatePath` 四者关系，并在 GUI 里把任意路径平滑播放。

---

## 阶段 8 · 传感器可视化（Sensors）（半天）

**目标**：能用 Sensors 插件看 Camera/Scanner2D/Scanner25D/FTSensor 的实时数据。

**对照源码**：
- [RobWorkStudio/src/rwslibs/sensors/Sensors.cpp](RobWorkStudio/src/rwslibs/sensors/Sensors.cpp)、[SensorView.cpp](RobWorkStudio/src/rwslibs/sensors/SensorView.cpp)
- 传感器模型：[RobWork/src/rw/sensor/](RobWork/src/rw/sensor/)（Camera / Scanner1D/2D/25D / FTSensor / TactileArray）

**练习（基于 [RobWorkStudio/gtest/testfiles/SensorTest.wc.xml](RobWorkStudio/gtest/testfiles/SensorTest.wc.xml)）**：
1. 打开 Sensors 插件，选中 Camera，看图像。
2. 切 Scanner2D 看点云，切 Scanner25D 看深度图。
3. 在 Jog 里拖动机械臂，相机视角/点云随之变化。
4. 打开 [RobWork/example/ModelData/XMLScenes/SensorTestScene/SimpleWorkcell.xml](RobWork/example/ModelData/XMLScenes/SensorTestScene/SimpleWorkcell.xml) 对比 4 种传感器同台。
5. 看 [RobWorkStudio/example/cpp/ex-simulated-camera.cpp](RobWorkStudio/example/cpp/ex-simulated-camera.cpp)，知道怎么用 C++ 主动抓帧。

**完成标志**：能解释 CameraModel 与 `rw::sensor::Camera::getImage` 的关系。

---

## 阶段 9 · PropertyView / Log / Lua 插件（半天）

**目标**：会用 PropertyView 改 WorkCell 属性；用 Log 看所有 rw::core::Log 输出；用 Lua 在 GUI 里跑脚本。

**对照源码**：
- PropertyView：[RobWorkStudio/src/rwslibs/propertyview/](RobWorkStudio/src/rwslibs/propertyview/)
- Log：[RobWorkStudio/src/rwslibs/log/](RobWorkStudio/src/rwslibs/log/)
- Lua：[RobWorkStudio/src/rwslibs/lua/Lua.cpp](RobWorkStudio/src/rwslibs/lua/Lua.cpp)

**练习**：
1. 改一个 frame 的 `Pos / Rot` 在 PropertyView，3D 实时更新。
2. 在 Log 面板切换 `Debug/Info/Warning/Error` 过滤。
3. 在 Lua 插件里跑 [RobWorkStudio/example/lua/state_path_1.lua](RobWorkStudio/example/lua/state_path_1.lua)，看机械臂跑一遍。
4. 改写脚本：在 home 和一个目标点之间手动画条 trajectory，跑 PlayBack。

**完成标志**：能用 Lua 脚本从 GUI 里完整跑一遍"加载 WC → 设 Q → 画轨迹 → PlayBack"。

---

## 阶段 10 · C++ API 入门（1–2 天）

> **这一步是后续二次开发的根基**——GUI 用熟了，做电机减速器选型/自定义规划验证时必须直接写 C++。

**必读文档**：
- [doc/sphinx/tutorials/basic.rst](doc/sphinx/tutorials/basic.rst) —— "My first cmake project"、"Math joggling"、Exercise 1/2
- [doc/sphinx/manual/workcells.rst](doc/sphinx/manual/workcells.rst) —— Loading a WorkCell
- [doc/sphinx/manual/kinematics.rst](doc/sphinx/manual/kinematics.rst)
- [doc/sphinx/manual/inverse_kinematics.rst](doc/sphinx/manual/inverse_kinematics.rst)
- [doc/sphinx/manual/collision_detection.rst](doc/sphinx/manual/collision_detection.rst)
- [doc/sphinx/manual/motionplanning.rst](doc/sphinx/manual/motionplanning.rst)
- [doc/sphinx/interfaces/python.rst](doc/sphinx/interfaces/python.rst)（若要用 Python 绑定）

**必跑示例（按"主线"顺序，每个都跑 + 改一改）**：

| 步骤 | C++ | Python | Lua |
|---|---|---|---|
| 加载 WorkCell | [ex-load-workcell.cpp](RobWork/example/cpp/ex-load-workcell.cpp) | [ex-load-workcell.py](RobWork/example/python/ex-load-workcell.py) | [ex-load-workcell.lua](RobWork/example/lua/ex-load-workcell.lua) |
| 列出设备 | [ex-print-devices.cpp](RobWork/example/cpp/ex-print-devices.cpp) | 同 | 同 |
| 打印运动学树 | [ex-print-kinematic-tree.cpp](RobWork/example/cpp/ex-print-kinematic-tree.cpp) | 同 | 同 |
| FK | [ex_fwd-kinematics.cpp](RobWork/example/cpp/ex_fwd-kinematics.cpp) | 同 | 同 |
| FK（设备级）| [ex_fwd-kinematics-device.cpp](RobWork/example/cpp/ex_fwd-kinematics-device.cpp) | 同 | 同 |
| 帧间变换 | [ex_frame-to-frame-transform.cpp](RobWork/example/cpp/ex_frame-to-frame-transform.cpp) | 同 | 同 |
| IK（UR10e 闭式）| [ex-invkin.cpp](RobWork/example/cpp/ex-invkin.cpp) | 同 | 同 |
| 碰撞 | [ex-collisions.cpp](RobWork/example/cpp/ex-collisions.cpp) | 同 | 同 |
| 路径规划 | [ex-motionplanning.cpp](RobWork/example/cpp/ex-motionplanning.cpp) | 同 | [ex-motionplanning.lua](RobWork/example/lua/ex-motionplanning.lua) |

**CMake 工程模板**：
- [RobWork/example/bfgsApp/](RobWork/example/bfgsApp/)（README 写明"复制改名"流程）
- 或 [RobWork/example/consoleapp/SampleTest.cpp](RobWork/example/consoleapp/SampleTest.cpp)

**练习**：
1. 用 `bfgsApp` 模板建 `MyApp/`，先 `ex-load-workcell.cpp`，跑通编译链。
2. 接着加入 `ex_fwd-kinematics-device`，输出 UR 的 TCP 位姿。
3. 加入 `ex-invkin`，对一个目标 `Transform3D` 求关节角。
4. 加入 `ex-collisions`，验证两 frame 不碰撞。
5. 最后 `ex-motionplanning`，跑出 RRT 路径。

**完成标志**：能脱离 GUI 写完整 C++ 程序：加载工作单元 → FK/IK → 碰撞检查 → 路径规划 → 保存 `.path`。

**对照 API 入口**：
- [rw/models/Device.hpp](RobWork/src/rw/models/Device.hpp) —— `setQ / baseTend / baseJend / getBounds / getDOF`
- [rw/kinematics/Kinematics.hpp](RobWork/src/rw/kinematics/Kinematics.hpp) —— `worldTframe / frameTframe`
- [rw/invkin/JacobianIKSolver.hpp](RobWork/src/rw/invkin/JacobianIKSolver.hpp)、[ClosedFormIKSolverUR.hpp](RobWork/src/rw/invkin/ClosedFormIKSolverUR.hpp)
- [rw/proximity/CollisionDetector.hpp](RobWork/src/rw/proximity/CollisionDetector.hpp)
- [rw/pathplanning/QToQPlanner.hpp](RobWork/src/rw/pathplanning/QToQPlanner.hpp)

---

## 阶段 11 · 写第一个 RobWorkStudio 插件（1–2 天）

> **这是 GUI → 二次开发的桥梁**。插件能继承你阶段 10 写的 C++ 逻辑，直接做成 GUI 工具。

**必读文档**：
- [doc/sphinx/tutorials/plugins_sdurws.rst](doc/sphinx/tutorials/plugins_sdurws.rst) —— 插件开发
- [doc/sphinx/manual/robworkstudio.rst](doc/sphinx/manual/robworkstudio.rst) —— User Plugins 段

**对照源码（按难度递进）**：
1. 最简：[RobWorkStudio/example/tutorial/SamplePlugin.cpp](RobWorkStudio/example/tutorial/SamplePlugin.cpp) —— 50 行，2 个 QPushButton + initialize/open/close
2. 带 .ui：[RobWorkStudio/example/pluginUIapp/SamplePlugin.cpp](RobWorkStudio/example/pluginUIapp/SamplePlugin.cpp) + [SamplePlugin.ui](RobWorkStudio/example/pluginUIapp/SamplePlugin.ui) + [SamplePlugin.json](RobWorkStudio/example/pluginUIapp/SamplePlugin.json)
3. 网络通信：[RobWorkStudio/example/UDPKinPlugin/SamplePlugin.cpp](RobWorkStudio/example/UDPKinPlugin/SamplePlugin.cpp) —— Boost.Asio UDP 驱动 15 body

**关键文件**：
- 插件基类：[RobWorkStudio/src/rws/RobWorkStudioPlugin.hpp](RobWorkStudio/src/rws/RobWorkStudioPlugin.hpp)
- 加载机制：[RobWorkStudio.cpp:422 / :661](RobWorkStudio/src/rws/RobWorkStudio.cpp) 的 `loadPlugin / setupPlugin / addPlugin`
- 编译模板：[RobWorkStudio/example/pluginapp/README.txt](RobWorkStudio/example/pluginapp/README.txt) 含 Linux/MinGW/MSVC 三种命令

**练习（3 步）**：
1. **复刻最简插件**：把 `example/tutorial/` 拷出来改名 `MyFirstPlugin/`，编译成动态库放进 `plugins/` 目录，启动 RWS 自动加载；把 `Hello` 按钮的回调换成"打印当前 WorkCell 名 + 选中 frame 名"。
2. **加 .ui 与图标**：照 `pluginUIapp` 模板加一个 `.ui` 和 `plugin.json`，再加 Q_INTERFACES + Q_PLUGIN_METADATA；编译后从 `Plugins → Load plugin...` 手动加载一次。
3. **做"关节读数 + 截图"小工具**：在 UI 里加 6 个 QDoubleSpinBox 实时显示 UR 各关节角，并加一个"Save screenshot"按钮触发 `Ctrl+G`。

**完成标志**：能独立写一个自定义 dock 插件，含按钮/输入框，能在 RWS 里实时显示与机械臂状态。

---

## 阶段 12 · 复盘与对接二次开发（半天）

**目标**：总结以上 11 阶段，建立对你后续 5 大方向的"能力地图"。

**自检清单**：

| 二次开发方向 | 哪一阶段给的能力 | 还需新增的扩展点（预热） |
|---|---|---|
| 机械臂设计（DH 调参） | 阶段 3/4（WorkCell XML）、阶段 10（FK 验证） | 仿照 [RobWorkStudio/src/rwslibs/workcelleditorplugin/](RobWorkStudio/src/rwslibs/workcelleditorplugin/) 加自定义属性面板 |
| 运动学（FK/IK 二次开发） | 阶段 10（API 入门）、阶段 11（插件 GUI） | 新模块挂到 [RobWork/src/rw/CMakeLists.txt](RobWork/src/rw/CMakeLists.txt) 的 `sdurw` 接口 |
| 动力学（Newton–Euler、RecurDyn 对接） | 阶段 10（API）、阶段 7（轨迹） | 在阶段 11 插件里调 [RobWorkSim/src/rwsim/util/RecursiveNewtonEuler.hpp](RobWorkSim/src/rwsim/util/RecursiveNewtonEuler.hpp) |
| 电机减速器选型 | 阶段 10（计算）、阶段 11（GUI 展示）、阶段 7（轨迹生成） | **本仓库没有 Motor/Transmission 模型**，需要新建 `rwlibs/actuator/` 并扩展 `.dwc.xml` schema |
| 规划验证 | 阶段 6（GUI 规划）、阶段 7（轨迹）、阶段 8（传感器） | 自定义 StopCriteria + Log，与 PlayBack 协同 |

**完成后建议立刻**：
- 把 `MyFirstPlugin` 改造为"机械臂设计工具"：在 UI 里加 6 个 DH 参数输入框，实时调 → 重新计算 FK → 在 3D 视图看变化。
- 把 C++ 版的 `MyApp` 改造为"关节负载分析"：跑 `ex-motionplanning` 后，调 [RecursiveNewtonEuler.hpp](RobWorkSim/src/rwsim/util/RecursiveNewtonEuler.hpp) 输出每个关节 τ(t)。
- 等你完成这步后，回来找我，我会基于你已经验证的 GUI + 插件 + C++ 三层能力，给你写**电机减速器选型模块**和**规划验证插件**的具体改造方案。

---

## 学习节奏与里程碑

| 周 | 阶段 | 里程碑验收 |
|---|---|---|
| Day 1 上午 | 0–1 | 能跑通示例 + 默写主窗口结构 |
| Day 1 下午 | 2–3 | 能用键盘快捷键切视角 + 定位任一 frame |
| Day 2 | 4–5 | 能"不写代码"建一个新 WC + 用 Jog 拖到任意姿态 |
| Day 3 | 6 | 能在 GUI 跑出至少一条 RRT 路径并保存 |
| Day 4 | 7–8 | 能播放 trajectory + 看传感器数据 |
| Day 5 | 9 | 能用 Lua 在 GUI 跑完整流程 |
| Day 6–7 | 10 | 能脱离 GUI 写完整 C++ 程序（加载 → FK → IK → 规划 → 保存） |
| Day 8–9 | 11 | 能独立写带 .ui 的自定义插件，在 RWS 中加载 |
| Day 10 | 12 | 复盘，进入二次开发 |

---

## 关键参考路径速查

**GUI 源码**：
- 主窗口：[RobWorkStudio/src/rws/RobWorkStudio.cpp](RobWorkStudio/src/rws/RobWorkStudio.cpp)
- 3D 视图：[RobWorkStudio/src/rws/SceneOpenGLViewer.cpp](RobWorkStudio/src/rws/SceneOpenGLViewer.cpp)、[RWStudioView3D.cpp](RobWorkStudio/src/rws/RWStudioView3D.cpp)、[ArcBallController.cpp](RobWorkStudio/src/rws/ArcBallController.cpp)
- 插件基类：[RobWorkStudio/src/rws/RobWorkStudioPlugin.hpp](RobWorkStudio/src/rws/RobWorkStudioPlugin.hpp)
- 官方插件目录：[RobWorkStudio/src/rwslibs/](RobWorkStudio/src/rwslibs/)

**示例与数据**：
- 入门工作单元：[RobWork/gtest/testfiles/workcells/simple_wc/SimpleWorkcell.wc.xml](RobWork/gtest/testfiles/workcells/simple_wc/SimpleWorkcell.wc.xml)
- 可玩场景：[RobWork/example/ModelData/XMLScenes/RobotOnTable/Scene.xml](RobWork/example/ModelData/XMLScenes/RobotOnTable/Scene.xml)
- 设备目录：[RobWork/example/ModelData/XMLDevices/](RobWork/example/ModelData/XMLDevices/)
- 示例插件模板：[RobWorkStudio/example/tutorial/](RobWorkStudio/example/tutorial/)、[pluginUIapp/](RobWorkStudio/example/pluginUIapp/)、[UDPKinPlugin/](RobWorkStudio/example/UDPKinPlugin/)

**C++ API 入口**：
- 设备：[RobWork/src/rw/models/Device.hpp](RobWork/src/rw/models/Device.hpp)
- 运动学：[RobWork/src/rw/kinematics/Kinematics.hpp](RobWork/src/rw/kinematics/Kinematics.hpp)
- 逆解：[RobWork/src/rw/invkin/](RobWork/src/rw/invkin/)
- 碰撞：[RobWork/src/rw/proximity/CollisionDetector.hpp](RobWork/src/rw/proximity/CollisionDetector.hpp)
- 规划：[RobWork/src/rw/pathplanning/QToQPlanner.hpp](RobWork/src/rw/pathplanning/QToQPlanner.hpp)
- 动力学（仿真）：[RobWorkSim/src/rwsim/util/RecursiveNewtonEuler.hpp](RobWorkSim/src/rwsim/util/RecursiveNewtonEuler.hpp)

**文档**：
- GUI 总览：[doc/sphinx/manual/robworkstudio.rst](doc/sphinx/manual/robworkstudio.rst)
- 编程入门：[doc/sphinx/tutorials/basic.rst](doc/sphinx/tutorials/basic.rst)
- 插件开发：[doc/sphinx/tutorials/plugins_sdurws.rst](doc/sphinx/tutorials/plugins_sdurws.rst)
- 手册目录：[doc/sphinx/manual/](doc/sphinx/manual/)

---

## 验证方式（如何确认你掌握了）

- **阶段 0–4**：打开 RWS 加载任意 `.wc.xml`，能用键盘切视角、TreeView 定位 frame、WorkcellEditor 编辑并保存。
- **阶段 5–7**：能用 Jog 拖出 start/goal，Planning 跑通 RRT/PRM 并保存路径，PlayBack 播放。
- **阶段 8–9**：能打开 SensorTest 看相机图像，Lua 脚本能完整跑一遍"加载 → 设 Q → 播放"。
- **阶段 10**：脱离 GUI 用 C++ 写完"加载 → FK → IK → 碰撞 → 规划 → 保存"完整流程，控制台打印结果正确。
- **阶段 11**：能独立编译并加载一个自定义插件，含按钮/输入框，GUI 内能实时显示关节角。
- **阶段 12**：能基于所学做"DH 实时调参 UI"和"关节负载计算命令行工具"。

---

## 二次开发衔接（学完回到主线）

完成阶段 12 后，回主线任务，我会按以下顺序与你一起做：

1. **新增 `rwlibs/actuator/` 子模块** —— 扩展 `.dwc.xml` schema 加 `<Motor> <Reduction> <Efficiency>`，在 XML 解析、Device 属性、Newton–Euler 三层落地。
2. **写"电机选型验证"插件**（基于阶段 11 的模板）—— 加载 `.wc.xml` → 读电机参数 → 跑规划 → 调 RecursiveNewtonEuler → 画 τ-ω 曲线对比电机包络。
3. **写"规划验证"插件** —— 集成你自定义的 StopCriteria + 日志，与 PlayBack 协同可视化。

届时会给你一份独立的"改造方案"文件，列出每个文件改什么、新建什么。