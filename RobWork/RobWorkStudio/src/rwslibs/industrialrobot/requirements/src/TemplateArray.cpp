/**
 * @file   TemplateArray.cpp
 * @brief  ITemplateArrayService 的实现——六类工艺模板网格生成、过参考系
 *         镜像面反射（位置反射＋姿态反射共轭＋不可镜像规则处置）、四类
 *         批量阵列几何、批次确定性重算重生成与解除关联。
 *
 * 设计依据：units/requirements.md §7.1（模板）、§7.2（镜像/阵列/派生无环/
 * 删除保护）、§9.5（ITemplateArrayService 契约）、§4.7（I-REQ-10 派生不
 * 回写）、§3.4（纯函数/确定性/SI）；任务契约 tasks/foundation/WP-14-T07.json
 * acceptance 1~4。
 *
 * 确定性来源（NFR-COR-01/02）：
 *   - 生成序＝参数派生序（模板行主序、阵列构型派生序），不做重排；
 *   - 反射/平移/三角函数均为 IEEE754 确定运算（同输入跨进程同结果）；
 *   - 参数快照的数值文本化用 "%.17g"（IEEE754 double 的无损往返格式）、
 *     解析用 strtod 全消费校验——写读互逆，同进程 C locale 约定（本进程
 *     不调用 setlocale，与 canonical CSV 导出同口径）；
 *   - 冲突清单升序去重；批次内名称消歧按确定序试缀。
 *
 * 线程安全：全部函数无共享可变状态（栈上局部），可重入。
 */

#include <sdurws/ird/requirements/TemplateArray.hpp>

#include <sdurws/ird/requirements/Editor.hpp>  // RequirementWorkingSet 完整类型——regenerate 入参
#include <sdurws/ird/requirements/DiagCodes.hpp>  // T07 派生族稳定码常量（REQ-DERIVE-*——唯一书写点）

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace sdurws::ird::requirements {

namespace {

// =====================================================================
// 数值文本化（参数快照的确定性载体——%.17g 无损往返）
// =====================================================================

/// double → 无损往返文本（%.17g 对 IEEE754 double 恒可由 strtod 精确还原；
/// "%.17g" 不受 locale 数位影响面——小数点为 C locale 约定，本进程不
/// 调用 setlocale）。
std::string formatNumber(double v)
{
    // 缓冲 64 字节：%.17g 最长约 24 字符，余量充分。
    char buf[64] = {0};
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

/// 文本 → double（strtod 全消费校验——尾随垃圾即解析失败，不猜测）。
bool parseNumber(const std::string& text, double& out)
{
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size()) {
        return false;
    }
    out = v;
    return true;
}

/// 三维向量 → "a,b,c"（formatNumber 分量拼接——参数快照向量键值）。
std::string formatVec3(const rw::math::Vector3D<double>& v)
{
    return formatNumber(v[0]) + "," + formatNumber(v[1]) + "," + formatNumber(v[2]);
}

/// "a,b,c" → 三维向量（全消费——任一分量解析失败即 false）。
bool parseVec3(const std::string& text, rw::math::Vector3D<double>& out)
{
    const auto first = text.find(',');
    const auto second = text.find(',', first == std::string::npos ? 0 : first + 1);
    if (first == std::string::npos || second == std::string::npos) {
        return false;
    }
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!parseNumber(text.substr(0, first), x)
        || !parseNumber(text.substr(first + 1, second - first - 1), y)
        || !parseNumber(text.substr(second + 1), z)) {
        return false;
    }
    out = rw::math::Vector3D<double>(x, y, z);
    return true;
}

// =====================================================================
// 参数快照存取（GenerationProvenance.parameters 的键值读写）
// =====================================================================

/// 按键读参数快照（命中＝值；缺失＝nullopt）。
std::optional<std::string> paramValue(const GenerationProvenance& gen,
                                      std::string_view key)
{
    for (const auto& kv : gen.parameters) {
        if (kv.first == key) {
            return kv.second;
        }
    }
    return std::nullopt;
}

/// 快照内全部 "source-id" 参数的源 ObjectId 清单（任一非法文本＝nullopt
/// ——快照字节受损属实现缺陷面，调用方按错误处置）。
std::optional<std::vector<core::ObjectId>> sourceIdsOf(const GenerationProvenance& gen)
{
    std::vector<core::ObjectId> ids;
    for (const auto& kv : gen.parameters) {
        if (kv.first == kGenParamSourceId) {
            auto id = core::ObjectId::tryFromCanonical(kv.second);
            if (!id) {
                return std::nullopt;  // 非法 ObjectId 文本——快照损坏
            }
            ids.push_back(std::move(*id));
        }
    }
    return ids;
}

/// 参考系 → 文本（与导入通道 ref_frame 文本语法同源——Import v0.4 ⑦：
/// World | model:<oid> | scene:<oid>；参数快照与重解析共用）。
std::string refFrameToText(const RequirementReference& ref)
{
    switch (ref.kind) {
    case RequirementRefKind::World: return "World";
    case RequirementRefKind::ModelFrame:
        return "model:" + (ref.objectId ? ref.objectId->toCanonical() : std::string{});
    case RequirementRefKind::SceneObject:
        return "scene:" + (ref.objectId ? ref.objectId->toCanonical() : std::string{});
    default: return "World";  // 不可达——镜像面/模板参考系仅场景词表（构造边界核对）
    }
}

/// 文本 → 参考系（refFrameToText 的逆；词表外＝nullopt）。
std::optional<RequirementReference> refFrameFromText(const std::string& text)
{
    RequirementReference ref;
    if (text == "World") {
        return ref;  // kind=World 缺省
    }
    const auto parseOid = [](std::string_view body) {
        return core::ObjectId::tryFromCanonical(body);
    };
    if (text.rfind("model:", 0) == 0) {
        auto id = parseOid(std::string_view{text}.substr(6));
        if (!id) {
            return std::nullopt;
        }
        ref.kind = RequirementRefKind::ModelFrame;
        ref.objectId = std::move(*id);
        return ref;
    }
    if (text.rfind("scene:", 0) == 0) {
        auto id = parseOid(std::string_view{text}.substr(6));
        if (!id) {
            return std::nullopt;
        }
        ref.kind = RequirementRefKind::SceneObject;
        ref.objectId = std::move(*id);
        return ref;
    }
    return std::nullopt;
}

// =====================================================================
// 诊断构造（C-3 工厂——context/cause/action 非空由本处字面保证；码语法
// 经 DiagData 工厂校验，违约即实现缺陷 fail-fast。subject 置空：派生
// 条目尚持编辑期临时句柄，正式 ObjectId 在命令 prepare 才分配（O-36），
// 临时句柄入诊断会随重绑悬空——与 Import.cpp 同口径，定位以条目名承载）
// =====================================================================

core::DiagnosticRecord makeDiag(std::string_view code, std::string context,
                                std::string cause, std::string action)
{
    return core::DiagnosticRecord::make(std::string{code}, std::nullopt,
                                        std::nullopt, std::nullopt,
                                        std::move(context), std::move(cause),
                                        std::move(action));
}

// =====================================================================
// 错误批次构造（err 态——参数非法值面；码用 IllegalTolerance 承载"参数
// 非法"族，域错误表 9 值内的就近承载约定，validateOrientationRule 同源）
// =====================================================================

EditBatch errorBatch(const std::string& field, const std::string& detail)
{
    EditBatch out;
    out.ok = false;
    out.error.code = RequirementErrorCode::IllegalTolerance;
    out.error.params.emplace_back("field", field);
    out.error.detail = "requirements/template-array: " + detail;
    return out;
}

/// 有限性核对（三分量均有限——NaN/Inf 参数直接拒绝，避免污染下游字节）。
bool finiteVec3(const rw::math::Vector3D<double>& v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// =====================================================================
// 反射几何（§7.2"过参考系镜像面"的数学面——全部在 refFrame 系坐标下，
// 需求数据变换、非机器人坐标变换，卡 §7.2 隔离声明）
// =====================================================================

/// 位置反射 p' = p − 2(n·p)n（n 单位化法向；反射是等距变换——|p'|=|p|）。
rw::math::Vector3D<double> reflectPosition(const rw::math::Vector3D<double>& p,
                                           const rw::math::Vector3D<double>& nUnit)
{
    const double d = p[0] * nUnit[0] + p[1] * nUnit[1] + p[2] * nUnit[2];
    return rw::math::Vector3D<double>(p[0] - 2.0 * d * nUnit[0],
                                      p[1] - 2.0 * d * nUnit[1],
                                      p[2] - 2.0 * d * nUnit[2]);
}

/// Z-Y-X 欧拉（roll,pitch,yaw，rad）→ 旋转矩阵 R = Rz(yaw)·Ry(pitch)·Rx(roll)
/// （与 RequirementTypes.hpp OrientationRule.fixedRpy 登记的应用序一致）。
void rotationFromRpy(double roll, double pitch, double yaw,
                     double r[3][3])
{
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    // R = Rz·Ry·Rx 的逐元素展开（解析式——确定性乘加序）。
    r[0][0] = cy * cp;               r[0][1] = -sy * cr + cy * sp * sr;  r[0][2] = sy * sr + cy * sp * cr;
    r[1][0] = sy * cp;               r[1][1] = cy * cr + sy * sp * sr;   r[1][2] = -cy * sr + sy * sp * cr;
    r[2][0] = -sp;                   r[2][1] = cp * sr;                  r[2][2] = cp * cr;
}

/// 反射矩阵 M = I − 2nnᵀ（n 单位化；对称且 M²=I——反射的对合性）。
void reflectionMatrix(const rw::math::Vector3D<double>& nUnit, double m[3][3])
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            m[i][j] = (i == j ? 1.0 : 0.0) - 2.0 * nUnit[i] * nUnit[j];
        }
    }
}

