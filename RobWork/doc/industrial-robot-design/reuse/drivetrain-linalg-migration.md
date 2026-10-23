# REUSE-T09 迁移说明：传动映射数值核复用上游线性代数

状态：**实施完成、待所有者独立会话验收**（实施者不自签）。隔离起点 `8e4a08ac8`（modeling SVD 包），分支 `codex/reuse-drivetrain-linalg`；上游基点仍为 catalog 锁定 `b12e8503cc1c2a1acc040f506842a02e126b9515`。

## 1. 审计结论（真实调用链）

`MappingCore.cpp` 的 R2 数值核有三类真实消费：

1. **§7.2 映射入口条件数判定**：`singularValues(win.C)` 产出 σmax/σmin → 奇异档（σmin ≤ σmax×1e-12 → DT-MATRIX-SINGULAR）与病态档（κ ＞ 1e8 → DT-MATRIX-ILL-CONDITIONED）判定；校验顺序（非有限/维度→自由轴零传动比→奇异→病态）与比较型诊断语义冻结。
2. **反射惯量窗口投影**：`invertSquareMatrix(model.window->C)` → `J_reflected(wi)＝Σ_mm J_rotor(mm)·C⁻¹(mm,wi)²` 与负载比 `(C⁻¹ᵀ·J_load·C⁻¹)(wi,wi)/J_rotor`（kg·m²/无量纲）。
3. **功率/运动序列窗口反推**：`θ_W/θ̇_W/θ̈_W＝C⁻¹·(q/q̇/q̈)_W`（§7.3：方阵良态下 C⁺≡C⁻¹，伪逆不放行非法矩阵）。

输入恒为窗口耦合矩阵 `win.C`（RowMatrix 行主序、无量纲、方阵、调用前已过工厂/校验器形态检查）。原实现为自写单侧 Jacobi/Hestenes SVD（60 轮上限、1e-16 相对正交阈）与 Gauss-Jordan 部分主元求逆——与上游 `rw::math::LinearAlgebra::svd/inverse` 构成 H2 平行实现。

## 2. 保留／删除／适配／归属

- **保留**：`kDtSingularSigmaRatio＝1e-12`、`kWellConditionedLimit＝1e8`（业务分界阈值，P-DT-2/P-RT-7 同族默认，单元单点承载）；§7.2 校验顺序、DT-MATRIX-SINGULAR/ILL-CONDITIONED 码值与比较型诊断（actual＝σmin/σmax 比值或 κ）；`matVec`/`windowIdealMotorTorque`/`trapezoidIntegral`/`diagonalConditionNumber`（业务面，非分解核）；RowMatrix 容器与全部公共接口签名。
- **删除**：`singularValuesJacobi` 整体；`invertSquareMatrix` 的 Gauss-Jordan 消元体；`kDtJacobiOrthogonalRelative`、`kDtJacobiMaxSweeps`（仅服务于已删核）。
- **适配**：`singularValues`（改名——原名含已删算法）：RowMatrix→Eigen 逐元素复制 ＋ max|a_ij| 归一（防 1e±200 级输入平方上溢/下溢；比值判据不变）＋ 上游 svd ＋ σ×scale 乘回；U/V 仅为接口签名要求不消费。`invertSquareMatrix`（保留名——行为描述）：转换 ＋ `LinearAlgebra::inverse` ＋ 转回；防御面由"零主元抛"改为"逆结果非有限 fail-fast 抛 DT-MATRIX-SINGULAR"（Eigen 逆对奇异输入无异常语义；调用前提仍为 §7.2 校验通过，正常流不可达）。
- **归属**：分解（特征值/旋转迭代）、求逆（LU）归上游 sdurw_math；业务合法性（奇异/病态分界、校验顺序、诊断）归 drivetrain；窗口/单位/映射语义不变。

## 3. 上游适用证据

- `LinearAlgebra.hpp:65` `svd(M,U,sigma,V)`；实现 `LinearAlgebra.cpp:55-63`＝`Eigen::JacobiSVD`（ComputeFullU|ComputeFullV），`singularValues()` 降序——与原产品"降序序列"契约一致（阈值邻界用例实证分界带可判定）。
- `LinearAlgebra.hpp:132-135` `inverse(M)→M.inverse()`（Eigen LU，非奇异前提——由 §7.2 业务校验承载）。
- `pseudoInverse`（`LinearAlgebra.cpp`，precision 截断）**仅评估不采用**：产品 §7.3 冻结口径为"伪逆不得放行非法矩阵"，方阵良态下 C⁺≡C⁻¹，直接 inverse 即已批准公式的计算表示；pseudoInverse 的截断语义反而会掩盖奇异/病态。
- 导出目标 `sdurw_math`（catalog MATH-SVD build.targets 一致）；modeling REUSE-T07 同款链接落位先例（PRIVATE sdurw_math ＋ 别名兜底，集成/冒烟两模式同构）。

## 4. 验证与证据

见 `traceability/reuse/REUSE-T09/README.md`（构建、回归、新增用例、断开实验、性能前后、门禁）。数值结果按需求容差判断（附录 D 第 9 项标量相对 1e-9），不要求跨算法逐位一致；既有 R2 黄金回归（独立解析 oracle）为双路独立锚。

## 5. 共享文件整合建议（主会话统一整合）

1. **catalog MATH-SVD**：upstream.symbols 现为 `[svd, pseudoInverse]`，本包实际消费 `LinearAlgebra::inverse`（LinearAlgebra.hpp:132）。建议补登 `inverse` 符号（同文件同目标 sdurw_math，无新增依赖渠道）；不补则 inverse 消费缺 catalog 归属。
2. **`kinematics/src/Fk.cpp:32` 注释**（"O-40 登记的 SVD 唯一实现点（rw::math 无 SVD）"）已过时——rw::math::LinearAlgebra::svd 为登记上游能力；该文件属 kinematics 单元，不在本包范围，建议主会话在 kinematics 矩阵整合时更正。

## 6. 限制

- 本说明不构成独立验收结论；验收由所有者另开会话执行。
- 窗口矩阵 C 的非有限显式拒绝档（chat 有、C 无显式检查，NaN 经 κ 比较落病态档拒绝）为既有行为，本包未变更，未扩大治理范围。
