/**
 * @file   PanelRegionModel.hpp
 * @brief  区域面板呈现模型（零 Qt）——区域表＋采样定义（计数/间距双模式）
 *         ＋覆盖率目标＋区域轮廓与采样格几何三维预览（卡 §9.8 面板表第 3 行）。
 *
 * 设计依据：
 *   - units/requirements.md §9.8（面板组成表第 3 行——"区域表＋采样定义
 *     编辑（计数/间距双模式）＋覆盖率目标＋区域轮廓与采样格三维预览（仅
 *     几何预览；结果着色归 KIN-07）"；消费契约＝§9.4/ISamplingPlanBuilder
 *     ＋ui View3D 契约）、§5.2（采样计划定义——Grid/GridBySpacing/Random
 *     三方法；normalizeSampling 规范化规则 D-REQ-2）、§4.4（区域字段表
 *     ——盒/覆盖率目标）、§4.7（I-REQ-6：盒非退化/覆盖率∈[0,1]）
 *   - 需求 REQ-03（区域与采样定义）、UX-05；任务契约
 *     tasks/foundation/WP-14-T08.json acceptance 1（区域面板行）
 *
 * 背景说明（零计算逻辑的实现面——acceptance 3）：
 *   - 间距→计数的规范化（floor(size/spacing)+1）是**域算法**（D-REQ-2，
 *     planContentIdentity 的来源）——本面板不复制：双模式视图经
 *     IWorkRegionService::normalizeSampling 现算（域函数），插件只呈现
 *     "已规范化计数＋是否发生规范化改写"两行事实。
 *   - 预览几何（盒角点/采样格线）是**呈现几何**：盒与计数本是工作集权威
 *     值，把它们的轮廓/格线画出来不产生新语义、不回写、不参与身份——
 *     与 modeling 预览页"仅基于已应用修订"同源的呈现纪律（结果着色归
 *     KIN-07，本预览零结果语义——不伪造覆盖率结论）。
 *
 * 线程约束：全部函数纯函数，可重入；入参仅 UI 线程可变（§3.4）。
 * 确定性：行序/几何序固定；同输入同输出（域规范化为 IEEE754 确定运算）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_PANELREGIONMODEL_HPP
#define IRD_REQUIREMENTS_PLUGIN_PANELREGIONMODEL_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <rw/math/Vector3D.hpp>  // rw::math::Vector3D<double>——预览几何顶点（m）
#include <sdurws/ird/ui/FormEditCommon.hpp>  // ui::QuantityFieldSpec/ParamEditSet（B2 字段编辑——工位模型同款公共件）
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // WorkRegion/BoundingBox/PositionSampling
#include <sdurws/ird/requirements/Services.hpp>  // IWorkRegionService（规范化域函数——零复制纪律）
#include "PanelStationModel.hpp"  // StationFieldRow/StationFieldEnablement（检查器行模型复用——同目录插件私有头）

namespace sdurws::ird::requirements {

// =====================================================================
// 区域表行（卡 §9.8 第 3 行"区域表"的行载体）
// =====================================================================

/**
 * @brief 区域面板的一个区域行（纯值——区域表的行数据源；选择联动锚
 *        ＝条目 ObjectId，L-R1）。
 */
struct RegionRow {
    core::ObjectId objectId;  ///< 区域锚（L-R1 选中联动键）
    std::string name;         ///< 语义名（工程用语——UX-02）
    std::string level;        ///< 等级（"Must"/"Should"——词表直投）
    bool enabled = false;     ///< 启用事实
    std::string boxText;      ///< 区域盒摘要（"cx, cy, cz ‖ sx×sy×sz"——m）
    std::string samplingText; ///< 采样摘要（"Grid 4×3×2"等——方法＋计数事实）
    std::string coverageText; ///< 覆盖率目标摘要（"P≥0.8"形态——∈[0,1] 直投）

    bool operator==(const RegionRow& o) const
    {
        return objectId == o.objectId && name == o.name && level == o.level
            && enabled == o.enabled && boxText == o.boxText
            && samplingText == o.samplingText && coverageText == o.coverageText;
    }
    bool operator!=(const RegionRow& o) const { return !(*this == o); }
};

/**
 * @brief 投影区域表行（§9.8 第 3 行——一区域一行；行序＝工作集序）。
 * @param regions [in] 区域集合（工作集权威——只读）
 * @return 行序列（纯投影——不缓存；确定性）
 *
 * 纯函数；确定性；不抛。
 */
std::vector<RegionRow> regionRows(const std::vector<WorkRegion>& regions);

// =====================================================================
// 采样定义双模式视图（"计数/间距双模式"——卡 §9.8 第 3 行）
// =====================================================================