/// 3×3 矩阵乘 A·B（朴素三重循环——固定 3 阶，乘加序固定＝确定性）。
void matMul(const double a[3][3], const double b[3][3], double out[3][3])
{
    double tmp[3][3] = {};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) {
                s += a[i][k] * b[k][j];
            }
            tmp[i][j] = s;
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out[i][j] = tmp[i][j];
        }
    }
}

/**
 * @brief 姿态反射：R' = M·R·M（M 为镜像面反射矩阵）后提取 Z-Y-X 欧拉。
 *
 * 数学要点：反射共轭保持旋转（det(M R M)=det(R)=+1）——结果仍是纯旋转，
 * 可无损回到 Z-Y-X 参数。黄金性质（逐元素规则 R'ij = σiσj·Rij（σi=±1 为
 * 镜像对角的符号），UT 黄金断言的依据）——绕镜像面**法向轴**的旋转分量
 * 与反射对易（不变），面内两轴的旋转分量取反：
 *   - 法向 X：(roll,pitch,yaw) → (+roll, −pitch, −yaw)；
 *   - 法向 Y：(roll,pitch,yaw) → (−roll, +pitch, −yaw)；
 *   - 法向 Z：(roll,pitch,yaw) → (−roll, −pitch, +yaw)。
 * 万向锁分支（|sin pitch|→1，cp≈0）：roll 与 yaw 退化不唯一——按确定
 * 约定取 roll'=0、yaw' 由余量元素解析（重构出的 R' 与反射结果逐元素
 * 一致——欧拉表示不唯一但旋转唯一）。
 */
rw::math::Vector3D<double> reflectRpy(const rw::math::Vector3D<double>& rpy,
                                      const rw::math::Vector3D<double>& nUnit)
{
    double r[3][3] = {};
    rotationFromRpy(rpy[0], rpy[1], rpy[2], r);
    double m[3][3] = {};
    reflectionMatrix(nUnit, m);
    double mr[3][3] = {};
    matMul(m, r, mr);      // M·R
    double rp[3][3] = {};
    matMul(mr, m, rp);     // (M·R)·M = M·R·M

    // Z-Y-X 提取：sin(pitch) = −R20；一般位形 roll=atan2(R21,R22)、
    // yaw=atan2(R10,R00)。
    double sp = -rp[2][0];
    if (sp > 1.0) {
        sp = 1.0;  // 数值护栏（反射共轭的精确数学结果 |sp|≤1——浮点误差钳制）
    }
    if (sp < -1.0) {
        sp = -1.0;
    }
    constexpr double kGimbalEpsilon = 1.0 - 1e-12;  // 万向锁判定阈：cp²=1−sp²<2e-12 即进入退化分支
    if (std::fabs(sp) < kGimbalEpsilon) {
        const double pitch = std::asin(sp);
        const double roll = std::atan2(rp[2][1], rp[2][2]);
        const double yaw = std::atan2(rp[1][0], rp[0][0]);
        return rw::math::Vector3D<double>(roll, pitch, yaw);
    }
    // 万向锁退化：roll'=0、pitch'=±π/2、yaw' 取 −R12/R11 的反正切
    // （cp=0 时 R11=cy'·cr、R12=−sy'·cr（cr=roll'=0 时为 1）——重构
    // R(0,±π/2,yaw') 与 R' 逐元素相等）。
    const double pitch = (sp > 0.0 ? 3.14159265358979323846 : -3.14159265358979323846) / 2.0;
    const double roll = 0.0;
    const double yaw = std::atan2(-rp[1][2], rp[1][1]);
    return rw::math::Vector3D<double>(roll, pitch, yaw);
}

// =====================================================================
// 派生条目公共装配（镜像/阵列/重生成共用——I-REQ-10 的单点执行面）
// =====================================================================

/// 生成器 methodTag（ValueProvenance 语法 [a-z0-9./_-]——生成器标识的
/// 连字符形态；与 generatorId 的冒号形态一一对应）。
std::string methodTagOfGenerator(std::string_view generatorId)
{
    std::string tag;
    for (const char ch : generatorId) {
        tag.push_back(ch == ':' ? '-' : ch);
    }
    return tag;
}

/// 派生条目的来源标记（D-REQ-4：普通条目可继续手改——kind 恒 UserProvided，
/// methodTag 记生成器，绝不 DerivedReadOnly）。
core::ValueProvenance generatedSource(std::string_view generatorId)
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                       std::nullopt, std::nullopt,
                                       methodTagOfGenerator(generatorId));
}

/**
 * @brief 批次内名称消歧（确定性试缀）：candidate 被批内其他产物名占用
 *        时按 "-2"、"-3" …顺序试缀直到空闲。
 *
 * 说明：这只保证**批次内部**唯一（同一批内两个源名恰在加缀后相撞的
 * 边界自洽）；与工作集既有条目的冲突由编辑器集合级核对拒绝（I-REQ-3
 * ——服务无工作集视图，越界拒绝归编辑边界，文件头"草稿生成辅助"注）。
 */
std::string disambiguateName(const std::string& candidate,
                             const std::set<std::string>& takenInBatch)
{
    if (takenInBatch.find(candidate) == takenInBatch.end()) {
        return candidate;
    }
    for (int suffix = 2;; ++suffix) {
        const std::string attempt = candidate + "-" + std::to_string(suffix);
        if (takenInBatch.find(attempt) == takenInBatch.end()) {
            return attempt;
        }
    }
}

