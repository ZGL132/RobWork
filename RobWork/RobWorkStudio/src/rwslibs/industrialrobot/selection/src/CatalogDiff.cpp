/**
 * @file   CatalogDiff.cpp
 * @brief  目录差异比较实现（selection 单元）——CatalogDiffer::compare
 *         （§13.6 结构化差异：型号三态＋字段级增量＋曲线点集增量＋兼容
 *         关系增量；CatalogDiff.hpp 头注为语义权威）。
 *
 * 设计依据：
 *   - units/selection.md §13.6（目录差异比较——纯函数会话工具；目录更新
 *     不静默改变历史结果）、§4.2（内容身份失效链——业务字段变更→新
 *     内容身份→依赖切片失效）、§4.3（(catalogId, version, modelId) 三元
 *     组身份）、D-SEL-14、§14.0（错误两分法）
 *   - 需求 SEL-08（目录差异比较＋项目锁定版本；更新不静默改变历史）、
 *     CON-05（diff 非空 ⇔ 包内容身份不同——契约测试钉住）、NFR-COR-02
 *     （输出确定性）
 *   - 任务契约 tasks/foundation/WP-19-T07.json acceptance 2
 *
 * 确定性（NFR-COR-02）：型号字段比较集为固定注册表（比较执行序＝
 * fieldChanges 序）；型号/曲线/兼容查找用有序向量线性扫描（输入规模＝
 * 目录条目数，O(n·m) 主表对比较在目录规模下充分——万级条目经 execution
 * 后台任务承载，NFR-PERF-01）；输出序固定（CatalogDiff.hpp 头注）；
 * 数值文本＝std::to_chars 最短 round-trip（与 canonicalPackageText 同源，
 * 同值必同文本——locale 无关）。
 */

#include <sdurws/ird/selection/CatalogDiff.hpp>

#include <algorithm>
#include <charconv>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace sdurws::ird::selection {
namespace {

// ---------------------------------------------------------------------
// 值格式化与比较工具（TU 内私有——不建跨 TU 私有头，NFR-MNT-04 先例）
// ---------------------------------------------------------------------

/// double → canonical 文本（std::to_chars 最短 round-trip——与
/// CatalogTypes.cpp appendNumber 同源同规则：locale 无关、同值恒同文本；
/// 非有限值在导入校验层已拒绝，防御分支以占位文本承载不抛——保纯函数性）。
std::string formatNumber(double v)
{
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), v);
    if (res.ec != std::errc{}) {
        return "(nonfinite)";  // 不可达防御位（校验层前置拒绝）
    }
    return std::string(buf, static_cast<std::size_t>(res.ptr - buf));
}

/// 可缺失数值 → 文本（nullopt＝kDiffAbsent 哨兵——缺失是事实陈述）。
std::string formatOptional(double v)
{
    return formatNumber(v);
}

/// 文本集合序列化（curves 引用列表的比较承载——先按 curveId 排序消除
/// 列表序语义，再逐项 "curveId|xQ/yQ" 连接；引用是集合，序不参与语义）。
std::string formatCurveRefs(std::vector<CurveRef> refs)
{
    std::sort(refs.begin(), refs.end(),
              [](const CurveRef& a, const CurveRef& b) { return a.curveId < b.curveId; });
    std::string out;
    for (std::size_t i = 0; i < refs.size(); ++i) {
        if (i > 0) {
            out += ";";
        }
        out += refs[i].curveId + "|" + refs[i].xQuantity + "/" + refs[i].yQuantity;
    }
    return out;
}

/// 单字段差异行追加（值相同不记——差异即变更）。
void addIfChanged(std::vector<FieldChange>& out, const char* field,
                  const std::string& oldV, const std::string& newV)
{
    if (oldV != newV) {
        out.push_back(FieldChange{field, oldV, newV});
    }
}

/// 单数值字段比较（现值 vs 基值；差即记）。
void compareNumber(std::vector<FieldChange>& out, const char* field,
                   double oldV, double newV)
{
    addIfChanged(out, field, formatNumber(oldV), formatNumber(newV));
}

/// 可缺失数值子字段比较（optional 复合字段的子值展开：nullopt 子字段
/// 视为 kDiffAbsent——overload 从有到无会产出其全部子字段的
/// 值→(absent) 差异行，逐字段可定位）。
void compareOptional(std::vector<FieldChange>& out, const char* field,
                     const std::optional<double>& oldV, const std::optional<double>& newV)
{
    const std::string o = oldV.has_value() ? formatOptional(*oldV) : std::string(kDiffAbsent);
    const std::string n = newV.has_value() ? formatOptional(*newV) : std::string(kDiffAbsent);
    addIfChanged(out, field, o, n);
}

