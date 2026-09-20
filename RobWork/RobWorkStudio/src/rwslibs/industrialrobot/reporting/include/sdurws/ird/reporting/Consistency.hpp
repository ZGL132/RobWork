/**
 * @file   Consistency.hpp
 * @brief  一致性检查器（RPT-T07 产物）——多格式渲染工件的逐字段一致性
 *         校验契约＋CSV 回读 io reader 注入缝（§8.5/§9.4/§3.3）。
 *
 * 设计依据：
 *   - units/reporting.md §8.5（AT-22 逐字段一致性判定：对 FieldMatrix 的
 *     每个 FieldCell，三格式回读值与源值逐字符一致；缺失字段＝mismatch；
 *     限定语丢失＝mismatch〔§6.4 硬约束〕；数值以文本规范形比对——非浮点
 *     再解析，D-09 避免二次舍入假差异；mismatches 全量列出不短路）、
 *     §9.4（IReportConsistencyChecker/ConsistencyInput/FieldMismatch/
 *     ConsistencyResult 契约——纯函数、并发安全、不修改任何输入；
 *     回读本身失败〔格式损坏〕→ ConsistencyMismatch〔dimension=
 *     parse-failed〕；artifacts<2 平凡通过＋注记）、§3.3/§3.4（io 能力
 *     注入边界——公共头零 io 类型；CSV 回读经注入 reader）
 *   - 需求 RPT-02（多格式逐字段一致——AT-22）、NFR-SEC-03（CSV 转义
 *     roundtrip——转义唯一实现归 io，本单元回读消费 io reader 而非自写
 *     解析）、NFR-DEP-04（未知字段拒绝的读取侧——消费 kJsonTopLevelKeys
 *     冻结集）
 *   - 任务契约 tasks/foundation/RPT-T07.json acceptance 1~5
 *
 * 背景说明（检查器在报告导出链中的位置）：
 *   导出服务（RPT-T09，§8.5 时序图）在"渲染三格式工件"之后、发布之前
 *   调用本检查器（步④）：以字段矩阵（§8.4 单次投影——三格式的唯一值
 *   源）为基准，对每个工件做**逐字段回读提取**（JSON parse→字段遍历 /
 *   CSV 经注入 io reader→行/列映射 / HTML data-field 提取器——模板自有
 *   的机器可解析结构），再把回读值与矩阵单元格七元组逐字符比对。它把
 *   "以格式存在当内容一致"（§9.4 非法调用行）堵死：文件存在≠一致，
 *   必须逐字段回读。
 *
 * 七元组与各格式的维度覆盖（acceptance 3——判定面的冻结登记）：
 *   七元组＝fieldKey/value/unit/status/qualifier/引用（resultRef＋
 *   evidenceRef）。fieldKey 是对齐键（join key）不设维度词；比对维度词
 *   表＝value/unit/status/qualifier/ref/missing（§9.4 FieldMismatch
 *   dimension 注释），外加 parse-failed（错误轨——抛出而非入列）。
 *   各格式**实际携带**的维度由三格式冻结契约（RPT-T06 落位的 §8.1/§8.3
 *   格式面）决定：
 *     - value/unit/qualifier：三格式全携带——全比对（qualifier 任一格式
 *       丢失＝mismatch，§6.4"跳过限定语维度"禁止）；
 *     - status：CSV 携带原生 token 列；HTML 携带中文显示名（经共享映射
 *       表 src/RenderText.hpp 反查 token 后比对）；JSON 字段镜像
 *       （ird-report-json/1 的 fields[] 成员集冻结于 RPT-T06）不携带
 *       field 对齐的 status——该维度在 JSON 无回读值可比，不比对；
 *     - 引用（resultRef/evidenceRef）：仅 CSV 携带字段对齐的引用列
 *       （HTML/JSON 的引用在追溯区块/顶层镜像，非字段对齐）——仅 CSV
 *       比对；
 *     - missing：三格式全比对（矩阵驱动成员检查）。
 *   "缺失字段（某格式未输出该 fieldKey）＝mismatch"与"限定语丢失＝
 *   mismatch"由此在全格式面上成立；不比对的维度是该格式结构上不输出
 *   的维度（不存在"输出而不同"的样本空间），与"跳过限定语维度"的禁止
 *   项不同质——后者是"携带却不查"，本检查器不犯。
 *
 * 与 §9.4 契约形状的偏差登记（DTB §5.4——单元卡 §14.4 v0.10 同步）：
 *   ConsistencyResult 增补 notes 成员（std::vector<std::string>）——
 *   §9.4 原文结构无注记位，而 ConsistencyInput 注释与 acceptance 4 明文
 *   "artifacts<2 返回平凡通过＋**注记**"，须有承载位；其余成员与原文
 *   逐字一致。
 *
 * 线程契约（§9.4 维度表）：check() 为确定性纯函数——同输入同结论、零
 *   副作用、不修改任何输入（全部 const 只读）；并发安全（注入的
 *   IReportCsvReaderFactory 为无状态工厂，每次调用产出独立 reader）。
 * 上界：字段数×格式数（§9.4 取消行——无取消检查点；性能护栏＝字段矩阵
 *   规模上限，登记 §14.2 R-4）。
 */