/// 参数快照公共段（kind/source-id/参考系——三类生成器共用；追加序＝
/// 登记序，保序 vector 确定性）。
void appendCommonParams(GenerationProvenance& gen, std::string_view kindToken,
                        const std::vector<core::ObjectId>& sourceIds)
{
    gen.parameters.emplace_back(kGenParamKindToken, std::string{kindToken});
    for (const auto& id : sourceIds) {
        gen.parameters.emplace_back(kGenParamSourceId, id.toCanonical());
    }
}

/// 批内条目内容等值（忽略 ObjectId 与 generation——"手改"判定的比较面；
/// 名称为空者不参与比较，由调用方前置排除）。
bool contentEqualIgnoringIdentity(const TaskPoint& a, const TaskPoint& b)
{
    TaskPoint x = a;
    TaskPoint y = b;
    x.objectId = core::ObjectId{};
    y.objectId = core::ObjectId{};
    x.generation.reset();
    y.generation.reset();
    return x == y;
}

/// 批次溯源参考系参数键值（模板/镜像参考系入快照）。
void appendRefFrameParam(GenerationProvenance& gen, std::string_view key,
                         const RequirementReference& ref)
{
    gen.parameters.emplace_back(key, refFrameToText(ref));
}

}  // namespace

// =====================================================================
// EditBatch / RegenerateOutcome 值语义（头文件声明、实现置此——与
// RequirementTypes 的域值类型同款布局）
// =====================================================================

bool EditBatch::operator==(const EditBatch& o) const
{
    return ok == o.ok && newPoints == o.newPoints && provenance == o.provenance
        && replaceNames == o.replaceNames && diagnostics == o.diagnostics
        && summary == o.summary && error.code == o.error.code
        && error.params == o.error.params && error.detail == o.error.detail;
}

bool RegenerateOutcome::operator==(const RegenerateOutcome& o) const
{
    return found == o.found && batch == o.batch && conflictNames == o.conflictNames
        && diagnostics == o.diagnostics && error.code == o.error.code
        && error.params == o.error.params && error.detail == o.error.detail;
}

// =====================================================================
// 模板设计默认（黄金数据集——§7.1"模板参数数值为设计默认"锁定表）
// =====================================================================

TemplateParams defaultTemplateParams(TemplateKind kind)
{
    // 共同默认：原点基准、World 系、ReferenceZ 进退 0.1 m、Fixed (0,0,0)
    // rad、默认容差；逐类差异仅网格规模与间距（类注黄金表）。
    TemplateParams p;
    p.refFrame = RequirementReference{};  // World
    p.origin = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    p.approachAxis = SegmentAxis::ReferenceZ;
    p.approachDistanceM = 0.1;  // m
    p.fixedRpy = rw::math::Vector3D<double>(0.0, 0.0, 0.0);  // rad
    p.tolerance = ToleranceSpec{};  // 1×10⁻³ m / 1°（§4.3 设计默认）
    switch (kind) {
    case TemplateKind::BinPicking:
        p.baseName = "bin-picking";
        p.countX = 2;  p.countY = 1;
        p.spacingM = 0.5;  p.spacingYM = 0.5;  // m（Y 向间距在该构型未被消费——恒同值保持黄金形状完整）
        break;
    case TemplateKind::MachineTending:
        p.baseName = "machine-tending";
        p.countX = 3;  p.countY = 1;
        p.spacingM = 0.4;  p.spacingYM = 0.4;  // m
        break;
    case TemplateKind::Palletizing:
        p.baseName = "palletizing";
        p.countX = 2;  p.countY = 2;
        p.spacingM = 0.3;  p.spacingYM = 0.3;  // m
        break;
    case TemplateKind::Inspection:
        p.baseName = "inspection";
        p.countX = 1;  p.countY = 1;
        p.spacingM = 0.5;  p.spacingYM = 0.5;  // m
        break;
    case TemplateKind::ToolChange:
        p.baseName = "tool-change";
        p.countX = 1;  p.countY = 1;
        p.spacingM = 0.5;  p.spacingYM = 0.5;  // m
        break;
    case TemplateKind::Handover:
        p.baseName = "handover";
        p.countX = 2;  p.countY = 1;
        p.spacingM = 0.6;  p.spacingYM = 0.6;  // m
        break;
    }
    return p;
}