/// 电机主表业务字段全量比较（注册表序＝执行序＝fieldChanges 序——登记
/// 于 CatalogDiff.hpp 头注：20 组。catalog 身份字段/missing/status 不比
/// ——版本演进必然差异与导入期派生标记，非内容差异）。
std::vector<FieldChange> diffMotor(const MotorCatalogEntry& a, const MotorCatalogEntry& b)
{
    std::vector<FieldChange> out;
    out.reserve(24);
    addIfChanged(out, "vendor", a.vendor, b.vendor);                       // 文本
    addIfChanged(out, "displayName", a.displayName, b.displayName);       // 文本
    compareNumber(out, "ratedTorque", a.ratedTorque, b.ratedTorque);      // N·m
    compareNumber(out, "peakTorque", a.peakTorque, b.peakTorque);         // N·m
    compareNumber(out, "ratedSpeed", a.ratedSpeed, b.ratedSpeed);         // rad/s
    compareNumber(out, "maxSpeed", a.maxSpeed, b.maxSpeed);               // rad/s
    compareNumber(out, "ratedPower", a.ratedPower, b.ratedPower);         // W
    // 复合可缺失 overload：子字段展开（nullopt＝(absent)——统一规则）。
    compareOptional(out, "overload.torque",
                    a.overload.has_value() ? std::optional<double>(a.overload->torque)
                                           : std::nullopt,
                    b.overload.has_value() ? std::optional<double>(b.overload->torque)
                                           : std::nullopt);               // N·m
    compareOptional(out, "overload.duration",
                    a.overload.has_value() ? std::optional<double>(a.overload->duration)
                                           : std::nullopt,
                    b.overload.has_value() ? std::optional<double>(b.overload->duration)
                                           : std::nullopt);               // s
    addIfChanged(out, "dutyClass", a.dutyClass, b.dutyClass);             // 词表文本
    compareOptional(out, "ratedVoltage", a.ratedVoltage, b.ratedVoltage); // V
    compareOptional(out, "thermal.refTemp",
                    a.thermal.has_value() ? std::optional<double>(a.thermal->refTemp)
                                          : std::nullopt,
                    b.thermal.has_value() ? std::optional<double>(b.thermal->refTemp)
                                          : std::nullopt);                // 档位值（v1）
    compareOptional(out, "thermal.factorPerRef",
                    a.thermal.has_value() ? std::optional<double>(a.thermal->factorPerRef)
                                          : std::nullopt,
                    b.thermal.has_value() ? std::optional<double>(b.thermal->factorPerRef)
                                          : std::nullopt);                // 无量纲 (0,1]
    compareOptional(out, "brakeTorque", a.brakeTorque, b.brakeTorque);    // N·m
    compareOptional(out, "holdingTorque", a.holdingTorque, b.holdingTorque); // N·m
    compareNumber(out, "rotorInertia", a.rotorInertia, b.rotorInertia);   // kg·m²
    compareNumber(out, "mass", a.mass, b.mass);                           // kg
    addIfChanged(out, "mounting.flangeKind", a.mounting.flangeKind, b.mounting.flangeKind);
    addIfChanged(out, "mounting.shaftKind", a.mounting.shaftKind, b.mounting.shaftKind);
    // 曲线引用列表：集合语义——排序后序列化比较（列表序不参与语义）。
    addIfChanged(out, "curves", formatCurveRefs(a.curves), formatCurveRefs(b.curves));
    return out;
}

