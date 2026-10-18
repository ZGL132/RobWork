# Isaac Sim 运动规划框架源码解剖 —— 面向 trajectory 单元的接口/架构借鉴（专题 1）

> 性质：外部参考研究笔记（非设计文档链组成部分，不影响需求语义）。
> 来源：Isaac Sim 6.2.0 随包源码 `exts/isaacsim.robot_motion.experimental.motion_generation`（Apache-2.0，SPDX 头逐文件标注）。
> 分析日期：2026-10-15。代码量：核心实现 4,994 行 + 测试 9,514 行。
> 目标：为 `trajectory` 单元（及相关 policy 端口）的接口设计提供源码级参考。

---

## 1. 扩展总体结构

```text
impl/
├── trajectory.py          ( 61 行) Trajectory 抽象接口
├── base_controller.py     ( 79 行) BaseController 抽象接口
├── path.py                (605 行) Path → 最小时间轨迹（核心算法）
├── controller_structures.py(363 行) 组合子：Selectable / Combined / Chained
├── trajectory_follower.py (140 行) 轨迹跟随控制器
├── types.py              (1666 行) 状态表示：JointState / SpatialState / RootState / RobotState
├── world_interface.py     (544 行) 规划障碍接口（8 种几何 + 位姿/速度同步）
├── world_binding.py       (598 行) USD 场景 → 规划世界绑定
├── obstacle_strategy.py   (378 行) 障碍表示策略（类型重映射 + 安全容差）
├── scene_query.py         (317 行) 场景空间查询（AABB 内按 API 搜索 prim）
├── trackable_api.py        (39 行) 可跟踪障碍标记 API
└── utils/collision_approximation/   包围盒/网格三角化等碰撞近似
```

分层非常干净：**接口层（2 个抽象类，合计 140 行）→ 算法层（path/trajectory）→ 组合层（controllers）→ 数据层（types）→ 环境层（world/obstacles）**。没有一处 UI 代码、没有一处 Qt/Viewport 依赖——纯计算内核，GUI 通过同一 Python API 驱动。这与本项目"L2 计算内核零 Qt"红线完全同构。

---

## 2. 两个核心接口（值得原样借鉴的极简设计）

### 2.1 Trajectory（trajectory.py）

```python
class Trajectory(ABC):
    @property
    @abstractmethod
    def duration(self) -> float: ...
    @abstractmethod
    def get_target_state(self, time: float) -> RobotState | None: ...
```

**要点：**
- 整个轨迹抽象只有 **两个成员**：时长 + 按时间采样。
- 采样失败/超界统一返回 `None`（接口契约写明"离散实现需自行插值"）。
- 约定所有轨迹从 t=0 开始，到 `duration` 结束——**时间语义在接口层一次性定死**，所有实现共享。

### 2.2 BaseController（base_controller.py）

```python
class BaseController(ABC):
    @abstractmethod
    def forward(self, estimated_state: RobotState, setpoint_state: RobotState | None,
                t: float, **kwargs) -> RobotState | None: ...
    @abstractmethod
    def reset(self, estimated_state: RobotState, setpoint_state: RobotState | None,
              t: float, **kwargs) -> bool: ...
```

**要点：**
- 控制器 = `forward`（状态→状态）+ `reset`（回到安全初值）两个方法，**输入输出统一为 RobotState**，时间显式传入（不藏在控制器内部时钟）。
- `reset` 返回 bool，**reset 失败是可恢复错误**（上层决定怎么办）；`forward` 失败返回 None，**不带异常穿越框架**。
- 文档明确 `reset` 必须"在第一次 forward 之前立即调用"——生命周期三步曲 `set_xxx → reset → forward` 贯穿所有实现。

---

## 3. Path → 最小时间轨迹（path.py，trajectory 单元最值得抄的算法）

`Path` = 关节空间路标点序列（线性连接）。`Path.to_minimal_time_joint_trajectory()` 生成 `MinimalTimeJointTrajectory`。

### 3.1 输入验证（13 种 ValueError，全部在构造期 fail-fast）