/**
 * @brief 一个区域的采样定义视图（双模式编辑面＋规范化事实的呈现承载）。
 *
 * 双模式语义（§5.2）：Grid（显式计数——counts 即权威）｜GridBySpacing
 * （间距式——编辑态表达，进计划恒经规范化）。规范化是域算法（D-REQ-2）：
 * 本视图的 normalizedCounts 经 IWorkRegionService::normalizeSampling 现算
 * （零复制纪律——插件不实现 floor 公式），spacingChanged 即域函数的
 * changed 标志直投（"是否发生规范化改写"的编辑器提示面）。
 */
struct SamplingDefinitionView {
    PositionSamplingMethod method = PositionSamplingMethod::Grid;  ///< 当前模式
    std::array<std::uint32_t, 3> counts{1, 1, 1};    ///< Grid 载荷（每轴计数；≥0）
    std::array<double, 3> spacing{0.0, 0.0, 0.0};    ///< GridBySpacing 载荷（m；三分量>0）
    std::uint32_t randomCount = 0;                   ///< Random 载荷（总计数；≥0）
    std::array<std::uint32_t, 3> normalizedCounts{1, 1, 1};  ///< 规范化计数（域函数现算——Grid 恒等于 counts）
    bool spacingChanged = false;                     ///< 规范化改写事实（GridBySpacing 输入→Grid 计数）
    std::string normalizedText;                      ///< 规范化事实文本（"规范化 4×3×2"——确定性）

    bool operator==(const SamplingDefinitionView& o) const
    {
        return method == o.method && counts == o.counts && spacing == o.spacing
            && randomCount == o.randomCount && normalizedCounts == o.normalizedCounts
            && spacingChanged == o.spacingChanged && normalizedText == o.normalizedText;
    }
    bool operator!=(const SamplingDefinitionView& o) const { return !(*this == o); }
};

/**
 * @brief 投影一个区域的采样定义视图（双模式＋规范化事实——§5.2/§9.8）。
 *
 * @param region  [in] 区域条目（工作集权威——只读）
 * @param service [in] 区域服务（normalizeSampling 域函数入口——规范化
 *                零复制纪律；无状态可默认构造）
 * @return 采样定义视图（规范化失败的域错误态→normalizedText 携"规范化
 *         未定"占位——不伪造计数；合法性裁决仍归编辑器校验链）
 *
 * 纯函数；确定性（域规范化 IEEE754 确定运算——同输入同计数）。
 */
SamplingDefinitionView samplingDefinitionView(const WorkRegion& region,
                                              const IWorkRegionService& service);

/**
 * @brief 区域检查器的字段行（区域选中后的编辑面投影——盒/覆盖率目标/
 *        采样定义/姿态采样；与 stationFieldsFor 同构的行模型）。
 * @param region   [in] 区域条目（只读）
 * @param service  [in] 区域服务（采样规范化域函数）
 * @param writable [in] 会话可写性（false→可编辑行灰显——L-R12 行半区）
 * @return 检查器行序列（行序＝登记序；确定性）
 *
 * 纯函数；确定性；不抛。
 */
std::vector<StationFieldRow> regionFieldsFor(const WorkRegion& region,
                                             const IWorkRegionService& service,
                                             bool writable);

// ---- B2 字段编辑提交协议（UI-T31——与 PanelStationModel 的 specs/
//      applyStationEditSet 同构：单一词表两处消费，行键＝回填键）----

/**
 * @brief 区域可编辑数值字段的规格词表（UI-T31 B2——盒中心/尺寸三分量
 *        ＋位置覆盖率下限；parseFieldValueText 的量纲/范围输入约束面）。
 *
 * 词表与 applyRegionEditSet 的回填键一致（单一词表两处消费——工位
 * stationQuantitySpecs 同款纪律）；单位锚：长度 m（尺寸＞0——I-REQ-6
 * 非退化的呈现层预过滤，业务裁决仍归域链）、覆盖率无量纲 ∈[0,1]。
 *
 * @return 规格序列（静态词表——区域条目字段数固定，无逐条目动态行）
 *
 * 纯函数；确定性；单位注册表缺项＝实现缺陷 fail-fast（装配期暴露）。
 */
std::vector<ui::QuantityFieldSpec> regionQuantitySpecs();

/**
 * @brief 区域字段编辑回填（ParamEditSet→WorkRegion——值拷贝改字段，
 *        非表单字段〔身份/名称/采样定义/引用〕原样保留）。
 *
 * @param base   [in] 编辑前条目（权威值——从工作集现取）
 * @param edits  [in] 本次编辑（SI 真值——parseFieldValueText 产出）
 * @param known  [out] 实际回填的键序列（词表外键＝实现缺陷——fail-fast
 *               抛 std::logic_error，不静默丢弃；与工位回填同纪律）
 * @return 候选条目（提交给 editor.applyEdit 域裁决——唯一性/合法性
 *         拒绝面在编辑器校验链）
 *
 * 纯函数（值语义）；确定性。
 */
