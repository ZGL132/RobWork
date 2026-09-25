/**
 * @file   Import.hpp
 * @brief  需求导入器——CSV/JSON 字段映射（字段字典自动识别＋手动改映射）、
 *         单位预览与 SI 归一、行级逐条错误（部分成功）与副本导出。
 *
 * 设计依据：
 *   - units/requirements.md §7.3（CSV 坐标表导入——字段字典冻结表/缺失与
 *     多余字段/行级错误三要素/单位缺省与声明/部分成功/导入溯源）、§7.4
 *     （JSON 导入导出——同一字段字典与安全规则/exportCopy 副本导出/
 *     draft 标记）、§7.5（预览与正式分离——导入只产草稿）、§9.5
 *     （IRequirementImporter 接口契约）、§9.6（T04 行四码）、§10.2
 *     （V-08/V-09/V-10 观测点）
 *   - 需求 REQ-05（CSV 坐标表导入——字段映射/单位预览/逐行错误；CSV 按
 *     数据解析不执行公式）、REQ-12（需求定义 JSON 导入导出——同一字段
 *     字典与安全规则；导出为副本不影响项目）、NFR-SEC-03（数据-only 解析
 *     ＋导出转义归 io 唯一转义点）、AT-02（错误行保留正确行、错误定位到
 *     列与原文）、AT-24（JSON roundtrip 副本语义）、NFR-COR-01/02/03
 *   - 任务契约 tasks/foundation/WP-14-T04.json acceptance 1~6
 *
 * 背景说明（职责边界——为什么本单元"只映射不解析"）：
 *   CSV/JSON 的底层解析归 io（R-REQ-4/SA-12：方言、编码、转义还原、
 *   预算全部是 io 所有权）——本单元的输入恒为 io 已解析产物
 *   （io::RawTable / JSON 字节经 io IStructuredDataReader 通道），mapCsv
 *   **不读文件**（纯函数，同输入同输出，NFR-COR-01）；任何取消/失败＝
 *   无草稿无修订无半成品（§7.5，导入只产出内存草稿条目，落盘经编辑器
 *   DraftService、产生修订经命令端口——本单元零触达，PA-1）。
 *
 * 实现决策登记（卡面 §9.5 签名的等价调整与细化，DTB §5.4 同源口径）：
 *   1. 卡面签名的 `io::ExportTarget` 在 io 单元（§1.2 基线，P-REQ-8 处
 *      置）不存在——io 磁盘落位形态为 CsvOutputTarget/JsonOutputTarget/
 *      IAtomicFileWriter（io.md §9.0"签名均为实现建议，实现期允许等价
 *      调整、语义不变"的同源纪律）。本头在 requirements 侧定义
 *      ExportTarget{filePath, draft}（导出目标路径＋草稿标记），写出
 *      通道仍全部经 io：CSV 走 ICsvWriter 文件目标（内部"暂存＋原子替
 *      换"，Csv.hpp §9.4 等价承载），JSON 走 IAtomicFileWriter
 *      （prepare→write→commit，OverwriteAtomic）——"io AtomicFile 原子
 *      写出、失败时旧文件完好"（§7.4）语义不变。
 *   2. CSV 源摘要口径：mapCsv 的输入 RawTable 不携带文件字节（io 流式
 *      契约），sourceDigest 取"RawTable 规范投影"的 SHA-256（表头＋各
 *      行原文按 \x1F 分隔串接——Import.cpp digestRawTable 单点实现）：
 *      内容相同的表必得同摘要（CON-05 内容寻址一致口径）；JSON 通道
 *      sourceDigest＝输入字节原文的 SHA-256（mapJson 直接持有字节）。
 *      两通道摘要输入不同构，跨通道摘要不可比（各通道内部自洽）。
 *   3. 行号口径：RawTable 不携带物理行号（io 流式交付），行级错误的行
 *      号＝数据行在 RawTable::rows 中的序号（1 起）；recordNumber 同源
 *      （I-REQ-8 导入溯源的记录号）。
 *   4. 导入条目的 ObjectId 恒为默认构造（全零保留值）——正式分配权归
 *      project 命令 prepare（§7.3"新 ObjectId 在命令 prepare 分配"、
 *      O-36 口径）；纯函数确定性（同输入同输出）因此与随机数生成解耦。
 *
 * 线程安全：IRequirementImporter 实现无共享可变状态（字段字典为只读冻
 * 结表）——const 方法可并发调用（§3.4 纯函数服务总约定）。JSON 通道内
 * 部的 io JsonProfileRegistry 装配期注册完成后并发只读（io.md §9.5）。
 * 确定性（NFR-COR-01/02）：数值解析 std::from_chars（不经 locale）、单
 * 位换算经 core 唯一入口（NFR-COR-03 不静默转 0）、错误收集按字典序＋
 * 行序稳定排列；同输入两次调用产出逐字段一致（含诊断）。
 */