维度匹配（路标点二维、v/a 一维且长度=关节数、active_joints 长度一致）、v/a 严格大于 0、至少 2 个路标点、容差必须为正、**相邻路标点不得在相对+绝对容差内重合**（重合会除零，提前报错并给出可操作的提示语）。本项目 trajectory 单元的输入验证清单可直接对照此表。

### 3.2 多轴同步：逐段按"最慢关节"缩放（LSPB 同步梯形）

对每段（waypoint[i] → waypoint[i+1]），先按关节算两个约束：

```text
velocity_constraint[i] = 2·|Δq[i]| / v_max[i]
acceleration_constraint[i] = 4·|Δq[i]| / a_max[i]
p = max(velocity_constraints)
q = (acceleration_constraint / p)      若 p ≥ √acceleration_constraint
  = √acceleration_constraint           否则
段时长 motion_time = (p + q) / 2
段内缩放上限：v'_max[i] = 2·|Δq[i]| / p，  a'_max[i] = 4·|Δq[i]| / (p·q)
```

这样**所有关节在同一时刻到达该段终点**（以最慢关节为准），且不改变路径的空间形状——多关节"同步梯形速度曲线"（Linear Segments with Parabolic Blends）的标准解法。缩放后的 v'/a' 保证每个关节要么顶到自己的 v_max，要么顶到 a_max，其余关节被动降速。

### 3.3 两种 profile 的解析采样（无积分、无查表）

- **三角 profile**（加速度段直接接减速度段，无匀速"漂移"段）：切换时刻 `t_switch = √(|Δq|/a_max)`，两侧各为 `½·a·t²` 抛物线。
- **梯形 profile**（`a_max·t_switch > v_max` 时出现匀速段）：加速 `½·a·t²` → 匀速 `v_max` → 减速，三段均为闭式公式。
- 段查找用 `searchsorted(段切换累计时刻)`，采样输出**位置+速度**（加速度未输出——跟随器只需 pos/vel 做目标）。
- 采样时间输入先 `clip(0, duration)`——**越界时间永不报错，只是端点保持**。
- 数值实现用 Warp kernel 按关节并行（GPU/CPU 同一套代码）；构造期一次性预计算（段时长/缩放参数用 numpy），采样期无 Python 循环。

### 3.4 名称寻址的关节空间（重要的解耦设计）

构造时传入 `robot_joint_space`（机器人全部关节的有序名单）与 `active_joints`（本轨迹涉及的关节子集，长度=路标点维数）。采样输出 `JointState.from_name(robot_joint_space, positions=(active_joints, values), ...)`——**轨迹只认关节名，不认索引**，轨迹维数与机器人自由度彻底解耦。同一份轨迹可以喂给不同自由度映射的机器人实例。

### 3.5 测试语义（tests/test_minimal_time_trajectory.py）

- 解析解精确性：三角 profile 总时长精确等于 `4·t_switch`（误差 < 1e-8）；
- 全程约束：逐步采样断言 `|velocity| < v_max`、位置单调不回退、始终在 [起点, 终点] 区间；
- 边界条件：起点 pos/vel=0、终点 pos=Δq 且 vel=0、中点恰为 Δq/2 且 vel=0；
- 正/负方向各测一遍。
**这套断言模板可直接搬进本项目 trajectory 单元的验收测试设计。**

---

## 4. 控制器组合子（controller_structures.py + trajectory_follower.py）

### 4.1 SelectableController——运行时切换，切换即复位

- `dict[Enum, BaseController]` + initial/active/next 三个选择状态。
- **惰性切换**：`set_next_controller(key)` 只登记意图（由上层状态机/行为树调用），真正切换发生在下一次 `forward()` 内部——切换时**先用当前状态对新控制器执行 reset**，reset 失败抛 RuntimeError（切换失败是硬错误，不能带病运行）。
- `get_controller(key)` 提供"逃逸口"：调用接口之外的定制方法（如给某个规划器设置长期目标点）。

### 4.2 CombinedController——并联混合

所有子控制器并行 forward，输出经 `combine_robot_states` 合并；**任一子控制器返回 None 或两个控制器写同一状态分量（冲突）→ 整体返回 None 并告警**。典型用途：手臂 IK 控制器 + 夹爪控制器并联。

