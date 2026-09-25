# 工业机械臂设计软件 · 冻结版本基线（ird/share/baseline）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（首版——阶段 A 收口登记；阶段 E 复核待办） |
| 日期 | 2026-09-26 |
| 任务 | WP-24-T01（DTB §2.25；owner 指示 2026-09-26 随 WP-24-T03 首版装配提前启动） |
| 需求追溯 | NFR-DEP-05（冻结版本基线）、P-POL-5（碰撞后端版本回填消账）、UX-14（关于对话框组件版本同源面） |
| 路径说明 | 本文件即 NFR-DEP-05 所指 `ird/share/baseline.md`——组件相对落位＝`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/share/`（ird 组件根下的 share/ 目录首件） |
| 变更纪律 | **基线变更走设计变更评审**（DTB §2.25 WP-24-T01 禁止项）；本文为复现要素与兼容判定的取值权威（policy §8.1 CollisionBackendDescriptor 同源） |

---

## 1. 框架锁定 commit/tag（RobWork / RobWorkStudio / RobWorkSim）

| 子树 | 锁定 commit | 日期 | 说明 |
| --- | --- | --- | --- |
| RobWork（rw 核心库，`RobWork/RobWork/`） | `31b818452913f3a794c5ca802411e83b95510d70` | 2026-07-01 | 初始可运行版本（导入基线） |
| RobWorkSim（`RobWork/RobWorkSim/`） | `31b818452913f3a794c5ca802411e83b95510d70` | 2026-07-01 | 同上（随核心同批导入） |
| RobWorkStudio（`RobWork/RobWorkStudio/`，不含 industrialrobot 产品区） | `31b818452913f3a794c5ca802411e83b95510d70` | 2026-07-01 | 框架源码零变更；伴随登记行变更 `db31b6ab`（2026-09-09）仅触碰 `RobWork/RobWorkStudio/src/rwslibs/CMakeLists.txt`（+7 行——industrialrobot 子目录注册），非框架源码语义变更 |

- **SA-02 零源码修改声明**：截至本版登记，`patches/PATCHES.md` 补丁表为空（实测零 patch）——框架工作区无任何未登记改动。
- **tag**：未打（基线以完整 commit 锁定）；是否补 tag 归阶段 E 复核决策。
- 框架构建版本宏（`RobWorkConfig.hpp` 的 `RW_VERSION`）为**配置期动态产物**（日期＋当前分支名，git-ignored），不作为冻结取值——碰撞后端版本冻结值见 §3。

## 2. Qt 与编译器版本

| 项 | 值 |
| --- | --- |
| Qt | 6.11.1（开源发行版，msvc2022_64 预编译；本地安装 `D:\software\Qt\6.11.1\msvc2022_64`，经 `CMAKE_PREFIX_PATH` 接入） |
| 编译器 | MSVC v143 工具集（Visual Studio 2022，productDisplayVersion 17.12.0） |
| 生成器/平台 | Visual Studio 17 2022，x64（AMD64） |
| C++ 标准 | C++17（逐目标显式 `CXX_STANDARD 17`——core.md D-01，不用 C++20） |

## 3. 碰撞检测后端及版本（P-POL-5 冻结取值）

| 项 | 冻结值 |
| --- | --- |
| backendId | `rw.proximity.builtin-rw` |
| **backendVersion（本文登记的权威冻结值）** | **`rw-31b8184`**（框架核心锁定 commit 短哈希前缀＋`rw-` 前缀；policy 侧单点常量 `kFrozenBuiltinBackendVersion` 与此同值——CollisionEvaluator.hpp） |
| toleranceModel | `builtin-rw/bvtree;binary-collision+distance;numeric-resolution=backend-internal(not-an-engineering-threshold;P-POL-5 frozen@WP-24-T01:ird/share/baseline.md)` |
| 唯一后端纪律 | 内置 `rw::proximity::rwstrategy::ProximityStrategyRW`（OBV 树＋`BVTreeToleranceCollider`）为默认且唯一注册后端（ARC-05/P-POL-5）；框架附带的 bullet3 等可选接近策略插件**不注册、不使用** |

取值消费点（构建期冻结，字面量单点）：`CollisionEvaluator.cpp detail::makeBuiltinBackendDescriptor`——版本串经公共常量注入，两处装配调用（`Compatibility.cpp`／`RobWorkCollisionEvaluator.cpp`）同源同值（§9.1"复现要素同源"的机械保证）。本值进入策略会话身份与复现块（evidence ReproductionBlock）；**变更走设计变更评审**（全体切片身份受影响）。

## 4. 构建选项

| 项 | 值 |
| --- | --- |
| 集成模式（唯一交付口径） | 仓库根 `build/` 构建树；`RWS_BUILD_INDUSTRIALROBOT=ON`；配置集 `Debug;Release;MinSizeRel;RelWithDebInfo`（交付/验证口径 **Release**） |
| 独立冒烟模式 | `industrialrobot/` 单独配置＋`-DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake`＋`-DCMAKE_PREFIX_PATH=<Qt>`（仅验证目标注册与 include 路径） |
| 第三方依赖通道 | vcpkg 经典模式（仓库根 `vcpkg/`，无 manifest）；禁源码 vendor 与第二渠道（DTB §5.1；例外登记见 §6 表"通道"列） |
| 单元门禁 | `ird_gates`（cmake -P 脚本模式）随构建自查；框架基线词表见 §5 机器口径 |

