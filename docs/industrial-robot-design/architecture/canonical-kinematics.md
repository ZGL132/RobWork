# 规范运动学契约

> 契约 ID：`CTR-KIN-001`、`CTR-KIN-002`（转换判定部分）  
> 检查点：`IRD-D2-20260829`  
> 文档状态：`Proposed`（IRD-D10-20260829 联合评审通过，待签署）  
> 权威边界：本文件是 SE(3) 变换链、坐标系、旋转表示、关节向量、任意轴适配和补偿规则的唯一权威；需求 §7.3 是产品语义决策来源。`module-design/runtime-model.md`、`module-design/robot-modeling.md` 及一切下游只能引用本文件，不得复述公式。

## 1. 变换与坐标系约定

- 列向量齐次变换约定；乘积 `A · B` 对点先作用 `B`、再作用 `A`。
- 姿态 `R ∈ SO(3)`，平移 `t ∈ R³`；齐次变换 `T = [R t; 0 1]`。
- 每个姿态、网格、质心、惯量必须显式声明参考坐标系，不得依赖“当前选中坐标系”。

一节可动关节涉及四个规范坐标系：

| 坐标系 | 记号 | 定义 |
| --- | --- | --- |
| 父连杆坐标系 | `P` | 父连杆的规范连杆坐标系 |
| 关节零位坐标系 | `J0` | `q = 0` 时的关节运动坐标系；`T_P_J0 = OriginPose` |
| 关节运动坐标系 | `Jm(q)` | 施加关节运动后的坐标系；`T_J0_Jm(q) = Motion(â, q)` |
| 子连杆坐标系 | `C` | 子连杆规范坐标系；`T_Jm_C` 为固定变换 |

`WORLD` 是显式根。机器人基座安装位姿 `T_WORLD_base` 保存在 `RobotDesign`（安装参数）；基座、地面与障碍几何归 `EnvironmentModel`。

## 2. 关节变换链（冻结）

```text
T_P_C(q) = T_P_J0 · Motion(â, q) · T_Jm_C
         = OriginPose · Motion(â, q) · T_Jm_C

Revolute / Continuous:  Motion(â, q) = Rot(â, q)      绕 â 右手旋转 q rad
Prismatic:              Motion(â, q) = Trans(â · q)   沿 â 平移 q m
Fixed:                  无 Motion；T_P_C = OriginPose（可含固定 Frame 链）
```

- `T_Jm_C` 默认为单位变换；非单位时必须编译为拥有稳定 `objectId` 的固定 Frame 序列（`objectKind = FixedFrame`），不改变可动关节数。
- 需求 §7.3.1 的 `T_parent_joint(q) = OriginPose · Motion(â, q)` 中 "joint frame" 即本契约的 `Jm`；本契约只是显式命名 `J0/Jm/C`，不改变该公式。

## 3. Zero、Home 与 DH offset（裁决）

1. **项目 Zero ⇔ `q = 0` ⇔ 全部 `Motion(â, 0) = I`**。任何零位偏置（DH `offset`、URDF `<origin>`、导入偏置）只允许在编译/导入边界吸收进 `OriginPose`。
2. **运行时变换链禁止出现第二个偏置项**。`Origin · AxisRotation(q − q_zero)`、`Origin · Rot(â, offset + q)` 一类双偏置链为非法；等价合法形式是把偏置折叠后的 `OriginPose' = OriginPose · Motion(â, q_zero)` 作为编译输出。`module-design/runtime-model.md` 旧表述“链计算固定为 `Origin * AxisRotation(q-zero)`”自本契约起由上式替代。
3. `Home` 是独立的命名关节向量 `q_home`，保存在 `RobotDesign`；不改变 Zero、不参与规范模型编译、不进入变换链。
4. DH `offset` 只在 `StandardDH → ExplicitJoint` 转换时参与生成 `q = 0` 的 `OriginPose`（需求 §7.3.2）；反向不承诺恢复逐项相同数值。

## 4. 关节向量与 qIndex

- `qIndex` 按目标主链从基座到法兰的拓扑顺序，仅分配给可动关节（`Revolute / Continuous / Prismatic`），从 0 连续编号。
- 固定关节与 `FixedFrame` 永不进入 `q`，不占用 `qIndex`。
- `dim(q) = 可动关节数（4～7）`。`Continuous` 在 `q` 中无界；工程工作范围由用户确认后仅用于采样、校核与优化边界（需求 §8.1.2）。
- `q` 各分量单位由关节类型决定：转动 rad、移动 m；对应广义力 N·m / N（CTR-DOM-003）。

## 5. 关节轴规则

- `â` 在该关节自身的运动坐标系 `J0` 中表达，必须有限、非零、单位化（进入规范模型时 `|‖a‖ − 1| ≤ 1e-15`）。
- 范数 `< 1e-12` 判非法；非单位合法输入规范化进入模型，领域层同时保留原始输入轴、`ValueProvenance` / `ImportOrigin` 和规范化记录。
- 固定关节不保存轴语义，不执行缺失、规范化或零轴校验（需求 §7.1）。
- 修改权威轴线属于设计变更：运动学切片变化，实际依赖的下游结果需要重算（MDL-09）。