#ifndef SDURWS_IRD_REPORTING_CONSISTENCY_HPP
#define SDURWS_IRD_REPORTING_CONSISTENCY_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/reporting/Render.hpp>       // FieldMatrix/RenderArtifact/ReportRenderFormat/token(format)
#include <sdurws/ird/reporting/ReportModel.hpp>  // ReviewReport（同源前置的基准身份）

namespace sdurws::ird::reporting {

// =====================================================================
// mismatch 维度词表（§9.4 FieldMismatch dimension 注释列＋错误轨词）
// =====================================================================

/// 值维度（valueRepr 文本规范形——数值 to_chars 逐字符比对，非浮点再解析）。
inline constexpr std::string_view kMismatchDimValue = "value";
/// 单位维度（显示单位 token；缺席面三格式各有承载形，提取器归一为缺席）。
inline constexpr std::string_view kMismatchDimUnit = "unit";
/// 状态维度（工程判定 token——HTML 经共享映射表反查，见文件头覆盖表）。
inline constexpr std::string_view kMismatchDimStatus = "status";
/// 限定语维度（token 序列比对——§6.4 硬约束：任一格式丢失＝mismatch）。
inline constexpr std::string_view kMismatchDimQualifier = "qualifier";
/// 引用维度（resultRef＋evidenceRef——仅 CSV 字段对齐携带，见文件头覆盖表）。
inline constexpr std::string_view kMismatchDimRef = "ref";
/// 缺失维度（某格式未输出该 fieldKey——§8.5 缺失字段行）。
inline constexpr std::string_view kMismatchDimMissing = "missing";
/// 解析失败词（错误轨：dimension=parse-failed 进 ReportError detail——
/// §9.4 错误行"回读本身失败（格式损坏）"；不作为 mismatch 入列）。
inline constexpr std::string_view kMismatchDimParseFailed = "parse-failed";

/// 缺席标记（expected/actual 中表达"该维度无值"——与空串值区分：
/// 空串值在比对中以空串出现，本标记含尖括号，不与任何 canonical 文本混淆）。
inline constexpr std::string_view kMismatchAbsentMark = "<absent>";

// =====================================================================
// 输入/输出值类型（§9.4 原文形状；notes 增补见文件头偏差登记）
// =====================================================================

/**
 * @brief 一致性检查输入（§9.4 原文 struct ConsistencyInput）。
 *
 * 指针全部为**借用**（调用方持有并覆盖 check() 调用期——检查器零拷贝
 * 只读，不修改任何输入：§9.4"确定性纯函数"行）。前置：artifacts 与
 * report/matrix 同源（§9.4 前置行——导出服务内部组装；检查器对可廉价
 * 验证的"工件↔报告"面以 RenderArtifact.sourceReportIdentity 执行同源
 * 校验〔违约＝调用方错误 Usage fail-fast〕，"矩阵↔报告"面按前置信任
 * ——矩阵无身份字段，复算 extractFieldMatrix 验证属渲染链职责）。
 *
 * artifacts 为 2~3 个**格式互异**的工件指针（§9.4 合法调用行"2~3 格式
 * 工件集"；空指针元素/格式重复＝调用方契约违约 Usage）。artifacts<2
 * ＝平凡通过＋注记（§9.4 ConsistencyInput 注释——多格式一致性无判定
 * 对象；结果经 ConsistencyResult.notes 表达）。
 *
 * 值语义；线程安全：纯值（指向的数据被调用方保证只读）。
 */
struct ConsistencyInput {
    /// 基准报告（冻结——contentIdentity 非零；同源判定的身份基准）。
    const ReviewReport* report = nullptr;
    /// 字段矩阵（§8.4 单次投影——七元组比对的源值基准）。
    const FieldMatrix* matrix = nullptr;
    /// 待检工件集（2~3 个、格式互异；<2 平凡通过；指针借用）。
    std::vector<const RenderArtifact*> artifacts;
};

/**
 * @brief 单字段不匹配记录（§9.4 原文 struct FieldMismatch）。
 *
 * 定位四元组 (format, fieldKey, dimension, expected/actual)——acceptance 1
 * "mismatch 定位到 (format, fieldKey, dimension)"的承载。expected/actual
 * 为该维度上的源值/回读值文本形（缺席以 kMismatchAbsentMark 表达）；
 * dimension 取维度词表常量（kMismatchDim*）。
 *
 * 值语义；线程安全：纯值。
 */
struct FieldMismatch {
    /// 不一致所在格式的工件。
    ReportRenderFormat format = ReportRenderFormat::Html;
    /// 字段对齐键（fieldKey 全报告唯一——§8.4）。
    std::string fieldKey;
    /// 源值（矩阵单元格该维度文本形；缺失维度＝源单元格值）。
    std::string expected;
    /// 回读值（工件提取该维度文本形；缺失维度＝kMismatchAbsentMark）。
    std::string actual;
    /// 维度词（kMismatchDimValue/Unit/Status/Qualifier/Ref/Missing 之一）。
    std::string dimension;
};

/**
 * @brief 一致性检查结果（§9.4 原文形状＋notes 增补——见文件头偏差登记）。
 *
 * consistent＝mismatches 全空（§8.5"一致→发布/导出"的判定位；不一致
 * →导出服务以 ConsistencyMismatch 失败、不出工件——§8.5 步⑤）。
 * fieldCount＝比对的矩阵单元格数（恒等于 matrix 大小——矩阵驱动比对；
 * acceptance 1"fieldCount 断言"的承载）。notes＝非阻断性注记（平凡通过
 * 场景必非空；正常比对为空）。
 *
 * 值语义；线程安全：纯值。
 */
struct ConsistencyResult {
    /// 是否逐字段一致（mismatches 全空）。
    bool consistent = false;
    /// 全量不匹配记录（不短路——§9.4 后置行；序＝矩阵序→工件输入序→维度序）。
    std::vector<FieldMismatch> mismatches;
    /// 比对的字段单元格数（＝矩阵大小）。
    std::size_t fieldCount = 0;
    /// 非阻断注记（平凡通过场景必非空——§9.4 ConsistencyInput 注释）。
    std::vector<std::string> notes;
};

// =====================================================================
// CSV 回读注入缝（P-RPT-1——§3.3 注入边界；公共头零 io 类型纪律）
// =====================================================================

/**
 * @brief 回读 CSV 表（注入缝的行/列载荷——io ICsvReader/RawTable 的
 *        reporting 侧投影形状）。
 *
 * header 为表头行（转义还原后）；rows 为数据行（每行字段数与 header
 * 一致——读取方按名取列）。方言行/文档分帧/前缀转义还原（NFR-SEC-03
 * ——io escapeCsvText/unescapeCsvText 唯一实现对的消费）全部行为归 io
 * 适配实现（真实）或测试替身（P-RPT-1 裁决前）；本结构零行为。
 *
 * 值语义；线程安全：纯值。
 */
struct ReportCsvTable {
    /// 表头行（列名——主表首列恒 "field_key"，附表首列 code/case_id）。
    std::vector<std::string> header;
    /// 数据行集（行序＝文件序；字段序＝列序）。
    std::vector<std::vector<std::string>> rows;
};

/**
 * @brief CSV 读取器投影（P-RPT-1 注入缝——io ICsvReader 的报告侧同形
 *        投影；"不私建第二套 CSV 解析器"的承载面）。
 *
 * 契约（§3.3 注入边界＋NFR-SEC-03）：
 *   - 实现方（L5 装配的 io 适配器）消费 io ICsvReader 完成真实解析：
 *     方言标识行判别（#rwcsv1）、文档分帧（报告 CSV 工件＝三份文档顺序
 *     拼接、文档间空行分隔——Render.hpp CsvReportRenderer 类注）、RFC4180
 *     去引号、前缀转义还原（带标识文件剥离恰一个 ' 前缀——§5.3 导入行）；
 *   - reporting 消费侧（一致性检查器）**不做任何 CSV 字节解析**——转义
 *     唯一实现归 io（SA-12/NFR-SEC-03 M-15），检查器只按列名取字段；
 *   - P-RPT-1/P-IO-1 合并裁决补边后直连 io、签名零改动（§3.3 纪律）。
 *
 * 会话约束：一个实例一次 readTables（io ICsvReader"一个实例绑定一个
 * 文件一次读取"同形）；实例单线程使用。
 */
class IReportCsvReader {
public:
    virtual ~IReportCsvReader() = default;

