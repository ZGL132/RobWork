/**
 * @file   Json.hpp
 * @brief  JSON 读写器——受限 DOM（每值位置区间）、JsonProfile 声明式结构
 *         校验（版本判定先于 schema）、canonical 写出（同语义同字节）与
 *         内容身份（摘要经 core ContentDigester——SHA-256 唯一摘要算法）。
 *
 * 设计依据：
 *   - units/io.md §5.9（JSON 与结构化数据——受限读写器总纲）、§5.9.1
 *     （受限 DOM 与安全限制：文档大小/嵌套深度/重复键/NaN·Infinity·
 *     1e999/大字符串/数值范围/仅 UTF-8——逐条带超限码）、§5.9.2（版本
 *     字段、schema 校验与 JsonProfile：VERSION-MISSING/TYPE/FUTURE/
 *     LEGACY、未知字段默认 Reject＋Preserve 名单、必填不注默认值、
 *     类型/范围比较型）、§5.9.3（顺序规范化与内容身份：读保序、写
 *     canonical 键序＋to_chars 最短表示＋2 空格缩进＋LF、摘要经 core
 *     ContentDigester）、§5.9.4（格式校验与业务校验边界：io 只管语法/
 *     安全/版本/结构——字段业务语义与引用存在性归业务单元；JSON 通道
 *     无部分成功——IO-D10）、§9.5（IStructuredDataReader＋IJsonWriter
 *     契约表）、§3.1（公共头表 Json.hpp 行）
 *   - 需求 REQ-12（需求 JSON 工件）、OPT-12（研究结果/审计 JSON 工件）、
 *     PM-06（未来版本只读拒绝＋升级指引数据——判定与升级器归 project，
 *     io 提供格式探测数据）、NFR-DEP-04（schema 演进必须走版本升级，
 *     不允许静默吞字段）、NFR-SEC-02（JSON 文档/深度/字符串预算）、
 *     SA-12/NFR-MNT-03（SHA-256 唯一摘要算法——第二摘要算法禁止）
 *   - 任务契约 tasks/foundation/IO-T04.json（≙WP-11-T05，DTB §2.12）
 *     acceptance 1~3（V07/V08 全绿、canonical 字节一致、摘要唯一算法）
 *
 * 背景说明（为什么 io 自研受限 JSON 读写器而不用 testkit JsonLite——
 * WP-11-T05 禁止项）：JsonLite 是 testkit 的最小测试侧工具（仅测试
 * 代码可用），不具备产品级要求：每值位置区间（诊断定位）、重复键拒绝
 * （"后者覆盖"即静默数据丢失）、数值溢出拒绝、预算检查点、版本判定与
 * profile 校验、canonical 写出。产品 JSON 通道（REQ-12/OPT-12/manifest/
 * future 工件）一律经本头接口；JsonLite 永不出现在产品格式路径上。
 *
 * 与 CSV 通道的粒度差异（§5.9.4/IO-D10）：CSV 是行粒度部分成功（正确
 * 行照常交付）；JSON 用于整档结构化数据，行级容错无业务场景——结构
 * 校验失败则整个文档不进入业务层（失败时无部分 DOM 外泄，§9.5 后置）。
 *
 * 边界承诺（§5.9.4）：io 只负责语法、安全限制、版本、结构（profile 驱
 * 动）；**不负责**字段业务语义（单位合法性最终判定、取值工程含义）、
 * 引用对象存在性判定、任何工程结论；**不注入默认值**（默认值语义归业
 * 务单元——防"结构补全"变成业务决策）。
 *
 * 线程约束：IStructuredDataReader 无状态服务、并发安全；IJsonWriter 会
 * 话型单线程（一个实例一个目标一次写出）；JsonProfileRegistry 装配期
 * 单线程注册、注册完成后并发只读。JsonValue/JsonDocument/JsonProfile
 * 为纯值类型，并发只读安全。
 *
 * 确定性（NFR-COR-01/02）：同字节输入同 DOM 同诊断（首错即返、遍历按
 * 文件序）；canonical 写出（固定键序＋std::to_chars 最短表示＋2 空格
 * 缩进＋LF＋UTF-8 无 BOM）保证同一 DOM 两次写出字节相同、同语义文档
 * （仅键序/空白/数值书写差异）canonical 字节相同。
 */

