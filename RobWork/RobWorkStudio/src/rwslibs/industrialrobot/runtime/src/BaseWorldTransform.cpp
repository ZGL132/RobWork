/**
 * @file   BaseWorldTransform.cpp
 * @brief  基座—世界变换规则实现（§6）——预设常量矩阵、Rodrigues 换算、
 *         正解/校验/反解/S9 一致性判定的逐元素实现。
 *
 * 设计依据（契约见同名公共头 BaseWorldTransform.hpp 的文件头）：
 *   - units/runtime.md §6 全章、§4.3.2（合法域）、§3.1（模块清单）、
 *     附录 D 第 4 项（一致性容差 1×10⁻⁹）与第 6 项（正交性 1×10⁻¹²）
 *   - 需求 MDL-22、DYN-01、AT-37、V15-04、NFR-COR-02/03
 *   - 任务契约 tasks/foundation/RT-T06.json
 *
 * 实现要点（为什么全部逐元素算术）：
 *   冒烟模式（DTB §5.1 双模式之一）对 rw 只提供模板头 header-only 可达、
 *   不链接框架库——Rotation3D::multiply/operator*、Transform3D::operator*、
 *   Rotation3D::identity()、EAA::toRotation3D() 等均为框架 .cpp 中的外联
 *   符号，引用即未解析。故本文件的全部矩阵/向量运算以 double 元素算术
 *   手写（3×3 规模，O(1) 常量级）；唯一使用的 rw 方法是
 *   Rotation3D::inverse()（原地转置，头内 inline 定义，两模式语义一致）。
 *   该纪律同时服务于确定性（NFR-COR-02）：同输入在同二进制内逐位同输出。
 */

#include <sdurws/ird/runtime/BaseWorldTransform.hpp>

#include <cmath>
#include <string>

namespace sdurws::ird::runtime {

namespace {

// ---------------------------------------------------------------------
// 容差常量（来源逐条标注——AGENTS.md §2.5 魔法数字纪律）。
// ---------------------------------------------------------------------

/// 正交性容差：max|RᵀR−I| ≤ 1×10⁻¹²（§6.6 正交性检查；与 CanonicalModel.cpp
/// 的 kOrthoTolerance、DescriptionValidator 的同值常量三处同源——附录 D
/// 第 6 项对称性容差同尺度，任何一处修改必须三处同步评审）。
constexpr double kOrthoTolerance = 1e-12;

/// 一致性容差：S9 检查与数值断言的元素级上限（附录 D 第 4 项容差档案：
/// 1×10⁻⁹ m / 1×10⁻⁹ rad——§6.5④ 结合律断言同尺度）。
constexpr double kConsistencyTolerance = 1e-9;

/// 预设矩阵匹配容差：预设元素 ∈ {0,±1} 编码无舍入（§6.2），判定用与
/// 正交性同源的 1×10⁻¹²——只吸收浮点通道的表示误差，不放宽语义。
constexpr double kPresetMatchTolerance = kOrthoTolerance;

// ---------------------------------------------------------------------
// 逐元素矩阵/向量助手（匿名命名空间——单元内部实现细节，不入公共契约）。
// 全部为纯元素算术：零 rw 外联符号（文件头实现要点）。
// ---------------------------------------------------------------------

/// 逐元素构造 3×3 旋转（行主序九参——调用处直接可读出矩阵形状）。
rw::math::Rotation3D<double> makeRotation(double r00, double r01, double r02,
                                          double r10, double r11, double r12,
                                          double r20, double r21, double r22)
{
    return rw::math::Rotation3D<double>(r00, r01, r02, r10, r11, r12,
                                        r20, r21, r22);
}

/// 单位旋转 I（逐元素——不用外联的 Rotation3D::identity()）。
rw::math::Rotation3D<double> identityRotation()
{
    return makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1);
}

/// 矩阵乘法 R＝A·B（3×3 逐元素：R(i,j)＝Σk A(i,k)B(k,j)——确定性循环序）。
rw::math::Rotation3D<double> multiplyRotation(const rw::math::Rotation3D<double>& a,
                                              const rw::math::Rotation3D<double>& b)
{
    rw::math::Rotation3D<double> r(1, 0, 0, 0, 1, 0, 0, 0, 1);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += a(i, k) * b(k, j);
            }
            r(i, j) = sum;
        }
    }
    return r;
}