    /**
     * @brief 读取内存工件字节中的全部 CSV 表（文档分帧＋转义还原——
     *        行为全归实现方）。
     *
     * @param bytes [in] 工件完整字节（UTF-8；借用——调用须覆盖本调用期）
     * @param size  [in] 字节数
     * @param out   [out] 表集接收容器（按文档序追加；调用前不必为空，
     *              成功时本调用追加的表在前次内容之后——检查器每工件
     *              新建容器，无复用场景）
     * @return true＝解析成功（out 追加全部表）；false＝格式损坏（方言
     *         不识别/引号不闭合/结构截断等——调用方以 parse-failed 轨
     *         报 ConsistencyMismatch，§9.4 错误行）
     *
     * 复杂度：O(字节数)（单遍解析）。
     */
    virtual bool readTables(const std::uint8_t* bytes, std::size_t size,
                            std::vector<ReportCsvTable>& out) = 0;
};

/**
 * @brief CSV 读取器工厂（P-RPT-1 注入缝——与 Render.hpp IReportIoFactory
 *        同款注入形态：reporting 定义、L5 装配适配 io，裁决补边后直连、
 *        签名零改动）。
 *
 * 工厂语义（与 IReportCsvReader 会话约束配套）：每次 makeCsvReader()
 * 产出**独立新实例**（一次读取一个实例）——检查器因此保持无状态纯函数
 * 性（并发 check() 各取各的 reader，§9.4 并发安全行）。工厂无状态、
 * 并发只读安全。
 */
class IReportCsvReaderFactory {
public:
    virtual ~IReportCsvReaderFactory() = default;