namespace {

/// 模板类别的默认工艺标签（ProcessTag 11 值词表内就近映射——黄金登记：
/// BinPicking→Pick、MachineTending→MachineLoad、Palletizing→Place、
/// Inspection→Inspect、ToolChange→ToolChange、Handover→Handover）。
ProcessTag defaultProcessTag(TemplateKind kind)
{
    switch (kind) {
    case TemplateKind::BinPicking: return ProcessTag::Pick;
    case TemplateKind::MachineTending: return ProcessTag::MachineLoad;
    case TemplateKind::Palletizing: return ProcessTag::Place;
    case TemplateKind::Inspection: return ProcessTag::Inspect;
    case TemplateKind::ToolChange: return ProcessTag::ToolChange;
    case TemplateKind::Handover: return ProcessTag::Handover;
    }
    return ProcessTag::Generic;  // 不可达（全枚举 switch）
}

/// 小写连字符化 kind token（"BinPicking"→"bin-picking"——generatorId 与
/// methodTag 的构造；词表 token 驼峰稳定，逐大写字母前插连字符再小写）。
std::string lowerHyphenToken(std::string_view token)
{
    std::string out;
    for (const char ch : token) {
        if (ch >= 'A' && ch <= 'Z') {
            if (!out.empty()) {
                out.push_back('-');
            }
            out.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

/// 模板生成器标识（"template:<小写连字符 token>"——类注 generatorId 词表）。
std::string templateGeneratorId(TemplateKind kind)
{
    return "template:" + lowerHyphenToken(std::string{templateKindToken(kind)});
}

/// 阵列生成器标识（"array:<小写连字符 token>"）。
std::string arrayGeneratorId(ArrayKind kind)
{
    return "array:" + lowerHyphenToken(std::string{arrayKindToken(kind)});
}

}  // namespace

// =====================================================================
// applyTemplate（§7.1）
// =====================================================================

EditBatch TemplateArrayService::applyTemplate(TemplateKind kind,
                                              const TemplateParams& params) const
{
    // ---- 参数校验（短路——首个违例即返回；字段名入 params 定位）----
    if (params.baseName.empty()) {
        return errorBatch("base-name", "模板名前缀为空（生成条目名的语义前缀"
                                        "——空前缀无定位意义）");
    }
    if (!params.refFrame.wellFormed()) {
        return errorBatch("ref-frame", "模板参考系引用结构违约（I-REQ-4 结构半区）");
    }
    if (params.countX < 1 || params.countY < 1) {
        return errorBatch("count", "模板网格计数 <1（countX/countY ≥1——至少"
                                    "生成一个工位）");
    }
    if (!std::isfinite(params.spacingM) || params.spacingM <= 0.0
        || !std::isfinite(params.spacingYM) || params.spacingYM <= 0.0) {
        return errorBatch("spacing", "模板间距非法（spacingM/spacingYM >0 且"
                                      "有限，单位 m）");
    }
    if (!std::isfinite(params.approachDistanceM) || params.approachDistanceM <= 0.0) {
        return errorBatch("approach-distance", "模板进退段距离非法（>0 且有限，"
                                                "单位 m）");
    }
    if (!finiteVec3(params.origin) || !finiteVec3(params.fixedRpy)) {
        return errorBatch("origin/fixed-rpy", "模板基准点/姿态角含非有限分量"
                                               "（m/rad——NaN/Inf 拒绝）");
    }
    if (auto e = validateTolerance(params.tolerance)) {
        return errorBatch("tolerance", "模板容差非法（I-REQ-5——两分量 >0 且"
                                        "有限）: " + e->detail);
    }

    // ---- 批次骨架（溯源先行——同批同 instanceId/generatorId）----
    const std::string generatorId = templateGeneratorId(kind);
    EditBatch out;
    out.ok = true;
    out.provenance.generatorId = generatorId;
    out.provenance.instanceId = core::ObjectId::generate().toCanonical();
    out.provenance.linked = true;
    appendCommonParams(out.provenance, templateKindToken(kind), {});
    appendRefFrameParam(out.provenance, "ref-frame", params.refFrame);
    out.provenance.parameters.emplace_back("base-name", params.baseName);
    out.provenance.parameters.emplace_back("origin", formatVec3(params.origin));
    out.provenance.parameters.emplace_back("spacing-m", formatNumber(params.spacingM));
    out.provenance.parameters.emplace_back("spacing-ym", formatNumber(params.spacingYM));
    out.provenance.parameters.emplace_back("count-x", std::to_string(params.countX));
    out.provenance.parameters.emplace_back("count-y", std::to_string(params.countY));
    out.provenance.parameters.emplace_back("approach-axis",
                                           std::string{segmentAxisToken(params.approachAxis)});
    out.provenance.parameters.emplace_back("approach-distance-m",
                                           formatNumber(params.approachDistanceM));
    out.provenance.parameters.emplace_back("fixed-rpy", formatVec3(params.fixedRpy));
    out.provenance.parameters.emplace_back("tol-pos",
                                           formatNumber(params.tolerance.positionTolerance));
    out.provenance.parameters.emplace_back("tol-ori",
                                           formatNumber(params.tolerance.orientationTolerance));

    // ---- 网格派生（行主序：row 1..countY 外层、col 1..countX 内层——
    //      生成序确定性；名称 r<行>c<列> 后缀无歧义编码网格坐标）----
    const core::ValueProvenance src = generatedSource(generatorId);
    std::set<std::string> taken;  // 批内已用名（消歧基准）
    for (int row = 1; row <= params.countY; ++row) {
        for (int col = 1; col <= params.countX; ++col) {
            TaskPoint pt;
            pt.objectId = core::ObjectId::generate();  // 编辑期临时句柄（O-36——正式分配归命令 prepare）
            pt.name = disambiguateName(
                params.baseName + "-r" + std::to_string(row) + "c" + std::to_string(col),
                taken);
            taken.insert(pt.name);
            pt.processTag = defaultProcessTag(kind);
            pt.level = RequirementLevel::Must;
            pt.enabled = true;
            pt.source = src;
            pt.generation = out.provenance;
            pt.refFrame = params.refFrame;
            // 位置＝基准点＋X 向 (col−1)·spacingM＋Y 向 (row−1)·spacingYM。
            pt.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
                rw::math::Vector3D<double>(
                    params.origin[0] + static_cast<double>(col - 1) * params.spacingM,
                    params.origin[1] + static_cast<double>(row - 1) * params.spacingYM,
                    params.origin[2]),
                src);
            // 黄金默认：模板工位六分量全约束（拾放类工位的完整位姿约束——
            // 满足 I-REQ-5 至少一真，且语义上是"标准工位"的保守约束面）。
            pt.pose.constrainedDof.x = true;
            pt.pose.constrainedDof.y = true;
            pt.pose.constrainedDof.z = true;
            pt.pose.constrainedDof.roll = true;
            pt.pose.constrainedDof.pitch = true;
            pt.pose.constrainedDof.yaw = true;
            pt.pose.orientation.kind = OrientationRuleKind::Fixed;
            pt.pose.orientation.fixedRpy = params.fixedRpy;
            pt.tolerance = params.tolerance;
            // 进退段按参数轴；作业段恒启用（§4.3——占位距离 1.0 m 同
            // createPoint 约定，不进工程判定）。
            pt.approach = TaskSegment{true, params.approachAxis, params.approachDistanceM};
            pt.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
            pt.retract = TaskSegment{true, params.approachAxis, params.approachDistanceM};
            out.newPoints.push_back(std::move(pt));
        }
    }
    out.summary = "工艺模板 " + std::string{templateKindToken(kind)} + " 生成 "
                + std::to_string(out.newPoints.size()) + " 个工位（批次 "
                + out.provenance.instanceId + "）";
    return out;
}

// =====================================================================
// applyMirror（§7.2——位置反射＋姿态反射共轭＋不可镜像规则处置）
// =====================================================================

namespace {

/// 单条任务点的镜像派生（applyMirror 主体内联步；规则处置与警告产码
/// 集中于此——§7.2 逐规则语义的单点实现）。
TaskPoint mirrorPoint(const TaskPoint& src, const rw::math::Vector3D<double>& nUnit,
                      const GenerationProvenance& provenance,
                      const core::ValueProvenance& srcTag,
                      std::vector<core::DiagnosticRecord>& diags,
                      const std::set<std::string>& takenInBatch)
{
    TaskPoint d = src;  // 同构字段起底（容差/三段/level/enabled/processTag/demands 原样——§7.2"同构字段"）
    d.objectId = core::ObjectId::generate();  // 独立 ObjectId（I-REQ-10——产物为普通独立条目）
    d.name = disambiguateName(src.name + "-M", takenInBatch);
    // 派生条目不入顺序链（sequenceKey 是"前驱条目名"引用——原样继承会
    // 与源条目形成同前驱重复键，R7 Blocking；顺序由用户派生后显式编排）。
    d.sequenceKey.reset();
    // 导入溯源不继承（它是"该值来自文件 X 行 Y"的事实记录——派生条目的
    // 值来自镜像变换，沿用会失真；溯源由 generation 参数快照承载）。
    d.importProvenance.reset();
    d.source = srcTag;
    d.generation = provenance;

    // ---- 位置反射（Provided 才变换——MDL-06 缺失不转零；反射等距，
    //      非零位置不会反射为零向量）----
    if (d.pose.position.state() == core::FieldState::Provided) {
        d.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            reflectPosition(d.pose.position.value(), nUnit), srcTag);
    }

    // ---- 姿态规则处置（§7.2 原文逐规则）----
    switch (d.pose.orientation.kind) {
    case OrientationRuleKind::Fixed:
        // 姿态反射共轭（黄金性质见 reflectRpy 注）。
        d.pose.orientation.fixedRpy = reflectRpy(d.pose.orientation.fixedRpy, nUnit);
        break;
    case OrientationRuleKind::PointAtTarget:
        // 目标点正常镜像（反射等距——非零目标仍非零，validateOrientationRule 保持通过）。
        d.pose.orientation.targetPoint =
            reflectPosition(d.pose.orientation.targetPoint, nUnit);
        break;
    case OrientationRuleKind::ToolRollFree: {
        // 滚转区间翻转（反射反手性：共轭把绕轴角 φ 映为 −φ，故
        // [min,max] → [−max,−min]；对称区间 [−π,π] 不变）。临时变量
        // 承接原值——顺序赋值会因覆盖丢失原 min。
        const double flippedMin = -src.pose.orientation.rollRange.max;
        const double flippedMax = -src.pose.orientation.rollRange.min;
        d.pose.orientation.rollRange.min = flippedMin;
        d.pose.orientation.rollRange.max = flippedMax;
        break;
    }
    case OrientationRuleKind::AlignFrame:
    case OrientationRuleKind::AlignGeometryNormal:
        // 不可镜像规则：引用目标在镜像侧不存在（镜像只派生需求条目，
        // 目标对象不随镜像创建）——**保留规则原样**＋参数快照打标记＋
        // warning 诊断；不静默猜、不静默降级为 Fixed（§7.2 原文）。
        d.generation->parameters.emplace_back(kGenParamOrientationPending, "1");
        diags.push_back(makeDiag(
            kReqDeriveMirrorPending,
            "entry=" + d.name + "; rule="
                + std::string{orientationRuleKindToken(d.pose.orientation.kind)}
                + "; target="
                + (d.pose.orientation.targetSceneObject
                       ? d.pose.orientation.targetSceneObject->toCanonical()
                       : (d.pose.orientation.targetFrame.objectId
                              ? d.pose.orientation.targetFrame.objectId->toCanonical()
                              : std::string{"-"})),
            "姿态规则引用的目标在镜像侧不存在（镜像派生不含目标对象本身）"
            "——规则已保留，标记待人工处理",
            "在镜像侧人工指定新目标（拾取/对齐）或显式改用 Fixed 规则后"
            "消除待处理标记"));
        break;
    }
    return d;
}

}  // namespace

EditBatch TemplateArrayService::applyMirror(const std::vector<TaskPoint>& sources,
                                            const MirrorPlaneSpec& plane) const
{
    // ---- 参数校验 ----
    if (sources.empty()) {
        return errorBatch("sources", "镜像源条目为空（至少一个源）");
    }
    if (!finiteVec3(plane.axisNormal)
        || (plane.axisNormal[0] == 0.0 && plane.axisNormal[1] == 0.0
            && plane.axisNormal[2] == 0.0)) {
        return errorBatch("axis-normal", "镜像面法向非法（非零且有限——零法向"
                                          "不定义平面）");
    }
    if (!plane.refFrame.wellFormed()) {
        return errorBatch("ref-frame", "镜像面参考系引用结构违约（I-REQ-4 结构半区）");
    }
    for (const auto& src : sources) {
        // 同参考系表达核对：镜像面定义在 plane.refFrame，源位置坐标在
        // src.refFrame——异系反射＝跨坐标变换语义，本单元不做（卡 §7.2
        // 隔离声明），调用方须在同系内调用。
        if (!(src.refFrame == plane.refFrame)) {
            return errorBatch("ref-frame", "源条目 " + src.name
                + " 与镜像面参考系不一致（反射须同参考系表达——本单元不做"
                  "跨坐标变换，§7.2 隔离声明）");
        }
    }

    // ---- 单位化法向（除以模长——IEEE754 确定除法）----
    const double norm = std::sqrt(plane.axisNormal[0] * plane.axisNormal[0]
                                  + plane.axisNormal[1] * plane.axisNormal[1]
                                  + plane.axisNormal[2] * plane.axisNormal[2]);
    const rw::math::Vector3D<double> nUnit(plane.axisNormal[0] / norm,
                                           plane.axisNormal[1] / norm,
                                           plane.axisNormal[2] / norm);

    // ---- 批次骨架＋参数快照（平面参数全量入快照——重生成重解析面）----
    EditBatch out;
    out.ok = true;
    out.provenance.generatorId = "mirror";
    out.provenance.instanceId = core::ObjectId::generate().toCanonical();
    out.provenance.linked = true;
    std::vector<core::ObjectId> sourceIds;
    for (const auto& src : sources) {
        sourceIds.push_back(src.objectId);
    }
    appendCommonParams(out.provenance, "Mirror", sourceIds);
    appendRefFrameParam(out.provenance, "plane-ref", plane.refFrame);
    out.provenance.parameters.emplace_back("plane-normal", formatVec3(plane.axisNormal));

    const core::ValueProvenance srcTag = generatedSource("mirror");
    std::set<std::string> taken;  // 批内已用名
    for (const auto& src : sources) {
        out.newPoints.push_back(mirrorPoint(src, nUnit, out.provenance, srcTag,
                                            out.diagnostics, taken));
        taken.insert(out.newPoints.back().name);
    }
    out.summary = "工位镜像生成 " + std::to_string(out.newPoints.size())
                + " 条（批次 " + out.provenance.instanceId + "）";
    if (!out.diagnostics.empty()) {
        out.summary += "；其中 " + std::to_string(out.diagnostics.size())
                     + " 条姿态规则待人工处理（REQ-DERIVE-MIRROR-PENDING）";
    }
    return out;
}

// =====================================================================
// applyArray（§7.2——四构型）
// =====================================================================

EditBatch TemplateArrayService::applyArray(ArrayKind kind, const ArrayParams& params) const
{
    // ---- 公共校验：源非空＋源位置在场（阵列几何的定位基准）＋同系 ----
    if (params.sources.empty()) {
        return errorBatch("sources", "阵列源条目为空（至少一个源）");
    }
    for (const auto& src : params.sources) {
        if (src.pose.position.state() != core::FieldState::Provided) {
            return errorBatch("pose.position", "源条目 " + src.name
                + " 位置未提供（阵列按位置几何派生——无基准位置不可派生，"
                  "MDL-06 缺失不猜值）");
        }
    }

    // ---- 批次骨架 ----
    const std::string generatorId = arrayGeneratorId(kind);
    EditBatch out;
    out.ok = true;
    out.provenance.generatorId = generatorId;
    out.provenance.instanceId = core::ObjectId::generate().toCanonical();
    out.provenance.linked = true;
    std::vector<core::ObjectId> sourceIds;
    for (const auto& src : params.sources) {
        sourceIds.push_back(src.objectId);
    }
    appendCommonParams(out.provenance, arrayKindToken(kind), sourceIds);

    const core::ValueProvenance srcTag = generatedSource(generatorId);
    std::set<std::string> taken;  // 批内已用名

    /// 单条派生装配（位置已定；姿态规则原样继承——含 PointAtTarget 目标，
    /// 多工位指向同一目标是有意义的工艺语义，类注）。
    auto derive = [&](const TaskPoint& src, const rw::math::Vector3D<double>& pos,
                      const std::string& suffix) {
        TaskPoint d = src;
        d.objectId = core::ObjectId::generate();
        d.name = disambiguateName(src.name + suffix, taken);
        taken.insert(d.name);
        d.sequenceKey.reset();        // 顺序链不入派生（同 applyMirror 注——R7 重复键防护）
        d.importProvenance.reset();   // 导入溯源不继承（同 applyMirror 注）
        d.source = srcTag;
        d.generation = out.provenance;
        d.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            pos, srcTag);
        out.newPoints.push_back(std::move(d));
    };