#ifndef SDURWS_IRD_IO_JSON_HPP
#define SDURWS_IRD_IO_JSON_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>   // core::Digest256——内容身份（§5.9.3，SHA-256 唯一摘要算法）
#include <sdurws/ird/io/Budget.hpp>     // IBudgetGuard/BudgetScopeId——预算检查点（§4.4/§4.5）
#include <sdurws/ird/io/IoFwd.hpp>      // IoString/IoResult/IoCancelToken
#include <sdurws/ird/io/IoError.hpp>    // IoError/IoErrorCode——错误轨道

namespace sdurws::ird::io {

// =====================================================================
// 源位置区间（§5.9.4"解析器记录每个值的位置区间"）
// =====================================================================

/**
 * @brief JSON 值的源位置区间（行/列闭区间——诊断定位面，V07 观测点
 *        "错误码＋JSON 路径＋行列区间"的承载）。
 *
 * 行列口径：行号 1 起；列号 1 起，按 **UTF-8 字节**计（多字节字符的
 * 后续字节计入同列推进——定位到字节是确定性口径，不依赖终端宽度或
 * 码点切分争议）。区间为闭区间：[line,column]＝首字节位置，
 * [lineEnd,columnEnd]＝末字节位置（含）。纯值类型。
 */
struct JsonSourceSpan {
    std::uint32_t line = 0;       ///< 首字节所在行（1 起；0＝无定位——编程构造的 DOM）
    std::uint32_t column = 0;     ///< 首字节所在列（1 起，UTF-8 字节列）
    std::uint32_t lineEnd = 0;    ///< 末字节所在行（含）
    std::uint32_t columnEnd = 0;  ///< 末字节所在列（含——闭区间）

    bool operator==(const JsonSourceSpan& o) const noexcept
    {
        return line == o.line && column == o.column && lineEnd == o.lineEnd
               && columnEnd == o.columnEnd;
    }
};

// =====================================================================
// 受限 DOM（§5.9.1——对象键保文件出现序；每值携带位置区间）
// =====================================================================

/**
 * @brief 受限 DOM 的值节点（§3.1 JsonDocument 行"受限 DOM"）。
 *
 * "受限"的三重含义（§5.9.1 全表在解析层的落点）：
 *   1. **类型受限**：Null/Boolean/Integer/Real/String/Array/Object 七
 *      形态——NaN/Infinity/-Infinity 语法层拒绝（非法 JSON），1e999 溢
 *      出拒绝，不存在"非有限数值"节点；整数 |i|≤2^63−1 以 Integer 无
 *      损承载，超范围整数按 §5.9.1 数值行**原文保留为 String 透传**
 *      （不静默截断——report.outOfRangeIntegers 计数为"提示"面）。
 *   2. **结构受限**：对象同层同名键第二次出现即拒绝（IO-FORMAT-JSON-
 *      DUPKEY）——不"后者覆盖"；嵌套深度超 JsonDepth 拒绝；单字符串
 *      长度超 JsonStringChars 拒绝。
 *   3. **可定位**：每个节点携带源位置区间 span（诊断逐路径定位的依据；
 *      程序化构造的 DOM span 为全零——"无定位"哨兵）。
 *
 * 对象成员序：保留**文件出现序**（§5.9.3 读行"DOM 保留对象键的文件出
 * 现序"——诊断定位需要；排序只发生在写出侧 canonical 化）。
 *
 * 生命周期/所有权：纯值类型，归调用方（§9.5 所有权行"DOM 归调用方"）；
 * 拷贝即深拷贝（成员为值语义容器）。线程安全：并发只读。
 */
struct JsonMember;   // 前置声明（对象成员——键＋值对）

struct JsonValue {
    /// 值形态（§5.9.1 数值行的 Integer/Real 二分＋四种基础类型＋两容器）。
    enum class Type : std::uint8_t {
        Null,      ///< null 字面量
        Boolean,   ///< true/false
        Integer,   ///< 整数（int64 无损承载；|i|≤2^63−1——§5.9.1 数值行）
        Real,      ///< 实数（IEEE754 double；解析保证有限——溢出在语法层拒绝）
        String,    ///< 字符串（UTF-8；亦承载超范围整数的原文透传——见类注）
        Array,     ///< 数组（items 保序）
        Object     ///< 对象（members 保文件出现序；解析保证无重复键）
    };