## 5. 已使用 API 清单（框架面）

**库级（链接语句实测快照——机器校验口径＝ird_gates 框架基线词表 `IRD_FRAMEWORK_LIB_PREFIXES`）**：

| 产品单元 | 链接的框架库 | 关键 API 面（类级清单权威归各单元卡 §1.2，此处登记代表面） |
| --- | --- | --- |
| core | `sdurw_math` | `rw::math`（Q/Transform3D/Vector3D/EAA/RPY 等数学值类型；boost::serialization 头随 Q.hpp 传染——header-only 消费） |
| policy | `sdurw_math` `sdurw_models` `sdurw_kinematics` `sdurw_proximity` | 碰撞面：`CollisionDetector/CollisionStrategy/CollisionToleranceStrategy/DistanceCalculator/ProximitySetup/ProximitySetupRule/ProximityFilter`＋内置策略 `rwstrategy::ProximityStrategyRW`（policy.md §1.2 实测清单）；运动学模型链（Device/Frame/QState） |
| runtime | `sdurw_math` `sdurw_models` `sdurw_kinematics` `sdurwsim` | 确定性编译产出 WorkCell/Device 装配（sim 面含 DynamicWorkCell 消费） |
| ui（宿主插件） | `sdurws` | RobWorkStudio 插件门面（RobWorkStudioPlugin 基类／视图注册——集成树专属目标） |
| modeling（头文件面） | 无直接链接（math 类型经公共头传染＋boost 序列化头 header-only；`sdurw_kinematics` 仅 `TARGET` 判别式 gating） | URDF/Xacro 解析自实现（pugixml，P-MDL-5）——**不使用** `sdurw_loaders` 的 XML 通道（io 同口径） |

- **`sdurw_loaders`**：原则不使用（io/modeling 导入通道自行实现安全解析）；门禁词表 `IRD_FRAMEWORK_REGISTER_REQUIRED` 持续登记该名——任何启用须先登记 DTB。
- WIP 单元（requirements/kinematics/trajectory/dynamics/drivetrain/selection/optimization 中未收口者）的框架面**不在本首版冻结范围**——阶段 E 复核时随收口状态刷新本表。

## 6. 第三方许可证清单

| 组件 | 版本 | 许可证 | 引入通道 | 用途 |
| --- | --- | --- | --- | --- |
| Qt | 6.11.1 | LGPL-3.0（开源发行版） | 本地安装（非 vcpkg） | ui/workflow 界面与插件目标（R-3 唯一例外面） |
| boost | 1.92.0 | BSL-1.0 | vcpkg | 框架传递（rw::math 序列化头，header-only 消费） |
| eigen3 | 5.0.1 | MPL-2.0 | vcpkg | kinematics Jacobian SVD/线性代数（P-KIN-6，O-40 登记） |
| pugixml | 1.16 | MIT | vcpkg | modeling URDF/Xacro DOM 解析（P-MDL-5，O-40 登记） |
| expat | 2.8.3 | MIT | vcpkg | io XML 解析（io PRIVATE） |
| libzip | 1.11.4 | BSD-3-Clause | vcpkg | io 规范包容器（io PRIVATE） |
| gtest | 1.18.0 | BSD-3-Clause | vcpkg | 单元/契约测试（`find_package(GTest CONFIG)`） |
| MSVC 运行时 | v143（14.42） | Microsoft 软件许可条款 | VS 2022 安装 | CRT/工具链运行时 |
| pqp／yaobi（框架内置源码） | 随框架 | 随框架 LICENSE（`RobWork/LICENSE`） | 框架自带 | 框架接近策略候选实现（产品当前注册面仅 builtin-rw，见 §3） |

- 许可证全文位置：vcpkg 组件见 `vcpkg/installed/x64-windows/share/<port>/copyright`；Qt 见本地安装目录许可文件；框架及其内置第三方见 `RobWork/LICENSE`。
- vcpkg 在装但产品未链接的组件（bullet3/fcl/octomap/qhull/ccd/freeglut 等框架可选面）：不进入产品分发口径；如未来启用先登记 DTB 再扩本表。

## 7. 登记与复核记录

| 版本 | 日期 | 记录 |
| --- | --- | --- |
| v1.0 | 2026-09-26 | 首版登记（WP-24-T01，阶段 A 收口补登记；owner 指示随 WP-24-T03 首版装配提前启动）。P-POL-5 消账：`CollisionBackendDescriptor` 取值按 §3 冻结回填（policy 单元卡增量修订同批）。阶段 E 复核＝待办（M7 判据：基线与构建树一致性复核＋WIP 单元框架面刷新＋tag 决策） |