/// 减速器主表业务字段全量比较（注册表序——18 组，同 diffMotor 纪律）。
std::vector<FieldChange> diffGearbox(const GearboxCatalogEntry& a, const GearboxCatalogEntry& b)
{
    std::vector<FieldChange> out;
    out.reserve(22);
    addIfChanged(out, "vendor", a.vendor, b.vendor);                              // 文本
    addIfChanged(out, "displayName", a.displayName, b.displayName);               // 文本
    compareNumber(out, "ratedOutputTorque", a.ratedOutputTorque, b.ratedOutputTorque); // N·m
    compareNumber(out, "peakOutputTorque", a.peakOutputTorque, b.peakOutputTorque);    // N·m
    compareNumber(out, "maxInputSpeed", a.maxInputSpeed, b.maxInputSpeed);        // rad/s
    compareNumber(out, "ratio", a.ratio, b.ratio);                                // 无量纲
    compareNumber(out, "efficiency", a.efficiency, b.efficiency);                 // (0,1]
    compareOptional(out, "backlash", a.backlash, b.backlash);                     // rad（v1 冻结）
    compareOptional(out, "ratedLife", a.ratedLife, b.ratedLife);                  // 循环数（v1）
    addIfChanged(out, "mountingOrientation", a.mountingOrientation, b.mountingOrientation);
    compareOptional(out, "externalLoad.radial",
                    a.extLoad.has_value() ? std::optional<double>(a.extLoad->radial)
                                          : std::nullopt,
                    b.extLoad.has_value() ? std::optional<double>(b.extLoad->radial)
                                          : std::nullopt);                        // N
    compareOptional(out, "externalLoad.axial",
                    a.extLoad.has_value() ? std::optional<double>(a.extLoad->axial)
                                          : std::nullopt,
                    b.extLoad.has_value() ? std::optional<double>(b.extLoad->axial)
                                          : std::nullopt);                        // N
    compareOptional(out, "externalLoad.dist",
                    a.extLoad.has_value() ? std::optional<double>(a.extLoad->distance)
                                          : std::nullopt,
                    b.extLoad.has_value() ? std::optional<double>(b.extLoad->distance)
                                          : std::nullopt);                        // m
    compareNumber(out, "mass", a.mass, b.mass);                                   // kg
    compareOptional(out, "housingInertia", a.housingInertia, b.housingInertia);   // kg·m²
    addIfChanged(out, "mounting.flangeKind", a.mounting.flangeKind, b.mounting.flangeKind);
    addIfChanged(out, "mounting.shaftKind", a.mounting.shaftKind, b.mounting.shaftKind);
    addIfChanged(out, "curves", formatCurveRefs(a.curves), formatCurveRefs(b.curves));
    return out;
}

/// 快照身份有效性 + 主表 modelId 唯一性契约校验（fail-fast——compare
/// @throws 表的两项；返回值无意义，失败即抛）。
void requireComparable(const CatalogPackageSnapshot& s, const char* side)
{
    // ①无身份快照不可比较（CON-05——差异报告的 from/to 身份是呈现与
    // 升级影响提示的锚，无身份即无锚）。
    if (!s.contentIdentity.isValid()) {
        throw std::invalid_argument(
            std::string("SEL-CATALOG-DIFF(compare): ") + side
            + " 快照包内容身份无效（全零——须先经 CatalogImporter::assemble"
              " 计算内容身份，CON-05，调用方契约违约）");
    }
    // ②主表 modelId 唯一性（键唯一性前提——assemble 已拒绝
    // DUPLICATE-ID；直构快照同责。有序表二分不适用于未排序输入——
    // 目录规模线性扫描 + map 判重即可）。
    std::map<ModelId, bool> seen; // 有序键——判重路径（不影响输出序）
    for (const MotorCatalogEntry& m : s.motors) {
        if (!seen.emplace(m.modelId, true).second) {
            throw std::invalid_argument(
                std::string("SEL-CATALOG-DIFF(compare): ") + side
                + " 电机主表 modelId 重复（modelId=" + m.modelId
                + "——装配产物唯一性前提破坏，SEL-CATALOG-DUPLICATE-ID 同责）");
        }
    }
    for (const GearboxCatalogEntry& g : s.gearboxes) {
        if (!seen.emplace(g.modelId, true).second) {
            throw std::invalid_argument(
                std::string("SEL-CATALOG-DIFF(compare): ") + side
                + " 减速器主表 modelId 重复（modelId=" + g.modelId
                + "——装配产物唯一性前提破坏，SEL-CATALOG-DUPLICATE-ID 同责）");
        }
    }
}

/// 主表内按 modelId 查电机条目（未命中 nullptr——调用方决定三态归类）。
const MotorCatalogEntry* findMotor(const CatalogPackageSnapshot& s, const ModelId& id)
{
    for (const MotorCatalogEntry& m : s.motors) {
        if (m.modelId == id) {
            return &m;
        }
    }
    return nullptr;
}

/// 主表内按 modelId 查减速器条目（同上）。
const GearboxCatalogEntry* findGearbox(const CatalogPackageSnapshot& s, const ModelId& id)
{
    for (const GearboxCatalogEntry& g : s.gearboxes) {
        if (g.modelId == id) {
            return &g;
        }
    }
    return nullptr;
}