    Type type = Type::Null;              ///< 当前形态（决定哪个载荷字段有效）
    bool boolValue = false;              ///< Type::Boolean 有效
    std::int64_t integerValue = 0;       ///< Type::Integer 有效（单位：无——数值语义归业务）
    double realValue = 0.0;              ///< Type::Real 有效（恒有限——解析层保证）
    IoString stringValue;                ///< Type::String 有效（UTF-8 原文）
    std::vector<JsonValue> items;        ///< Type::Array 有效（元素保文件序）
    std::vector<JsonMember> members;     ///< Type::Object 有效（成员保文件出现序）
    JsonSourceSpan span;                 ///< 源位置区间（闭区间；全零＝编程构造无定位）

    /// 形态判别便利面（可读性——校验器与写侧大量分支用）。
    bool isNull() const noexcept { return type == Type::Null; }
    bool isBoolean() const noexcept { return type == Type::Boolean; }
    bool isInteger() const noexcept { return type == Type::Integer; }
    bool isReal() const noexcept { return type == Type::Real; }
    bool isString() const noexcept { return type == Type::String; }
    bool isArray() const noexcept { return type == Type::Array; }
    bool isObject() const noexcept { return type == Type::Object; }
    /// 数值形态（Integer 或 Real——profile JsonValueType::Number 的匹配面）。
    bool isNumber() const noexcept { return type == Type::Integer || type == Type::Real; }

