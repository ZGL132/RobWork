/**
 * @file   BaseWorldTransform.hpp
 * @brief  基座—世界变换规则（§6 单一不变量的规则面）——安装预设、
 *         T_world_base 校验、正反解纯函数。
 *
 * 设计依据：
 *   - units/runtime.md §6 全章（§6.1 坐标系定义与乘法顺序、§6.2 安装预设
 *     精确定义、§6.3 唯一存储/唯一写入点、§6.4 四类消费方读取契约与禁止
 *     清单、§6.5 最小数值验证例、§6.6 未显式设置与错误表达）、§4.3.2
 *     （WorldPlacement 字段表——T_world_base 唯一存储位置的合法域）、
 *     §3.1（本头模块清单："安装预设、T_world_base 校验、正反解纯函数"）
 *   - 需求 MDL-22（基座安装姿态与重力配置——基座相对世界系姿态、世界系
 *     重力恒定、不在基座系参数化 g）、DYN-01（重力投影 g_base＝Rᵀ·g_W）、
 *     AT-37（安装姿态与重力投影一致消费）、V15-04（未显式配置默认地面）、
 *     NFR-COR-03（非有限不静默）、NFR-COR-02（确定性）
 *   - 任务契约 tasks/foundation/RT-T06.json（acceptance：RT-BW-1/2/3/6
 *     规则部分＋单一写入点规则＋P-RT-4 冻结留痕）
 *
 * 背景说明（本头在 §6 不变量中的位置）：
 *   基座—世界变换在全产品只有一个权威值——CanonicalModel.WorldPlacement.
 *   T_world_base 单字段（§6.3 唯一存储），由编译链唯一写入点（S6 的
 *   BaseMount FixedFrame）一次性置入。本头提供的是该不变量的**规则面**：
 *   编辑表示（Description.base，preset＋customEaa＋basePosition）到
 *   T_world_base 的确定性正解、正解结果的合法域校验与预设一致性校验、
 *   世界系↔基座系的正反解（点变换/逆变换/重力投影/FK 组合）、以及 S9
 *   一致性检查的判定规则（"下游二次叠加"反例的数学检测）。编译链各段
 *   （S5 构造、S6 唯一写入、S9 检查——RT-T07/T11）与 §6.4 四类消费方的
 *   读取接口（IRuntimeModelView::worldToBase()/baseToWorld()/gravityBase()
 *   ——RT-T07 Adapter）一律经本头的纯函数取得变换值或判定结论，
 *   **不得另写第二套安装姿态规则**（§6.3"不新增第二套基座变换规则"）。
 *
 * ★ P-RT-4 冻结留痕（本头即冻结锚点）：倒挂（Inverted）与壁装（Wall）
 *   的旋转轴向在 MDL-22 中未指明（"预设 180°/90°"无绕轴），§6.2 按
 *   "基座 Z 反向"取 R_x(π)＝diag(1,−1,−1)、壁装取 R_y(π/2)（第 1 行
 *   [0,0,1]、第 3 行 [−1,0,0]）单侧选定并冻结（runtime.md D-07：安装
 *   预设轴向前定义后冻结）。installationPresetRotation() 是该冻结矩阵的
 *   **唯一权威产出点**——modeling 卡（WP-13-T01/T11）交叉核对的对照基准；
 *   若建模侧用户口径与本定义分歧，按 §15.3 P-RT-4 处置约束"以建模侧为
 *   准并回改本文"，届时只改本函数一处（单一权威，调用方零改动）。
 *
 * 实现纪律（冒烟 header-only 约束，随 RT-T03 登记）：本头契约的全部矩阵
 *   运算在实现文件内以**逐元素算术**完成——不调用 Rotation3D::multiply/
 *   operator*、Transform3D::operator*、Rotation3D::identity()、
 *   EAA::toRotation3D() 等 rw 外联符号（冒烟模式 rw 仅模板头可达、不链接
 *   框架库，外联符号会产生未解析引用）；Rotation3D::inverse()（原地转置，
 *   头内 inline）是唯一使用的 rw 变换方法，两模式语义一致。
 *
 * 线程安全：全部纯函数（无共享可变状态、无环境/时钟/locale 依赖），
 * 并发只读安全；确定性——同输入同输出（NFR-COR-02），预设矩阵为编译期
 * 常量逐元素写定（§6.2"编码无舍入"）。
 */

