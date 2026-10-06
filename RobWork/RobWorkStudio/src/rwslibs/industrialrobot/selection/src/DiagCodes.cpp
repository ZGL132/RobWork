/**
 * @file   DiagCodes.cpp
 * @brief  selection 稳定诊断码登记表的实现——SEL-* 全表 17 码登记行清单
 *         （逐码出处与语义登记）。
 *
 * 设计依据：
 *   - units/selection.md §2.2（移动关节范围外纪律）、§5.3（目录业务校验
 *     清单——逐行触发条件与比较型字段要求）、§6.2（默认禁止外推——不
 *     自动使用最近点）、§6.3（曲线校验表——拒绝优于排序掩盖）、§9.3
 *     （组合校核清单——兼容/轴映射/身份一致性；各码登记值与处置口径的
 *     唯一权威）、§1.3（SEL-* 建议值；前缀已在 diagnostics §4.5 在册）
 *   - units/core.md §4.8（DiagCode 句法——core 仅承载；本清单码值经
 *     DiagCodesTest 以 core::DiagnosticRecord 句法权威校验）
 *   - 先例：drivetrain/src/DiagCodes.cpp（依赖白名单无 diagnostics 编译
 *     边的登记表物化同款形态——kinematics/src/DiagCodes.cpp 的注册函数
 *     形态在此不适用，差异论证见 DiagCodes.hpp 文件头注）
 *   - 任务契约 tasks/foundation/WP-19-T02.json acceptance 2
 *
 * 确定性（NFR-COR-02）：清单序＝卡面章节序（§2.2 → §5.3 → §6.2 → §6.3
 * → §9.3，同节按表行序）；每次调用返回同序同值新清单（登记行为纯值
 * 聚合）；码值经 DiagCodes.hpp 常量引用（唯一书写点）。
 */

#include <sdurws/ird/selection/DiagCodes.hpp>