    /// 产出独立 CSV 读取器（io 适配实现——真实解析行为载体；可返回
    /// nullptr 表示适配缺位，检查器对 CSV 工件以 parse-failed 轨处理）。
    virtual std::unique_ptr<IReportCsvReader> makeCsvReader() const = 0;
};

// =====================================================================
// IReportConsistencyChecker（§9.4——纯函数检查器）
// =====================================================================

/**
 * @brief 多格式逐字段一致性检查器接口（§9.4）。
 *
 * 契约（§9.4 注释行逐条）：
 *   - 前置：artifacts 与 report/matrix 同源（导出服务内部组装——可验证
 *     面见 ConsistencyInput 注释）；artifacts 为 2~3 个格式互异工件；
 *   - 后置：逐字段回读比对（§8.5：JSON parse/CSV io reader 回读/HTML
 *     data-field 提取）；mismatches 全量列出（不短路）；
 *   - 错误：回读本身失败（格式损坏）→ ConsistencyMismatch（dimension=
 *     parse-failed）——**抛出**（ReportError，错误轨）而非入列；调用方
 *     前置违约（空指针/工件格式重复/同源身份不符/报告未冻结）→ Usage；
 *   - 取消：无（上界＝字段数×格式数）。
 *
 * 线程：并发安全。副作用：零（纯函数——同输入同结论，不修改任何输入）。
 * 合法调用：2~3 格式工件集。非法调用：以"格式存在"当"内容一致"（本
 * 检查器即其反义面）；跳过限定语维度（§6.4 硬约束——qualifier 全格式
 * 比对，见文件头覆盖表）。
 */
class IReportConsistencyChecker {
public:
    virtual ~IReportConsistencyChecker() = default;