    switch (kind) {
    case ArrayKind::Linear: {
        // 校验：方向非零有限、间距 >0 有限、count ≥1。
        if (!finiteVec3(params.direction)
            || (params.direction[0] == 0.0 && params.direction[1] == 0.0
                && params.direction[2] == 0.0)) {
            return errorBatch("direction", "线性方向非法（非零且有限）");
        }
        if (!std::isfinite(params.spacingM) || params.spacingM <= 0.0) {
            return errorBatch("spacing", "线性间距非法（>0 且有限，单位 m）");
        }
        if (params.count < 1) {
            return errorBatch("count", "线性数量非法（≥1）");
        }
        // 参数快照（本构型字段子集——重生成重解析面）。
        out.provenance.parameters.emplace_back("direction", formatVec3(params.direction));
        out.provenance.parameters.emplace_back("spacing-m", formatNumber(params.spacingM));
        out.provenance.parameters.emplace_back("count", std::to_string(params.count));

        // 单位化方向（确定除法）；第 i 条＝源位置＋dir·(spacing·i)。
        const double norm = std::sqrt(params.direction[0] * params.direction[0]
                                      + params.direction[1] * params.direction[1]
                                      + params.direction[2] * params.direction[2]);
        const rw::math::Vector3D<double> dirU(params.direction[0] / norm,
                                              params.direction[1] / norm,
                                              params.direction[2] / norm);
        for (const auto& src : params.sources) {
            for (int i = 1; i <= params.count; ++i) {
                const rw::math::Vector3D<double> p = src.pose.position.value();
                derive(src,
                       rw::math::Vector3D<double>(
                           p[0] + dirU[0] * (params.spacingM * static_cast<double>(i)),
                           p[1] + dirU[1] * (params.spacingM * static_cast<double>(i)),
                           p[2] + dirU[2] * (params.spacingM * static_cast<double>(i))),
                       "-A" + std::to_string(i));
            }
        }
        break;
    }
    case ArrayKind::Rectangular: {
        // 校验：两方向非零有限、两间距 >0 有限、count/count2 ≥1。
        const auto validDir = [](const rw::math::Vector3D<double>& v) {
            return finiteVec3(v) && !(v[0] == 0.0 && v[1] == 0.0 && v[2] == 0.0);
        };
        if (!validDir(params.direction) || !validDir(params.direction2)) {
            return errorBatch("direction", "矩形方向非法（两方向均非零且有限）");
        }
        if (!std::isfinite(params.spacingM) || params.spacingM <= 0.0
            || !std::isfinite(params.spacing2M) || params.spacing2M <= 0.0) {
            return errorBatch("spacing", "矩形间距非法（两向均 >0 且有限，单位 m）");
        }
        if (params.count < 1 || params.count2 < 1) {
            return errorBatch("count", "矩形数量非法（count/count2 ≥1）");
        }
        out.provenance.parameters.emplace_back("direction", formatVec3(params.direction));
        out.provenance.parameters.emplace_back("spacing-m", formatNumber(params.spacingM));
        out.provenance.parameters.emplace_back("count", std::to_string(params.count));
        out.provenance.parameters.emplace_back("direction2", formatVec3(params.direction2));
        out.provenance.parameters.emplace_back("spacing2-m", formatNumber(params.spacing2M));
        out.provenance.parameters.emplace_back("count2", std::to_string(params.count2));

        const auto unitize = [](const rw::math::Vector3D<double>& v) {
            const double n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            return rw::math::Vector3D<double>(v[0] / n, v[1] / n, v[2] / n);
        };
        const rw::math::Vector3D<double> u1 = unitize(params.direction);
        const rw::math::Vector3D<double> u2 = unitize(params.direction2);
        // (i,j) 行主序：i=1..count 主向、j=1..count2 第二向——生成序确定。
        for (const auto& src : params.sources) {
            const rw::math::Vector3D<double> p = src.pose.position.value();
            for (int i = 1; i <= params.count; ++i) {
                for (int j = 1; j <= params.count2; ++j) {
                    derive(src,
                           rw::math::Vector3D<double>(
                               p[0] + u1[0] * (params.spacingM * static_cast<double>(i))
                                    + u2[0] * (params.spacing2M * static_cast<double>(j)),
                               p[1] + u1[1] * (params.spacingM * static_cast<double>(i))
                                    + u2[1] * (params.spacing2M * static_cast<double>(j)),
                               p[2] + u1[2] * (params.spacingM * static_cast<double>(i))
                                    + u2[2] * (params.spacing2M * static_cast<double>(j))),
                           "-A" + std::to_string(i) + "x" + std::to_string(j));
                }
            }
        }
        break;
    }
    case ArrayKind::Circular: {
        // 校验：半径 >0 有限、角步距非零有限（0 步距＝全同位点重名，拒绝）、
        // count ≥1、圆心有限。
        if (!finiteVec3(params.center)) {
            return errorBatch("center", "圆心含非有限分量（单位 m）");
        }
        if (!std::isfinite(params.radiusM) || params.radiusM <= 0.0) {
            return errorBatch("radius", "圆半径非法（>0 且有限，单位 m）");
        }
        if (!std::isfinite(params.startAngleRad) || !std::isfinite(params.angleStepRad)
            || params.angleStepRad == 0.0) {
            return errorBatch("angle-step", "角步距非法（非零且有限，单位 rad）");
        }
        if (params.count < 1) {
            return errorBatch("count", "圆形数量非法（≥1）");
        }
        out.provenance.parameters.emplace_back("center", formatVec3(params.center));
        out.provenance.parameters.emplace_back("radius-m", formatNumber(params.radiusM));
        out.provenance.parameters.emplace_back("start-angle-rad",
                                               formatNumber(params.startAngleRad));
        out.provenance.parameters.emplace_back("angle-step-rad",
                                               formatNumber(params.angleStepRad));
        out.provenance.parameters.emplace_back("count", std::to_string(params.count));

        // 第 i 条位于 refFrame 系 XY 平面圆上（法向 Z——ArrayParams 类注）：
        // θᵢ = start + step·i，位置＝center＋radius·(cosθᵢ, sinθᵢ, 0)。
        for (const auto& src : params.sources) {
            for (int i = 1; i <= params.count; ++i) {
                const double theta =
                    params.startAngleRad + params.angleStepRad * static_cast<double>(i);
                derive(src,
                       rw::math::Vector3D<double>(
                           params.center[0] + params.radiusM * std::cos(theta),
                           params.center[1] + params.radiusM * std::sin(theta),
                           params.center[2]),
                       "-A" + std::to_string(i));
            }
        }
        break;
    }
    case ArrayKind::Polyline: {
        // 校验：顶点 ≥2 且均有限、相邻点不重合、间距 >0；条数由总弧长
        // 与间距派生（floor——"数量"不由调用方直指）。
        if (params.polyline.size() < 2) {
            return errorBatch("polyline", "折线顶点不足（≥2 点）");
        }
        for (const auto& v : params.polyline) {
            if (!finiteVec3(v)) {
                return errorBatch("polyline", "折线顶点含非有限分量（单位 m）");
            }
        }
        for (std::size_t i = 1; i < params.polyline.size(); ++i) {
            const rw::math::Vector3D<double> d = params.polyline[i] - params.polyline[i - 1];
            if (d[0] == 0.0 && d[1] == 0.0 && d[2] == 0.0) {
                return errorBatch("polyline", "折线相邻顶点重合（第 "
                    + std::to_string(i) + " 段长度为零——弧长参数化无定义）");
            }
        }
        if (!std::isfinite(params.polylineSpacingM) || params.polylineSpacingM <= 0.0) {
            return errorBatch("polyline-spacing", "折线间距非法（>0 且有限，单位 m）");
        }
        // 累计弧长表（分段欧氏长度——IEEE754 确定运算）。
        std::vector<double> cumulative(params.polyline.size(), 0.0);
        for (std::size_t i = 1; i < params.polyline.size(); ++i) {
            const rw::math::Vector3D<double> d = params.polyline[i] - params.polyline[i - 1];
            const double seg = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            cumulative[i] = cumulative[i - 1] + seg;
        }
        const double total = cumulative.back();
        out.provenance.parameters.emplace_back(
            "polyline", [&] {
                std::string s;
                for (std::size_t i = 0; i < params.polyline.size(); ++i) {
                    if (i > 0) {
                        s += ";";
                    }
                    s += formatVec3(params.polyline[i]);
                }
                return s;
            }());
        out.provenance.parameters.emplace_back("polyline-spacing-m",
                                               formatNumber(params.polylineSpacingM));

        // sₖ = spacing·k（k=1..floor(total/spacing)）处的弧长定位：走段
        // 查找——s 落在第 seg 段（cumulative[seg−1] ≤ s ≤ cumulative[seg]）
        // 内按段内线性插值；恰在顶点时取顶点（插值系数 1 的退化形态）。
        const auto locate = [&cumulative, &params](double s) {
            for (std::size_t seg = 1; seg < params.polyline.size(); ++seg) {
                if (s <= cumulative[seg] || seg + 1 == params.polyline.size()) {
                    const double segStart = cumulative[seg - 1];
                    const double segLen = cumulative[seg] - segStart;
                    // t∈[0,1]——段内比例；segLen>0 由"相邻顶点不重合"校验保证。
                    const double t = (s - segStart) / segLen;
                    const rw::math::Vector3D<double> a = params.polyline[seg - 1];
                    const rw::math::Vector3D<double> b = params.polyline[seg];
                    return rw::math::Vector3D<double>(
                        a[0] + (b[0] - a[0]) * t,
                        a[1] + (b[1] - a[1]) * t,
                        a[2] + (b[2] - a[2]) * t);
                }
            }
            return params.polyline.back();  // 不可达（s ≤ total 由循环界保证）
        };
        const long long nPoints =
            static_cast<long long>(std::floor(total / params.polylineSpacingM));
        if (nPoints < 1) {
            return errorBatch("polyline-spacing",
                              "折线间距大于总弧长（总弧长 " + formatNumber(total)
                                  + " m——零产物，拒绝空批次）");
        }
        for (const auto& src : params.sources) {
            for (long long k = 1; k <= nPoints; ++k) {
                derive(src, locate(params.polylineSpacingM * static_cast<double>(k)),
                       "-A" + std::to_string(k));
            }
        }
        break;
    }
    }