/// 曲线表内按 curveId 查曲线（同上）。
const PerformanceCurve* findCurve(const CatalogPackageSnapshot& s, const CurveId& id)
{
    for (const PerformanceCurve& c : s.curves) {
        if (c.curveId == id) {
            return &c;
        }
    }
    return nullptr;
}

/// 曲线头部字段增量（xQuantity/xUnit/yQuantity/yUnit——注册表序；
/// 点集差异另由点级增量承载，不进 fieldChanges）。
std::vector<FieldChange> diffCurveHeader(const PerformanceCurve& a, const PerformanceCurve& b)
{
    std::vector<FieldChange> out;
    out.reserve(4);
    addIfChanged(out, "xQuantity", a.xQuantity, b.xQuantity);
    addIfChanged(out, "xUnit", a.xUnit, b.xUnit);
    addIfChanged(out, "yQuantity", a.yQuantity, b.yQuantity);
    addIfChanged(out, "yUnit", a.yUnit, b.yUnit);
    return out;
}

/// 曲线点级增量（按 x 匹配——双方点集 x 严格升序是构造/装配入口保证的
/// 前置不变量，双指针线性归并 O(n+m)：
///   - to 有 from 无的 x → addedPoints（新点）；
///   - from 有 to 无的 x → removedPoints（废弃点）；
///   - 同 x：y 相同→无差异；y 不同→removed 记旧值＋added 记新值
///     （"移除旧值＋加入新值"差分语义——呈现层可直接理解））。
void diffCurvePoints(const PerformanceCurve& a, const PerformanceCurve& b,
                     std::vector<CapabilityPoint>& added,
                     std::vector<CapabilityPoint>& removed)
{
    std::size_t i = 0, j = 0;
    while (i < a.points.size() && j < b.points.size()) {
        const CapabilityPoint& pa = a.points[i];
        const CapabilityPoint& pb = b.points[j];
        if (pa.x < pb.x) {
            removed.push_back(pa);  // 基线独有 x → 点被移除
            ++i;
        } else if (pb.x < pa.x) {
            added.push_back(pb);    // 目标独有 x → 新增点
            ++j;
        } else {
            if (pa.y != pb.y) {     // 同 x 变 y：旧值移除＋新值加入
                removed.push_back(pa);
                added.push_back(pb);
            }
            ++i;
            ++j;
        }
    }
    // 尾部残余（一侧点集更长——剩余全为单侧增/删）。
    for (; i < a.points.size(); ++i) {
        removed.push_back(a.points[i]);
    }
    for (; j < b.points.size(); ++j) {
        added.push_back(b.points[j]);
    }
}

}  // namespace

// =====================================================================
// CatalogDiffer::compare（语义——CatalogDiff.hpp 头注）
// =====================================================================

