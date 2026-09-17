/**
 * @file   Csv.hpp
 * @brief  CSV 读写器——方言标识行（v1）、可逆转义编码（唯一转义/还原实现）、
 *         流式读取（逐行错误定位/部分成功）与 canonical 写出。
 *
 * 设计依据：
 *   - units/io.md §5.1（方言标识行 v1 语法——封闭键集/缺省值/字节级精确
 *     判定/P-IO-5 语法冻结）、§5.2（外部 CSV 编码探测与分隔符嗅探——BOM
 *     判定/无 BOM 严格 UTF-8 校验稳定拒绝 IO-FORMAT-CSV-ENCODING/候选
 *     统计嗅探）、§5.3（可逆转义编码——导出前置恰一个 '、导入带标识剥
 *     恰一个 '、无标识零改写、decode(encode(s))==s 自反）、§5.4（结构问
 *     题处置——DUPCOL/ARITY/QUOTE/CHAR/空行策略）、§5.5（roundtrip 语义
 *     ——数据层逐字符一致/文件层字节一致/canonical 写出）、§5.6（流式/
 *     逐行错误定位/部分成功/maxRowErrors 截断/CsvCell）、§5.8（RawTable
 *     归 io 产出调用方所有/原文保留/行粒度部分成功/零项目修订）
 *   - units/io.md §9.3（ICsvReader 契约表）、§9.4（ICsvWriter 契约表）、
 *     §9.0（公共约定：签名均为实现建议，实现期允许等价调整、语义不变）、
 *     §3.1（公共头表 Csv.hpp 行：CsvDialect/CsvCell/RawTable/CsvParseReport/
 *     ICsvReader/ICsvWriter）
 *   - 需求 NFR-SEC-03（导出方言标识＋前缀转义防公式注入；R5 roundtrip；
 *     M-15 唯一转义实现）、REQ-05（CSV 导入解析与逐行错误支撑）、AT-02
 *     （正确行保留、错误定位到列与原文、预览不产生正式证据由调用方保证）
 *   - 任务契约 tasks/foundation/IO-T03.json（≙WP-11-T04，DTB §2.12）acceptance 1~5
 *
 * 背景说明（为什么转义/还原只在 io 做且只有一份——SA-12/NFR-SEC-03 M-15）：
 *   公式注入防护的完整口径是"数据-only 解析＋导出前缀转义"（§5.3）：本软
 *   件导出的 CSV 若被表格软件打开，以 = + - @ ' 开头的单元格可能被当成
 *   公式/宏执行。防护手段不是"清除"疑似公式字符（那会改写用户数据），
 *   而是导出时前置一个 ' 前缀使表格软件按文本处理；导入本软件文件时剥
 *   离该前缀还原原文。因此转义形式只允许存在于文件层（io 内部缓冲＋导出
 *   文件），绝不进入结构化数据层（RawTable 保存的是剥离后的原文）；转义/
 *   还原的唯一实现点就是本头的 escapeCsvText/unescapeCsvText——任何单元
 *   需要"写出带前缀的 CSV 文本"都必须经 ICsvWriter 或这两个函数，不得复
 *   制第二份规则（SA-12：唯一实现，规则漂移即数据损坏）。
 *
 * 数据-only 承诺（acceptance 4/§12 IO-T03 禁止项）：本通道解析路径零公式
 * /命令执行入口——所有单元格一律按文本承载（原文保留），不存在任何把单
 * 元格内容当作表达式求值/进程执行的代码路径；单元格含控制字符也仅按
 * §5.4 的 NUL 拒绝规则处理，不解释、不执行。
 *
 * 线程约束：ICsvReader/ICsvWriter 均为会话型单线程对象（§9.3/§9.4 契约
 * 表）——一个实例绑定一个文件一次读取/一个目标一次写出，禁止并发调用；
 * probe() 结果（CsvDialect 值）可跨线程只读共享。escapeCsvText/
 * unescapeCsvText/renderDialectMarker 为纯函数，并发安全。
 *
 * 确定性（NFR-COR-01/02）：同文件同选项同输出；行错误顺序按行号列号稳
 * 定；canonical 写出（固定方言行＋声明行尾＋UTF-8 无 BOM＋std::to_chars
 * 最短双精度表示）保证同一结构化数据两次导出字节相同（§5.5 文件层字节
 * 一致——AT-22 多格式一致的基础）。
 */