### 4.3 ChainedController——串联管道

顺序执行，前一控制器的输出作为后一控制器的 setpoint（estimated_state 与时间全程透传），任一环返回 None 则整体 None。典型用途：目标点采样 → IK → 速度平滑。

### 4.4 TrajectoryFollower——轨迹执行器

- 生命周期：`set_trajectory()` → `reset(t)`（登记起始时刻）→ `forward(t)`（t − start_time 查轨迹）。
- **越界语义分级**：时间倒流（stale clock）→ 清空状态 + 告警 + 返回 None；超过 duration → 清空状态 + **静默**返回 None（这是"正常完成"，不该告警）。完成后 `has_trajectory()` 变 False——**执行器自清理，避免复用过期轨迹**。

---

## 5. 状态表示（types.py）

- **按名称键控**：`JointState` 的 positions/velocities/efforts 各是 `(关节名列表, 数组)` 元组 + 各自到 robot_joint_space 的索引映射，内部打包为紧凑 `data_array` + `valid_array`（Warp 数组，GPU/CPU 通用）。
- `from_name` / `from_index` 双构造；构造期验证 8 类错误（含"关节名重复""不在 robot_joint_space 内"）。
- `RobotState` = `joints: JointState?` + `root: RootState?` + `links: SpatialState?` + `sites: SpatialState?`，全部可选——**一个状态对象同时承载关节空间与笛卡尔空间信息**，控制器想填哪块填哪块。
- `combine_robot_states(a, b)`：并集合并；**同一分量被双方都定义 → 冲突，返回 None**（CombinedController 的"合并失败"由此来）。

---

## 6. 障碍管理（world_interface / world_binding / obstacle_strategy，概要）

- `WorldInterface` 是纯抽象：`add_spheres/cubes/cones/planes/capsules/cylinders/meshes/triangulated_meshes/oriented_bounding_boxes` + `update_obstacle_transforms/twists`——规划器面向"几何原语集合"编程，不感知 USD。
- `WorldBinding` 通过 `TrackableApi` 标记从场景自动收集碰撞体，建立规划世界；支持后续位姿/速度同步（形状与碰撞开关只在初始化时读取——**静态形状、动态位姿**的边界划分明确）。
- `ObstacleStrategy`：表示类型重映射（如某后端不支持任意网格 → 自动换成三角网格或 OBB）+ 每障碍安全容差 + 默认配置。**"规划后端能力差异"被隔离在一个策略对象里，算法层不写 if-else。**

---

## 7. 对本项目 trajectory 单元的接口/架构建议

以下建议均可在"六类端口 + L2 零 Qt + PA-2 不可变历史"红线内落地。

### 7.1 接口签名（C++17 表达）

```cpp
// trajectory 单元核心抽象（对标 Trajectory，61 行的克制值得保留）
class ITrajectory {
public:
    virtual ~ITrajectory() = default;
    /// 轨迹总时长，单位 s。所有实现约定：有效采样区间为 [0, duration]。
    virtual double duration() const = 0;
    /// 按时间采样目标状态；t 由调用方负责裁剪到 [0, duration]。
    /// 失败返回 std::nullopt（替代 Python None，语义一致：调用方 fail-fast）。
    virtual std::optional<traj::RobotState> sample(double t) const = 0;
};

// policy 端口侧的控制器抽象（对标 BaseController）
class IMotionController {
public:
    virtual ~IMotionController() = default;
    /// 由估计状态与可选设定值推下一拍目标状态；失败返回 std::nullopt。
    virtual std::optional<traj::RobotState> forward(const traj::RobotState& estimated,
                                                    const traj::RobotState* setpoint,
                                                    double t) = 0;
    /// 复位到安全初值；失败返回 false（可恢复错误，由上层裁决）。
    virtual bool reset(const traj::RobotState& estimated,
                       const traj::RobotState* setpoint, double t) = 0;
};
```

### 7.2 第一个内置实现：最小时间参数化（MinTimeJointTrajectory）