CatalogDiff CatalogDiffer::compare(const CatalogPackageSnapshot& from,
                                   const CatalogPackageSnapshot& to) const
{
    // ---- 契约校验（fail-fast——调用方契约违约；比较是纯只读动作，
    //      校验只读输入不变量，无副作用）----
    requireComparable(from, "基线（from）");
    requireComparable(to, "目标（to）");

    CatalogDiff diff;
    diff.fromIdentity = from.manifest.identity;
    diff.toIdentity = to.manifest.identity;

    // ---- 型号差异（电机先——输出序键 isMotor：电机在前）----
    // 键＝modelId，两主表独立三态归类。遍历以 from 主表为基线序、to 为
    // 目标序，末尾统一排序——中间序不影响输出契约。
    // 电机：Added = to 有 from 无；Removed = from 有 to 无；
    //       Modified = 双方有且字段注册表有差异（fieldChanges 空向量＝
    //       无差异——不产条目，避免零信息条目）。
    for (const MotorCatalogEntry& m : to.motors) {
        if (findMotor(from, m.modelId) == nullptr) {
            diff.models.push_back(ModelDiff{ModelDiffKind::Added, true, m.modelId, {}});
        }
    }
    for (const MotorCatalogEntry& m : from.motors) {
        const MotorCatalogEntry* t = findMotor(to, m.modelId);
        if (t == nullptr) {
            diff.models.push_back(ModelDiff{ModelDiffKind::Removed, true, m.modelId, {}});
        } else {
            std::vector<FieldChange> changes = diffMotor(m, *t);
            if (!changes.empty()) {
                diff.models.push_back(
                    ModelDiff{ModelDiffKind::Modified, true, m.modelId, std::move(changes)});
            }
        }
    }
    // 减速器（同构）。
    for (const GearboxCatalogEntry& g : to.gearboxes) {
        if (findGearbox(from, g.modelId) == nullptr) {
            diff.models.push_back(ModelDiff{ModelDiffKind::Added, false, g.modelId, {}});
        }
    }
    for (const GearboxCatalogEntry& g : from.gearboxes) {
        const GearboxCatalogEntry* t = findGearbox(to, g.modelId);
        if (t == nullptr) {
            diff.models.push_back(ModelDiff{ModelDiffKind::Removed, false, g.modelId, {}});
        } else {
            std::vector<FieldChange> changes = diffGearbox(g, *t);
            if (!changes.empty()) {
                diff.models.push_back(
                    ModelDiff{ModelDiffKind::Modified, false, g.modelId, std::move(changes)});
            }
        }
    }
    // 输出序：(isMotor 电机先)→modelId→kind 枚举序（Added<Removed<Modified
    // ——枚举声明序即排序序，NFR-COR-02 确定性契约）。
    std::sort(diff.models.begin(), diff.models.end(),
              [](const ModelDiff& a, const ModelDiff& b) {
                  if (a.isMotor != b.isMotor) { return a.isMotor; }  // 电机在前
                  if (a.modelId != b.modelId) { return a.modelId < b.modelId; }
                  return a.kind < b.kind;
              });

    // ---- 曲线差异（curveId 升序输出）----
    for (const PerformanceCurve& c : to.curves) {
        if (findCurve(from, c.curveId) == nullptr) {
            diff.curves.push_back(CurveDiff{CurveDiff::Kind::Added, c.curveId, {}, {}, {}});
        }
    }
    for (const PerformanceCurve& c : from.curves) {
        const PerformanceCurve* t = findCurve(to, c.curveId);
        if (t == nullptr) {
            diff.curves.push_back(CurveDiff{CurveDiff::Kind::Removed, c.curveId, {}, {}, {}});
            continue;
        }
        // Modified 判定：头部字段/点集/内容身份任一不同（内容身份是
        // 点集的规范摘要——与其比较等价但显式逐项比较可产出点级定位）。
        std::vector<FieldChange> header = diffCurveHeader(c, *t);
        std::vector<CapabilityPoint> added, removed;
        diffCurvePoints(c, *t, added, removed);
        if (!header.empty() || !added.empty() || !removed.empty()) {
            diff.curves.push_back(CurveDiff{CurveDiff::Kind::Modified, c.curveId,
                                            std::move(header), std::move(added),
                                            std::move(removed)});
        }
    }
    std::sort(diff.curves.begin(), diff.curves.end(),
              [](const CurveDiff& a, const CurveDiff& b) {
                  if (a.curveId != b.curveId) { return a.curveId < b.curveId; }
                  return a.kind < b.kind;
              });

    // ---- 兼容关系差异（全键三列存在性；(motorId,gearboxId,mountKind)
    //      升序输出）----
    // 存在性判定：from 有 to 无 → Removed；to 有 from 无 → Added；
    // 双方皆有 → 无差异（无 Modified 态——关系只有存在/不存在）。
    auto compatKey = [](const CompatibilityRecord& r) {
        return std::make_pair(r.motorId,
                              std::make_pair(r.gearboxId, r.mountKind));
    };
    auto hasCompat = [&compatKey](const CatalogPackageSnapshot& s,
                                  const CompatibilityRecord& r) {
        for (const CompatibilityRecord& x : s.compatibility) {
            if (compatKey(x) == compatKey(r)) {
                return true;
            }
        }
        return false;
    };
    for (const CompatibilityRecord& r : to.compatibility) {
        if (!hasCompat(from, r)) {
            diff.compatibility.push_back(
                CompatibilityDiff{CompatibilityDiff::Kind::Added, r});
        }
    }
    for (const CompatibilityRecord& r : from.compatibility) {
        if (!hasCompat(to, r)) {
            diff.compatibility.push_back(
                CompatibilityDiff{CompatibilityDiff::Kind::Removed, r});
        }
    }
    std::sort(diff.compatibility.begin(), diff.compatibility.end(),
              [&compatKey](const CompatibilityDiff& a, const CompatibilityDiff& b) {
                  const auto ka = compatKey(a.record);
                  const auto kb = compatKey(b.record);
                  if (ka != kb) { return ka < kb; }
                  return a.kind < b.kind;
              });

    return diff;
}

}  // namespace sdurws::ird::selection