#ifndef SDURWS_IRD_IO_CSV_HPP
#define SDURWS_IRD_IO_CSV_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/io/Budget.hpp>   // BudgetScopeId——预算 scope 句柄（read/probe 记账目标）
#include <sdurws/ird/io/IoFwd.hpp>    // IoString/IoResult/IoCancelToken/IoProgressCallback
#include <sdurws/ird/io/IoError.hpp>  // IoError——行错误 reason 与读失败错误轨道

namespace sdurws::ird::io {

// =====================================================================
// 方言（§5.1 v1 封闭键集——P-IO-5：语法冻结前不私改，变更走方言版本升级）
// =====================================================================

/**
 * @brief v1 方言声明的行尾值（§5.1 键 eol 的值域）。
 *
 * 声明值只约束**写出方**（写出时行尾用声明的形态）；读取方对行尾保持
 * 容错（CRLF/LF/CR 三种形态均接受——§5.2 行尧行），不按声明值校验文件。
 */
enum class CsvEol : std::uint8_t {
    Crlf,   ///< "CRLF"——\r\n（v1 缺省；本软件 canonical 导出形态，§5.5）
    Lf,     ///< "LF"——\n
};

/**
 * @brief v1 方言声明的编码值（§5.1 键 encoding 的值域）。
 *
 * v1 封闭为仅 utf-8（io 导出恒 UTF-8 无 BOM——§5.1"与 BOM"行）；声明其
 * 他值＝方言标识行语法错（IO-FORMAT-CSV-DIALECT，读侧拒绝/写侧拒开）。
 * 单值枚举是"封闭集"的类型化表达：未来扩充编码＝方言版本升级（P-IO-5），
 * 不在本枚举就地加值。
 */
enum class CsvEncoding : std::uint8_t {
    Utf8,   ///< "utf-8"——v1 唯一合法值
};

/**
 * @brief CSV 方言参数（§5.1 标识行四个键的运行时形态；§9.4 写出选项载体的值类型）。
 *
 * 值语义（纯值类型，调用方所有）； RwDefault() 给出 §5.1 缺省值组合。
 * 约束（§5.1 封闭键集，构造后由调用方保证、读写器入口校验）：
 *   - delimiter ∈ {',', ';', '\t'}（v1 三候选）；
 *   - quote 恒 '"'——仅此一个合法值（' 与转义前缀冲突，§5.1 明文；
 *     其余字符无语法位置）；
 *   - eol/encoding 见 CsvEol/CsvEncoding。
 */
struct CsvDialect {
    char delimiter = ',';                 ///< 分隔符（',' ';' '\t'——§5.1 v1 封闭）
    char quote = '"';                     ///< 引号字符（恒 '"'——§5.1；' 与转义冲突禁用）
    CsvEol eol = CsvEol::Crlf;            ///< 行尾声明（约束写出方；读取方容错）
    CsvEncoding encoding = CsvEncoding::Utf8; ///< 编码声明（v1 恒 utf-8）

    /**
     * @brief 本软件缺省方言（§5.1 缺失键的 v1 缺省组合＋§9.4 写出缺省）：
     *        delimiter=',' quote='"' eol=CRLF encoding=utf-8。
     */
    static CsvDialect rwDefault() noexcept { return CsvDialect{}; }

