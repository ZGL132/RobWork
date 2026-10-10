/**
 * @file   CatalogTypes.cpp
 * @brief  目录包业务模型的相等算子、规范序列化与内容身份实现（selection
 *         单元）——包/曲线 canonical 文本与 SHA-256 摘要（CON-05）。
 *
 * 设计依据：
 *   - units/selection.md §4.2（目录数据冻结——CatalogPackageSnapshot＝四表
 *     业务模型的规范序列化；包 canonical 字节 SHA-256（core ContentDigester
 *     唯一哈希路径）；变更任何字节→新内容身份→依赖切片失效）、§4.3（身份
 *     关系表）、§6.1（曲线内容身份＝点集规范序列化摘要）
 *   - 需求 CON-05（内容寻址）、NFR-COR-01（确定性——同快照恒同文本）、
 *     NFR-COR-02（稳定序）
 *   - 任务契约 tasks/foundation/WP-19-T03.json（目录包模型的实现面；
 *     AT-08"版本锁定"的内容身份判据＝本文件的计算面）
 *
 * 序列化确定性来源（NFR-COR-02 逐项登记）：
 *   1. 条目排序：motors/gearboxes 按 modelId 字典序升序、curves 按 curveId
 *      升序、compatibility 按 (motorId, gearboxId, mountKind) 升序——快照
 *      装配入口（CatalogImporter::assemble）已排序，本函数再次独立排序
 *      输入副本后序列化（不依赖调用方守约，纯函数自洽）；
 *   2. 数值格式化：std::to_chars 最短 round-trip 十进制（C++17，与 C
 *      locale 无关——snprintf 受 locale 影响小数点字符，禁止）；
 *   3. 字段顺序/分隔符：固定书写序（结构体成员序）＋'|' 字段分隔——文本
 *      仅作摘要输入（不是持久化格式，呈现/存储归 reporting/project 端口）。
 */

#include <sdurws/ird/selection/CatalogTypes.hpp>

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string>
#include <vector>