#ifndef SDURWS_IRD_RUNTIME_BASEWORLDTRANSFORM_HPP
#define SDURWS_IRD_RUNTIME_BASEWORLDTRANSFORM_HPP

#include <optional>

#include <rw/math/Rotation3D.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/runtime/Description.hpp>  // BasePlacementDescription/
                                               // InstallationPresetToken（§4.2——
                                               // 枚举自 v0.4 起随 Description 落位）
#include <sdurws/ird/runtime/Errors.hpp>       // Expected/RuntimeError（错误轨）

namespace sdurws::ird::runtime {

// =====================================================================
// 安装预设规则（§6.2——预设 token → 旋转矩阵的权威映射）。
// =====================================================================

/**
 * @brief 取安装预设的旋转矩阵 R_world_base（§6.2 精确定义表——唯一权威）。
 *
 * 映射（P-RT-4 冻结值，逐元素常量写定、编码无舍入）：
 *   - Ground   → I（单位阵；基座 +Z 指世界 +Z，竖直向上——MDL-22 默认）；
 *   - Inverted → R_x(π)＝diag(1,−1,−1)（基座 +Z 指世界 −Z，竖直向下，
 *                吊装；轴向选择登记 P-RT-4，见文件头留痕）；
 *   - Wall     → R_y(π/2)（第 1 行 [0,0,1]、第 3 行 [−1,0,0]；基座 +Z
 *                转入世界水平方向——壁/侧装；同 P-RT-4）；
 *   - Custom   → 无预设矩阵（自定义姿态的矩阵由编辑表示换算，见
 *                rotationFromCustomEaa()）——抛异常 fail-fast。
 *
 * ★ 预设矩阵元素全为 {0,±1}（§6.2"编码无舍入"）：故意不用 rotX(π)/rotY(π/2)
 *   一类三角函数实现——浮点 π 的 sin/cos 不精确为零，会破坏"逐位确定"
 *   与"元素 ∈ {0,±1}"的精确契约（RT-BW-2 以位相等断言钉住）。
 *
 * @param preset [in] 安装预设 token（四值之一）
 * @return 该预设的 R_world_base（值语义；与 §6.2 表逐元素相等——精确
 *         {0,±1}，非近似）
 *
 * @throws RuntimeError 码＝InputInvalid：preset＝Custom（Custom 无固定
 *         预设矩阵，属调用方误用——矩阵应经 rotationFromCustomEaa() 从
 *         编辑表示换算；fail-fast 而非返回占位矩阵静默吞错，NFR-COR-03）
 */
rw::math::Rotation3D<double> installationPresetRotation(InstallationPresetToken preset);

/**
 * @brief Custom 预设的编辑表示→旋转矩阵（EAA 等价轴角→R，Rodrigues 公式）。
 *
 * 背景（§6.2 Custom 行）：自定义安装姿态在编辑侧以欧拉角/轴角表示编辑
 * （"R 由 modeling 换算"指编辑器预览），注入通道（Description.base.
 * customEaa）携带的权威编辑表示是 EAA 旋转矢量——本函数是 EAA→R 的
 * 确定性换算点（旋转本体存矩阵入 CanonicalModel，编辑表示不入身份，
 * §4.3.2"preset 不入身份、R 入身份"）。
 *
 * 数学（Rodrigues）：记 eaa＝(kx,ky,kz)，θ＝‖eaa‖（单位 rad）、
 * k＝eaa/θ（θ≠0 时），则
 *   R＝I＋sin(θ)·[k]×＋(1−cos(θ))·[k]×²（[k]× 为 k 的反对称叉乘矩阵）；
 * θ＝0（零矢量）为合法输入，恒等返回 I（零旋转的极限情形）。
 *
 * @param eaa [in] 旋转矢量：方向＝旋转轴（无需预单位化），模长＝旋转角，
 *             单位 rad；分量须全部有限
 * @return R_world_base（正交、det＝+1；同输入逐位同输出——确定性）
 *
 * @throws RuntimeError 码＝InputInvalid：eaa 含非有限分量（NaN/Inf——
 *         NFR-COR-03 非有限不静默，就地拒绝）
 */
rw::math::Rotation3D<double> rotationFromCustomEaa(const rw::math::Vector3D<double>& eaa);

// =====================================================================
// 正解（§6.1 正向：编辑表示 → T_world_base）。
// =====================================================================

/**
 * @brief 从基座布置编辑表示解析 T_world_base（§6.1 正向——**唯一产生
 *        入口**，§6.3"全产品不存在第二处安装变换定义"的产生侧承载）。
 *
 * 规则（逐分支出处）：
 *   - Ground   → T＝(basePosition, I)（§6.2 默认预设；未显式配置的
 *                Description.base 即本形态——RT-BW-3：确定性默认非错误，
 *                V15-04 含 URDF/Xacro 导入与空白模板路径）；
 *   - Inverted → T＝(basePosition, R_x(π))（P-RT-4 冻结矩阵）；
 *   - Wall     → T＝(basePosition, R_y(π/2))（同上）；
 *   - Custom   → customEaa 必填（NotProvided→err InputInvalid——§4.2
 *                "Custom 时必填"），矩阵经 rotationFromCustomEaa() 换算；
 *   - basePosition 任一分量非有限→err InputInvalid（§4.3.2 合法域
 *                "t 有限"；NFR-COR-03）。
 *
 * 单位：basePosition 分量单位 m（世界系坐标）；旋转 rad 通道（customEaa）。
 * 产出的 T 满足 checkWorldBaseTransform() 与 checkPresetConsistency()——
 * 两个校验函数在本函数的规则域内恒通过（自洽性由 RT-BW 用例钉住）。
 *
 * 非抛出接口：一切失败走 Expected 错误侧（S2/S5 编译链将该错误转译为
 * 定位到 base 字段的编译诊断；Description 校验器不覆盖本函数面——
 * S3 归属说明见 DescriptionValidator.hpp"不覆盖面"清单）。
 *
 * @param base [in] 基座布置编辑表示（只读；Description.base 字段值）
 * @return ok＝T_world_base（§6.1 读法：基座系相对世界系；平移单位 m）；
 *         err＝RuntimeError（码＝InputInvalid，detail 中文定位到字段与
 *         实测值——比较型诊断素材）
 *
 * 确定性：同 base 同 T（预设常量矩阵＋Rodrigues 纯函数——NFR-COR-02）。
 */
Expected<rw::math::Transform3D<double>, RuntimeError>
    resolveWorldBaseTransform(const BasePlacementDescription& base);

// =====================================================================
// 校验（§4.3.2 合法域＋§6.2 一致性——S5 构造校验与 S9 检查的规则面）。
// =====================================================================

/**
 * @brief 校验 T_world_base 的变换合法性（§4.3.2 合法域——矩阵层）。
 *
 * 检查项（按执行序，任一违例即返回错误、不再后续检查——非有限输入的
 * 比较无意义，必须先拦截）：
 *   1. 有限性：R 九元素与平移三分量全部有限（§4.3.2"含非有限分量→
 *      InputInvalid"；NFR-COR-03）；
 *   2. 正交性：max|RᵀR−I| ≤ 1×10⁻¹²（§6.6 正交性容差，附录 D 第 6 项
 *      对称性容差同尺度；与 CanonicalModel.cpp 的 kOrthoTolerance 同源
 *      同值）——detail 携带实测最大偏差（§6.6"实测偏差值的比较型诊断"）；
 *   3. 非反射：det(R)＞0（§4.3.2"det＝+1"的判定面——正交矩阵行列式
 *      为 ±1，负值即反射变换，非法）。
 *
 * @param tWorldBase [in] 待校验变换（只读；平移单位 m）
 * @return 合法＝nullopt；非法＝RuntimeError（码＝InputInvalid，detail
 *         含违例项与实测偏差值——错误经调用方转译进编译诊断/异常轨，
 *         本函数自身不抛）
 */
std::optional<RuntimeError> checkWorldBaseTransform(
    const rw::math::Transform3D<double>& tWorldBase);

/**
 * @brief 校验预设 token 与实际旋转的一致性（§6.2"S5 构造校验"规则面）。
 *
 * 一致性规则（逐条出处；预设是编辑表示/来源记录，R 是本体——§6.2）：
 *   - Inverted：R 必须逐元素等于 R_x(π)＝diag(1,−1,−1)（§4.3.2 合法域
 *     "inverted 且 R＝R_x(π)"）；
 *   - Wall：R 必须逐元素等于 R_y(π/2)（§6.2 精确定义表的对称承接——
 *     与 CanonicalModel.cpp"Custom 面"注释的分工一致：Inverted/Wall
 *     匹配归本函数，不与 builder 重复实现）；
 *   - Custom：R 不得为恒等阵（§4.3.2 明示"Custom 而 R＝I 校验失败"；
 *     恒等判定容差与 builder 同源——逐元素 1×10⁻¹²）；
 *   - Ground：无一致性约束（§6.2 括号规则"preset≠ground 而 R＝I"明确
 *     只覆盖非 ground 侧；Ground 兼作未显式配置的默认解释——V15-04，
 *     不扩大语义拒绝任何合法 R）。
 *
 * 判定容差：预设矩阵匹配与恒等判定均用逐元素 1×10⁻¹²（与 kOrthoTolerance
 * 同源；预设矩阵元素 ∈ {0,±1} 编码无舍入，浮点通道的微小偏差不误判）。
 *
 * 前置：tWorldBase 已通过 checkWorldBaseTransform()（矩阵层合法）——
 * 未过矩阵层校验的输入属调用方违约，本函数的判定不对其负责。
 *
 * @param preset     [in] 声明的安装预设 token
 * @param tWorldBase [in] 实际旋转对应的变换（只读；取其 R 部分判定）
 * @return 一致＝nullopt；违例＝RuntimeError（码＝InputInvalid，detail
 *         含声明的预设与实测矩阵形态——§6.6"定位对象＋实测偏差"）
 */
std::optional<RuntimeError> checkPresetConsistency(
    InstallationPresetToken preset,
    const rw::math::Transform3D<double>& tWorldBase);

// =====================================================================
// 反解（§6.1 反向与消费数学——四类消费方读取接口的规则实现）。
// =====================================================================

/**
 * @brief 反解 T_base_world＝inverse(T_world_base)（§6.1 反向——世界系
 *        位姿在基座系中的表达）。
 *
 * 数学：R 正交时逆旋转＝转置（R⁻¹＝Rᵀ），逆平移＝−Rᵀ·t（§6.5② 手算
 * 例：R_x(π)、t＝(0,0,2) 时 Rᵀt＝(0,0,−2)，逆平移 t'＝−Rᵀt＝(0,0,+2)；
 * ★ 卡文 §6.5② 曾写 "t'＝(0,0,−2.0)"，属转写笔误——其同例闭环断言
 *   （p_base' 与输入逐位一致）唯一要求 +2，已按 DTB §5.4 修订卡文，
 *   详见 §15.4 v0.7）。转置实现不经通用求逆，
 * 逐元素一次完成（数值精确、确定性）。
 *
 * 消费对应（§6.4）：四类消费方接口 baseToWorld()（世界系目标转基座系
 * 表达）的规则实现即本函数；kinematics 的 IK 目标表达、trajectory 的
 * 路径点变换均以此为唯一规则点。
 *
 * @param tWorldBase [in] 正向变换（前置：已过 checkWorldBaseTransform
 *                    ——旋转正交是对合可逆前提；非正交输入属调用方
 *                    契约违约）
 * @return T_base_world（读法 core §4.6：世界系相对基座系；平移单位 m）
 *
 * @throws RuntimeError 码＝InputInvalid：旋转部分非正交（正交性偏差
 *         超 1×10⁻¹²——转置不再是逆，fail-fast 而非产出错误值）
 */
rw::math::Transform3D<double> invertWorldBase(
    const rw::math::Transform3D<double>& tWorldBase);

/**
 * @brief 点变换正向：p_world＝T_world_base·p_base（§6.5①——基座系点
 *        表达到世界系）。
 *
 * 消费对应（§6.4）：四类消费方接口 worldToBase() 家族的"设备链结果投到
 * 世界系"方向；policy 的设备几何经同一 WC 消费同一变换（禁止清单 3：
 * 环境几何不使用本函数——环境固连世界系，绝不预乘安装旋转）。
 *
 * @param tWorldBase [in] 正向变换（平移单位 m）
 * @param pBase      [in] 基座系中的点（坐标单位 m——如法兰/TCP 链结果）
 * @return 世界系中的同一点（单位 m）
 */
rw::math::Vector3D<double> transformBasePointToWorld(
    const rw::math::Transform3D<double>& tWorldBase,
    const rw::math::Vector3D<double>& pBase);

/**
 * @brief 点变换反向：p_base＝T_base_world·p_world（§6.5②——世界系点
 *        表达转回基座系）。
 *
 * 实现按 Rᵀ·(p_world−t) 直接计算（等价于先 invertWorldBase 再复合，
 * 但不构造中间变换对象——同一数学、一次遍历）。
 *
 * @param tWorldBase [in] 正向变换 T_world_base（前置：旋转正交）
 * @param pWorld     [in] 世界系中的点（坐标单位 m——如 IK 目标、测量点）
 * @return 基座系中的同一点（单位 m）
 */
rw::math::Vector3D<double> transformWorldPointToBase(
    const rw::math::Transform3D<double>& tWorldBase,
    const rw::math::Vector3D<double>& pWorld);

/**
 * @brief 重力向基座系投影：g_base＝R_world_baseᵀ·g_world（§6.1——DYN-01
 *        消费的权威公式）。
 *
 * 背景（MDL-22 实现口径）：世界系重力恒定 g_W＝(0,0,−9.81) m/s²，**不在
 * 基座系参数化 g**——禁止清单 2 明示"倒挂就令 g_base 取反"一类的就地
 * 翻转非法，必须经本函数的 Rᵀ 投影（§6.5③：倒挂时 g_base＝(0,0,+9.81)，
 * 基座系中重力沿 +Z_base——倒挂静态重力矩符号由 RNEA 据此正确产生，
 * AT-37 联动 DYN-01 解析算例）。
 *
 * @param rWorldBase [in] 正向旋转 R_world_base（前置：正交；单位阵＝
 *                    地面安装时投影为恒等）
 * @param gWorld     [in] 世界系重力加速度（单位 m/s²；规范值
 *                    (0,0,−9.81)——WorldPlacement.gravityWorld）
 * @return 基座系中的重力加速度（单位 m/s²）
 */
rw::math::Vector3D<double> gravityToBase(
    const rw::math::Rotation3D<double>& rWorldBase,
    const rw::math::Vector3D<double>& gWorld);

/**
 * @brief 世界系 TCP 正向运动链组合：T_world_tcp＝T_world_base·T_base_tcp
 *        （§6.1 乘法表；core §4.6 读法 T_ac＝T_ab·T_bc）。
 *
 * 消费对应（§6.4）：kinematics/trajectory 的世界系 FK/TCP/雅可比全部
 * 消费该组合；§6.5④ 结合律（三因子两种结合顺序逐元素一致）是本函数
 * 与消费方组合次序的契约测试面（RT-BW-1④）。
 *
 * 实现纪律：逐元素复合（R_wc＝R_wb·R_bc，t_wc＝R_wb·t_bc＋t_wb）——
 * 不用 rw 的 Transform3D::operator*（其内部经 Rotation3D::multiply 外联
 * 符号，冒烟模式不可达；两模式数学一致）。
 *
 * @param tWorldBase [in] T_world_base（唯一写入点的编译产物；前置正交）
 * @param tBaseTcp   [in] T_base_tcp：基座系内设备链 FK＋TCP 偏置的复合
 *                    （RobWork 设备坐标系内；平移单位 m）
 * @return T_world_tcp（TCP 在世界系位姿；平移单位 m）
 */
rw::math::Transform3D<double> composeWorldBaseTcp(
    const rw::math::Transform3D<double>& tWorldBase,
    const rw::math::Transform3D<double>& tBaseTcp);

/**
 * @brief S9 一致性检查的偏差载荷（§6.6"重复应用基座变换"的比较型数据）。
 *
 * 语义：编译产物中实际 BaseMount 变换与权威 T_world_base 不一致时携带
 * 的诊断素材——码值（BaseWorldInconsistent）与 DiagnosticRecord 的装配
 * 归编译链（S9/RT-T11，码值权威＝diagnostics StableCodeRegistry——PA-1），
 * 本结构只承载判定与比较数据。线程安全：纯值。
 */
struct BaseMountDeviation {
    /// 平移分量最大绝对偏差，单位 m（max_i |t_cand(i) − t_ref(i)|）。
    double maxPositionDeviation = 0.0;
    /// 旋转分量最大元素绝对偏差（无量纲；max_{ij} |R_cand(i,j) − R_ref(i,j)|）。
    double maxRotationDeviation = 0.0;
    /**
     * 是否呈 ≈T·T 形态（§6.6"含 ≈T·T 形态"的二次叠加标记——AT-37 反例
     * 特征：某消费方/编译段把安装变换又乘了一次）。true＝候选与
     * T_world_base·T_world_base 在一致性容差内逐元素相等——该标记帮助
     * 定位"重复应用"缺陷形态，false＝其他形态的不一致。
     */
    bool doubleAppliedPattern = false;
};

/**
 * @brief S9 基座—世界一致性检查规则（§6.6：编译产物实际变换 vs 权威 T）。
 *
 * 规则（§6.3 唯一写入点的事后验证面——"此后编译器任何阶段不再改写"）：
 * 候选变换与权威 T_world_base 逐元素比较（平移分量 m、旋转元素无量纲），
 * 偏差全部 ≤ 一致性容差（1×10⁻⁹ m / 1×10⁻⁹——附录 D 第 4 项容差档案
 * rt profile）→ 一致（nullopt）；否则返回偏差载荷（含 ≈T·T 形态探测）。
 *
 * AT-37 反例观测点（§6.6）：倒挂机型若下游自行再乘 R_x(π)，R²＝I——
 * 候选呈 T·T 形态，本检查必须检出（RT-BW-4 规则部分；doubleAppliedPattern
 * ＝true）。不一致＝实现缺陷类失败（BaseWorldInconsistent——§3.4"不得
 * 放行"），编译链不得对非 nullopt 结果继续发布。
 *
 * @param candidate   [in] 编译产物中实际读到的基座安装变换（如 WC 内
 *                    BaseMount FixedFrame 的 transform；平移单位 m）
 * @param tWorldBase  [in] 权威 T_world_base（CanonicalModel.WorldPlacement
 *                    单字段值；唯一存储/唯一写入点——§6.3）
 * @return 一致＝nullopt；不一致＝偏差载荷（非空即编译失败信号——由
 *         调用方装配 BaseWorldInconsistent 诊断）
 */
std::optional<BaseMountDeviation> checkBaseMountConsistency(
    const rw::math::Transform3D<double>& candidate,
    const rw::math::Transform3D<double>& tWorldBase);

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_BASEWORLDTRANSFORM_HPP