    /**
     * @brief 按键查找对象成员（首个同名成员——解析保证无重复，故至多一个）。
     * @param key [in] 目标键名（UTF-8，区分大小写——JSON 键语义）
     * @return 命中＝成员值指针（指向本对象内部，**调用方不持有**——
     *         本对象存活期内有效）；未命中或非对象＝nullptr
     */
    const JsonValue* findMember(std::string_view key) const noexcept;
};

/// 对象成员（键＋值；键名 UTF-8 原文——文件出现序由容器保序承载）。
struct JsonMember {
    IoString key;                  ///< 键名（UTF-8 解码后——\u 转义不改变键身份）
    JsonSourceSpan keySpan;        ///< 键名 token 的源位置区间（重复键/未知键定位——V07 观测点；
                                   ///< 全零＝编程构造无定位）
    JsonValue value;               ///< 值节点（递归受限 DOM）
};

/// 解析报告（§3.1 JsonParseReport——受限解析的元数据与"提示"面）。
struct JsonParseReport {
    std::uint64_t docBytes = 0;             ///< 文档字节数（BOM 剥离后计入解析的字节量）
    bool bomStripped = false;               ///< 是否剥离了 UTF-8 BOM（§5.9.1 编码行"容忍剥离"）
    std::uint64_t valueCount = 0;           ///< 解析产出的值节点总数（含根）
    std::uint32_t maxDepth = 0;             ///< 实际最大嵌套深度（根＝1）
    std::uint64_t outOfRangeIntegers = 0;   ///< 超范围整数按原文 String 透传的计数（§5.9.1
                                            ///< "＋提示"——不静默截断的观测面；非错误）
};

/// 受限 DOM 文档＝根节点＋解析报告（§3.1 JsonDocument）。
struct JsonDocument {
    JsonValue root;            ///< 根值（产品文档约定为 Object——版本字段判定面）
    JsonParseReport report;    ///< 解析元数据（纯观测面，不参与校验语义）
};

// =====================================================================
// JsonProfile（§5.9.2——声明式结构契约；格式所有者装配期注册、io 执行校验）
// =====================================================================

/**
 * @brief profile 声明的值类型约束（JsonShape::type 的值域）。
 *
 * Number 是"数值"匹配面（Integer 或 Real 均命中——文档写 1 或 1.0 同
 * 语义）；Integer 只命中整数码；其余与 JsonValue::Type 一一对应。Any
 * ＝不约束形态（仍受子结构递归约束）。封闭枚举：扩充＝版本升级，不在
 * 本枚举就地改义。
 */
enum class JsonValueType : std::uint8_t {
    Any,       ///< 不约束形态
    Null,      ///< 必须 null
    Boolean,   ///< 必须 true/false
    Integer,   ///< 必须整数（Integer——超范围透传的 String 不命中，见 JsonValue 类注）
    Number,    ///< 必须数值（Integer 或 Real）
    String,    ///< 必须字符串
    Array,     ///< 必须数组
    Object     ///< 必须对象
};

/**
 * @brief 对象键声明（JsonShape 内的逐键契约——声明序即 canonical 写出序）。
 *
 * 三态正交：required（缺失→IO-FORMAT-JSON-REQUIRED）、preserve（该子
 * 树透传保留——前向兼容扩展块，§5.9.2 未知字段行）、shape（子结构契约，
 * 空指针＝不约束子结构）。
 */
struct JsonShape;   // 前置声明（JsonProperty 递归引用——完整定义见下）

struct JsonProperty {
    IoString key;                                ///< 键名（UTF-8）
    bool required = false;                       ///< 必填（缺失→REQUIRED；io 不注入默认值——§5.9.2）
    bool preserve = false;                       ///< Preserve 子树（校验透传——扩展块名单属格式所有者契约）
    std::shared_ptr<const JsonShape> shape;      ///< 子结构契约（null＝不约束；递归经 shared_ptr）
};

/**
 * @brief 值结构契约节点（§5.9.2 rootShape"per-key type|enum|range|array
 *        bounds|递归 shape"的运行时形态；递归定义）。
 *
 * 约束逐字段正交，全部满足才通过（校验按文档序取首违例——确定性首错）：
 *   - type：形态约束（JsonValueType）；
 *   - enumValues：值域约束（String 按字节比对；Integer 按数值比对；
 *     命中任一枚举值即过；非空才生效）——违例归 IO-FORMAT-JSON-TYPE
 *     （值域不符的定位码面，params expected/actual，detail 注明 enum）；
 *   - hasRange/rangeMin/rangeMax/rangeUnit：数值范围约束（Integer/Real
 *     按 double 比较参与；越界→IO-FORMAT-JSON-RANGE 比较型三要素
 *     actual/limit/unit——§5.9.2"比较型：实际/期望/单位"）；
 *   - hasItemBounds/minItems/maxItems：数组长度约束（越界→RANGE，
 *     unit=count）；
 *   - items：数组元素递归契约（null＝不约束元素）；
 *   - properties：对象逐键契约（声明序＝canonical 写出序——§5.9.3；
 *     未声明键按 profile 未知字段策略处置——默认 Reject）。
 *
 * Preserve 子树（JsonProperty::preserve）的整棵子文档**跳过校验**（透
 * 传保留——扩展块内的任何结构/键都允许，但解析层安全限制仍生效：预算
 * 与重复键在 parse 阶段已裁决，Preserve 不豁免解析层）。
 *
 * 所有权：子节点经 shared_ptr 共享（profile 由注册表持有、校验只读）；
 * 纯数据聚合，构造后由格式所有者填充。
 */
struct JsonShape {
    JsonValueType type = JsonValueType::Any;     ///< 形态约束
    std::vector<IoString> enumValues;            ///< 值域枚举（空＝不约束）
    bool hasRange = false;                       ///< 数值范围约束是否生效
    double rangeMin = 0.0;                       ///< 范围下界（闭区间含）
    double rangeMax = 0.0;                       ///< 范围上界（闭区间含）
    IoString rangeUnit;                          ///< 范围单位 token（比较型三要素 unit——如 "m"/"rad"/"count"）
    bool hasItemBounds = false;                  ///< 数组长度约束是否生效
    std::uint64_t minItems = 0;                  ///< 数组最小长度（含）
    std::uint64_t maxItems = 0;                  ///< 数组最大长度（含）
    std::shared_ptr<const JsonShape> items;      ///< 数组元素契约（null＝不约束）
    std::vector<JsonProperty> properties;        ///< 对象键契约（声明序＝canonical 写出序）
};

/**
 * @brief profile 级未知字段策略（§5.9.2 未知字段行"默认 Reject"）。
 *
 * 单值枚举＝封闭集的类型化表达（CsvEncoding 同款先例）：io 的产品语义
 * 只有 Reject——"schema 演进必须走版本升级（NFR-DEP-04），不允许静默
 * 吞字段"；前向兼容走 Preserve 名单（属格式所有者契约，JsonProperty::
 * preserve），不开放"全局容忍未知字段"的策略位（那会把漏声明静默变成
 * 吞字段）。
 */
enum class JsonUnknownKeyPolicy : std::uint8_t {
    Reject,   ///< 未声明键一律拒绝（IO-FORMAT-JSON-UNKNOWN，定位路径）——唯一策略
};

/**
 * @brief JSON 格式 profile（§5.9.2 JsonProfile——格式所有者单元装配期
 *        注册进 io 注册表；io 只执行结构校验，字段业务语义归所有者）。
 *
 * supportedVersions 语义（版本判定先于一切 schema 校验——§5.9.2）：
 * 文档 schemaVersion ∈ 表内＝支持；> 表内最大→IO-FORMAT-VERSION-FUTURE
 * （稳定只读拒绝＋升级指引数据——PM-06 执行侧）；< 表内最小→
 * IO-FORMAT-VERSION-LEGACY（同口径）；夹在界内但未列（版本空洞）→按
 * FUTURE 处置（本实现未登记的中间版本＝写方版本线高于本实现登记线，
 * 拒绝＋升级指引数据，不猜测其语义）。表必须升序排列（注册期校验）。
 */
struct JsonProfile {
    IoString profileId;                        ///< 注册标识（如 "ird-requirements/1"；注册表键）
    std::vector<std::int64_t> supportedVersions;  ///< 支持的 schemaVersion 集（升序，非空）
    JsonShape rootShape;                       ///< 根结构契约（产品文档根约定 Object）
    JsonUnknownKeyPolicy unknownKeyPolicy = JsonUnknownKeyPolicy::Reject;  ///< 未知键策略（唯一值 Reject）
};

// =====================================================================
// profile 注册表（§5.9.2"io 持注册表"＋§9.11 registerJsonProfile 的
// 装配期承载——IoRuntime 落位（IoSession.hpp）前的直接形态）
// =====================================================================

/**
 * @brief JsonProfile 注册表（格式所有者装配期注册、io 校验执行面查询）。
 *
 * C-6 注册协议（契约 note"业务单元装配期注册、io 执行校验"）：格式所
 * 有者单元在装配期把本格式契约注册进来，io 校验器按 profileId 查找执
 * 行——io 不硬编码任何业务格式知识（R-1：跨单元协作走注册端口）。
 *
 * 线程约束：注册期单线程（装配期约定，§9.11"装配期单线程"）；注册完
 * 成后 find 并发只读安全（内部锁保护——防御性，注册完成后行为只读）。
 * 生命周期：进程级（调用方持 shared_ptr 注入 reader 工厂）。
 */
class JsonProfileRegistry {
public:
    /// 构造空注册表（进程级——调用方持 shared_ptr 注入 reader 工厂）。
    JsonProfileRegistry();