- 算法直接采用 §3.2 的 p/q 代换 + 逐段同步缩放（这是多轴同步的通用工程解，非 NVIDIA 专有）；建议在头注释注明出处（对应 NVIDIA path.py 的设计）与 TRJ 需求条目。
- 输入验证照抄 §3.1 的 13 项清单（含"相邻路标点重合"的提前拦截）。
- 采样用解析分段公式（三角/梯形两种 profile），**不做数值积分**——确定性与可复现性（NFR-COR-01）天然满足。
- 关节寻址用名称（`JointRef` = 关节名 → 单元内索引的映射在 runtime 完成），实现"轨迹维数与机器人自由度解耦"，也契合 R-4"名称解析归 runtime"红线。

### 7.3 组合子四件套进 policy 端口

`SelectableController / CombinedController / ChainedController / TrajectoryFollower` 建议作为 policy 端口的四个标准适配器实现（或 trajectory 单元提供、经 policy 端口暴露）。关键语义必须保留：
- **切换即复位**（惰性切换 + reset 失败硬错误）；
- **并联合并冲突 = 失败**（不是静默覆盖）；
- **串联输出即下一级设定值**；
- **跟随器完成即自清理**（超 duration 静默、时钟倒退告警）。

### 7.4 失败语义统一约定

全框架用同一种失败表达：**算法性失败返回空可选值，生命周期性错误（切换时复位失败）抛异常**。禁止用返回码、禁止混用——本项目诊断码体系（RT-xxx 稳定码）用于环境错误，调用方错误仍走 fail-fast，两者边界与此设计一致。

### 7.5 测试策略

照搬 §3.5 的断言模板：解析解边界（总时长=4·t_switch）、全程速度上限扫描、位置单调、端点/中点精确值、正反方向对称。这类"解析可预期"的算法，验收测试应做到 1e-8 级精确断言而非区间模糊判断。

### 7.6 障碍数据流

`modeling → trajectory` 的环境数据可对标 TrackableApi 思路：建模侧给碰撞体打标记，trajectory 单元按标记自动收集为规划障碍（静态形状、动态位姿），并提供"表示类型重映射"策略位，避免规划算法感知建模细节。

### 7.7 与 PA-2 的结合

轨迹对象在本项目应是**不可变的修订产物**：构造期完成全部预计算（段时长、切换时刻表），采样接口只读——NVIDIA 的 `MinimalTimeJointTrajectory` 恰好就是这个形态（构造即定，`get_target_state` 为纯查询），两者天然一致。

---

## 8. 合规注意

- 来源扩展为 Apache-2.0（逐文件 SPDX 头）。**建议做法是"参考设计、自行实现"**：算法公式与接口划分属于通用工程知识，按本文档重写 C++17 实现即可；若确需复制原始 Python 片段，须保留版权与许可证声明（Apache-2.0 第 4 条）。
- 整包根目录的 `LICENSE.txt` 是 NVIDIA Omniverse 协议（覆盖 Kit 内核等闭源部分），与扩展级 Apache-2.0 **并存且适用范围不同**——引用时以具体文件头为准，勿把闭源部分纳入借鉴范围。

---

## 附录：关键 API 签名速查

```python
Path(waypoints)                                                    # NxM 关节路标点
Path.to_minimal_time_joint_trajectory(max_vel, max_acc,
    robot_joint_space, active_joints, rtol=1e-6, atol=1e-10) -> Trajectory
Trajectory.duration -> float
Trajectory.get_target_state(time) -> RobotState | None
BaseController.forward(estimated, setpoint|None, t) -> RobotState | None
BaseController.reset(estimated, setpoint|None, t) -> bool
SelectableController(dict[Enum, Controller], initial_key)
SelectableController.set_next_controller(key)     # 由上层状态机调用
CombinedController([c1, c2, ...])                 # 并联合并，冲突->None
ChainedController([c1, c2, ...])                  # 串联，输出->下级setpoint
TrajectoryFollower.set_trajectory(traj) -> None   # 之后必须 reset(t)
JointState.from_name(robot_joint_space, positions=(names, arr),
                     velocities=(names, arr), efforts=None)
combine_robot_states(a, b) -> RobotState | None   # 冲突->None
```