    out.summary = "阵列 " + std::string{arrayKindToken(kind)} + " 生成 "
                + std::to_string(out.newPoints.size()) + " 条（批次 "
                + out.provenance.instanceId + "）";
    return out;
}

// =====================================================================
// regenerate（§9.5——确定性重算＋逐名称配对＋冲突不静默覆盖）
// =====================================================================

namespace {

/// 参数快照 → 模板参数重建（applyTemplate 快照键的全量逆——键缺失/解析
/// 失败＝nullopt，重生成拒绝不猜测）。
std::optional<TemplateParams> templateParamsOf(const GenerationProvenance& gen)
{
    TemplateParams p;
    const auto need = [&](std::string_view key) -> std::optional<std::string> {
        return paramValue(gen, key);
    };
    auto refText = need("ref-frame");
    if (!refText) {
        return std::nullopt;
    }
    auto ref = refFrameFromText(*refText);
    if (!ref) {
        return std::nullopt;
    }
    p.refFrame = *ref;
    const auto base = need("base-name");
    if (!base || base->empty()) {
        return std::nullopt;
    }
    p.baseName = *base;
    const auto origin = need("origin");
    if (!origin || !parseVec3(*origin, p.origin)) {
        return std::nullopt;
    }
    const auto sp = need("spacing-m");
    if (!sp || !parseNumber(*sp, p.spacingM)) {
        return std::nullopt;
    }
    const auto spy = need("spacing-ym");
    if (!spy || !parseNumber(*spy, p.spacingYM)) {
        return std::nullopt;
    }
    const auto cx = need("count-x");
    const auto cy = need("count-y");
    if (!cx || !cy) {
        return std::nullopt;
    }
    p.countX = std::atoi(cx->c_str());
    p.countY = std::atoi(cy->c_str());
    const auto axis = need("approach-axis");
    if (!axis) {
        return std::nullopt;
    }
    auto axisKind = trySegmentAxis(*axis);
    if (!axisKind) {
        return std::nullopt;
    }
    p.approachAxis = *axisKind;
    const auto ad = need("approach-distance-m");
    if (!ad || !parseNumber(*ad, p.approachDistanceM)) {
        return std::nullopt;
    }
    const auto rpy = need("fixed-rpy");
    if (!rpy || !parseVec3(*rpy, p.fixedRpy)) {
        return std::nullopt;
    }
    const auto tp = need("tol-pos");
    const auto to = need("tol-ori");
    if (!tp || !to || !parseNumber(*tp, p.tolerance.positionTolerance)
        || !parseNumber(*to, p.tolerance.orientationTolerance)) {
        return std::nullopt;
    }
    return p;
}

/// 参数快照 → 镜像面重建（plane-ref/plane-normal 全量逆）。
std::optional<MirrorPlaneSpec> mirrorPlaneOf(const GenerationProvenance& gen)
{
    MirrorPlaneSpec plane;
    const auto refText = paramValue(gen, "plane-ref");
    if (!refText) {
        return std::nullopt;
    }
    auto ref = refFrameFromText(*refText);
    if (!ref) {
        return std::nullopt;
    }
    plane.refFrame = *ref;
    const auto normal = paramValue(gen, "plane-normal");
    if (!normal || !parseVec3(*normal, plane.axisNormal)) {
        return std::nullopt;
    }
    return plane;
}

/// 参数快照 → 阵列参数重建（按 kind 读各自字段子集；sources 由调用方
/// 回填——此处只重建标量/向量面）。
std::optional<ArrayParams> arrayParamsOf(ArrayKind kind, const GenerationProvenance& gen)
{
    ArrayParams p;
    const auto num = [&](std::string_view key, double& out) {
        const auto v = paramValue(gen, key);
        return v && parseNumber(*v, out);
    };
    const auto vec = [&](std::string_view key, rw::math::Vector3D<double>& out) {
        const auto v = paramValue(gen, key);
        return v && parseVec3(*v, out);
    };
    const auto integer = [&](std::string_view key, int& out) {
        const auto v = paramValue(gen, key);
        if (!v) {
            return false;
        }
        out = std::atoi(v->c_str());
        return true;
    };
    switch (kind) {
    case ArrayKind::Linear:
        if (!vec("direction", p.direction) || !num("spacing-m", p.spacingM)
            || !integer("count", p.count)) {
            return std::nullopt;
        }
        break;
    case ArrayKind::Rectangular:
        if (!vec("direction", p.direction) || !num("spacing-m", p.spacingM)
            || !integer("count", p.count) || !vec("direction2", p.direction2)
            || !num("spacing2-m", p.spacing2M) || !integer("count2", p.count2)) {
            return std::nullopt;
        }
        break;
    case ArrayKind::Circular:
        if (!vec("center", p.center) || !num("radius-m", p.radiusM)
            || !num("start-angle-rad", p.startAngleRad)
            || !num("angle-step-rad", p.angleStepRad) || !integer("count", p.count)) {
            return std::nullopt;
        }
        break;
    case ArrayKind::Polyline: {
        const auto line = paramValue(gen, "polyline");
        if (!line) {
            return std::nullopt;
        }
        // 顶点串按 ";" 分段逐点解析（formatVec3 的 ";" 连接逆）。
        std::vector<std::string> parts;
        std::string::size_type begin = 0;
        while (true) {
            const auto pos = line->find(';', begin);
            parts.push_back(line->substr(begin, pos == std::string::npos
                                                   ? std::string::npos
                                                   : pos - begin));
            if (pos == std::string::npos) {
                break;
            }
            begin = pos + 1;
        }
        for (const auto& part : parts) {
            rw::math::Vector3D<double> v(0.0, 0.0, 0.0);
            if (!parseVec3(part, v)) {
                return std::nullopt;
            }
            p.polyline.push_back(v);
        }
        if (!num("polyline-spacing-m", p.polylineSpacingM)) {
            return std::nullopt;
        }
        break;
    }
    }
    return p;
}

}  // namespace