namespace sdurws::ird::selection {

// =====================================================================
// 相等算子（字段多、语义需逐字段对齐的类型；字段少的在头内联定义）
// =====================================================================

bool MotorCatalogEntry::operator==(const MotorCatalogEntry& o) const
{
    // 逐字段比较（业务身份＝字段值全体；显示名重复但 ID 不同＝合法——
    // 相等语义不特殊对待 displayName，两行同 ID 不同显示名即不相等，
    // 导入校验层的 DUPLICATE-ID 判定另有"同 ID"单字段判据）。
    return modelId == o.modelId && vendor == o.vendor && displayName == o.displayName
        && catalog == o.catalog
        && ratedTorque == o.ratedTorque && peakTorque == o.peakTorque
        && ratedSpeed == o.ratedSpeed && maxSpeed == o.maxSpeed
        && ratedPower == o.ratedPower && overload == o.overload
        && dutyClass == o.dutyClass && ratedVoltage == o.ratedVoltage
        && thermal == o.thermal && brakeTorque == o.brakeTorque
        && holdingTorque == o.holdingTorque
        && rotorInertia == o.rotorInertia && mass == o.mass
        && mounting == o.mounting && curves == o.curves && missing == o.missing
        && status == o.status;
}

bool GearboxCatalogEntry::operator==(const GearboxCatalogEntry& o) const
{
    return modelId == o.modelId && vendor == o.vendor && displayName == o.displayName
        && catalog == o.catalog
        && ratedOutputTorque == o.ratedOutputTorque
        && peakOutputTorque == o.peakOutputTorque
        && maxInputSpeed == o.maxInputSpeed && ratio == o.ratio
        && efficiency == o.efficiency && backlash == o.backlash
        && ratedLife == o.ratedLife && mountingOrientation == o.mountingOrientation
        && extLoad == o.extLoad && mass == o.mass && housingInertia == o.housingInertia
        && mounting == o.mounting && curves == o.curves && missing == o.missing
        && status == o.status;
}

bool PerformanceCurve::operator==(const PerformanceCurve& o) const
{
    return curveId == o.curveId && xQuantity == o.xQuantity && yQuantity == o.yQuantity
        && xUnit == o.xUnit && yUnit == o.yUnit && points == o.points
        && catalog == o.catalog && contentIdentity == o.contentIdentity;
}

bool LinearDriveCatalogEntry::operator==(const LinearDriveCatalogEntry& o) const
{
    // 逐字段比较（业务身份＝字段值全体——与电机/减速器条目同款纪律；
    // kind 为封闭枚举值比较，missing 清单参与相等〔显式标记是业务事实，
    // 卡 §4.1 ERR-01〕）。
    return modelId == o.modelId && vendor == o.vendor && displayName == o.displayName
        && catalog == o.catalog && kind == o.kind
        && ratedForce == o.ratedForce && peakForce == o.peakForce
        && maxLinearSpeed == o.maxLinearSpeed && ratedPower == o.ratedPower
        && efficiency == o.efficiency && stroke == o.stroke
        && mass == o.mass && mounting == o.mounting
        && curves == o.curves && missing == o.missing && status == o.status;
}

// =====================================================================
// §17.2 直线传动器件类别词表文本（唯一映射点——WP-19-T12）
// =====================================================================

std::string_view linearDriveKindText(LinearDriveKind kind)
{
    switch (kind) {
        case LinearDriveKind::BallScrew:   return "ball-screw";   ///< 滚珠丝杠
        case LinearDriveKind::RackPinion:  return "rack-pinion";  ///< 齿条齿轮
        case LinearDriveKind::TimingBelt:  return "timing-belt";  ///< 同步带
        case LinearDriveKind::LinearMotor: return "linear-motor"; ///< 直线电机
    }
    // 枚举外整数值（防御分支——正常路径不可达；词表封闭性由测试钉住）。
    return "unknown-linear-drive-kind";
}

bool CatalogIssue::operator==(const CatalogIssue& o) const
{
    return code == o.code && file == o.file && rowNo == o.rowNo && column == o.column
        && modelId == o.modelId && message == o.message && actualText == o.actualText
        && expectedText == o.expectedText && actualValue == o.actualValue
        && expectedValue == o.expectedValue && unit == o.unit;
}

// =====================================================================
// ParsedCatalogInput（io 解析输入的查找便利）
// =====================================================================

const ParsedFileTable* ParsedCatalogInput::find(const std::string& fileName) const noexcept
{
    // 线性查找（表数固定为 5——规模无二分理由；nullptr＝未命中）。
    for (const ParsedFileTable& t : files) {
        if (t.fileName == fileName) {
            return &t;
        }
    }
    return nullptr;
}

// =====================================================================
// 规范序列化（确定性三来源见文件头注）
// =====================================================================

namespace {

/// 数值的 canonical 文本：std::to_chars 最短 round-trip 十进制（locale
/// 无关——NFR-COR-01 确定性的单位数值面；非有限值在序列化前已由校验层
/// 拒绝，此处遇非有限以占位文本承载并在注释明示——不抛，保证纯函数性）。
void appendNumber(std::string& out, double v)
{
    char buf[64];
    // to_chars（无格式重载）产出最短 round-trip 表示——同值恒同文本。
    const auto res = std::to_chars(buf, buf + sizeof(buf), v);
    if (res.ec != std::errc{}) {
        // 不可达（64 字节缓冲对 double 最短表示绰绰有余）；防御占位，
        // 不让序列化抛异常破坏纯函数契约（坏值应在校验层被拒）。
        out += "<num-format-error>";
        return;
    }
    out.append(buf, static_cast<std::size_t>(res.ptr - buf));
}

/// 单字段追加（文本侧——'|' 分隔由调用点书写；空文本不转义：摘要输入
/// 不是持久化格式，字段间定界由固定书写序保证）。
void appendText(std::string& out, const std::string& s)
{
    out += s;
}

/// 电机条目 canonical 行（字段序＝CatalogTypes.hpp 成员序——单一书写点，
/// 与序列化注释同步维护）。
void appendMotor(std::string& out, const MotorCatalogEntry& m)
{
    appendText(out, m.modelId);        out += '|';
    appendText(out, m.vendor);         out += '|';
    appendText(out, m.displayName);    out += '|';
    appendText(out, m.catalog.catalogId);   out += '|';
    appendText(out, m.catalog.version);     out += '|';
    appendNumber(out, m.ratedTorque);  out += '|';
    appendNumber(out, m.peakTorque);   out += '|';
    appendNumber(out, m.ratedSpeed);   out += '|';
    appendNumber(out, m.maxSpeed);     out += '|';
    appendNumber(out, m.ratedPower);   out += '|';
    if (m.overload) {                  // optional 有值＝显式数值；无值＝"~"占位
        appendNumber(out, m.overload->torque);    out += '|';
        appendNumber(out, m.overload->duration);
    } else {
        out += "~|~";
    }
    out += '|';
    appendText(out, m.dutyClass);      out += '|';
    if (m.ratedVoltage) { appendNumber(out, *m.ratedVoltage); } else { out += '~'; }
    out += '|';
    if (m.thermal) {                   // 温度降额（v1 无量纲档位——登记 3）
        appendNumber(out, m.thermal->refTemp);      out += '|';
        appendNumber(out, m.thermal->factorPerRef);
    } else {
        out += "~|~";
    }
    out += '|';
    if (m.brakeTorque) { appendNumber(out, *m.brakeTorque); } else { out += '~'; }
    out += '|';
    if (m.holdingTorque) { appendNumber(out, *m.holdingTorque); } else { out += '~'; }
    out += '|';
    appendNumber(out, m.rotorInertia); out += '|';
    appendNumber(out, m.mass);         out += '|';
    appendText(out, m.mounting.flangeKind); out += '|';
    appendText(out, m.mounting.shaftKind);  out += '|';
    for (const CurveRef& c : m.curves) {        // 引用按向量序（构造序——见排序纪律）
        out += 'R'; appendText(out, c.curveId); out += '/';
        appendText(out, c.xQuantity); out += '/';
        appendText(out, c.yQuantity); out += ';';
    }
    for (const MissingField& mf : m.missing) {  // 缺失清单进身份（显式标记是业务事实）
        out += 'M'; appendText(out, mf.column); out += '/';
        appendText(out, mf.reason); out += ';';
    }
    out += "|S";
    out += (m.status == ValidationStatus::Valid ? "V"
          : m.status == ValidationStatus::Partial ? "P" : "I");
}

/// 减速器条目 canonical 行（字段序同上纪律）。
void appendGearbox(std::string& out, const GearboxCatalogEntry& g)
{
    appendText(out, g.modelId);        out += '|';
    appendText(out, g.vendor);         out += '|';
    appendText(out, g.displayName);    out += '|';
    appendText(out, g.catalog.catalogId); out += '|';
    appendText(out, g.catalog.version);   out += '|';
    appendNumber(out, g.ratedOutputTorque); out += '|';
    appendNumber(out, g.peakOutputTorque);  out += '|';
    appendNumber(out, g.maxInputSpeed);     out += '|';
    appendNumber(out, g.ratio);        out += '|';
    appendNumber(out, g.efficiency);   out += '|';
    if (g.backlash) { appendNumber(out, *g.backlash); } else { out += '~'; }
    out += '|';
    if (g.ratedLife) { appendNumber(out, *g.ratedLife); } else { out += '~'; }
    out += '|';
    appendText(out, g.mountingOrientation); out += '|';
    if (g.extLoad) {                   // 外载荷（N/m 口径——ExternalLoadSpec 注）
        appendNumber(out, g.extLoad->radial);   out += '|';
        appendNumber(out, g.extLoad->axial);    out += '|';
        appendNumber(out, g.extLoad->distance);
    } else {
        out += "~|~|~";
    }
    out += '|';
    appendNumber(out, g.mass);         out += '|';
    if (g.housingInertia) { appendNumber(out, *g.housingInertia); } else { out += '~'; }
    out += '|';
    appendText(out, g.mounting.flangeKind); out += '|';
    appendText(out, g.mounting.shaftKind);  out += '|';
    for (const CurveRef& c : g.curves) {
        out += 'R'; appendText(out, c.curveId); out += '/';
        appendText(out, c.xQuantity); out += '/';
        appendText(out, c.yQuantity); out += ';';
    }
    for (const MissingField& mf : g.missing) {
        out += 'M'; appendText(out, mf.column); out += '/';
        appendText(out, mf.reason); out += ';';
    }
    out += "|S";
    out += (g.status == ValidationStatus::Valid ? "V"
          : g.status == ValidationStatus::Partial ? "P" : "I");
}

/// 直线传动器件条目 canonical 行（v2 第六表——WP-19-T12；字段序＝
/// LinearDriveCatalogEntry 成员序——单一书写点纪律同电机/减速器行）。
void appendLinearDrive(std::string& out, const LinearDriveCatalogEntry& d)
{
    appendText(out, d.modelId);        out += '|';
    appendText(out, d.vendor);         out += '|';
    appendText(out, d.displayName);    out += '|';
    appendText(out, d.catalog.catalogId); out += '|';
    appendText(out, d.catalog.version);   out += '|';
    // 词表文本经唯一映射点取得（string_view→std::string 显式承载——
    // canonical 输入是 const std::string&）。
    appendText(out, std::string(linearDriveKindText(d.kind))); out += '|';
    appendNumber(out, d.ratedForce);   out += '|';
    appendNumber(out, d.peakForce);    out += '|';
    appendNumber(out, d.maxLinearSpeed); out += '|';
    appendNumber(out, d.ratedPower);   out += '|';
    if (d.efficiency) { appendNumber(out, *d.efficiency); } else { out += '~'; }
    out += '|';
    if (d.stroke) { appendNumber(out, *d.stroke); } else { out += '~'; }
    out += '|';
    appendNumber(out, d.mass);         out += '|';
    appendText(out, d.mounting.flangeKind); out += '|';
    appendText(out, d.mounting.shaftKind);  out += '|';
    for (const CurveRef& c : d.curves) {        // 引用按向量序（构造序——同款纪律）
        out += 'R'; appendText(out, c.curveId); out += '/';
        appendText(out, c.xQuantity); out += '/';
        appendText(out, c.yQuantity); out += ';';
    }
    for (const MissingField& mf : d.missing) {  // 缺失清单进身份（显式标记是业务事实）
        out += 'M'; appendText(out, mf.column); out += '/';
        appendText(out, mf.reason); out += ';';
    }
    out += "|S";
    out += (d.status == ValidationStatus::Valid ? "V"
          : d.status == ValidationStatus::Partial ? "P" : "I");
}

}  // namespace

std::string canonicalPackageText(const CatalogPackageSnapshot& snapshot)
{
    // 输入副本独立排序（不依赖调用方守约——纯函数自洽；排序键＝卡 §4.3
    // 身份关系表的稳定 ID 序，NFR-COR-02）。
    std::vector<MotorCatalogEntry> motors = snapshot.motors;
    std::vector<GearboxCatalogEntry> gearboxes = snapshot.gearboxes;
    std::vector<PerformanceCurve> curves = snapshot.curves;
    std::vector<CompatibilityRecord> compat = snapshot.compatibility;
    std::vector<LinearDriveCatalogEntry> linearDrives = snapshot.linearDrives;

    std::sort(motors.begin(), motors.end(),
              [](const MotorCatalogEntry& a, const MotorCatalogEntry& b) {
                  return a.modelId < b.modelId;
              });
    std::sort(gearboxes.begin(), gearboxes.end(),
              [](const GearboxCatalogEntry& a, const GearboxCatalogEntry& b) {
                  return a.modelId < b.modelId;
              });
    std::sort(curves.begin(), curves.end(),
              [](const PerformanceCurve& a, const PerformanceCurve& b) {
                  return a.curveId < b.curveId;
              });
    std::sort(compat.begin(), compat.end(),
              [](const CompatibilityRecord& a, const CompatibilityRecord& b) {
                  // 组合键三段升序（motorId→gearboxId→mountKind）。
                  if (a.motorId != b.motorId) { return a.motorId < b.motorId; }
                  if (a.gearboxId != b.gearboxId) { return a.gearboxId < b.gearboxId; }
                  return a.mountKind < b.mountKind;
              });
    std::sort(linearDrives.begin(), linearDrives.end(),
              [](const LinearDriveCatalogEntry& a, const LinearDriveCatalogEntry& b) {
                  return a.modelId < b.modelId;   // 直线器件按稳定 ID 升序（同款纪律）
              });

    std::string out;
    out.reserve(4096);   // 经验初值——避免小包多次重分配（行为无关确定性）

    // 清单段：版本/目录身份/来源/文件清单（字段字典的语义文本不进包身份
    // ——包身份锚定业务数据本身；字典的"列集合"语义已经由四表内容体现）。
    appendText(out, snapshot.manifest.formatVersion); out += '|';
    appendText(out, snapshot.manifest.identity.catalogId); out += '|';
    appendText(out, snapshot.manifest.identity.version); out += '|';
    appendText(out, snapshot.manifest.identity.source); out += '|';
    for (const ManifestEntry& f : snapshot.manifest.files) {
        appendText(out, f.fileName); out += '/';
        appendText(out, f.role); out += '/';
        out += (f.required ? "req" : "opt"); out += '/';
        appendText(out, f.sha256Hex); out += ';';
    }
    out += '\n';

    // 曲线段：点集 canonical（含各曲线内容身份——曲线变更的可定位判据）。
    for (const PerformanceCurve& c : curves) {
        appendText(out, c.curveId); out += '|';
        appendText(out, c.xQuantity); out += '|';
        appendText(out, c.yQuantity); out += '|';
        appendText(out, c.xUnit); out += '|';
        appendText(out, c.yUnit); out += '|';
        appendText(out, c.contentIdentity.toCanonical()); out += '|';
        for (const CapabilityPoint& p : c.points) {   // 点序＝x 严格升序（构造入口保证）
            out += '(';
            appendNumber(out, p.x); out += ',';
            appendNumber(out, p.y);
            out += ')';
        }
        out += '\n';
    }

    // 电机段 / 减速器段 / 兼容段（各自排序后逐条目一行）。
    for (const MotorCatalogEntry& m : motors) {
        appendMotor(out, m);
        out += '\n';
    }
    for (const GearboxCatalogEntry& g : gearboxes) {
        appendGearbox(out, g);
        out += '\n';
    }
    for (const CompatibilityRecord& r : compat) {
        appendText(out, r.motorId); out += '>';
        appendText(out, r.gearboxId); out += '>';
        appendText(out, r.mountKind);
        out += '\n';
    }

    // 直线传动器件段（v2 第六表——WP-19-T12；追加在兼容段之后）。
    // ★ v1 身份零漂移的序列化承载：空表（v1 包恒空——linearDrives 无
    //   条目）时本循环体零次执行，不向文本追加任何字节——v1 快照的
    //   canonical 文本与本扩展引入前逐字节一致，包内容身份零漂移
    //   （sel-catalog-golden 黄金身份断言零回归的机制保证）。
    for (const LinearDriveCatalogEntry& d : linearDrives) {
        appendLinearDrive(out, d);
        out += '\n';
    }
    return out;
}

namespace {

/// SHA-256 摘要单点（core ContentDigester 唯一哈希路径——卡 §4.2）。
core::ContentIdentity digestText(const std::string& text)
{
    core::ContentDigester d;
    d.update(text.data(), text.size());
    core::ContentIdentity id;
    id.bytes = d.finalize();
    return id;
}

}  // namespace

core::ContentIdentity computePackageContentIdentity(const CatalogPackageSnapshot& snapshot)
{
    // 包内容身份＝canonical 全文摘要（变更任何业务字节→新身份→依赖切片
    // 失效——卡 §4.2；CON-05 内容寻址）。
    return digestText(canonicalPackageText(snapshot));
}

core::ContentIdentity computeCurveContentIdentity(const PerformanceCurve& curve)
{
    // 曲线内容身份＝点集规范序列化摘要（卡 §6.1——目录能力曲线变更的
    // 可定位判据：evidence §5.3"目录能力曲线"行的失效源）。
    std::string out;
    appendText(out, curve.curveId); out += '|';
    appendText(out, curve.xQuantity); out += '|';
    appendText(out, curve.yQuantity); out += '|';
    appendText(out, curve.xUnit); out += '|';
    appendText(out, curve.yUnit); out += '|';
    for (const CapabilityPoint& p : curve.points) {
        out += '(';
        appendNumber(out, p.x); out += ',';
        appendNumber(out, p.y);
        out += ')';
    }
    return digestText(out);
}

}  // namespace sdurws::ird::selection