/// 旋转向量 v'＝R·v（逐元素点积；确定性求和序 k＝0,1,2）。
rw::math::Vector3D<double> rotateVector(const rw::math::Rotation3D<double>& r,
                                        const rw::math::Vector3D<double>& v)
{
    return rw::math::Vector3D<double>(
        r(0, 0) * v(0) + r(0, 1) * v(1) + r(0, 2) * v(2),
        r(1, 0) * v(0) + r(1, 1) * v(1) + r(1, 2) * v(2),
        r(2, 0) * v(0) + r(2, 1) * v(1) + r(2, 2) * v(2));
}

/// 转置乘向量 v'＝Rᵀ·v（等价于逆旋转——R 正交时 R⁻¹＝Rᵀ，§6.1 重力投影
/// 与反向点变换的数学核心；逐元素按列取数，一次遍历不构造中间矩阵）。
rw::math::Vector3D<double> rotateTransposed(const rw::math::Rotation3D<double>& r,
                                            const rw::math::Vector3D<double>& v)
{
    return rw::math::Vector3D<double>(
        r(0, 0) * v(0) + r(1, 0) * v(1) + r(2, 0) * v(2),
        r(0, 1) * v(0) + r(1, 1) * v(1) + r(2, 1) * v(2),
        r(0, 2) * v(0) + r(1, 2) * v(1) + r(2, 2) * v(2));
}

/// 3×3 行列式（Sarrus 展开式——3 阶直接展开，O(1)）。
double determinant(const rw::math::Rotation3D<double>& r)
{
    return r(0, 0) * (r(1, 1) * r(2, 2) - r(1, 2) * r(2, 1))
         - r(0, 1) * (r(1, 0) * r(2, 2) - r(1, 2) * r(2, 0))
         + r(0, 2) * (r(1, 0) * r(2, 1) - r(1, 1) * r(2, 0));
}

/// 旋转正交性最大偏差 max|RᵀR−I|（逐元素；§6.6 检查量的实现）。
double orthogonalityDeviation(const rw::math::Rotation3D<double>& r)
{
    double worst = 0.0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            // (RᵀR)(i,j)＝R 的第 i 列与第 j 列的点积。
            double dot = 0.0;
            for (int k = 0; k < 3; ++k) {
                dot += r(k, i) * r(k, j);
            }
            const double dev = std::fabs(dot - (i == j ? 1.0 : 0.0));
            if (dev > worst) { worst = dev; }
        }
    }
    return worst;
}

/// 两旋转逐元素最大绝对偏差（S9 比较量）。
double maxRotationDeviation(const rw::math::Rotation3D<double>& a,
                            const rw::math::Rotation3D<double>& b)
{
    double worst = 0.0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const double dev = std::fabs(a(i, j) - b(i, j));
            if (dev > worst) { worst = dev; }
        }
    }
    return worst;
}

/// 两平移逐元素最大绝对偏差（单位 m；S9 比较量）。
double maxPositionDeviation(const rw::math::Vector3D<double>& a,
                            const rw::math::Vector3D<double>& b)
{
    double worst = 0.0;
    for (int i = 0; i < 3; ++i) {
        const double dev = std::fabs(a(i) - b(i));
        if (dev > worst) { worst = dev; }
    }
    return worst;
}

/// 数值转诊断串（std::to_string 固定 %f 语义、无 locale 依赖——诊断 detail
/// 的实测偏差值展示；诊断码面归 diagnostics，本处只产文本素材）。
std::string num(double v)
{
    return std::to_string(v);
}

}  // namespace

