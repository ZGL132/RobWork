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

    // ---- 区域盒（I-REQ-6 非退化——直投）----
    appendRow(rows, "box-center", "盒中心", formatVector(region.box.center), "m");
    appendRow(rows, "box-size", "盒尺寸",
              formatDeterministic(region.box.size[0]) + " × "
                  + formatDeterministic(region.box.size[1]) + " × "
                  + formatDeterministic(region.box.size[2]),
              "m");

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

    // ---- 第三步：采样格线（countsValid 且计数非零的轴产 counts[i]+1 条
    //      等分线；counts=0 的轴不产线——零样本区域仅呈现轮廓，V-02 口径：
    //      零样本合法存储，预览如实表达"无格"）。----
    if (countsValid) {
        // 半尺寸数组（下标 0/1/2 ↔ x/y/z——格位计算的轴向寻址载体）。
        const double half[3] = {hx, hy, hz};
        // 每轴格坐标：第 k 条（k=0..counts[i]）位于 center−half＋size·k/counts
        // （counts=0 时该轴无格——循环体不执行，零样本轴如实无格）。
        for (int axis = 0; axis < 3; ++axis) {
            const std::size_t ax = static_cast<std::size_t>(axis);
            const std::uint32_t n = counts[ax];
            if (n == 0) {
                continue;  // 零样本轴——无格线
            }
            for (std::uint32_t k = 0; k <= n; ++k) {
                // 该轴第 k 格位（盒下底起等分——D-REQ-2 计数语义的几何直译）。
                const double gridCoord = -half[ax] + box.size[ax]
                                       * (static_cast<double>(k) / static_cast<double>(n));
                // 线段沿该轴的横截方向铺开：线段两端在该轴上同取格位，
                // 其余两轴取盒的两端（-half 端 → +half 端——格面内平行线）。
                double a[3] = {-hx, -hy, -hz};
                double b[3] = {hx, hy, hz};
                a[ax] = gridCoord;
                b[ax] = gridCoord;
                rw::math::Vector3D<double> pa(box.center[0] + a[0], box.center[1] + a[1],
                                              box.center[2] + a[2]);
                rw::math::Vector3D<double> pb(box.center[0] + b[0], box.center[1] + b[1],
                                              box.center[2] + b[2]);
                geo.gridLines.emplace_back(pa, pb);
            }
        }
    }

    // ---- 第四步：摘要文本（确定性——盒尺寸＋格计数）。
    geo.summaryText = "盒 " + formatDeterministic(box.size[0]) + "×"
                    + formatDeterministic(box.size[1]) + "×"
                    + formatDeterministic(box.size[2]) + " m";
    geo.summaryText += countsValid ? (" · 格 " + formatCounts(counts)) : " · 格 未定";
    return geo;
}

}  // namespace sdurws::ird::requirements