    /**
     * @brief 逐字段一致性检查（§9.4 原文签名）。
     *
     * @param input [in] 检查输入（借用，见 ConsistencyInput 注释）
     * @return 全量比对结果（consistent/mismatches/fieldCount/notes）
     *
     * @throws ReportError Usage（前置违约——空 report/matrix、工件指针
     *         空、工件格式重复、工件与报告同源身份不符、报告未冻结）/
     *         ConsistencyMismatch（某工件回读失败——detail 含
     *         dimension=parse-failed 与格式 token；detail 为开发诊断面，
     *         面向用户前经 diagnostics 脱敏，§5.8 同则）
     *
     * 复杂度：O(字段数×格式数＋工件字节数)（提取一遍线性＋矩阵驱动查表
     * 均摊 O(1)——§9.4 上界行的实施形）。
     */
    virtual ConsistencyResult check(const ConsistencyInput& input) = 0;
};

/**
 * @brief 字段级一致性检查器（IReportConsistencyChecker 的产品实现——
 *        RPT-T07 交付面）。
 *
 * 回读提取分工（§8.5 步④）：
 *   - JSON：内部严格解析器（ird-report-json/1——reporting 自有 schema
 *     的回读面；顶层键集消费 kJsonTopLevelKeys 冻结集拒绝外来未知字段，
 *     NFR-DEP-04 读取侧）→ sections/entries/fields 字段遍历；
 *   - CSV：经注入 IReportCsvReaderFactory 取 reader 回读（本类**零 CSV
 *     字节解析**——转义唯一实现归 io，NFR-SEC-03/acceptance 5），按列名
 *     映射主表（首列 field_key）；
 *   - HTML：data-field 提取器（模板自有的机器可解析结构——§8.5；严格
 *     行列文法扫描，结构截断/未闭合＝parse-failed 轨）。
 *
 * 构造注入的工厂引用为**借用**（调用方〔导出服务装配/测试〕持有并覆盖
 * 本检查器存续期——§3.3 注入边界同则）；工厂须并发只读安全（并发
 * check() 经工厂各取独立 reader）。
 */
class FieldConsistencyChecker final : public IReportConsistencyChecker {
public:
    /**
     * @brief 注入构造（CSV 回读缝——P-RPT-1 处置：acceptance 5）。
     * @param csvReaders [in] CSV 读取器工厂（借用；并发只读安全）
     */
    explicit FieldConsistencyChecker(const IReportCsvReaderFactory& csvReaders) noexcept;

    /// 非拷贝（借用语义——工厂引用成员）。
    FieldConsistencyChecker(const FieldConsistencyChecker&) = delete;
    FieldConsistencyChecker& operator=(const FieldConsistencyChecker&) = delete;

    ConsistencyResult check(const ConsistencyInput& input) override;

private:
    const IReportCsvReaderFactory* m_csvReaders;  ///< CSV 回读工厂（借用；无状态）
};

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_CONSISTENCY_HPP