## 6. 旋转表示（冻结）

**四元数（持久化与计算接口）**

- 单位四元数 `q = (x, y, z, w)`，`‖q‖ = 1`；JSON 持久化为 `{"x","y","z","w"}` 四字段对象，禁止数组或 `(w,x,y,z)` 顺序。
- 载入校验：`|‖q‖ − 1| > 1e-12` 拒绝；落在 `(1e-15, 1e-12]` 内重归一化并生成诊断。
- 符号规范化：`q` 与 `−q` 表示同一旋转。持久化与缓存键使用规范符号——按 `(w, x, y, z)` 顺序第一个绝对值 `> 1e-12` 的分量为正。
- 姿态误差比较使用相对旋转测地角并忽略正负号（需求 §15.3）；不得用朴素 `acos`。

**RPY（仅界面输入与交换表示）**

- `RPY(roll, pitch, yaw)` 固定解释为 `R = Rz(yaw) · Ry(pitch) · Rx(roll)`（外旋固定轴 X-Y-Z，等价内旋 Z-Y′-X″；与 RobWork `rw::math::RPY` 一致）。
- 界面以 deg 显示/输入，内部一律 rad；修改 RPY 必须明确旋转顺序与单位。
- RPY 不得进入持久化领域对象或计算接口（需求 §7.1）；持久化对象使用四元数。

## 7. 任意轴到 RobWork Z 轴的适配补偿（冻结构造）

RobWork 关节绕/沿其局部 Z 运动。适配层构造对齐旋转 `R_c ∈ SO(3)`，满足 `R_c · ẑ = â`：

```text
c = ẑ · â
若 1 − c ≤ 1e-12（平行）：        R_c = I
若 1 + c ≤ 1e-12（反平行）：      R_c = diag(1, −1, −1)   // 绕 X 旋转 π
否则：                            v = ẑ × â，s = ‖v‖
                                  R_c = I + [v]× + [v]×² · (1 − c) / s²   // Rodrigues
```

- 平行、反平行与一般情形的分支阈值冻结为 `1e-12`，构造确定性、无连续性跳变歧义。
- 适配链（转动与移动同一构造）：

```text
T_P_Jm(q) = OriginPose · R_c · Motion_Z(q) · R_c⁻¹
```

  即在 `J0` 之后插入前置补偿固定帧（旋转 `R_c`）、RobWork 关节（局部 Z 运动）、后置补偿固定帧（旋转 `R_c⁻¹`）。`Trans(â·q) = R_c · Trans(ẑ·q) · R_c⁻¹`，移动关节同构。
- **补偿规则**：除这两枚补偿固定帧外，视觉/碰撞几何、质心、惯量一律绑定规范坐标系（默认所属连杆 Frame `C`），不得逐资产改写坐标系。若实现选择对资产整体重表达（等价实现），必须与帧级构造在 Zero、Home、有限边界和固定种子 100 个状态下满足需求 §15.3“URDF 轴对齐适配”容差（世界轴线 `1e-9 rad`、位置 `1e-9 m`、惯量 `1e-10 kg·m²`）的 FK 等价验证。
- 补偿帧拥有稳定 `objectId`（`objectKind = CompensationFrame`），不计入可动关节、不进入 `q`；领域层原始 `OriginPose / Axis` 不因适配改变。
- 反向查询（世界关节轴线、FK 对照、名称解析）以规范链为准，不以适配链为准。

## 8. 几何、碰撞、质心与惯量参考系

- 每个视觉/碰撞网格、质心、惯量必须声明参考坐标系；默认所属连杆 Frame `C`，也可引用项目内任一拥有稳定身份的 Frame（含 `FixedFrame / CompensationFrame`）。
- 惯量张量必须连同参考点（默认质心）、参考姿态（默认参考坐标系）一起保存（需求 §7.1）。
- 附着在连杆上的固定中间变换一律编译为有稳定身份的固定 Frame，不得折叠进可动关节语义。
- `StandardDH` 的 `a/alpha/d/theta(+offset)` 只存在于导入适配器；转换后的下游接口不再接受 DH 参数（`CTR-KIN-002`，需求 §7.3.2）。

## 9. 契约测试

1. 解析 FK 对照：Zero、Home、有限边界、固定种子 100 姿态，TCP 位置 `1e-9 m`、姿态 `1e-9 rad`（需求 §15.3）。
2. 非单位轴 / 非 Z 轴 / 反平行轴夹具：适配链与规范链 FK 等价（§7 容差）。
3. 双偏置拒绝：构造含第二偏置项的链必须在编译边界报稳定诊断，不得产出模型。
4. 四元数符号规范化：`q` 与 `−q` 序列化结果逐字节一致。
5. `qIndex` 连续性：固定关节不占位、`dim(q)` 等于可动关节数。