RegenerateOutcome TemplateArrayService::regenerate(
    const RequirementWorkingSet& ws, const std::string& generatorInstanceId) const
{
    RegenerateOutcome out;
    if (generatorInstanceId.empty()) {
        out.error.code = RequirementErrorCode::MalformedPayload;
        out.error.detail = "requirements/template-array: 批次实例标识为空";
        return out;
    }

    // ---- ①定位批次：工作集点集内同 instanceId 的全部条目（溯源快照是
    //      批次在数据内的唯一存在形态——没有独立的"批次登记表"）。
    std::vector<const TaskPoint*> batchEntries;
    for (const auto& p : ws.points.entries) {
        if (p.generation.has_value() && p.generation->instanceId == generatorInstanceId) {
            batchEntries.push_back(&p);
        }
    }
    if (batchEntries.empty()) {
        out.error.code = RequirementErrorCode::MalformedPayload;
        out.error.params.emplace_back("instance", generatorInstanceId);
        out.error.detail = "requirements/template-array: 批次实例未命中（工作集"
                           "内无该 instanceId 的派生条目——已全部删除或标识有误）";
        return out;
    }
    const GenerationProvenance& gen = *batchEntries.front()->generation;

    // ---- ②确定性重算：按 generatorId 分发回对应生成入口（参数快照全量
    //      逆→重 derive）。源条目自快照 "source-id" 在工作集内重解析——
    //      已删除的源使重算不可进行（不静默跳过、不静默部分重生成）。
    auto sourcesFrom = [&](const GenerationProvenance& g,
                           std::vector<TaskPoint>& sources) -> bool {
        auto ids = sourceIdsOf(g);
        if (!ids) {
            return false;
        }
        for (const auto& id : *ids) {
            const auto it = std::find_if(ws.points.entries.begin(), ws.points.entries.end(),
                                         [&id](const TaskPoint& p) { return p.objectId == id; });
            if (it == ws.points.entries.end()) {
                out.error.code = RequirementErrorCode::MalformedPayload;
                out.error.params.emplace_back("source-id", id.toCanonical());
                out.error.detail = "requirements/template-array: 源条目已删除"
                                   "（批次重算不可进行——删除源仅提示不阻断，"
                                   "但重生成需要源值在场；可先解除关联）";
                return false;
            }
            sources.push_back(*it);
        }
        return true;
    };

    EditBatch batch;
    if (gen.generatorId.rfind("template:", 0) == 0) {
        // 模板批次：kind 取自参数快照（generatorId 冒号后缀仅是人读面——
        // 快照 "kind" 键为机器权威）。
        const auto kindToken = paramValue(gen, kGenParamKindToken);
        auto kind = kindToken ? tryTemplateKind(*kindToken) : std::nullopt;
        auto params = kind ? templateParamsOf(gen) : std::nullopt;
        if (!kind || !params) {
            out.error.code = RequirementErrorCode::MalformedPayload;
            out.error.detail = "requirements/template-array: 模板批次参数快照"
                               "损坏（重算不可进行）";
            return out;
        }
        batch = applyTemplate(*kind, *params);
    } else if (gen.generatorId == "mirror") {
        auto plane = mirrorPlaneOf(gen);
        std::vector<TaskPoint> sources;
        const bool sourcesResolved = plane.has_value() && sourcesFrom(gen, sources);
        if (!plane) {
            out.error.code = RequirementErrorCode::MalformedPayload;
            out.error.detail = "requirements/template-array: 镜像批次参数快照损"
                               "坏（重算不可进行）";
            return out;
        }
        if (!sourcesResolved) {
            return out;  // 源已删除——sourcesFrom 已置定位错误（不静默部分重生成）
        }
        batch = applyMirror(sources, *plane);
    } else if (gen.generatorId.rfind("array:", 0) == 0) {
        const auto kindToken = paramValue(gen, kGenParamKindToken);
        auto kind = kindToken ? tryArrayKind(*kindToken) : std::nullopt;
        auto params = kind ? arrayParamsOf(*kind, gen) : std::nullopt;
        std::vector<TaskPoint> sources;
        if (!kind || !params || !sourcesFrom(gen, sources)) {
            if (out.error.detail.empty()) {
                out.error.code = RequirementErrorCode::MalformedPayload;
                out.error.detail = "requirements/template-array: 阵列批次参数快"
                                   "照损坏（重算不可进行）";
            }
            return out;
        }
        params->sources = std::move(sources);
        batch = applyArray(*kind, *params);
    } else {
        out.error.code = RequirementErrorCode::MalformedPayload;
        out.error.params.emplace_back("generator", gen.generatorId);
        out.error.detail = "requirements/template-array: 未知生成器标识（快照"
                           "损坏或非本服务产物）";
        return out;
    }
    if (!batch.ok) {
        // 重算出的参数未能再通过生成校验＝快照与校验面漂移（实现缺陷面
        // ——按错误值面上抛，不静默吞）。
        out.error = batch.error;
        return out;
    }
    // 沿用原批次身份（重生成＝同一批次的一次重放——instanceId/溯源参数
    // 与被替换条目一致，仅条目值刷新）。
    batch.provenance = gen;
    for (auto& p : batch.newPoints) {
        p.generation = gen;
    }

    // ---- ③逐名称配对：候选（重算产物，名唯一）↔ 批内 linked 条目。
    //      内容等值（忽略 ObjectId/generation）→该候选进入替换面；不等值
    //      /无同名候选→手改（冲突，原条目保留）。linked=false 条目不参与
    //      （§4.3）。batch.newPoints 收窄为**替换候选**——与手改保留条目
    //      同名的候选不进批次（否则编辑器集合级名称核对会整批拒绝）。
    std::set<std::string> conflictSet;  // 去重（同名不可能——集合唯一，防御性去重）
    std::vector<TaskPoint> replacements;
    for (const TaskPoint* entry : batchEntries) {
        if (!entry->generation->linked) {
            continue;  // 已解除关联——重生成不再替换
        }
        const auto cand = std::find_if(batch.newPoints.begin(), batch.newPoints.end(),
                                       [&entry](const TaskPoint& c) {
                                           return c.name == entry->name;
                                       });
        if (cand != batch.newPoints.end()
            && contentEqualIgnoringIdentity(*entry, *cand)) {
            batch.replaceNames.push_back(entry->name);  // 未手改→替换
            replacements.push_back(*cand);
        } else {
            conflictSet.insert(entry->name);  // 手改（含改名后无法配对）
        }
    }
    batch.newPoints = std::move(replacements);
    std::sort(batch.replaceNames.begin(), batch.replaceNames.end());
    out.conflictNames.assign(conflictSet.begin(), conflictSet.end());
    std::sort(out.conflictNames.begin(), out.conflictNames.end());

    // ---- ④冲突诊断（REQ-DERIVE-REGENERATE-CONFLICT，warning 逐条——
    //      不静默覆盖，§9.5 行原文）。
    for (const auto& name : out.conflictNames) {
        out.diagnostics.push_back(makeDiag(
            kReqDeriveRegenerateConflict,
            "entry=" + name + "; instance=" + generatorInstanceId,
            "linked 条目已手改（与生成器确定性重算不一致）——重生成不静默"
            "覆盖，原条目保留",
            "恢复该条目为生成值后重试重生成，或先解除关联（保留手改值）"));
    }
    batch.diagnostics = out.diagnostics;
    batch.summary = "重生成批次 " + generatorInstanceId + "：替换 "
                  + std::to_string(batch.replaceNames.size()) + " 条，手改保留 "
                  + std::to_string(out.conflictNames.size()) + " 条";

    out.found = true;
    out.batch = std::move(batch);
    return out;
}

// =====================================================================
// unlinkGenerator（§7.1 解除关联）
// =====================================================================

std::vector<TaskPoint> unlinkGenerator(const std::vector<TaskPoint>& entries,
                                       const std::string& generatorInstanceId)
{
    std::vector<TaskPoint> out = entries;  // 值拷贝——未命中条目逐字节保持
    for (auto& e : out) {
        if (e.generation.has_value() && e.generation->linked
            && e.generation->instanceId == generatorInstanceId) {
            e.generation->linked = false;  // 仅翻关联位——参数快照留痕不失真
        }
    }
    return out;
}

}  // namespace sdurws::ird::requirements