    /// 析构（pimpl 释放；注册完成后仅持只读 profile——并发安全收尾）。
    ~JsonProfileRegistry();

    /**
     * @brief 注册一份 profile（装配期；拷贝入库——调用方此后可销毁原值）。
     *
     * @param profile [in] 格式契约（profileId 非空、supportedVersions
     *                升序非空为前置——违反＝调用方装配期契约违约）
     * @throws std::invalid_argument profileId 为空、版本表为空/非升序
     *         （装配期编程错误 fail-fast——BudgetSpec 同款口径：装配期
     *         纯计算无错误返回轨道，静默吞掉比抛出更危险）
     * @throws std::logic_error profileId 重复注册（同 id 二次注册＝装配
     *         防漏语义——§9.5 前置"未注册＝IO-FORMAT-INTERNAL，装配期
     *         防漏"的注册侧镜像）
     */
    void registerProfile(JsonProfile profile);

    /**
     * @brief 按 id 查找 profile（校验执行面）。
     * @return 命中＝profile 指针（注册表持有，**调用方不接管**；注册表
     *         销毁前有效——进程级）；未命中＝nullptr（reader 侧把
     *         "profileId 给定但未注册"映射为 IO-FORMAT-INTERNAL——§9.5 前置）
     */
    const JsonProfile* find(std::string_view profileId) const;