// =====================================================================
// 安装预设规则（§6.2）。
// =====================================================================

rw::math::Rotation3D<double> installationPresetRotation(InstallationPresetToken preset)
{
    // 逐预设返回 §6.2 精确定义表的常量矩阵——元素直接写数值（±1/0），
    // 绝不经三角函数：浮点 π 的 sin/cos 不精确为零，会破坏"元素 ∈ {0,±1}
    // 编码无舍入"的逐位契约（RT-BW-2 以精确相等断言钉住；P-RT-4 轴向
    // 冻结见公共头文件头留痕——本函数即冻结矩阵的唯一权威产出点）。
    switch (preset) {
    case InstallationPresetToken::Ground:
        // 地面安装：I——基座 +Z 指世界 +Z（竖直向上，MDL-22 默认）。
        return identityRotation();
    case InstallationPresetToken::Inverted:
        // 倒挂 180°：R_x(π)＝diag(1,−1,−1)——基座 +Z 指世界 −Z（吊装）。
        // 轴向选择登记 P-RT-4（MDL-22 未指明绕轴，按"基座 Z 反向"取绕 X）。
        return makeRotation(1, 0, 0, 0, -1, 0, 0, 0, -1);
    case InstallationPresetToken::Wall:
        // 壁/侧装 90°：R_y(π/2)＝[[0,0,1],[0,1,0],[−1,0,0]]——基座 +Z
        // 转入世界水平方向（+X_W）。轴向选择登记 P-RT-4（取绕 Y）。
        return makeRotation(0, 0, 1, 0, 1, 0, -1, 0, 0);
    case InstallationPresetToken::Custom:
        // Custom 无固定预设矩阵（§6.2 表：任意欧拉角编辑结果）——矩阵
        // 应经 rotationFromCustomEaa() 从编辑表示换算。调用方对本预设
        // 取"预设矩阵"属误用：fail-fast 拒绝而非返回占位阵静默吞错
        // （NFR-COR-03；错误码面 InputInvalid——S3 归属同表）。
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           "base：Custom 预设无固定矩阵——经 "
                           "rotationFromCustomEaa(customEaa) 由编辑表示换算"
                           "（§6.2 Custom 行）");
    }
    // 全枚举 switch 后不可达——防御性兜底仍走 fail-fast（新增枚举值未
    // 登记映射时编译期即应扩展本表；运行期到达此处＝表与枚举失同步，
    // 属实现缺陷，不得静默返回）。
    throw RuntimeError(RuntimeErrorCode::InputInvalid,
                       "base：未登记的安装预设枚举值（实现缺陷——"
                       "installationPresetRotation 全枚举表失同步）");
}

rw::math::Rotation3D<double> rotationFromCustomEaa(const rw::math::Vector3D<double>& eaa)
{
    // 第一步：非有限拦截（NFR-COR-03——NaN/Inf 一经算术传播会污染整个
    // 矩阵且比较恒假，必须在入口拒绝）。
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(eaa(i))) {
            throw RuntimeError(RuntimeErrorCode::InputInvalid,
                               "base.customEaa：含非有限分量（index "
                               + std::to_string(i) + "）——单位 rad 的旋转"
                               "矢量须全部有限（NFR-COR-03）");
        }
    }

    // 第二步：角度 θ＝‖eaa‖（单位 rad）。零矢量＝零旋转（数学合法极限，
    // 恒等返回 I；其是否可入模型由 checkPresetConsistency 的"Custom 而
    // R＝I"一致性面判定——本函数只做纯换算，不做语义层拒绝）。
    const double norm = std::sqrt(eaa(0) * eaa(0) + eaa(1) * eaa(1)
                                  + eaa(2) * eaa(2));
    if (norm == 0.0) {
        return identityRotation();
    }

    // 第三步：Rodrigues 公式（轴 k＝eaa/θ，角 θ）：
    //   R＝I＋sin(θ)·[k]×＋(1−cos(θ))·[k]×²
    // 展开为逐元素标准形（[k]× 为反对称叉乘矩阵）——三角函数只作用于
    // 标量角度，矩阵组装为纯算术，同输入逐位同输出（确定性）。
    const double kx = eaa(0) / norm;
    const double ky = eaa(1) / norm;
    const double kz = eaa(2) / norm;
    const double c = std::cos(norm);
    const double s = std::sin(norm);
    const double bigC = 1.0 - c;

    return makeRotation(
        c + bigC * kx * kx,     bigC * kx * ky - s * kz, bigC * kx * kz + s * ky,
        bigC * kx * ky + s * kz, c + bigC * ky * ky,     bigC * ky * kz - s * kx,
        bigC * kx * kz - s * ky, bigC * ky * kz + s * kx, c + bigC * kz * kz);
}

