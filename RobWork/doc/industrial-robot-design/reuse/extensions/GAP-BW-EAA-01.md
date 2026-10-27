# GAP-BW-EAA-01：WP-12 / REUSE-T29 基座旋转矢量转换入口

起点 `ea8ac53baa8923d93c6233ca88db77f8e8778291`。仅涉及 runtime 的 BaseWorldTransform 数学值，未修改生产。所属 migration §13 WP-12，非模型编译/所有权变更。

## 缺口证据

原 `rotationFromCustomEaa` 对有限旋转矢量执行 Rodrigues；其消费者的变换精度为原 1e-9，合法旋转正交性检查为原 1e-12。上游 `rw/math/EAA.hpp::axis()` 在 `theta<1e-6` 时返回零轴；`EAA.cpp::toRotation3D()` 消费该轴。输入 `(1e-8,0,0)` rad 时，独立解析 `R(2,1)=sin(1e-8)`，上游返回0，误差1e-8，超过本接口要求。真实编译原件：`traceability/reuse/REUSE-T29/eaa-probe.cpp`、`run-eaa-probe.cmd`、`eaa-probe.log`。`Math::eaaToQuaternion` 同样调用 `eaa.axis()`，不能修复该语义。已撤销的EAA补丁不恢复，T11局部1e-6接受不推广。

## 方案A：既有 Eigen 公共原语的单点值适配（适用性已批准，性能阻断）

保留原有限分量检查、原范数/零矢量分支；将原九项 Rodrigues 展开删除，在原函数中以 `Eigen::AngleAxisd(theta, eaa.e()/theta).toRotationMatrix()` 构造现有 `rw::math::Rotation3D<double>`。角度 rad，轴在基座/世界安装约定下不变；只连接现有两种数学值，不增加公共函数、分支fallback、模型、缓存或数值核心。非零范数的普通/微小/近π输入走同一成熟库原语。

已有依赖为 vcpkg 安装的 Eigen（`Eigen/src/Geometry/AngleAxis.h`），现有 RobWork 类型及 core PUBLIC 供给已导出；不新增依赖渠道/CMake目标，不修改 RobWork/RobWorkSim 或 vcpkg。探测中该入口对上述反例误差0。最终仍须多轴/非平凡/边界的独立oracle、生产返回值扰动、受影响消费者、冒烟与性能，探测不是产品修复或独验。

catalog 仍只能将 Git 中 RobWork/RobWorkSim 文件作为能力来源。若批准A，`MATH-ROT` 只登记真实 RobWork 值构造接口，Eigen 运算来源、版本/摘要和实际调用在本包逐功能/生产凭证如实绑定；明确保留第三方来源 schema 表达债务，不把 Eigen 算法冒称 RobWork EAA 或 `NONE`，不扩大schema/门禁。这需要本项明确确认，既有其他包接受不外推。

## 被否备选与范围

- B：原 EAA 入口并将容差改1e-6。拒绝：会降低本包原变换/正交性要求，违反局部接受不得推广。
- C：恢复EAA补丁或自写小角分支。拒绝：违反撤销决议/框架只读，继续自持核心。
- D：继续保留原Rodrigues并只迁移同文件其他数学。现有整文件功能准入要求未解决功能不得迁移；若A不批准，本文件生产修改暂停，保持pending，不放宽门禁。

其余计划直接复用 `Transform3D::operator*`、`inverse(Transform3D)`、`inverse(Rotation3D)`、`Rotation3D::operator*` 与现有 LinearAlgebra 的正交检查/行列式，删除相应重复helper；预设常量、安装来源/一致性规则和诊断素材保留。严格有限性、正交阈值、预设精确元素与公共接口不变。原有限极大值导致范数溢出的历史输入缺陷不借本包扩大修复；若阻塞迁移单独登记，不悄改契约。

回滚边界为完整T29增量。最终性能无预算的退化单项交所有者决定；独验仍由独立会话/人工，实施者不自签。

## 所有者决定与当前收口

所有者明确“批准方案A及明确保留表达债务”，候选已实施并验证。随后对本包6场景性能明确“暂不接受，保留性能阻断”。因此方案适用性批准保留，但T29状态blocked；两个生产/测试文件已恢复起点，候选仅保存在traceability/reuse/REUSE-T29，不进入生产，不更新catalog/矩阵为已迁移。53项runtime及19项直接消费者自查和真实扰动不是独验；独验NOT RUN。详见本包README及verification.json。