    /// 已注册 profile 数（测试/装配自检面）。
    std::size_t size() const;

private:
    struct Impl;                                  ///< pimpl——map 等实现细节不出公共头
    std::unique_ptr<Impl> m_impl;                 ///< 实现承载（注册期可变、运行期只读）
};

// =====================================================================
// 读取选项与读取器（§9.5——无状态服务；并发安全）
// =====================================================================

/**
 * @brief 读取选项（§9.5 JsonReadOptions 原文成员）。
 *
 * profileId 语义（§9.5 契约表前置条件行）：
 *   - null：仅语法/安全层（manifest 等内部件——不查版本不查 schema）；
 *   - 非 null：parse 后接版本判定（先）与结构校验（后）；profileId 未
 *     注册＝IO-FORMAT-INTERNAL（装配期防漏——调用方装配缺陷，不进用
 *     户文案）。
 * 指针由调用方持有并保证 parse 调用期间存活（仅读取所指字符串，不接管）。
 */
struct JsonReadOptions {
    const IoString* profileId = nullptr;   ///< null＝仅语法/安全层；非 null＝结构校验（须已注册）
};

/**
 * @brief JSON 结构化数据读取器接口（§9.5 IStructuredDataReader 原文
 *        签名；无状态服务——可并发调用）。
 *
 * 后置条件（§9.5 契约表）：成功＝受限 DOM（含每值位置区间）；**版本判
 * 定先于 schema 校验**（§5.9.2）；失败＝无部分 DOM 外泄（IoResult 失败
 * 轨 value 为默认构造——整个文档不进入业务层，§5.9.4 IO-D10）。
 *
 * 错误类型（§9.5）：IO-FORMAT-JSON-*（编码/重复键/数值/版本/schema 族/
 * 语法——SYNTAX 为卡面码表缺口按 DTB §5.4 增量修订补登的表尾追加码，
 * 见 IoError.hpp 枚举注）；IO-SEC-BUDGET-JSON*（文档/深度/字符串预算
 * ——比较型三要素）；IO-RES-*（文件读取四分类）；IO-CANCELLED。
 */
class IStructuredDataReader {
public:
    virtual ~IStructuredDataReader() = default;

    /**
     * @brief 解析文件（§9.5 parse 原文签名＋已登记的等价增补）。
     *
     * 流程：① 文件 stat 预检（§5.9.1 文档大小行"先 stat"——JsonDocBytes
     * 预算超限即拒，不读全文）；② 读入字节（IO-RES-* 四分类映射）；
     * ③ 转 parseBytes（UTF-8 校验/BOM 剥离/受限解析/版本/schema）。
     *
     * 等价增补（§9.0"签名均为实现建议……实现期允许等价调整，语义不变"；
     * Csv.hpp probe/read 同款登记）：§9.5 原文签名无 scope 句柄位，调用
     * 方无法把收紧后的预算规格传导给读取会话——补尾随参数 budgetScope：
     * 非零句柄＝在该（调用方 tighten 后的）scope 上记账；0＝由 reader
     * 以产品缺省规格自开自关一个内部 scope（§4.5.2 语义不变）。guard 为
     * null＝不记账但保留对产品默认限额的防御性比较（预算不豁免）。
     *
     * @param file   [in] 目标 JSON 路径（存在性/权限错误→IO-RES-*）
     * @param options [in] 读取选项（profileId 见 JsonReadOptions 注）
     * @param budget [in] 预算守卫（null＝防御性限检——见上）
     * @param cancel [in] 取消令牌（null＝不可取消；检查点＝每 64 KiB 消
     *               费窗口/文件读取块——§9.5 取消行为行）
     * @param budgetScope [in] 预算 scope 句柄（0＝自开内部 scope——见上）
     * @return 成功＝JsonDocument；失败＝见类注错误类型（value 为默认构造）
     */
    virtual IoResult<JsonDocument>
        parse(const std::filesystem::path& file, const JsonReadOptions& options,
              IBudgetGuard* budget, IoCancelToken* cancel, BudgetScopeId budgetScope = {}) = 0;

    /**
     * @brief 解析内存字节（§9.5 parseBytes 原文签名＋parse 同款等价增补
     *        ——唯一执行体）。
     *
     * 输入契约：utf8 为原始文档字节（UTF-8；BOM 容忍剥离；UTF-16 BOM
     * ＝IO-FORMAT-JSON-ENCODING 拒绝——§5.9.1 编码行"仅 UTF-8"）。
     * 字节量计入 JsonDocBytes 预算（budget 非 null 时经 charge——超限
     * 三要素诊断由守卫产出）。
     */
    virtual IoResult<JsonDocument> parseBytes(std::string_view utf8,
                                              const JsonReadOptions& options,
                                              IBudgetGuard* budget, IoCancelToken* cancel,
                                              BudgetScopeId budgetScope = {}) = 0;