// =====================================================================
// 正解（§6.1 正向）。
// =====================================================================

Expected<rw::math::Transform3D<double>, RuntimeError>
    resolveWorldBaseTransform(const BasePlacementDescription& base)
{
    // 平移分量：basePosition（世界系坐标，单位 m——§4.2 字段注释）。
    // 先做有限性拦截：非有限平移经任何预设分支都会产出非法 T（§4.3.2
    // "t 有限"合法域），且比较型诊断需要如实回显实测值。
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(base.basePosition(i))) {
            return Expected<rw::math::Transform3D<double>, RuntimeError>::err(
                RuntimeError(RuntimeErrorCode::InputInvalid,
                             "base.basePosition：含非有限分量（index "
                             + std::to_string(i) + "）——单位 m 的世界系"
                             "坐标须全部有限（§4.3.2/NFR-COR-03）"));
        }
    }

    // 旋转分量：按预设分支解析（Custom 走编辑表示换算）。
    rw::math::Rotation3D<double> rotation = identityRotation();
    switch (base.preset) {
    case InstallationPresetToken::Ground:
    case InstallationPresetToken::Inverted:
    case InstallationPresetToken::Wall:
        // 三个固定预设：直接取 §6.2 冻结矩阵（Ground/Inverted/Wall 的
        // 映射在 installationPresetRotation 内不会抛——抛出路径仅 Custom）。
        rotation = installationPresetRotation(base.preset);
        break;
    case InstallationPresetToken::Custom:
        // Custom：customEaa 必填（§4.2"Custom 时必填"）。NotProvided＝
        // 编辑表示缺失——err 侧拒绝（不伪造缺省旋转，NFR-COR-03"缺失
        // 不伪造"）。
        if (base.customEaa.state() != core::FieldState::Provided) {
            return Expected<rw::math::Transform3D<double>, RuntimeError>::err(
                RuntimeError(RuntimeErrorCode::InputInvalid,
                             "base.customEaa：preset=Custom 而 customEaa 未"
                             "提供（§4.2 Custom 时必填；NotProvided 不伪造"
                             "缺省旋转）"));
        }
        // rotationFromCustomEaa 对有限输入不抛（非有限在上方 basePosition
        // 同源拦截之外由其自身入口拒绝——双保险语义独立）。value() 为带
        // 前置的取值（Provided 态已在上分支确认——违约即调用方错误）。
        rotation = rotationFromCustomEaa(base.customEaa.value());
        break;
    }

    // 组装正向变换：T＝(basePosition, R_preset)（§6.1 正向定义——基座系
    // 在世界系中的表达；"未显式配置"的默认 Description.base 即 Ground＋
    // 零平移＋I＝地面安装默认，RT-BW-3 的确定性默认语义）。
    return Expected<rw::math::Transform3D<double>, RuntimeError>::ok(
        rw::math::Transform3D<double>(base.basePosition, rotation));
}

// =====================================================================
// 校验（§4.3.2 合法域＋§6.2 一致性）。
// =====================================================================