#ifndef IRD_REQUIREMENTS_IMPORT_HPP
#define IRD_REQUIREMENTS_IMPORT_HPP

#include <sdurws/ird/core/DiagData.hpp>   // core::DiagnosticRecord——行级/列级诊断载体
#include <sdurws/ird/core/Digest.hpp>     // core::Digest256——源内容摘要（I-REQ-8）
#include <sdurws/ird/io/AtomicFile.hpp>   // io::IAtomicFileWriter——JSON 副本导出原子写出（可注入）
#include <sdurws/ird/io/Csv.hpp>          // io::RawTable——mapCsv 唯一输入（io 已解析产物）
#include <sdurws/ird/requirements/Editor.hpp>          // RequirementWorkingSet——exportCopy 输入
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // TaskPoint/ImportProvenance——导入产出条目

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sdurws::ird::requirements {

// =====================================================================
// 字段字典（§7.3 冻结表——WP-14-T01 交付物零改动承接；CSV/JSON 同源，
// REQ-12"同一字段字典与安全规则"）
// =====================================================================

/**
 * @brief 字段字典的字段标识（§7.3 冻结表 20 字段，枚举序＝卡面原文序）。
 *
 * 持久化契约面纪律：冻结表不改名不重排（WP-14-T01 交付物——本枚举只是
 * 表的身份索引，字段 canonical 名以 importFieldToken/importField 的冻结
 * 串为准）；扩展走单元卡增量修订（表尾追加）。
 */
enum class ImportField : std::uint8_t {
    Id,              ///< "id"——条目标识串（导入批次内去重键；正式 ObjectId 归命令分配）
    Name,            ///< "name"——语义名（集合内唯一，I-REQ-3）
    ProcessTag,      ///< "process_tag"——工艺标签（11 值词表，ObjectTypes.hpp）
    Level,           ///< "level"——需求等级（Must|Should）
    Enabled,         ///< "enabled"——启用（true|false）
    RefFrame,        ///< "ref_frame"——参考坐标系（引用文本语法：""|"World"→World；
                     ///< "model:<obj-…>"→ModelFrame；"scene:<obj-…>"→SceneObject；
                     ///< 裸"<obj-…>"→ModelFrame＋REQ-IMPORT-FRAME-UNKNOWN 悬空警告）
    Tcp,             ///< "tcp"——工具/TCP 引用（""→未引用；"DefaultTcp"→DefaultTcp；
                     ///< "tool:<obj-…>|<tcpKey>"→Tool；其余→行级错误）
    X,               ///< "x"——位置 X（长度列，m 落库）
    Y,               ///< "y"——位置 Y（长度列，m 落库）
    Z,               ///< "z"——位置 Z（长度列，m 落库）
    Roll,            ///< "roll"——姿态 roll（角度列，rad 落库）
    Pitch,           ///< "pitch"——姿态 pitch（角度列，rad 落库）
    Yaw,             ///< "yaw"——姿态 yaw（角度列，rad 落库）
    PosTol,          ///< "pos_tol"——位置容差（长度列，m；>0——I-REQ-5）
    OriTol,          ///< "ori_tol"——姿态容差（角度列，rad；>0——I-REQ-5）
    ApproachAxis,    ///< "approach_axis"——接近/撤离轴（ToolZ|ReferenceZ）
    ApproachDist,    ///< "approach_dist"——接近段距离（长度列，m；>0）
    RetractDist,     ///< "retract_dist"——撤离段距离（长度列，m；>0）
    MinJointMargin,  ///< "min_joint_margin"——最小关节裕量（无单位列——rad/m 依关节类型，原样落库）
    Note,            ///< "note"——备注（原文承载）
};

/// 冻结表字段数（§7.3 表 20 列）。
inline constexpr std::size_t kImportFieldCount = 20;

/**
 * @brief 字段的 canonical 名（§7.3 冻结表"canonical 名"列原文串）。
 *
 * @param field [in] 字段标识（全表 20 值均有 token——switch 全枚举）
 * @return canonical 名（静态存储期；表头自动识别与导出表头共用——
 *         唯一书写点，禁字面量第二处）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同字段同串）。
 */
std::string_view importFieldToken(ImportField field) noexcept;

/**
 * @brief 字段列的数量种类（决定单位缺省与可声明单位——§7.3"长度列默认
 *        m、角度列默认 rad，可按列声明 mm/deg"）。
 */
enum class ImportColumnKind : std::uint8_t {
    Text,    ///< 文本列（无单位语义——词表/布尔/标识/备注）
    Length,  ///< 长度列（缺省 m；可声明 mm/cm 等注册表长度单位）
    Angle,   ///< 角度列（缺省 rad；可声明 deg 等注册表角度单位）
    Number,  ///< 无单位数值列（min_joint_margin——rad/m 依关节类型，不参与单位换算）
};

/**
 * @brief 冻结表单字段规格（canonical 名/必备性/数量种类/别名集）。
 *
 * 纯值聚合；字段序（fields() 返回序）＝§7.3 冻结表原文序（确定性序）。
 * aliases 含中英文常见表头写法（精确等值匹配——不折叠大小写、不剥离
 * 空白：NFR-COR-03 不改写用户输入，表格污染由导入报告暴露而非静默纠
 * 错）；canonical 名本身参与识别且优先级最高。
 */
struct FieldSpec {
    ImportField field;                             ///< 字段标识
    bool required;                                 ///< 必备列（id/name/x/y/z——缺列＝结构级拒绝）
    ImportColumnKind kind;                         ///< 数量种类（单位缺省与可声明集的依据）
    std::vector<std::string_view> aliases;         ///< 别名集（canonical 名之外；静态存储期串）
};

/**
 * @brief 字段字典（§7.3 冻结表的运行时形态；REQ-12"同一字段字典"）。
 *
 * 生命周期：由 RequirementImporter 持有（构造期装配冻结表，之后只读）；
 * fieldDictionary() 以 const 引用暴露给向导做表头自动识别与单位预览
 * （卡 §9.5 原文"供向导自动识别表头与单位预览"）。线程安全：只读共享。
 */
class FieldDictionary {
public:
    /// 装配 §7.3 冻结表（20 字段；必备性/种类/别名集见 FieldSpec 注）。
    FieldDictionary();

    /// 冻结表全量（序＝卡面原文序）。
    const std::vector<FieldSpec>& fields() const noexcept { return fields_; }

    /// 按字段标识取规格（field 必为表内值——枚举即索引身份）。
    const FieldSpec& spec(ImportField field) const noexcept;

    /**
     * @brief 表头名 → 字段（自动识别单点头；canonical 优先、别名次之）。
     *
     * 匹配规则：精确等值（区分大小写、不剥离空白——见 FieldSpec 注）。
     * @param header [in] 表头单元格原文
     * @return 命中＝字段标识；未命中＝nullopt（调用方计入未识别列）
     *
     * 纯函数；线程安全；确定性。
     */
    std::optional<ImportField> tryRecognize(std::string_view header) const noexcept;

private:
    std::vector<FieldSpec> fields_;  ///< 冻结表（构造期装配，之后只读）
};

// =====================================================================
// 字段映射（向导"自动识别＋手动改映射"的承载——§7.3 时序图第二段）
// =====================================================================

/**
 * @brief 字段→列的映射表（mapCsv 的 mapping 参数）。
 *
 * 值语义；columnOf 以 ImportField 为索引（kImportFieldCount 定长数组）。
 * 未映射的必备列（id/name/x/y/z）＝结构级拒绝（该文件不可导入——§7.3
 * "必填列缺失"）；未映射的可选列＝缺省值＋报告（defaultedFields）。
 *
 * 线程安全：纯值；确定性：映射即输入，无隐式重排。
 */
struct FieldMapping {
    /// "未映射"哨兵列号（std::size_t 最大值——合法列号不可能达到）。
    static constexpr std::size_t kNoColumn = static_cast<std::size_t>(-1);

    /// 各字段映射的列号（0 起对 RawTable 行字段下标；kNoColumn＝未映射）。
    std::array<std::size_t, kImportFieldCount> columnOf{};

    /// 全未映射的空映射（手动逐列 assign 的起点）。
    static FieldMapping none() noexcept
    {
        FieldMapping m;
        m.columnOf.fill(kNoColumn);
        return m;
    }

    /// 字段是否已映射列。
    bool isMapped(ImportField field) const noexcept
    {
        return columnOf[static_cast<std::size_t>(field)] != kNoColumn;
    }

    /// 字段映射的列号（未映射＝kNoColumn）。
    std::size_t columnFor(ImportField field) const noexcept
    {
        return columnOf[static_cast<std::size_t>(field)];
    }

    /// 手动改映射（向导下拉框回写入口；覆盖自动识别结果——幂等）。
    void assign(ImportField field, std::size_t column) noexcept
    {
        columnOf[static_cast<std::size_t>(field)] = column;
    }

    /// 取消映射（改判为未映射——可选列走缺省、必备列走结构级拒绝）。
    void clear(ImportField field) noexcept
    {
        columnOf[static_cast<std::size_t>(field)] = kNoColumn;
    }
};

/**
 * @brief 表头自动识别产出（autoDetectMapping 的返回值）。
 */
struct AutoDetectResult {
    FieldMapping mapping;                        ///< 识别出的映射（未识别列不映射）
    std::vector<std::size_t> unrecognizedColumns; ///< 未识别列号（0 起，升序——进忽略项清单）
};

/**
 * @brief 表头自动识别（§7.3 时序图"字段字典自动识别"的域侧实现）。
 *
 * 对 table.report.header 逐列调 FieldDictionary::tryRecognize；同一字段
 * 命中多列时**首列胜出**（先到先得——确定性；后到列计入未识别清单，向
 * 导可手动改映射消解）；识别不出（canonical/别名全不匹配）的列进
 * unrecognizedColumns（导入时进 ignoredColumns 报告——"未映射/多余列
 * →忽略项清单（不静默丢弃）"）。
 *
 * @param table      [in] io 已解析表（须 hasHeader——无表头时自动识别无
 *                   意义，属调用方契约违约）
 * @param dictionary [in] 字段字典（fieldDictionary() 暴露的冻结表）
 * @return 识别结果（mapping＋未识别列清单）
 *
 * @throws std::invalid_argument table 无表头（report.hasHeader==false）
 *
 * 纯函数；线程安全；确定性。
 */
AutoDetectResult autoDetectMapping(const io::RawTable& table,
                                   const FieldDictionary& dictionary);

// =====================================================================
// 单位声明与预览（§7.3"长度列默认 m、角度列默认 rad，可按列声明 mm/deg
// ——预览展示换算结果，落库前经 core 唯一换算归一 SI"；NFR-COR-03）
// =====================================================================

/**
 * @brief 按字段的单位声明（mapCsv/previewUnitConversion 的 units 参数）。
 *
 * unitByField 缺省全空串＝按列种类取缺省（Length→"m"、Angle→"rad"、
 * Text/Number 无单位——声明非空即列种类不符）。合法声明＝core
 * UnitToken 注册表内的 token 且量纲与列种类一致（Length↔Length、
 * Angle↔Angle）；违约→REQ-IMPORT-UNIT-ILLEGAL（逐列诊断＋该列各行按
 * "单位无法换算"行错误处置——§7.3 行级错误清单分支）。
 *
 * 线程安全：纯值；确定性：声明即输入。
 */
struct ImportUnitOptions {
    /// 每字段声明的单位 token（""＝按列种类取缺省；仅长度/角度列可非空）。
    std::array<std::string, kImportFieldCount> unitByField{};

    /// 缺省选项（全空声明——长度 m/角度 rad）。
    static ImportUnitOptions defaults() noexcept { return ImportUnitOptions{}; }

    /// 声明某列单位（覆盖缺省；token 合法性在 mapCsv 消费时统一校验）。
    void setUnit(ImportField field, std::string token)
    {
        unitByField[static_cast<std::size_t>(field)] = std::move(token);
    }

    /// 取某列声明（""＝缺省）。
    const std::string& unitFor(ImportField field) const noexcept
    {
        return unitByField[static_cast<std::size_t>(field)];
    }
};

/**
 * @brief 单位列预览条目（previewUnitConversion 的产出单元）。
 */
struct UnitPreviewEntry {
    ImportField field;              ///< 字段标识
    std::string declaredUnit;       ///< 实际生效单位 token（含缺省补全后的结果）
    std::string rawSample;          ///< 首个非空单元格原文（原样——预览不改写）
    std::optional<double> siSample; ///< 样本经 core 唯一换算的 SI 值（m/rad）；样本
                                    ///< 非法数值＝nullopt（预览如实呈现，不猜测）
    bool unitUsable = true;         ///< 单位声明可换算（false＝声明非法——向导应提示）
};

/**
 * @brief 单位换算预览（§7.3"换算预览"的域侧实现；V-09 观测点前半）。
 *
 * 对 mapping 中已映射的长度/角度列逐列产出预览：声明补全→首个非空样本
 * 原文→core tryConvert 归一 SI。预览是纯计算（不产诊断、不产条目）——
 * 与 mapCsv 用同一套声明校验与换算入口（语义单源：预览值＝落库值，
 * NFR-MNT-04）；向导据此向用户展示"1000（mm）→ 1（m）"式换算。
 *
 * @param table      [in] io 已解析表
 * @param mapping    [in] 字段映射
 * @param units      [in] 单位声明
 * @param dictionary [in] 字段字典
 * @return 预览条目（序＝冻结表字典序，仅含已映射的长度/角度列）
 *
 * @throws std::invalid_argument mapping 列号越界（同 mapCsv 前置）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<UnitPreviewEntry> previewUnitConversion(const io::RawTable& table,
                                                    const FieldMapping& mapping,
                                                    const ImportUnitOptions& units,
                                                    const FieldDictionary& dictionary);

// =====================================================================
// 导入产出（§7.3 ImportOutcome{entries[], rowErrors[], sourceDigest}）
// =====================================================================

/**
 * @brief 导入产出（纯函数值对象——不携带任何文件/项目句柄）。
 *
 * 语义分段（§7.3/§7.5）：
 *   - status==Rejected：结构级拒绝（必填列缺失/JSON 文档级失败）——该
 *     文件不可导入，entries 恒空；结构级诊断（REQ-IMPORT-UNIT-ILLEGAL
 *     的"列缺失"分支等）经 mapCsv/mapJson 的 diags 出参承载。
 *   - status==Partial：行粒度部分成功（≥1 行错误）——正确行保留为
 *     entries，错误行逐条进 rowErrors（AT-02"错误行保留正确行、错误
 *     定位到列与原文"）。
 *   - status==Completed：全部行成功（含空表——零行合法）。
 *
 * 生命周期/所有权：纯值，调用方所有；entries 内 TaskPoint 的 objectId
 * 恒为全零（待命令 prepare 分配——见文件头注实现决策 4）。
 */
struct ImportOutcome {
    /// 导入完成状态（三分语义见类注）。
    enum class Status : std::uint8_t { Completed, Partial, Rejected };

    Status status = Status::Rejected;                   ///< 完成状态（缺省拒绝——防御性安全）
    std::vector<TaskPoint> entries;                     ///< 正确行草稿条目（文档序——确定性）
    std::vector<core::DiagnosticRecord> rowErrors;      ///< 行级错误（REQ-IMPORT-ROW-ERROR／
                                                        ///< REQ-IMPORT-DUPLICATE-ID；行序稳定）
    std::vector<std::string> ignoredColumns;            ///< 未映射/未识别列清单（表头名或列号文本——
                                                        ///< "不静默丢弃"报告，§7.3）
    std::vector<std::string> defaultedFields;           ///< 缺省补全的可选列 canonical 名（文件级
                                                        ///< 缺列＋行内空单元格同列合并去重，字典序）
    core::Digest256 sourceDigest{};                     ///< 源内容摘要（CSV＝RawTable 规范投影、
                                                        ///< JSON＝输入字节——文件头注实现决策 2）
    bool sourceMarkedDraft = false;                     ///< 源文档带 "draft":true 标记（JSON 通道
                                                        ///< 观测面——防草稿副本误当正式数据）
};

// =====================================================================
// 副本导出（§7.4——REQ-12"导出为副本不影响项目"）
// =====================================================================

/// 导出格式（CSV＝§7.3 字段字典表；JSON＝§7.4 文档投影）。
enum class ExportFormat : std::uint8_t {
    Csv,   ///< CSV 坐标表（io ICsvWriter canonical 写出——唯一转义点 SA-12）
    Json,  ///< JSON 文档（io canonicalizeJson＋IAtomicFileWriter 原子就位）
};

/**
 * @brief 导出目标（卡面 §9.5 `io::ExportTarget` 的 requirements 侧等价
 *        承载——文件头注实现决策 1；io 磁盘基线无该类型，P-REQ-8 处置）。
 *
 * draft 语义（§7.4"草稿导出带 draft:true 标记防误当正式数据"）：true＝
 * 导出内容来自草稿——JSON 文档根写入 "draft": true；CSV 无草稿标记通道
 * （方言标识行归 io 方言契约、数据表不引入第二标记机制——NFR-DEP-04），
 * draft=true 的 CSV 导出请求被**值面拒绝**（ExportOutcome.ok=false，防
 * 草稿数据被误当正式数据的 fail-visible 处置，不静默省略标记）。
 */
struct ExportTarget {
    std::filesystem::path filePath;  ///< 发布目标路径（父目录必须已存在；原子替换就位）
    bool draft = false;              ///< 是否草稿导出（见类注——JSON 写标记、CSV 拒绝）
};

/**
 * @brief 导出产出（值面错误——不抛；失败时旧文件完好，§7.4）。
 */
struct ExportOutcome {
    bool ok = false;                  ///< true＝目标原子就位；false＝目标不变＋error 有效
    std::uint64_t bytesWritten = 0;   ///< 写出字节数（成功面；canonical 字节长度）
    std::string error;                ///< 失败细节（开发级——用户文案经 diagnostics 脱敏）
};

// =====================================================================
// IRequirementImporter（§9.5 接口契约——纯函数服务，可重入无副作用）
// =====================================================================

/**
 * @brief 需求导入器接口（§9.5 原文签名；ExportTarget 等价调整见其类注）。
 *
 * 线程约束：纯函数服务——const 方法可并发调用（§9.5"纯函数服务：可重
 * 入、无副作用、值语义产出"）。错误语义：调用方前置违约 fail-fast（异
 * 常——AGENTS.md 错误语义），源数据问题走值面（ImportOutcome/Diagnostic
 * Record——正常业务路径）。
 */
class IRequirementImporter {
public:
    virtual ~IRequirementImporter() = default;

    /**
     * @brief CSV 表 → 任务点草稿条目（§9.5 mapCsv）。
     *
     * @pre table 为 io ICsvReader 产物（retainRows=true 的 RawTable 或等
     *      价直构）；本函数不自行读文件（纯函数——REQ-05"导入只映射"）。
     * @post 正确行→条目草稿（importProvenance={sourceDigest, 行序}）；
     *       错误行→rowErrors 逐条（行号/列名/原文/原因）；同输入同输出。
     *
     * @param table    [in] io 已解析表（rows 与 report.dataRows 必须一致）
     * @param mapping  [in] 字段映射（自动识别产物或手动改映射）
     * @param units    [in] 单位声明（缺省 m/rad）
     * @param diags    [in,out] 列级/结构级/警告诊断出参（REQ-IMPORT-UNIT-
     *                 ILLEGAL/REQ-IMPORT-FRAME-UNKNOWN；追加不覆盖）
     * @return 导入产出（状态三分语义见 ImportOutcome 注）
     *
     * @throws std::invalid_argument mapping 列号越界、rows 与 dataRows 不
     *         一致（调用方契约违约——fail-fast）
     */
    virtual ImportOutcome mapCsv(const io::RawTable& table, const FieldMapping& mapping,
                                 const ImportUnitOptions& units,
                                 std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief JSON 文档字节 → 任务点草稿条目（§9.5 mapJson）。
     *
     * 解析经 io JSON 通道（§7.4"解析经 io JSON 通道（JsonProfile 注册
     * 制）"——本单元自持 profile "ird-requirements-import/1"：版本判定
     * 先于结构校验，未知顶层键拒绝（NFR-DEP-04））；字段语义校验/映射
     * 同 mapCsv（§7.4"语义校验/映射同 §7.3"）。
     *
     * @param jsonBytes [in] UTF-8 文档字节（原文——sourceDigest 的摘要输入）
     * @param diags     [in,out] 诊断出参（同 mapCsv；文档级失败以 row=0
     *                  哨兵承载 REQ-IMPORT-ROW-ERROR）
     * @return 导入产出
     *
     * @throws std::invalid_argument jsonBytes 为空（无文档可解析——调用
     *         方契约违约）
     */
    virtual ImportOutcome mapJson(const std::vector<std::uint8_t>& jsonBytes,
                                  std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief 副本导出（§9.5 exportCopy；REQ-12"导出为副本不影响项目"）。
     *
     * 导出内容＝工作集的任务点集合（§7.3 字段字典的对称导出面——区域/
     * 工况/计划不在坐标表字典内，其副本走正式工件通道，不在本接口）。
     * CSV 经 io ICsvWriter（canonical 写出＋内部原子替换）；JSON 经
     * io canonicalizeJson（canonical 字节）＋IAtomicFileWriter
     * （prepare→write→commit，OverwriteAtomic）——失败时旧文件完好
     * （§7.4）。不产生修订、不触达项目（结构保证：签名无项目句柄）。
     *
     * @param ws      [in] 需求工作集（只读——导出不改写）
     * @param format  [in] 导出格式
     * @param target  [in] 目标路径＋草稿标记（draft 语义见 ExportTarget 注）
     * @return 导出产出（ok=false 时目标不变）
     *
     * @throws std::invalid_argument target.filePath 为空（调用方契约违约）
     */
    virtual ExportOutcome exportCopy(const RequirementWorkingSet& ws, ExportFormat format,
                                     const ExportTarget& target) const = 0;

    /**
     * @brief 字段字典（冻结表 §7.3；供向导自动识别表头与单位预览）。
     * @return 冻结表只读引用（实现对象存活期内有效）
     */
    virtual const FieldDictionary& fieldDictionary() const noexcept = 0;
};

// =====================================================================
// 产品实现
// =====================================================================

/**
 * @brief IRequirementImporter 的产品实现（无状态——字典与 profile 注册
 *        表为只读成员；JSON 导出的原子写出器可注入以便故障注入测试）。
 *
 * 生命周期：调用方持有（值语义依赖成员）；并发 const 调用安全。
 */
class RequirementImporter final : public IRequirementImporter {
public:
    /// 生产装配（JSON 导出走 io::makeAtomicFileWriter 真实实现）。
    RequirementImporter();

    /**
     * @brief 测试装配（注入原子写出器——故障注入测试以 fake 替换，验证
     *        "commit 失败→旧文件完好"；io AtomicFile.hpp 故障注入口径）。
     * @param atomicWriter [in] 写出器（共享所有权；可为 null＝生产工厂延迟构造）
     */
    explicit RequirementImporter(io::IAtomicFileWriterPtr atomicWriter);

    ImportOutcome mapCsv(const io::RawTable& table, const FieldMapping& mapping,
                         const ImportUnitOptions& units,
                         std::vector<core::DiagnosticRecord>& diags) const override;
    ImportOutcome mapJson(const std::vector<std::uint8_t>& jsonBytes,
                          std::vector<core::DiagnosticRecord>& diags) const override;
    ExportOutcome exportCopy(const RequirementWorkingSet& ws, ExportFormat format,
                             const ExportTarget& target) const override;
    const FieldDictionary& fieldDictionary() const noexcept override
    {
        return dictionary_;
    }

private:
    FieldDictionary dictionary_;                 ///< §7.3 冻结表（构造期装配，只读）
    io::IAtomicFileWriterPtr atomicWriter_;      ///< JSON 导出原子写出器（测试注入点）
};

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_IMPORT_HPP
