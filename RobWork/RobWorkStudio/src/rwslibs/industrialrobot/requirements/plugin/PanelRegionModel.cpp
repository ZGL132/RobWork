/**
 * @file   PanelRegionModel.cpp
 * @brief  区域面板呈现模型实现——区域表/采样双模式视图/预览几何。
 *
 * 设计依据：units/requirements.md §9.8（面板表第 3 行）、§5.2（采样定义
 * 与 D-REQ-2 规范化）、§4.4（区域字段表）；契约 WP-14-T08 acceptance 1/3。
 * 实现纪律：规范化等域算法唯一经 IWorkRegionService（零复制——NFR-MNT-04）；
 * 本文件只做值搬运与确定性文本化。
 */

#include "PanelRegionModel.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <utility>

namespace sdurws::ird::requirements {
namespace {

// 确定性数值文本化（PanelStationModel 同款——locale 无关 6 位小数裁尾零）。
std::string formatDeterministic(double v)
{
    char buf[32];
    auto res = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 6);
    std::string s(buf, res.ptr);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') { s.pop_back(); }
        if (!s.empty() && s.back() == '.') { s.pop_back(); }
    }
    if (s == "-0") { s = "0"; }
    return s;
}

// "cx, cy, cz"三维文本。
std::string formatVector(const rw::math::Vector3D<double>& v)
{
    return formatDeterministic(v[0]) + ", " + formatDeterministic(v[1]) + ", "
         + formatDeterministic(v[2]);
}

// "a×b×c"计数摘要。
std::string formatCounts(const std::array<std::uint32_t, 3>& c)
{
    return std::to_string(c[0]) + "×" + std::to_string(c[1]) + "×" + std::to_string(c[2]);
}

// 行拼装辅助（同 PanelStationModel——登记序确定性）。
void appendRow(std::vector<StationFieldRow>& rows, std::string key, std::string label,
               std::string valueText, std::string unitText,
               StationFieldEnablement enablement = StationFieldEnablement::Editable)
{
    StationFieldRow r;
    r.fieldKey = std::move(key);
    r.label = std::move(label);
    r.valueText = std::move(valueText);
    r.unitText = std::move(unitText);
    r.enablement = enablement;
    rows.push_back(std::move(r));
}

}  // namespace