std::optional<RuntimeError> checkWorldBaseTransform(
    const rw::math::Transform3D<double>& tWorldBase)
{
    // 检查 1：有限性（先于正交性——NaN 参与的任何比较恒假，正交性
    // 检查对非有限矩阵无意义；§4.3.2"含非有限分量→InputInvalid"）。
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (!std::isfinite(tWorldBase.R()(i, j))) {
                return RuntimeError(
                    RuntimeErrorCode::InputInvalid,
                    "T_world_base.R：元素(" + std::to_string(i) + ","
                    + std::to_string(j) + ") 非有限——§4.3.2 合法域"
                    "（NFR-COR-03 非有限不静默）");
            }
        }
        if (!std::isfinite(tWorldBase.P()(i))) {
            return RuntimeError(
                RuntimeErrorCode::InputInvalid,
                "T_world_base.P：分量(" + std::to_string(i)
                + ") 非有限，单位 m——§4.3.2 合法域");
        }
    }

    // 检查 2：正交性——max|RᵀR−I| ≤ 1×10⁻¹²（§6.6 容差；detail 携带
    // 实测最大偏差＝比较型诊断素材，§6.6"实测偏差值的比较型诊断"）。
    const double orthoDev = orthogonalityDeviation(tWorldBase.R());
    if (orthoDev > kOrthoTolerance) {
        return RuntimeError(
            RuntimeErrorCode::InputInvalid,
            "T_world_base.R：非正交——max|RᵀR−I| 实测 "
            + num(orthoDev) + " 超容差 1e-12（§6.6；非法旋转/单位错误"
            "在 reader 侧拒绝后此处为编译链复核面）");
    }

    // 检查 3：非反射——正交矩阵 det＝±1，det＜0 即反射变换（行列式
    // −1 的"旋转"会把手性翻转，§4.3.2"det＝+1"的判定面；RT-BW-6
    // 反射矩阵反例的拦截点）。
    const double det = determinant(tWorldBase.R());
    if (det < 0.0) {
        return RuntimeError(
            RuntimeErrorCode::InputInvalid,
            "T_world_base.R：反射矩阵（det 实测 " + num(det)
            + "＜0）——§4.3.2 合法域 det=+1（RT-BW-6 反例面）");
    }

    return std::nullopt;
}

std::optional<RuntimeError> checkPresetConsistency(
    InstallationPresetToken preset,
    const rw::math::Transform3D<double>& tWorldBase)
{
    // 前置说明（见头注释）：调用方须先过 checkWorldBaseTransform——
    // 本函数只在矩阵层合法的前提下做"来源记录 vs 实际矩阵"的一致性层。
    const rw::math::Rotation3D<double> r = tWorldBase.R();

    switch (preset) {
    case InstallationPresetToken::Inverted:
    case InstallationPresetToken::Wall: {
        // 固定预设：R 必须与 §6.2 冻结矩阵逐元素一致（§4.3.2 合法域
        // "inverted 且 R＝R_x(π)"；Wall 为精确定义表的对称承接）。判定
        // 用 1×10⁻¹² 同源容差——预设路径产出的矩阵本就精确 {0,±1}，
        // 容差只吸收浮点表示误差，不放宽语义。这也覆盖 §6.2 括号规则
        // "preset≠ground 而 R＝I→InputInvalid"（Inverted/Wall 声明下
        // 的 I 矩阵必然失配被拒）。
        const rw::math::Rotation3D<double> want = installationPresetRotation(preset);
        const double dev = maxRotationDeviation(r, want);
        if (dev > kPresetMatchTolerance) {
            return RuntimeError(
                RuntimeErrorCode::InputInvalid,
                "world.installPreset：声明的预设与 T_world_base.R 不一致——"
                "实测最大元素偏差 " + num(dev) + " 超容差 1e-12（§6.2 S5 "
                "构造校验；§4.3.2 合法域）");
        }
        return std::nullopt;
    }
    case InstallationPresetToken::Custom: {
        // Custom：R 不得为恒等阵（§4.3.2 明示"preset=Custom 而 R=I
        // 校验失败"；恒等判定逐元素 1×10⁻¹²——与 CanonicalModel.cpp
        // builder 的 Custom 面同源同值，两处语义一致）。
        bool identity = true;
        for (int i = 0; i < 3 && identity; ++i) {
            for (int j = 0; j < 3; ++j) {
                const double want = (i == j) ? 1.0 : 0.0;
                if (std::fabs(r(i, j) - want) > kPresetMatchTolerance) {
                    identity = false;
                    break;
                }
            }
        }
        if (identity) {
            return RuntimeError(
                RuntimeErrorCode::InputInvalid,
                "world.installPreset：preset=Custom 而 T_world_base.R≈I"
                "（§4.3.2 一致性校验失败——Custom 必须携带非恒等旋转）");
        }
        return std::nullopt;
    }
    case InstallationPresetToken::Ground:
        // Ground：无一致性约束（§6.2 括号规则只覆盖"preset≠ground"侧；
        // Ground 兼作未显式配置的默认解释——V15-04，R 恒为合法域产物，
        // 不扩大语义拒绝）。
        return std::nullopt;
    }
    // 全枚举 switch 后防御性兜底（同 installationPresetRotation——枚举
    // 与表失同步属实现缺陷，fail-fast 不静默）。
    return RuntimeError(RuntimeErrorCode::InputInvalid,
                        "world.installPreset：未登记的安装预设枚举值"
                        "（实现缺陷——checkPresetConsistency 全枚举表失同步）");
}