    /**
     * @brief 结构校验（§9.5 validate 原文签名；profile 驱动，§5.9.2）。
     *
     * 与 parse 的分工：parse 内部已含"版本判定＋校验"（profileId 给定
     * 时）；本方法供调用方对**既有 DOM**（如程序化构造或重校验场景）执
     * 行同一套判定。流程：①版本判定（先于一切 schema 校验——§5.9.2；
     * 缺失/类型错/未来/废弃各归其码）；②结构遍历（profile 驱动，首违
     * 例即返——按文档序确定）。校验失败不修改 DOM（调用方须丢弃——整
     * 个文档不进入业务层，§5.9.4）。
     *
     * @param doc       [in] 待校验受限 DOM
     * @param profileId [in] 注册 profile 标识（未注册＝IO-FORMAT-INTERNAL）
     * @return 成功＝结构合法；失败＝IO-FORMAT-JSON-VERSION- 族、
     *         -REQUIRED/-TYPE/-RANGE/-UNKNOWN（定位：JSON path＋行列区间 params）
     */
    virtual IoResult<void> validate(const JsonDocument& doc, const IoString& profileId) const = 0;
};

// =====================================================================
// 写出选项、目标与写出器（§9.5 IJsonWriter；§5.9.3 canonical 编码）
// =====================================================================

/**
 * @brief 写出选项（§9.5 JsonWriteOptions——canonical 固定）。
 *
 * canonical 形态的全部版面参数（2 空格缩进/LF 行尾/UTF-8 无 BOM/to_chars
 * 最短数值）都是**固定值**（§5.9.3——确定性来源，不接受调用方定制：定
 * 制即破坏"同语义同字节"）；唯一自由度＝键序来源：
 *   - profile 为 null：全文档按键名字典序（UTF-8 字节序——无 profile
 *     的通用文档，§5.9.3 写行）；
 *   - profile 非 null：对象键按 profile 声明序（declaration order；声明
 *     外键——经校验的文档不存在——按文件出现序跟在声明键之后，确定性
 *     口径，见 JsonShape::properties 注）。
 * 指针由调用方持有并保证 write 调用期间存活（仅读取，不接管）。
 */
struct JsonWriteOptions {
    const JsonProfile* profile = nullptr;   ///< null＝字典序；非 null＝声明序（§5.9.3）
};

/**
 * @brief 写出目标（§9.5 write 的 IOutputTarget&& 注"原子目标（§4.6）或
 *        内存缓冲"的等价承载——CsvOutputTarget 同款等价调整登记形态）。
 *
 * 等价调整登记（§9.0"签名均为实现建议……实现期允许等价调整，语义不变"）：
 * §4.6 的 IAtomicFileWriter 公共头（AtomicFile.hpp）随 IO-T06 落位，
 * 本任务以 JsonOutputTarget 直承载两类目标，IJsonWriter::write 内部完成
 * 真实的"暂存＋原子替换"（同目录临时文件＋rename——文件目标 write 成
 * 功＝目标原子就位，write 失败/放弃＝目标不变，§9.5 副作用行"原子输出"
 * ＋§4.6 失败恢复语义）；IO-T06 交付 IAtomicFileWriter 后可无缝收编。
 *
 * 所有权：MemoryBuffer 的缓冲由调用方持有并保证存活至 write 返回（成功
 * 后缓冲内容＝canonical 字节整体替换）；FilePath 的目标路径归调用方。
 */
struct JsonOutputTarget {
    /// 目标类别（同 CsvOutputTarget::Kind 口径）。
    enum class Kind : std::uint8_t { MemoryBuffer, FilePath };

    Kind kind = Kind::MemoryBuffer;   ///< 当前类别
    IoString* buffer = nullptr;       ///< Kind::MemoryBuffer 有效——调用方持有的输出缓冲
    std::filesystem::path filePath;   ///< Kind::FilePath 有效——发布目标（write 原子替换就位）

    /// 内存缓冲目标（buffer 必须非空且存活至 write 返回——调用方契约）。
    static JsonOutputTarget memory(IoString& buffer) noexcept
    {
        JsonOutputTarget t;
        t.kind = Kind::MemoryBuffer;
        t.buffer = &buffer;
        return t;
    }

    /// 文件目标（write 时经同目录暂存文件原子替换；父目录必须已存在）。
    static JsonOutputTarget file(std::filesystem::path path) noexcept
    {
        JsonOutputTarget t;
        t.kind = Kind::FilePath;
        t.filePath = std::move(path);
        return t;
    }
};

/**
 * @brief JSON 写出器接口（§9.5 IJsonWriter 原文签名；会话型单线程——
 *        一个实例一个目标一次写出，§9.5 线程约束行）。
 *
 * 写出恒为 canonical（§5.9.3）：键序（选项决定来源）＋std::to_chars 最
 * 短数值表示＋2 空格缩进＋LF 行尾＋UTF-8 无 BOM——同一 DOM 两次写出字
 * 节相同（NFR-COR-01 的通道落点）。写出侧防回渗（§5.9.1 NaN 行"写出侧
 * std::isfinite 断言"）：Real 非有限＝IO-FORMAT-JSON-NUMBER 拒绝（解析
 * 层不可能产生非有限节点，此处防的是程序化构造的 DOM）。
 */
class IJsonWriter {
public:
    virtual ~IJsonWriter() = default;