namespace sdurws::ird::selection {

std::vector<DiagnosticEntry> selectionCodeEntries()
{
    // 清单序＝卡面章节序（登记契约序——追加只允许表尾）；逐码注释给出
    // 卡面出处与语义登记原文（触发条件/处置口径）。
    return {
        // ---- §2.2：移动关节轴范围外 ----
        // R1 只支持旋转传动（电机＋减速器）；目标链含移动关节→该轴输出
        // "范围外"诊断并按 DataInsufficient 语义处理。四不纪律：不静默
        // 套用旋转传动、不静默把移动关节转成旋转关节、不伪造电机工作点、
        // 不将该结果直接升级为整机工程不可行（判定权在 evidence 汇总
        // ——D-SEL-15）。
        {kSelInputAxisOutOfScope,
         "units/selection.md §2.2",
         "移动关节轴范围外：DataInsufficient 语义，不套用旋转传动、不伪造工作点、不升级整机不可行"},

        // ---- §5.3 行 1：字段字典不完整 ----
        // manifest 声明列与 CSV 表头不一致（多列/缺列）——逐列定位拒绝；
        // 目录 schema 的结构级错误在校验边界快速拒绝（整批拒绝＋逐项
        // 定位，卡 §10.2 短路边界）。
        {kSelCatalogSchemaMismatch,
         "units/selection.md §5.3",
         "字段字典与表头不一致：多列/缺列逐列定位，schema 级整批拒绝"},

        // ---- §5.3 行 2：单位非法 ----
        // 单位词表＋量纲检查不通过——未知单位拒绝、不猜测；比较型诊断
        // （实际/期望/单位，ERR-01）；单位换算唯一实现归 core（卡 §2.1
        // NFR-MNT-03 承接行），本域比较一律在 SI 域。
        {kSelCatalogUnitInvalid,
         "units/selection.md §5.3",
         "单位非法：未知单位拒绝不猜测；比较型（实际/期望/单位），比较在 SI 域"},

        // ---- §5.3 行 3：必填字段缺失 ----
        // 逐字段定位；与"允许缺失字段显式入 missing 清单"分轨（后者不
        // 伪造数值、按 Partial 状态降级——卡 §4.1 MotorCatalogEntry.missing）。
        {kSelCatalogFieldMissing,
         "units/selection.md §5.3",
         "必填字段缺失：逐字段定位；允许缺失字段另入 missing 清单，不伪造"},

        // ---- §5.3 行 4a：重复型号 ----
        // 同稳定 ID 的多行重复型号条目——候选身份唯一性前提破坏；注意
        // 显示名重复但 ID 不同＝合法（验证矩阵 V1 登记行），不落本码。
        {kSelCatalogDuplicateModel,
         "units/selection.md §5.3",
         "同稳定 ID 重复型号条目：候选身份唯一性破坏（显示名重复 ID 不同＝合法，不落本码）"},

        // ---- §5.3 行 4b：稳定 ID 重复 ----
        // (catalogId, version, modelId) 三元组唯一确定条目实例（卡 §4.2）
        // ——modelId 主键重复使兼容关系、淘汰原因定位全部失锚。
        {kSelCatalogDuplicateId,
         "units/selection.md §5.3",
         "稳定 ID 重复：三元组身份前提失效，主键唯一性拒绝"},

        // ---- §5.3 行 5：数值范围非法 ----
        // 转矩>0 N·m、效率∈(0,1] 无量纲、速比>0、寿命>0 等；非有限数
        // （NaN/±Inf）同路径——NFR-COR-03"非有限不静默通过"；比较型：
        // 实际值/期望范围/单位。
        {kSelCatalogRangeInvalid,
         "units/selection.md §5.3",
         "数值范围非法（转矩>0、效率∈(0,1]、速比>0 等；非有限同路径）：比较型（实际/期望范围/单位）"},

        // ---- §5.3 行 6＝§6.3 曲线缺失行：引用语义悬空 ----
        // curve_ref 指向曲线不存在或 owner 不匹配、compatibility 引用
        // 双方型号不存在——文件层存在性由 io 先行校验（io §7.8），语义
        // 层（owner 匹配）归本卡；§6.3"曲线缺失"行同码（条目引用了不
        // 存在的 curve_id）。
        {kSelCatalogRefDangling,
         "units/selection.md §5.3/§6.3",
         "引用语义悬空（curve_ref 无曲线或 owner 不匹配、compatibility 引用缺失；曲线缺失同码）"},

        // ---- §5.3 行 7：兼容关系冲突 ----
        // 同型号对多行且 mount_kind 安装关系矛盾——组合兼容判定（§9.3
        // 无记录即不兼容）的前提被破坏，导入期拒绝。
        {kSelCatalogCompatConflict,
         "units/selection.md §5.3",
         "兼容关系冲突：同型号对多行且 mount_kind 矛盾，导入期拒绝"},

        // ---- §6.2：默认禁止外推 ----
        // 查询点落在 [x_min, x_max] 闭区间外即拒绝——不自动使用最近点、
        // 不静默外推；比较型（实际输入点/有效区间/单位）。插值失败≠候选
        // 能力不足：本码属数据不足类标记，与能力不足类淘汰原因分轨
        // （卡 §6.2 分轨纪律）。
        {kSelCurveExtrapolationDenied,
         "units/selection.md §6.2",
         "默认禁止外推：区间外查询拒绝，不用最近点、不静默外推；数据不足类与能力不足分轨"},

        // ---- §6.3 行 1：采样点无序 ----
        // 未按 x 升序提交即拒绝——构造入口不代排序（排序会掩盖目录错误，
        // 要求目录修正——卡 §6.3 明示）；升序是分段线性插值的结构前提。
        {kSelCurveUnordered,
         "units/selection.md §6.3",
         "曲线采样点无序：拒绝，构造入口不代排序（排序会掩盖目录错误）"},

        // ---- §6.3 行 2：重复横坐标 ----
        // 同 x 不同 y 的点对——插值在该横坐标处语义歧义，导入期拒绝。
        {kSelCurveDupX,
         "units/selection.md §6.3",
         "重复横坐标（同 x 不同 y）：插值语义歧义，导入期拒绝"},

        // ---- §6.3 行 3：非有限点 ----
        // 曲线点含 NaN/±Inf 任一——NFR-COR-03 精确判据"非有限即拒绝"，
        // 不静默丢弃坏点（丢点会改变曲线区间与插值结果）。
        {kSelCurveNonfinite,
         "units/selection.md §6.3",
         "曲线点非有限（NaN/±Inf）：拒绝，不静默丢弃坏点"},

        // ---- §6.3 行 4：区间不合法 ----
        // x_max ≤ x_min 或单点曲线声明为曲线——分段线性插值区间前提
        // 破坏；单点能力值应走"固定额定值"显式口径（卡 §6.4——不得用
        // 额定值伪造缺失的能力曲线）。
        {kSelCurveIntervalInvalid,
         "units/selection.md §6.3",
         "区间不合法（x_max≤x_min 或单点曲线）：单点能力值走固定额定值口径，不伪造曲线"},

        // ---- §9.3 行 1：组合不兼容 ----
        // compatibility 表无该（motor, gearbox）型号对记录即不兼容——
        // 零行语义＝包内无预声明兼容对（卡 §5.2）；组合级淘汰原因
        // （§10.2 分层：轴级→组合级→整机素材）。
        {kSelComboIncompatible,
         "units/selection.md §9.3",
         "组合不兼容：无兼容记录即不兼容（零行＝无预声明兼容对）；组合级淘汰原因"},

        // ---- §9.3 行 2：轴映射不完整 ----
        // 每轴恰一组合的前提破坏（漏轴）——组合身份 DeviceCombinationId
        // 的轴序×器件完备性要求（卡 §9.5），不允许多轴/缺轴兜底。
        {kSelComboAxisMappingIncomplete,
         "units/selection.md §9.3",
         "轴映射不完整：每轴恰一组合，漏轴即拒绝（不缺轴兜底）"},

        // ---- §9.3 行 11/12：身份不一致 ----
        // 组合校核所用目录版本≠映射批候选参数来源版本，或 drivetrain
        // 映射 Facts 契约/算法版本与切片声明不一致——拒绝评估（AT-38
        // dynamics/drivetrain/selection 三方同口径；不以版本不符数据
        // 继续计算）。
        {kSelIdentityMismatch,
         "units/selection.md §9.3",
         "身份不一致（目录版本/映射契约版本与切片声明不符）：拒绝评估，AT-38 三方同口径"},
    };
}

}  // namespace sdurws::ird::selection