// =====================================================================
// 反解（§6.1 反向与消费数学）。
// =====================================================================

rw::math::Transform3D<double> invertWorldBase(
    const rw::math::Transform3D<double>& tWorldBase)
{
    // 前置防御：正交性是对合可逆前提（转置≠逆当 R 非正交）——调用方
    // 契约违约 fail-fast（§3.4 错误语义：编译链 S5 已拦截的输入再以
    // 非法形态到达消费函数属违约），不产出错误值静默吞错。
    const double orthoDev = orthogonalityDeviation(tWorldBase.R());
    if (orthoDev > kOrthoTolerance) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           "invertWorldBase：输入 R 非正交（max|RᵀR−I| 实测 "
                           + num(orthoDev) + "）——调用方须先经 "
                           "checkWorldBaseTransform（§6.6 容差 1e-12）");
    }

    // R 正交 ⇒ 逆旋转＝转置（原地 inverse()——rw 头内 inline 实现，
    // 两模式语义一致）；逆平移＝−Rᵀ·t（§6.5②：R_x(π)、t=(0,0,2) 时
    // Rᵀt=(0,0,−2)、t'＝−Rᵀt＝(0,0,+2)——卡文原 "(0,0,−2)" 为笔误，
    // 修正登记见 units/runtime.md §15.4 v0.7）。
    rw::math::Rotation3D<double> rInv = tWorldBase.R();
    rInv.inverse();
    const rw::math::Vector3D<double> tInv = rotateTransposed(tWorldBase.R(),
                                                             tWorldBase.P())
                                             * (-1.0);
    return rw::math::Transform3D<double>(tInv, rInv);
}

rw::math::Vector3D<double> transformBasePointToWorld(
    const rw::math::Transform3D<double>& tWorldBase,
    const rw::math::Vector3D<double>& pBase)
{
    // p_world＝R·p_base＋t（§6.5①——正向点变换；§6.5 手算例：
    // p_base=(0.3,0.2,0.5) 经倒挂 T 得 (0.3,−0.2,1.5)）。
    return rotateVector(tWorldBase.R(), pBase) + tWorldBase.P();
}