    /**
     * @brief 以 canonical 字节写出文档（§9.5 write 原文签名；单次会话）。
     *
     * @param target  [in] 输出目标（右值接收——暂存状态随本会话迁移；
     *                文件目标＝暂存＋rename 原子替换，内存目标＝缓冲整体替换）
     * @param doc     [in] 受限 DOM（写出不改输入）
     * @param options [in] 写出选项（键序来源——见 JsonWriteOptions 注）
     * @return 成功＝canonical 字节已就位；失败＝IO-FORMAT-JSON-NUMBER
     *         （非有限数值拒绝写出——§5.9.1 写出侧断言）、IO-RES-*
     *         （文件目标暂存/替换失败四分类）
     */
    virtual IoResult<void> write(JsonOutputTarget&& target, const JsonDocument& doc,
                                 const JsonWriteOptions& options) = 0;
};

// =====================================================================
// canonical 化与内容身份（§5.9.3——包 manifest/固化副本/缓存键统一口径）
// =====================================================================

/**
 * @brief 产出文档的 canonical 字节（§5.9.3 canonical 编码的唯一实现点）。
 *
 * 与 IJsonWriter::write 的关系：write＝canonicalize＋落盘；canonicalize
 * 供内容身份（digestCanonicalJson）与测试复用。同语义 JSON（仅键序/
 * 空白/数值书写差异——1 与 1.0 同为 Real/Integer 数值语义）产出同字节。
 *
 * @param doc     [in] 受限 DOM
 * @param options [in] 键序来源（见 JsonWriteOptions 注）
 * @return 成功＝canonical 字节（UTF-8 无 BOM，2 空格缩进＋LF）；失败＝
 *         IO-FORMAT-JSON-NUMBER（DOM 含非有限 Real——写出侧拒绝）
 */
IoResult<IoString> canonicalizeJson(const JsonDocument& doc, const JsonWriteOptions& options);

/**
 * @brief 对 canonical 字节计算内容摘要（§5.9.3 内容身份行——包 manifest、
 *        固化副本、缓存键统一口径）。
 *
 * 摘要算法唯一性（SA-12/NFR-MNT-03）：经 core ContentDigester（SHA-256，
 * D-05）——io 不引入第二摘要算法；本函数是 JSON 通道摘要的**唯一入口**
 * （对 canonical 字节而非文件原字节——同语义同身份，内容寻址一致口径）。
 *
 * @param doc     [in] 受限 DOM
 * @param options [in] 键序来源（canonical 化与 write 共用同一选项语义
 *                ——键序不同则摘要不同，故 manifest 摘要必须与写出用同
 *                一 profile 选项，§7.1"totalDigest＝对 manifest canonical
 *                字节的摘要"）
 * @return 成功＝32 字节摘要；失败＝同 canonicalizeJson（非有限数值）
 */
IoResult<core::Digest256> digestCanonicalJson(const JsonDocument& doc,
                                              const JsonWriteOptions& options);

// =====================================================================
// 具体实现工厂（§9.11 访问器 jsonReader() 的实现侧产物；IoSession.hpp
// 落位（IoRuntime 装配）前，调用方经此直接装配）
// =====================================================================

/**
 * @brief 创建 JSON 读取器（每次调用返回无状态服务实例——也可长期持有
 *        复用，并发安全）。
 *
 * @param registry [in] profile 注册表（调用方持有共享所有权——进程级；
 *                可为 null＝任何 profileId 查找都不命中，映射
 *                IO-FORMAT-INTERNAL，§9.5 前置"装配期防漏"）
 * @return 非空 reader（unique_ptr）
 */
std::unique_ptr<IStructuredDataReader>
    makeStructuredDataReader(std::shared_ptr<const JsonProfileRegistry> registry);

/**
 * @brief 创建 JSON 写出器（会话型——一个实例一个目标一次写出；可逐次
 *        新建实例，§9.5 生命周期行）。
 * @return 非空 writer（unique_ptr）
 */
std::unique_ptr<IJsonWriter> makeJsonWriter();

} // namespace sdurws::ird::io

#endif // SDURWS_IRD_IO_JSON_HPP