std::vector<RegionRow> regionRows(const std::vector<WorkRegion>& regions)
{
    std::vector<RegionRow> rows;
    rows.reserve(regions.size());
    // 一区域一行（行序＝工作集序＝ObjectId 字典序——投影不重排）。
    for (const WorkRegion& r : regions) {
        RegionRow row;
        row.objectId = r.objectId;
        row.name = r.name;
        row.level = std::string(requirementLevelToken(r.level));
        row.enabled = r.enabled;
        // 盒摘要："中心 ‖ 尺寸"（m——直投，零换算）。
        row.boxText = formatVector(r.box.center) + " ‖ "
                    + formatDeterministic(r.box.size[0]) + "×"
                    + formatDeterministic(r.box.size[1]) + "×"
                    + formatDeterministic(r.box.size[2]);
        // 采样摘要：方法＋计数事实（GridBySpacing 显示原始 spacing 声明——
        // 规范化事实归 samplingDefinitionView，本行只认条目存储值）。
        switch (r.positionSampling.method) {
        case PositionSamplingMethod::Grid:
            row.samplingText = "Grid " + formatCounts(r.positionSampling.counts);
            break;
        case PositionSamplingMethod::GridBySpacing:
            row.samplingText = "间距 " + formatDeterministic(r.positionSampling.spacing[0])
                             + "/" + formatDeterministic(r.positionSampling.spacing[1])
                             + "/" + formatDeterministic(r.positionSampling.spacing[2]) + " m";
            break;
        case PositionSamplingMethod::Random:
            row.samplingText = "Random " + std::to_string(r.positionSampling.count);
            break;
        }
        // 覆盖率目标摘要（REQ-03：minPositionCoverage 必填直投；姿态目标
        // 可选——未设不伪造）。
        row.coverageText = "P≥" + formatDeterministic(r.coverageTargets.minPositionCoverage);
        if (r.coverageTargets.minOrientationCoverage.has_value()) {
            row.coverageText += " O≥"
                              + formatDeterministic(
                                  r.coverageTargets.minOrientationCoverage.value());
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

SamplingDefinitionView samplingDefinitionView(const WorkRegion& region,
                                              const IWorkRegionService& service)
{
    SamplingDefinitionView view;
    view.method = region.positionSampling.method;
    view.counts = region.positionSampling.counts;
    view.spacing = region.positionSampling.spacing;
    view.randomCount = region.positionSampling.count;

    // 规范化经域函数现算（D-REQ-2 单点——插件零复制 floor 公式；诊断出参
    // 为服务签名契约预留位，服务恒不写入——Services.hpp 契约）。
    std::vector<core::DiagnosticRecord> diags;
    const auto normalized =
        service.normalizeSampling(region.positionSampling, region.box.size, diags);
    if (normalized.ok()) {
        const NormalizedSampling& n = normalized.get();
        view.normalizedCounts = n.normalized.counts;
        view.spacingChanged = n.changed;
        view.normalizedText = n.changed ? ("规范化 " + formatCounts(n.normalized.counts))
                                        : ("计数 " + formatCounts(n.normalized.counts));
    } else {
        // 域拒绝（间距非法/盒退化）＝合法性事实——呈现"规范化未定"占位
        // （不伪造计数；裁决面在编辑器校验链与就绪层 R6，呈现层不代判）。
        view.spacingChanged = false;
        view.normalizedText = "规范化未定（参数非法）";
    }
    return view;
}

std::vector<StationFieldRow> regionFieldsFor(const WorkRegion& region,
                                             const IWorkRegionService& service,
                                             bool writable)
{
    std::vector<StationFieldRow> rows;
    rows.reserve(14);

    // ---- 条目级行（同工位面板的登记序约定）----
    appendRow(rows, "name", "名称", region.name, "");
    appendRow(rows, "level", "等级", std::string(requirementLevelToken(region.level)), "");
    appendRow(rows, "enabled", "启用", region.enabled ? "是" : "否", "");
    appendRow(rows, "ref-frame", "参考系",
              std::string(requirementRefKindToken(region.refFrame.kind)), "",
              StationFieldEnablement::ReadOnlyGrey);  // 浅引用事实——灰显（语义解析归评估侧，§8.1）

    // ---- 区域盒（I-REQ-6 非退化——B2 分量行：复合向量行拆三标量行，
    //      行键与 regionQuantitySpecs/applyRegionEditSet 单一词表对齐）----
    appendRow(rows, "box-center-x", "盒中心 X",
              formatDeterministic(region.box.center[0]), "m");
    appendRow(rows, "box-center-y", "盒中心 Y",
              formatDeterministic(region.box.center[1]), "m");
    appendRow(rows, "box-center-z", "盒中心 Z",
              formatDeterministic(region.box.center[2]), "m");
    appendRow(rows, "box-size-x", "盒尺寸 X",
              formatDeterministic(region.box.size[0]), "m");
    appendRow(rows, "box-size-y", "盒尺寸 Y",
              formatDeterministic(region.box.size[1]), "m");
    appendRow(rows, "box-size-z", "盒尺寸 Z",
              formatDeterministic(region.box.size[2]), "m");

    // ---- 采样定义双模式（计数/间距两行按模式显隐——卡面"双模式"）----
    const SamplingDefinitionView view = samplingDefinitionView(region, service);
    if (view.method == PositionSamplingMethod::GridBySpacing) {
        // 间距模式：显示间距声明＋规范化事实（规范化经域函数——上方 view）。
        appendRow(rows, "sampling-spacing", "采样间距 (x, y, z)",
                  formatVector(rw::math::Vector3D<double>(view.spacing[0], view.spacing[1],
                                                          view.spacing[2])),
                  "m");
        appendRow(rows, "sampling-normalized", "规范化计数", view.normalizedText, "",
                  StationFieldEnablement::ReadOnlyGrey);  // 规范化是域事实——不可编辑
    } else if (view.method == PositionSamplingMethod::Grid) {
        appendRow(rows, "sampling-counts", "采样计数", formatCounts(view.counts), "");
    } else {
        appendRow(rows, "sampling-random", "随机样本数", std::to_string(view.randomCount), "");
    }
    appendRow(rows, "orientation-sampling", "姿态采样",
              "方向 " + std::to_string(region.orientationSampling.directionSamples)
                  + " · 滚转 " + std::to_string(region.orientationSampling.rollSamples),
              "");  // §5.2：姿态采样 ≥1——零样本仅位置侧表达

    // ---- 覆盖率目标（∈[0,1]——I-REQ-6）----
    appendRow(rows, "coverage-position", "位置覆盖率下限",
              formatDeterministic(region.coverageTargets.minPositionCoverage), "");
    if (region.coverageTargets.minOrientationCoverage.has_value()) {
        appendRow(rows, "coverage-orientation", "姿态覆盖率下限",
                  formatDeterministic(
                      region.coverageTargets.minOrientationCoverage.value()),
                  "");
    } else {
        appendRow(rows, "coverage-orientation", "姿态覆盖率下限", "未设", "");
    }

    // ---- 顺序键/备注（同工位面板收尾）----
    appendRow(rows, "sequence-key", "顺序键",
              region.sequenceKey.has_value() ? region.sequenceKey.value() : "未设", "");
    appendRow(rows, "note", "备注", region.note, "");

    // ---- L-R12 只读门控（行半区——同 stationFieldsFor 的降级规则）----
    if (!writable) {
        for (StationFieldRow& r : rows) {
            if (r.enablement == StationFieldEnablement::Editable) {
                r.enablement = StationFieldEnablement::ReadOnlyGrey;
            }
        }
    }
    return rows;
}

RegionPreviewGeometry regionPreviewGeometry(const WorkRegion& region,
                                            const IWorkRegionService& service)
{
    RegionPreviewGeometry geo;
    const BoundingBox& box = region.box;

    // ---- 第一步：规范化计数（与计划内容身份同源——预览格线与将生成的
    //      样本基准一致；域函数现算，零复制 D-REQ-2）。----
    std::vector<core::DiagnosticRecord> diags;
    std::array<std::uint32_t, 3> counts{1, 1, 1};
    bool countsValid = false;
    const auto normalized = service.normalizeSampling(region.positionSampling, box.size, diags);
    if (normalized.ok()) {
        counts = normalized.get().normalized.counts;
        countsValid = true;
    }
    // 规范化失败（盒退化/间距非法）：countsValid=false → 不产格线——仅轮廓
    // （退化是就绪层 R6 的发现面，预览不代判不伪造）。

    // ---- 第二步：盒角点（8 点；索引 i 的 bit0→+sx、bit1→+sy、bit2→+sz；
    //      中心±半尺寸的确定算术——同输入同顶点序）。
    const double hx = box.size[0] / 2.0;
    const double hy = box.size[1] / 2.0;
    const double hz = box.size[2] / 2.0;
    for (std::size_t i = 0; i < 8; ++i) {
        const double sx = (i & 0x1u) ? hx : -hx;
        const double sy = (i & 0x2u) ? hy : -hy;
        const double sz = (i & 0x4u) ? hz : -hz;
        geo.corners[i] = rw::math::Vector3D<double>(box.center[0] + sx, box.center[1] + sy,
                                                    box.center[2] + sz);
    }

    // ---- 第三步：单元边界辅助格，而非样本点或可行性结果。----
    // KIN 的真实 Grid 样本位于单元体心；这里只把规范化计数解释为分割数，
    // 不复制样本生成器。Random 的旧 counts 载荷不能冒充随机样本分布；
    // 任一零计数都代表位置样本空集，因此整盒无辅助格（REQ-03/KIN-04）。
    const bool random = region.positionSampling.method == PositionSamplingMethod::Random;
    const bool hasGrid = countsValid && !random
        && counts[0] != 0 && counts[1] != 0 && counts[2] != 0;
    bool thinned = false;
    if (hasGrid) {
        // 每轴最多 33 个边界坐标：三方向合计最多 3×33²=3267 条线。
        // 这是呈现预算，不是采样预算；合法 uint32 大计数仍原样进入计划。
        constexpr std::uint64_t maxBoundaries = 33;
        std::array<std::vector<double>, 3> coordinates;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const std::uint64_t n = counts[axis];
            const std::uint64_t shown = std::min(n + 1, maxBoundaries);
            thinned = thinned || n + 1 > shown;
            coordinates[axis].reserve(static_cast<std::size_t>(shown));
            for (std::uint64_t k = 0; k < shown; ++k) {
                // 64 位中间量防止 UINT32_MAX+1 和索引乘法溢出。
                // floor(k*n/(shown-1)) 稳定选取真实单元边界，保留两端；
                // n>=1 保证 shown>=2，不会除零。坐标单位 m、参考系 refFrame。
                const auto boundary = k * n / (shown - 1);
                coordinates[axis].push_back(box.center[axis] - box.size[axis] / 2.0
                    + box.size[axis] * (static_cast<double>(boundary) / static_cast<double>(n)));
            }
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto transverseA = (axis + 1) % 3;
            const auto transverseB = (axis + 2) % 3;
            for (const double a : coordinates[transverseA]) {
                for (const double b : coordinates[transverseB]) {
                    // 仅延伸轴的坐标变化，另外两轴固定，避免画出面对角线。
                    auto start = box.center;
                    start[axis] -= box.size[axis] / 2.0;
                    start[transverseA] = a;
                    start[transverseB] = b;
                    auto end = start;
                    end[axis] += box.size[axis];
                    geo.gridLines.emplace_back(start, end);
                }
            }
        }
    }

    // ---- 第四步：摘要文本（确定性——盒尺寸＋格计数）。
    geo.summaryText = "盒 " + formatDeterministic(box.size[0]) + "×"
                    + formatDeterministic(box.size[1]) + "×"
                    + formatDeterministic(box.size[2]) + " m";
    if (random) {
        geo.summaryText += " · 随机 " + std::to_string(region.positionSampling.count)
            + " · 样本未生成，无辅助格";
    } else if (!countsValid) {
        geo.summaryText += " · 辅助格 未定";
    } else {
        geo.summaryText += " · 单元边界辅助格 " + formatCounts(counts);
        if (!hasGrid) { geo.summaryText += " · 零样本，无辅助格"; }
        if (thinned) { geo.summaryText += " · 显示已抽稀（每轴至多33个边界）"; }
    }
    geo.refFrame = region.refFrame;  // UI-T33——参考系原值随几何（世界系变换的投影方输入）
    // UI-T65——区域锚与覆盖率目标（F-495 消费卡的投影过滤/框色对照输入；
    // 值直投零换算——目标下限域默认 0.8 由 CoverageTargets 承载）。
    geo.regionObjectId = region.objectId;
    geo.minPositionCoverage = region.coverageTargets.minPositionCoverage;
    return geo;
}

// =====================================================================
// B2 字段编辑提交协议（UI-T31——specs 词表＋回填；工位模型同构）
// =====================================================================

std::vector<ui::QuantityFieldSpec> regionQuantitySpecs()
{
    // SI 单位锚（core UnitToken::find——工位 specs 同款 fail-fast 纪律）。
    auto unitOrThrow = [](const char* symbol) {
        auto u = core::UnitToken::find(symbol);
        if (!u.has_value()) {
            throw std::logic_error(std::string("区域面板：单位注册表缺少 ") + symbol
                                   + "（实现缺陷）");
        }
        return u.value();
    };
    const core::UnitToken m = unitOrThrow("m");
    const core::UnitToken one = unitOrThrow("1");  // 无量纲 SI token（"1"）

    // 装配辅助（键＝回填词表同词表——单一词表两处消费）。
    auto lengthSpec = [&](const char* key, const char* label,
                          std::optional<ui::QuantityBounds> bounds) {
        return ui::makeQuantityFieldSpec(key, label, core::QuantityKind::Length,
                                         m, m, bounds);
    };
    // 尺寸正数下界（I-REQ-6 非退化的呈现层预过滤——业务裁决仍归域链，
    // 此处只是表单输入约束面；中心不设界——坐标无先验范围）。
    const ui::QuantityBounds positive{1.0e-12, 1.0e9};
    // 覆盖率 ∈[0,1]（无量纲——I-REQ-6 覆盖率目标的定义域）。
    const ui::QuantityBounds unit{0.0, 1.0};

    return {
        lengthSpec("box-center-x", "盒中心 X", std::nullopt),
        lengthSpec("box-center-y", "盒中心 Y", std::nullopt),
        lengthSpec("box-center-z", "盒中心 Z", std::nullopt),
        lengthSpec("box-size-x", "盒尺寸 X", positive),
        lengthSpec("box-size-y", "盒尺寸 Y", positive),
        lengthSpec("box-size-z", "盒尺寸 Z", positive),
        ui::makeQuantityFieldSpec("coverage-position", "位置覆盖率下限",
                                  core::QuantityKind::Dimensionless,
                                  one, one, unit),
        // 姿态覆盖率下限（UI-T37 R1——可选值 ∈[0,1]；未设态由面板灰显
        // 不进本 spec 的编辑面——设值入口随负载编辑后续批次）。
        ui::makeQuantityFieldSpec("coverage-orientation", "姿态覆盖率下限",
                                  core::QuantityKind::Dimensionless,
                                  one, one, unit),
    };
}

WorkRegion applyRegionEditSet(const WorkRegion& base, const ui::ParamEditSet& edits,
                              std::vector<std::string>& known)
{
    known.clear();
    known.reserve(edits.changes.size());
    WorkRegion out = base;  // 值拷贝——非表单字段（身份/名称/采样/引用）原样保留
    for (const ui::ParamChange& c : edits.changes) {
        // 词表外键＝表单模型与回填词表漂移（实现缺陷）——fail-fast 不静默
        // 丢弃（与 applyStationEditSet 同纪律）。
        if (c.key == "box-center-x") {
            out.box.center[0] = c.newSi;
        } else if (c.key == "box-center-y") {
            out.box.center[1] = c.newSi;
        } else if (c.key == "box-center-z") {
            out.box.center[2] = c.newSi;
        } else if (c.key == "box-size-x") {
            out.box.size[0] = c.newSi;
        } else if (c.key == "box-size-y") {
            out.box.size[1] = c.newSi;
        } else if (c.key == "box-size-z") {
            out.box.size[2] = c.newSi;
        } else if (c.key == "coverage-position") {
            out.coverageTargets.minPositionCoverage = c.newSi;
        } else if (c.key == "coverage-orientation") {
            // 姿态覆盖率（可选值——UI-T37 R1 设值入口；置值即进入 has_value 态）。
            out.coverageTargets.minOrientationCoverage = c.newSi;
        } else {
            throw std::logic_error("区域面板：编辑行携带词表外键 " + c.key);
        }
        known.push_back(c.key);
    }
    return out;
}

WorkRegion applyRegionToggleEdit(const WorkRegion& base, const std::string& key,
                                 bool on, std::vector<std::string>& known)
{
    known.clear();
    // 区域二态回填（UI-T37 R1——与 applyStationToggleEdit 同纪律：词表
    // 外键 fail-fast；一处一布尔位）。
    WorkRegion out = base;
    if (key == "enabled") {
        out.enabled = on;
    } else {
        throw std::logic_error("区域面板：二态回填携带词表外键 " + key
                               + "（实现缺陷）");
    }
    known.push_back(key);
    return out;
}

WorkRegion applyRegionSamplingCountsEdit(const WorkRegion& base,
                                         const std::array<std::uint32_t, 3>& counts,
                                         std::vector<std::string>& known)
{
    known.clear();
    // 采样计数回填（UI-T37 R1——规则网格计数模式；spacing 载荷清零保持
    // 单模式单载荷的确定性形态）。计数合法域（≥1/退化判定）归域校验链
    // （submitEntryEdit 域裁决）——本函数只做词表回填不预判。
    WorkRegion out = base;
    out.positionSampling.method = PositionSamplingMethod::Grid;
    out.positionSampling.counts = counts;
    out.positionSampling.spacing = {0.0, 0.0, 0.0};
    known.push_back("sampling-counts");
    return out;
}


WorkRegion applyRegionEnumEdit(const WorkRegion& base, const std::string& key,
                               const std::string& valueText,
                               std::vector<std::string>& known)
{
    known.clear();
    // 枚举词表回填（UI-T37 返工②——等级；词表外 fail-fast）。
    WorkRegion out = base;
    if (key == "level") {
        const auto level = tryRequirementLevel(valueText);
        if (!level.has_value()) {
            throw std::invalid_argument("区域面板：等级词表外文本 " + valueText);
        }
        out.level = level.value();
    } else {
        throw std::invalid_argument("区域面板：枚举回填携带词表外键 " + key
                                    + "（实现缺陷）");
    }
    known.push_back(key);
    return out;
}

}  // namespace sdurws::ird::requirements