rw::math::Vector3D<double> transformWorldPointToBase(
    const rw::math::Transform3D<double>& tWorldBase,
    const rw::math::Vector3D<double>& pWorld)
{
    // p_base＝Rᵀ·(p_world−t)——T_base_world 作用于 p_world 的直接展开
    // （§6.5②：数学上等价于先 invertWorldBase 再复合，但按逆变换公式
    // 一次遍历完成，不构造中间变换对象；§6.5 手算例：p_world 回代得
    // 与输入逐位一致的 p_base）。向量的减法为 rw 头内 Eigen 实现
    // （header-only，两模式一致）。
    return rotateTransposed(tWorldBase.R(), pWorld - tWorldBase.P());
}

rw::math::Vector3D<double> gravityToBase(
    const rw::math::Rotation3D<double>& rWorldBase,
    const rw::math::Vector3D<double>& gWorld)
{
    // g_base＝Rᵀ·g_W（§6.1 乘法表——DYN-01 唯一公式；禁止清单 2 禁止
    // "倒挂就地取反"式的基座系参数化，必须经本投影。§6.5③ 手算：倒挂
    // 时 (0,0,−9.81)→(0,0,+9.81)——重力沿 +Z_base）。
    return rotateTransposed(rWorldBase, gWorld);
}

rw::math::Transform3D<double> composeWorldBaseTcp(
    const rw::math::Transform3D<double>& tWorldBase,
    const rw::math::Transform3D<double>& tBaseTcp)
{
    // T_world_tcp＝T_world_base·T_base_tcp（§6.1；core §4.6 复合读法）：
    // 旋转 R_wc＝R_wb·R_bc，平移 t_wc＝R_wb·t_bc＋t_wb——逐元素实现，
    // 不经 rw Transform3D::operator*（其内部调 Rotation3D::multiply 外联
    // 符号，冒烟不可达；数学一致，见文件头实现要点）。
    const rw::math::Rotation3D<double> rWc = multiplyRotation(tWorldBase.R(),
                                                              tBaseTcp.R());
    const rw::math::Vector3D<double> tWc
        = rotateVector(tWorldBase.R(), tBaseTcp.P()) + tWorldBase.P();
    return rw::math::Transform3D<double>(tWc, rWc);
}

// =====================================================================
// S9 一致性检查（§6.6）。
// =====================================================================

std::optional<BaseMountDeviation> checkBaseMountConsistency(
    const rw::math::Transform3D<double>& candidate,
    const rw::math::Transform3D<double>& tWorldBase)
{
    // 第一步：逐元素比较（平移 m／旋转元素无量纲）——偏差全部分量取
    // 最大值，一次遍历同时供判定与诊断载荷使用。
    const double posDev = maxPositionDeviation(candidate.P(), tWorldBase.P());
    const double rotDev = maxRotationDeviation(candidate.R(), tWorldBase.R());

    // 一致性容差＝附录 D 第 4 项（1×10⁻⁹ m／1×10⁻⁹——§6.5④ 同尺度）：
    // 唯一写入点（§6.3）产出的 candidate 应与权威 T 逐位一致，容差仅
    // 吸收经 Frame 通道读回时的浮点表示误差。
    if (posDev <= kConsistencyTolerance && rotDev <= kConsistencyTolerance) {
        return std::nullopt;
    }

    // 第二步：不一致——探测 ≈T·T 形态（§6.6"含 ≈T·T 形态"）：某编译段/
    // 消费方把安装变换又乘一次（AT-37 反例：倒挂 R²＝I 渲染回正、重力
    // 矩符号翻转）。形态标记帮助定位"重复应用"缺陷，非 T² 形态置 false。
    const rw::math::Transform3D<double> doubleApplied
        = composeWorldBaseTcp(tWorldBase, tWorldBase);
    const bool doublePattern
        = maxPositionDeviation(candidate.P(), doubleApplied.P())
              <= kConsistencyTolerance
          && maxRotationDeviation(candidate.R(), doubleApplied.R())
                 <= kConsistencyTolerance;

    BaseMountDeviation dev;
    dev.maxPositionDeviation = posDev;
    dev.maxRotationDeviation = rotDev;
    dev.doubleAppliedPattern = doublePattern;
    return dev;
}

}  // namespace sdurws::ird::runtime