WorkRegion applyRegionEditSet(const WorkRegion& base, const ui::ParamEditSet& edits,
                              std::vector<std::string>& known);

/**
 * @brief 区域二态行回填（UI-T37 R1——启用开关提交轨；词表现仅 "enabled"，
 *        词表外键＝实现缺陷 throw std::logic_error）。
 * @return 候选条目（base 拷贝＋已命中布尔位）；纯函数；确定性。
 */
WorkRegion applyRegionToggleEdit(const WorkRegion& base, const std::string& key,
                                 bool on, std::vector<std::string>& known);

/**
 * @brief 区域枚举行回填（UI-T37 返工②——等级 QComboBox 提交轨）。
 *
 * 键词表："level" → tryRequirementLevel 反查（词表外 throw）；其余键＝
 * 实现缺陷 fail-fast（enabled 走 applyRegionToggleEdit）。
 */
WorkRegion applyRegionEnumEdit(const WorkRegion& base, const std::string& key,
                               const std::string& valueText,
                               std::vector<std::string>& known);

/**
 * @brief 区域采样计数回填（UI-T37 R1——规则网格三轴分割数；方法切至
 *        Grid、spacing 载荷清零）。计数合法域归域校验链（submitEntryEdit
 *        域裁决），本函数零预判；纯函数；确定性。
 */
WorkRegion applyRegionSamplingCountsEdit(const WorkRegion& base,
                                         const std::array<std::uint32_t, 3>& counts,
                                         std::vector<std::string>& known);

// =====================================================================
// 区域轮廓与采样格几何三维预览（§9.8 第 3 行——仅几何预览；结果着色归
// KIN-07，本模型零结果语义）
// =====================================================================

/**
 * @brief 区域三维预览的几何载体（refFrame 系下表达——m；与区域盒同系，
 *        §4.4 头行"本单元不做坐标变换"）。
 *
 * 线段对＝(起点, 终点)；corners 恒 8 点（盒角，索引序＝(x,y,z) 二进制序：
 * i 的 bit0→+sx、bit1→+sy、bit2→+sz）；gridLines 为采样格线（每轴
 * counts[i]+1 条——格面位置按 counts 均分盒尺寸；counts=0 的轴不产线——
 * 零样本区域仅呈现轮廓）。零结果语义：本几何不携带任何覆盖率/可行性
 * 信息（结果着色归 KIN-07——不伪造评估结论）。
 */
struct RegionPreviewGeometry {
    std::array<rw::math::Vector3D<double>, 8> corners{};     ///< 盒角点（m；refFrame 系）
    std::vector<std::pair<rw::math::Vector3D<double>, rw::math::Vector3D<double>>>
        gridLines;  ///< 采样格线段（m；refFrame 系；每轴 counts[i]+1 条）
    std::string summaryText;  ///< 预览摘要（"盒 sx×sy×sz m·格 4×3×2"——确定性）

    /// 参考系原值（UI-T33——世界系变换的投影方输入；World 缺省合法——
    /// 几何顶点即 refFrame 系，投影方经宿主帧位姿变换到世界系投递）。
    RequirementReference refFrame;

    /// 区域锚（UI-T65——F-495 消费卡的逐区域过滤键：宿主投影方据此从
    /// 评估账面过滤本区域样本；表尾追加零破坏）。
    core::ObjectId regionObjectId;
    /// 位置覆盖率目标下限（UI-T65——∈[0,1]；nullopt＝未设目标→投影方
    /// 框色无映射。呈现对照输入——非判定权威）。
    std::optional<double> minPositionCoverage;

    bool operator==(const RegionPreviewGeometry& o) const
    {
        return corners == o.corners && gridLines == o.gridLines
            && summaryText == o.summaryText && refFrame == o.refFrame
            && regionObjectId == o.regionObjectId
            && minPositionCoverage == o.minPositionCoverage;
    }
    bool operator!=(const RegionPreviewGeometry& o) const { return !(*this == o); }
};

/**
 * @brief 构建区域轮廓与采样格预览几何（§9.8 第 3 行——呈现几何；顶点
 *        由盒尺寸均分派生，采样计数取**规范化后计数**（与计划内容身份
 *        同源——预览与将生成的样本基准一致）。
 *
 * @param region  [in] 区域条目（只读；盒/采样定义权威值）
 * @param service [in] 区域服务（规范化域函数——零复制纪律）
 * @return 预览几何（盒退化时 gridLines 为空——仅轮廓；不抛——退化是
 *         就绪层 R6 的发现面，呈现层不代判）
 *
 * 纯函数；确定性（几何派生为确定算术——同输入同顶点序）。
 */
RegionPreviewGeometry regionPreviewGeometry(const WorkRegion& region,
                                            const IWorkRegionService& service);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_PANELREGIONMODEL_HPP