    /// 逐字段相等（探测结果比对/测试断言用）。
    bool operator==(const CsvDialect& o) const noexcept
    {
        return delimiter == o.delimiter && quote == o.quote && eol == o.eol
               && encoding == o.encoding;
    }
    bool operator!=(const CsvDialect& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// 可逆转义编码（§5.3——全产品唯一转义/还原实现点，SA-12/NFR-SEC-03 M-15）
// =====================================================================

/**
 * @brief 导出转义（§5.3 导出行）：文本字段以 = + - @ ' 之一开头时前置
 *        **恰好一个** ' 前缀；其余原文原样返回。
 *
 * 只对"文本字段"适用——数值/空字段由写出方先格式化为文本后再不走此规
 * 则（§5.3"数值/空字段不经此规则"；ICsvWriter 对 Kind::Int/Real/Empty
 * 单元格直接格式化、不调用本函数）。自反性契约：对任意原文 s（含 s 以
 * ' 开头、空串、含换行/引号/分隔符），unescapeCsvText(escapeCsvText(s))
 * == s 逐字符成立（§5.3 自反性行；契约测试 IO-V02 全样例断言）。
 *
 * 纯函数；输入输出均 UTF-8；不经 locale；O(n)。
 *
 * @param text [in] 单元格原文（UTF-8；可含任意字节序——本函数按字节处理，
 *             不解释内容、不执行任何求值）
 * @return 文件层文本（可能比原文多一个前导 '；绝不比原文短）
 */
IoString escapeCsvText(const IoString& text);

/**
 * @brief 导入还原（§5.3 导入行·带标识文件）：字段以 ' 开头时剥离**恰好
 *        一个**前导 '；其余原样返回。
 *
 * 只允许用于**携带 v1 方言标识**的文件（读取方在标识判定通过后调用）；
 * 无标识外部文件一律不执行本函数（§5.3 导入行·无标识——所有字段含 '
 * 开头原样读入，一字不改——acceptance 3/IO-V03 的被测语义）。
 *
 * 与 escapeCsvText 构成唯一转义/还原对（见文件头注）：restore 是 escape
 * 的左逆，对任意 s 恒等 unescapeCsvText(escapeCsvText(s)) == s；注意
 * escape 不是 unescape 的左逆（原文自带前缀的样例如 "'abc"：escape 得
 * "''abc"，再 unescape 回 "'abc"——一层前缀对应一层剥离，这正是可逆性
 * 的正确形态，IO-V02 逐样例断言）。
 *
 * 纯函数；UTF-8；O(n)。
 *
 * @param field [in] 文件层字段文本（已按 RFC4180 去引号后的内容）
 * @return 结构化层原文（剥离转义后——RawTable/RawCell 只保存本函数的产物）
 */
IoString unescapeCsvText(const IoString& field);

/**
 * @brief 渲染 v1 方言标识行（§5.1 唯一实现；不含行尾终止符）。
 *
 * 产出形如 §5.1 原文：`#rwcsv1 delimiter=, quote=" eol=CRLF encoding=utf-8`
 * ——键序固定为 delimiter、quote、eol、encoding（canonical，同方言同字
 * 节——§5.5 文件层字节一致的前提之一）；delimiter 为 tab 时按 §5.1 词法
 * 写作 "tab"；quote 恒渲染为 '"'；encoding 恒 utf-8。P-IO-5：v1 语法按
 * §5.1 原文实现，冻结评审前不私改；此后变更走方言版本升级。
 *
 * 纯函数；并发安全。
 */
IoString renderDialectMarker(const CsvDialect& dialect);

// =====================================================================
// 单元格与读取模型（§5.6/§5.8）
// =====================================================================

/**
 * @brief 写出侧单元格（§5.6：CsvCell＝string｜int64｜double｜空标记）。
 *
 * 值语义。写出行为（§5.5/§5.6）：
 *   - Kind::Text：先 escapeCsvText 转义、再按 RFC4180 决定是否引号化；
 *   - Kind::Int：std::to_chars 十进制（无本地化）；
 *   - Kind::Real：std::to_chars 最短往返表示（'.' 小数点、无本地化——
 *     同一 double 两次写出字节相同）；非有限值（NaN/Inf）属调用方契约
 *     违约（§9.4 前置"isfinite 断言"），运行时按防御性内部错误拒绝写出
 *     （IO-FORMAT-INTERNAL——不静默落盘 "nan"，io 对外非抛出故走错误轨）；
 *   - Kind::Empty：空字段（不触发转义——§5.3 数值/空字段不经此规则）。
 */
struct CsvCell {
    /// 单元格类别（§5.6 四形态）。
    enum class Kind : std::uint8_t { Empty, Text, Int, Real };

    Kind kind = Kind::Empty;        ///< 当前类别（决定哪个载荷字段有效）
    IoString text;                  ///< Kind::Text 有效——单元格原文（UTF-8）
    std::int64_t integer = 0;       ///< Kind::Int 有效——整数值（无单位语义——单位归业务）
    double real = 0.0;              ///< Kind::Real 有效——双精度（必须 isfinite）

    /// 构造空单元格（Kind::Empty）。
    static CsvCell empty() noexcept { return CsvCell{}; }
    /// 构造文本单元格（Kind::Text；原文照收，不复制转义——转义在写出层）。
    static CsvCell fromText(IoString value) { CsvCell c; c.kind = Kind::Text; c.text = std::move(value); return c; }
    /// 构造整数单元格（Kind::Int）。
    static CsvCell fromInt(std::int64_t value) noexcept { CsvCell c; c.kind = Kind::Int; c.integer = value; return c; }
    /// 构造实数单元格（Kind::Real；调用方保证 isfinite——见类注）。
    static CsvCell fromReal(double value) noexcept { CsvCell c; c.kind = Kind::Real; c.real = value; return c; }
};

/**
 * @brief 行列数失配处置策略（§5.4 缺失列/多余列行；§9.3 CsvReadOptions.arity）。
 */
enum class CsvArityPolicy : std::uint8_t {
    Reject,        ///< 缺省——缺列/多列均为行错误（IO-FORMAT-CSV-ARITY，定位行号/期望/实际）
    PadTrailing,   ///< 缺列补空串至表头列数（CsvParseReport::rowsPadded 记数——策略按声明处置并记录）
    TrimExcess,    ///< 多列截断至表头列数（CsvParseReport::rowsTrimmed 记数；默认拒绝因多余列可能是映射错误信号——§5.4）
};

/**
 * @brief 空行处置策略（§5.4 空行行；§9.3 CsvReadOptions.blank）。
 */
enum class CsvBlankPolicy : std::uint8_t {
    Skip,     ///< 缺省——完全空行跳过并计数（CsvParseReport::blankSkipped）
    Reject,   ///< 空行计为行错误（"或策略拒绝"——§5.4；码面无专属 token，按
              ///< 结构事实归 IO-FORMAT-CSV-ARITY，detail 注明 RejectBlank 策略）
};

/**
 * @brief 读取选项（§9.3 CsvReadOptions；两处登记过的等价增补见成员注）。
 */
struct CsvReadOptions {
    /**
     * 表头所在**物理行号**（1 起，含标识行偏移——标识行占其所在物理行，
     * 与 §5.6 RowError::rowNo 同一口径；nullopt＝无表头，首行即数据，列
     * 名由调用方按业务字段字典自备——§5.4"表头不存在"行）。
     */
    std::optional<std::uint64_t> headerRow;

    /// 行列数失配策略（缺省 Reject——§5.4 默认拒绝并定位）。
    CsvArityPolicy arity = CsvArityPolicy::Reject;

    /// 空行策略（缺省 Skip＋计数——§5.4）。
    CsvBlankPolicy blank = CsvBlankPolicy::Skip;

    /// 行错误集合上限，超出截断＋计数（缺省 1000——§5.6/§9.3 同值）。
    std::uint64_t maxRowErrors = 1000;

    /**
     * 等价增补（§9.3 后置"RawTable 可配置保留行"的选项承载）：true 时
     * RawTable::rows 同步保留交付行（小文件/测试便利；缺省 false——流式
     * 交付不在表内重复存储，大文件内存有界）。
     */
    bool retainRows = false;

    /**
     * 等价增补（§5.6"调用方可选 StopOnFirstError"的选项承载）：true 时
     * 首个行错误即停止解析（读取以 ok 结束、该错误入集合——部分成功语
     * 义不变，调用方以 report.errorRows 判定）；缺省 false＝继续解析并
     * 收集全部行错误（AT-02"正确行保留"缺省形态）。
     */
    bool stopOnFirstRowError = false;
};

/**
 * @brief 行视图——零拷贝交付载体（§9.3 所有权行："CsvRowView 为零拷贝
 *        视图，仅在回调内有效"）。
 *
 * fields 内的 string_view 指向 reader 会话内部的当前行缓冲：**仅在
 * onRow 回调返回前有效**，回调返回后立即失效（下一行复用同一缓冲）。
 * 需要跨回调持有的调用方必须自行拷贝（或用 retainRows 让 RawTable 持有
 * 拷贝）。字段文本为"剥离转义后"的原文（结构化层形态——§5.3 结构化层
 * 行：转义形式绝不进入结构化数据层）。
 *
 * 生命周期/线程：归 reader 会话所有，调用方不释放；单线程回调内使用。
 */
struct CsvRowView {
    std::uint64_t rowNo = 0;                 ///< 物理行号（1 起，含标识行偏移——§5.6 口径）
    std::vector<std::string_view> fields;    ///< 各字段（剥离转义后原文视图；col 0 起）

    /// 字段数（与 fields.size() 一致——便利面）。
    std::size_t fieldCount() const noexcept { return fields.size(); }

    /**
     * 第 col0 列字段视图（0 起）；越界返回空视图（调用方以 fieldCount
     * 判界——越界空视图只为让防御性读取不 UB，不构成"存在该列"语义）。
     */
    std::string_view field(std::size_t col0) const noexcept
    {
        return col0 < fields.size() ? fields[col0] : std::string_view{};
    }
};

/**
 * @brief 逐行错误定位记录（§5.6 RowError 五元组）。
 *
 * 顺序保证：rowErrors 按行号升序、同行按列号升序稳定排列（§9.3 确定性
 * 行"行错误顺序按行号列号稳定"——acceptance 5"RowError 集合内容与顺序
 * 可断言"的落点）。
 *
 * 敏感性（IoError.hpp 头注同源）：rawSnippet 为**原文片段**（截断至
 * 120 字节并按 UTF-8 边界取整，防止诊断面被超长单元格撑爆）——仅开发级
 * 保留，成为用户可见文案前必须经 diagnostics 脱敏设施（§5.8 诊断脱敏
 * 行；AT-02 定位口径"用户级仅定位"）。
 */
struct CsvRowError {
    std::uint64_t rowNo = 0;                ///< 物理行号（1 起，含标识行偏移——§5.6）
    std::uint64_t colNo = 0;                ///< 列号（1 起；0＝行级问题无单列定位）
    std::optional<IoString> fieldName;      ///< 表头名（若表头可用——§5.6"fieldName(若表头)"）
    IoString rawSnippet;                    ///< 触发处原文片段（截断；开发级，呈现经脱敏）
    IoError reason;                         ///< 稳定码＋定位参数（row/column/snippet）＋开发级细节
};

/**
 * @brief 解析报告（§3.1 CsvParseReport；§5.4"策略记入 ParseReport"的落点）。
 *
 * RawTable 元数据Bundle：方言（实际生效值）、表头、各计数与实际生效策略。
 * 归调用方所有（随 RawTable）。
 */
struct CsvParseReport {
    CsvDialect dialect;                 ///< 实际生效方言（带标识＝标识行声明；无标识＝嗅探结果）
    bool marked = false;                ///< 是否携带 v1 方言标识行（§5.1 字节级精确判定）
    bool hasHeader = false;             ///< 是否识别出表头（options.headerRow 给定且已解析）
    std::vector<IoString> header;       ///< 表头原文（剥离转义后；无表头为空）
    std::uint64_t dataRows = 0;         ///< 经回调交付的正确数据行数（错误行/空行不计入）
    std::uint64_t errorRows = 0;        ///< 行错误总数（含超出 maxRowErrors 未入集合者）
    std::uint64_t blankSkipped = 0;     ///< Skip 策略跳过的空行数（§5.4"跳过并计数"）
    std::uint64_t blankRejected = 0;    ///< Reject 策略拒绝的空行数（errorRows 的子集）
    std::uint64_t rowsPadded = 0;       ///< PadTrailing 补空的行数（§5.4 策略记入报告）
    std::uint64_t rowsTrimmed = 0;      ///< TrimExcess 截断的行数（同上）
    std::uint64_t rowErrorsTruncated = 0; ///< 超出 maxRowErrors 被截断未入集合的错误条数（§5.6"超出截断＋计数"）
    CsvArityPolicy arityApplied = CsvArityPolicy::Reject;   ///< 实际生效的行列数策略
    CsvBlankPolicy blankApplied = CsvBlankPolicy::Skip;     ///< 实际生效的空行策略
};

/**
 * @brief 解析后数据模型（§5.8 RawTable：方言＋表头＋行集＋行错误集）。
 *
 * 归属与所有权（§5.8 第一行）：io 产出、**调用方所有**——io 不生成业务
 * 对象（任务点表/目录表等归业务单元），调用方把原文经 core tryParse/
 * UnitToken 做业务解析（SourcedValue 保原文可溯——io 只保留原文，SA-12）。
 *
 * 行集语义（§9.3 后置"行数据已经回调交付（流式，不在表内重复存储）"）：
 * rows 仅在 CsvReadOptions::retainRows=true 时填充（交付行的拷贝）；缺
 * 省为空（行经回调流式交付）。rowErrors 与回调交付互斥——错误行不经回
 * 调（§5.6"错误行不进入业务数据模型"）。
 *
 * 纯内存值类型：解析/校验失败不产生任何项目修订（§5.8——io 不触达命令
 * 端口，本结构零落盘）。
 */
struct RawTable {
    CsvParseReport report;                        ///< 方言/表头/计数/策略元数据
    std::vector<std::vector<IoString>> rows;      ///< 交付行拷贝（仅 retainRows=true 非空）
    std::vector<CsvRowError> rowErrors;           ///< 行错误全量（≤maxRowErrors；稳定序）
};

// =====================================================================
// 写出目标与写出器（§9.4；§4.6 原子写出的前置等价承载）
// =====================================================================

/**
 * @brief 写出目标（§9.4 open(IOutputTarget&&) 注"原子目标（§4.6）或内存
 *        缓冲"的等价承载）。
 *
 * 等价调整登记（§9.0"实现期允许等价调整，语义不变"）：§4.6 的
 * IAtomicFileWriter 公共头（AtomicFile.hpp）随 IO-T06 落位（§12 任务表），
 * 本任务以 CsvOutputTarget 直承载两类目标，写出协议在 ICsvWriter 实现内
 * 完成**真实的"暂存＋原子替换"**（同目录临时文件＋rename 替换——finish
 * 成功＝目标原子就位，finish 前失败/放弃＝目标不变，§9.4 后置条件行）；
 * IO-T06 交付 IAtomicFileWriter 后可无缝收编（接口形态不变，仅实现内部
 * 换用公共设施）。
 *
 * 所有权：MemoryBuffer 的缓冲由调用方持有并保证存活至 finish（finish 成
 * 功后缓冲内容＝canonical 文件字节）；FilePath 的目标路径归调用方。
 */
struct CsvOutputTarget {
    /// 目标类别。
    enum class Kind : std::uint8_t { MemoryBuffer, FilePath };

    Kind kind = Kind::MemoryBuffer;   ///< 当前类别
    IoString* buffer = nullptr;       ///< Kind::MemoryBuffer 有效——调用方持有的输出缓冲
    std::filesystem::path filePath;   ///< Kind::FilePath 有效——发布目标（finish 原子替换就位）

    /// 内存缓冲目标（buffer 必须非空且存活至 finish——调用方契约）。
    static CsvOutputTarget memory(IoString& buffer) noexcept
    {
        CsvOutputTarget t;
        t.kind = Kind::MemoryBuffer;
        t.buffer = &buffer;
        return t;
    }

    /// 文件目标（finish 时经同目录暂存文件原子替换；父目录必须已存在）。
    static CsvOutputTarget file(std::filesystem::path path) noexcept
    {
        CsvOutputTarget t;
        t.kind = Kind::FilePath;
        t.filePath = std::move(path);
        return t;
    }
};

/**
 * @brief 写出选项（§9.4 CsvWriteOptions 原文成员）。
 */
struct CsvWriteOptions {
    /// 方言声明（§5.1 标识行参数；quote 恒 '"'——写出入口校验，非法即拒开）。
    CsvDialect dialect = CsvDialect::rwDefault();

    /// 是否写出方言标识行——本软件导出恒 true（false 仅测试使用——§9.4 注）。
    bool emitDialectMarker = true;
};

// =====================================================================
// ICsvReader（§9.3——会话型：一个实例绑定一个文件一次读取）
// =====================================================================

/**
 * @brief CSV 读取器接口（§9.3 契约表逐行承载；等价调整见各方法注）。
 *
 * 会话型生命周期（§9.3）：每次 read 一个实例或复用实例串行多次 read；
 * 实例单线程使用，禁止并发 read（非法调用行）。probe() 的返回值
 * （CsvDialect 值拷贝）可跨线程只读共享。
 *
 * 典型消费（requirements 字段映射——§13）：probe→（用户确认方言/映射）
 * →read；read 内部对带标识文件直接采用标识行方言、对无标识文件自动嗅探
 * （§5.2），probe 的预览结果与 read 的生效方言一致（同文件同字节）。
 */
class ICsvReader {
public:
    virtual ~ICsvReader() = default;

    /**
     * @brief 探测方言（§9.3 probe：BOM/标识行/分隔符探测）。
     *
     * 流程（§5.1/§5.2）：① 读 BOM 判编码（UTF-8/UTF-16LE/UTF-16BE 剥离
     * 并解码）；② 无 BOM 时对预检窗口（首 64 KiB，§5.2"全文或首 64 KiB
     * 预检"）做严格 UTF-8 校验——不通过即 IO-FORMAT-CSV-ENCODING 稳定拒
     * 绝（不猜 ANSI/GBK、不转码——IO-D09）；③ 首物理行按 §5.1 字节级精
     * 确判定标识行——命中则解析方言（语法错＝IO-FORMAT-CSV-DIALECT）；
     * ④ 未命中则对表头行与后续至多 100 行做候选分隔符统计（引号外出现
     * 次数，取一致且次数最多者；无一致结果＝IO-FORMAT-CSV-DIALECT，detail
     * 携带候选统计摘要供调用方提示用户显式选择）。
     *
     * 等价调整（budget 语义）：§9.3 前置"budget scope 已开"——IBudgetGuard
     * 接口按 scope 句柄记账而卡面签名无句柄位，本实现增补尾随参数
     * budgetScope：非空句柄＝在调用方 scope 上记账（§9.3 前置语义——调
     * 用方可 tighten 后开 scope 传入）；0 句柄＝由 reader 以产品缺省规格
     * 自开自关一个子 scope（§4.5.2 语义不变）。guard 为 null＝不记账（仅
     * 测试/预览轻量场景，生产装配恒传 guard——§9.3 前置条件）。
     *
     * @param file   [in] 目标 CSV 路径（P-1 用户源/P-7 导出回读角色——路径
     *               规范由调用方经 SafePath 保证，本接口只做存在性/可读性检查）
     * @param budget [in] 预算守卫（可为 null＝不记账——见上）
     * @param cancel [in] 取消令牌（可为 null＝不可取消；检查点＝每块读取）
     * @param budgetScope [in] 预算 scope 句柄（0＝reader 自开内部 scope）
     * @return 成功＝生效方言；失败＝IO-FORMAT-CSV-ENCODING/-DIALECT、
     *         IO-SEC-BUDGET-*、IO-RES-*（四分类）、IO-CANCELLED
     */
    virtual IoResult<CsvDialect> probe(const std::filesystem::path& file,
                                       IBudgetGuard* budget,
                                       IoCancelToken* cancel,
                                       BudgetScopeId budgetScope = {}) = 0;

    /**
     * @brief 单遍流式读取（§9.3 read；逐行回调，返回元数据＋行错误全量）。
     *
     * 交付纪律（§5.6/AT-02）：默认**继续解析并收集全部行错误**（上限
     * options.maxRowErrors，超出截断＋计数），正确行照常经 onRow 交付；
     * 错误行不经回调（不进入业务数据模型，由调用方按行过滤）；空行按
     * options.blank 处置。带标识文件执行前缀剥离还原（§5.3），无标识文
     * 件零改写（acceptance 3）。
     *
     * onRow 返回值约定（§9.3 签名 bool 的语义落点）：true＝继续读下一行；
     * false＝调用方要求提前停止（read 以 ok 结束，已交付行有效——调用方
     * 主动终止，不是取消也不是错误）。CsvRowView 仅回调内有效（§9.3 所
     * 有权行）。
     *
     * @param file    [in] 目标 CSV 路径（同 probe）
     * @param options [in] 读取选项（表头行/策略/错误上限——见 CsvReadOptions）
     * @param onRow   [in] 行回调（row＝物理行号 1 起含标识行偏移；见上）
     * @param budget  [in] 预算守卫（null＝不记账——probe 同款等价调整；
     *                非空时行预算/字段预算/文件与总量预算在检查点生效，
     *                超限即中止并返回对应 IO-SEC-BUDGET-*——§4.4⑤）
     * @param cancel  [in] 取消令牌（null＝不可取消；检查点＝每行——§9.3
     *                取消行为行；命中返回 IO-CANCELLED，已回调行数在
     *                progress 中可见——取消是状态不是错误，UX-03）
     * @param progress [in] 进度回调（可为空；done＝已消费物理行号，
     *                total＝0（总量未知——流式），stage＝"csv-read"）
     * @param budgetScope [in] 预算 scope 句柄（0＝reader 自开内部 scope——
     *                probe 同款等价调整）
     * @param progress [in] 进度回调（可为空；done＝已消费物理行数，
     *                total＝0（总量未知——流式），stage＝"csv-read"）
     * @return 成功＝RawTable（report/rowErrors/可选 rows）；失败＝无部分
     *         业务产物（回调已交付的行由调用方丢弃——§9.3 后置）：编码
     *         （IO-FORMAT-CSV-ENCODING）/方言（-DIALECT）/表头重复列
     *         （-DUPCOL，致命——列映射歧义无法按行容错）/引号不闭合
     *         （-QUOTE，致命——自起始行起结构失效）/预算（IO-SEC-BUDGET-*）/
     *         资源（IO-RES-*）/取消（IO-CANCELLED）
     */
    virtual IoResult<RawTable>
        read(const std::filesystem::path& file, const CsvReadOptions& options,
             const std::function<bool(std::uint64_t /*row*/, CsvRowView&&)>& onRow,
             IBudgetGuard* budget, IoCancelToken* cancel, IoProgressCallback progress = {},
             BudgetScopeId budgetScope = {}) = 0;
};

// =====================================================================
// ICsvWriter（§9.4——会话型：一个实例一个目标文件；RAII 放弃语义）
// =====================================================================

/**
 * @brief CSV 写出器接口（§9.4 契约表逐行承载）——唯一转义点（§5.7 流程
 *        图：ICsvWriter＝唯一转义点，ICsvReader＝唯一还原点）。
 *
 * 会话流程：open→（writeHeader）→writeRow*→finish。RAII 纪律（§9.4 生
 * 命周期行）：未 finish 析构＝放弃＝清理暂存产物，目标不受影响（§5.6
 * "取消即中止（目标不受影响——替换未发生）"的落点——调用方放弃/取消时
 * 直接丢弃实例即可）。
 *
 * 确定性（§9.4 确定性行）：canonical 写出——固定方言行、声明行尾、
 * UTF-8 无 BOM、std::to_chars 最短表示——同一数据两次导出字节相同。
 *
 * 线程：实例单线程（§9.4 线程约束行）；内部缓冲无锁。
 */
class ICsvWriter {
public:
    virtual ~ICsvWriter() = default;

    /**
     * @brief 绑定输出目标并校验方言（§9.4 open）。
     *
     * 方言校验（§5.1 封闭键集在写出侧的镜像）：delimiter ∈ {',',';','\t'}、
     * quote 恒 '"'（声明 ' 即拒——§5.1"声明 quote=' 判格式错误"）、eol ∈
     * {CRLF,LF}、encoding 恒 utf-8；非法＝IO-FORMAT-CSV-DIALECT（不猜测
     * 修正）。文件目标在 open 即建立同目录暂存文件（尽早暴露权限/占用
     * 问题——失败时目标不变）。
     *
     * @param target  [in] 输出目标（右值接收——暂存状态随本会话迁移）
     * @param options [in] 写出选项（方言＋是否带标识行）
     * @return 成功＝已就绪可写；失败＝IO-FORMAT-CSV-DIALECT（方言非法）、
     *         IO-RES-*（暂存文件建立失败四分类）
     */
    virtual IoResult<void> open(CsvOutputTarget&& target, const CsvWriteOptions& options) = 0;

    /**
     * @brief 写表头行（§9.4 writeHeader——不查重，列名重复由调用方/读侧管）。
     *
     * 表头名按文本字段同规则处理（escapeCsvText＋RFC4180 引号化——表头
     * 同样可能被表格软件误执行，防护不豁免首行）。未 open 即调用＝调用
     * 方契约违约，按防御性内部错误拒绝（IO-FORMAT-INTERNAL——io 非抛出
     * 约束下的 fail-fast 形态，§9.4 非法调用行）。
     */
    virtual IoResult<void> writeHeader(const std::vector<IoString>& names) = 0;

    /**
     * @brief 写一行数据（§9.4 writeRow——转义＋RFC4180 引号化）。
     *
     * 单元格规则见 CsvCell 注（文本转义/整数与实数 to_chars/空标记；
     * NUL＝IO-FORMAT-CSV-CHAR 拒绝——E7 写侧；非有限实数＝
     * IO-FORMAT-INTERNAL 拒绝——见 CsvCell 注）。失败后本会话作废（调用
     * 方应丢弃实例——§9.4 后置"finish 前失败＝目标不变"）。
     */
    virtual IoResult<void> writeRow(const std::vector<CsvCell>& cells) = 0;

    /**
     * @brief 收尾：flush＋原子替换就位（§9.4 finish）。
     *
     * 文件目标：暂存文件刷盘关闭后经同卷 rename 原子替换发布目标——
     * 成功＝目标原子就位（先前输出或完整保留或被确认替换——§9.4 后置）；
     * 失败＝清理暂存、目标不变。内存目标：canonical 字节整体追加进调用
     * 方缓冲。重复 finish/未 open finish＝IO-FORMAT-INTERNAL（契约违约的
     * 防御性拒绝，同 writeRow 注）。
     */
    virtual IoResult<void> finish() = 0;
};

// =====================================================================
// 具体实现工厂（§9.11 访问器 csvReader()/atomicWriter 的实现侧产物；
// IoRuntime 装配入口随 IoSession.hpp 落位前，调用方经此直接装配会话）
// =====================================================================

/**
 * @brief 创建 CSV 读取器会话实例（每次 read 一个或串行复用——§9.3 生命周期行）。
 * @return 非空 reader（unique_ptr——会话对象不共享，单线程使用）
 */
std::unique_ptr<ICsvReader> makeCsvReader();

/**
 * @brief 创建 CSV 写出器会话实例（一个实例一个目标——§9.4 生命周期行）。
 * @return 非空 writer（unique_ptr——RAII：未 finish 析构＝放弃＝清理暂存）
 */
std::unique_ptr<ICsvWriter> makeCsvWriter();

} // namespace sdurws::ird::io

#endif // SDURWS_IRD_IO_CSV_HPP
