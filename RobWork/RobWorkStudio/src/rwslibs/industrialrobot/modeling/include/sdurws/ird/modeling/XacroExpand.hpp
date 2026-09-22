/**
 * @file   XacroExpand.hpp
 * @brief  Xacro 受控展开语义——IXacroExpandService 纯函数服务、
 *         ValidatedXacroSource/XacroSubstitutionMap 输入值类型、
 *         ExpandOutcome 展开产出值类型（卡 §6.5/§9.4.9；任务 WP-13-T06）。
 *
 * 设计依据：
 *   - units/modeling.md §6.5（Xacro 受控预处理——输入为 io 已验证字节＋
 *     依赖树；展开失败/依赖缺失→可定位诊断（宏名/行列），不产生草稿；
 *     展开产物进入与 URDF 相同的映射与安全边界（§6.3/§6.4）；来源记录：
 *     原始 .xacro 的 ResourceSnapshot digest 进草稿 externalRefs）、§9.4.9
 *     （IXacroExpandService 概要契约——ExpandOutcome expand(const
 *     ValidatedXacroSource&, const SubstitutionMap&, Diags&) const；未定义
 *     宏→行列定位诊断；绝不执行任意代码）、§3.3（公共头表 XacroExpand.hpp
 *     行——T06）、§3.4（纯函数服务：无共享可变状态、可重入、确定性）
 *   - units/io.md §6.2（URDF/Xacro include/宏展开边界——展开递归深度 ≤
 *     IncludeDepth(16)、展开产物总字节 ≤ TotalBytes、循环→IO-FORMAT-XML-
 *     CYCLE、缺文件→IO-RES-MISSING＋缺失清单、未定义宏/参数→定位诊断、
 *     绝不执行任意代码——Xacro 语言子集按声明式处理）、§6.5（资源依赖树）、
 *     §10.5（与 modeling 边界——展开机制护栏归 io 机制、展开语义归
 *     modeling；modeling 不自行读文件）
 *   - 需求 MDL-19（Xacro 受控预处理：参数列表、展开环境、离线依赖交付；
 *     展开失败/依赖缺失可定位诊断；展开结果记录来源并进入与 URDF 相同的
 *     安全解析边界——不放松 NFR-SEC-01/02）、AT-31/V-09（include/宏循环→
 *     IO-FORMAT-XML-CYCLE 定位诊断，无草稿、无修订、无对象写入——io 码
 *     透传）、NFR-COR-02（同输入字节＋同 substitutions→同展开字节、诊断
 *     顺序稳定）、NFR-COR-03（不静默：不支持构造显式失败不猜测）
 *   - 任务契约 tasks/foundation/WP-13-T06.json acceptance 1~5；
 *     knownPitfalls：O-40（pugixml 复用 WP-13-T05 已登记依赖——vcpkg 经典
 *     模式 1.16/x64-windows，modeling PRIVATE，不二次引入渠道）、P-MDL-4
 *     （展开引擎所有权——按"护栏 io／语义 modeling"切分：io 公共面零改动
 *     （io.md §13.3 同名重叠以契约 acceptance 4 为准，io 卡所有者确认前
 *     不实现 io 侧新原语），本头交付展开语义引擎）、P-MDL-8（io 契约
 *     Draft——io 值类型按当周 ResourceIo.hpp 落位形态消费，漂移按卡
 *     R-MDL-1 增量同步）
 *
 * 背景说明（展开引擎在导入管线中的位置——为什么它是"纯函数＋值面产出"）：
 * Xacro 导入的第一段是 io 文件层（唯一文件读取者）：编码识别/良构检查/
 * include 依赖树枚举（循环→IO-FORMAT-XML-CYCLE、缺席→IO-RES-MISSING）/
 * 预算（SingleFileBytes/TotalBytes/IncludeDepth）——即"护栏原语由 io 机制
 * 承载"。本单元消费 io 的已验证产物（字节＋快照＋依赖树），在护栏内做
 * **展开语义**：属性/宏/参数的声明式替换，把 .xacro 变成 URDF 形态的展开
 * 产物。展开绝不执行任意代码（无表达式求值、无 shell-out、无 eval 类
 * 设施——${表达式} 与 $(...) ROS 替换参数一律显式拒绝并给定位诊断，
 * NFR-COR-03 不静默猜测）。展开产物由调用方装配回 ValidatedSource 走
 * mapXacroExpanded——与 mapUrdf 完全相同的映射与安全边界（§6.5"不放松
 * URDF 解析安全规则"）。本服务全程不读写文件系统、不产生修订、不落盘
 * ——失败时展开产物为空（无草稿面），一切诊断经 diags 输出参数按源文件
 * 行序稳定排序。
 *
 * 所有权口径（P-MDL-4，契约 acceptance 4）：io 交付护栏原语确认
 * （IResourceReader 依赖树的循环检测/缺失清单/预算入账——已随 IO-T02/
 * IO-T05 落位），modeling 交付展开语义引擎（本头）。实现期增登说明：
 * 未定义宏/参数/不支持构造的定位诊断需要稳定码面，io 侧按 P-MDL-4 不扩
 * 公共面，故经单元卡 §9.5 实现期增登 MDL-IMPORT-XACRO-UNRESOLVED
 * （WP-13-T05 先例 MDL-IMPORT-TEMPLATE-RANGE 同款——§14.6 v0.7 登记）。
 *
 * 线程安全：XacroExpandService 无共享可变状态、可重入、多线程并发调用
 * 安全（卡 §3.4 总约定 1）。确定性：不读环境变量/时钟/locale/文件系统；
 * 展开为单遍扫描（替换文本不二次展开——防注入递归）；诊断按（相对键，
 * 行，列，产出序）稳定排序。全 SI（m/rad/kg/N·m）；角度制式 rad（注释
 * 显式标注）。
 */

#ifndef IRD_MODELING_XACROEXPAND_HPP
#define IRD_MODELING_XACROEXPAND_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>      // core::DiagnosticRecord（diags 输出参数元素类型）
#include <sdurws/ird/io/ResourceIo.hpp>      // io::ResourceSnapshot/ResourceDependencyTree（§9.6 产物值类型）
#include <sdurws/ird/modeling/Import.hpp>    // XacroProvenance/ImportSourceSpan（来源记录与定位面——T05 公共面）

namespace sdurws::ird::modeling {

// =====================================================================
// 输入值类型：ValidatedXacroSource（§9.4.9 ValidatedXacroSource——io 已
// 验证字节＋依赖树的 modeling 侧组合契约，P-MDL-8 处置同 Import.hpp
// ValidatedSource 先例）
// =====================================================================

/**
 * @brief Xacro 展开的已验证输入（io open/snapshot/dependencyTree 产物＋
 *        各 include 文件已验证字节的装配面——装配归调用方（向导域侧），
 *        本单元零文件访问（SA-14：io 是唯一文件读取者））。
 *
 * 字段语义：
 *   - entryBytes：入口 .xacro 的已验证字节（io 预算内产物；UTF-8 良构
 *     XML 已由 io 良构检查保证——IO-FORMAT-XML-SYNTAX 在 io 侧拦截，本
 *     单元解析失败即调用方违约面）。
 *   - entrySnapshot：入口文档读取快照（§8.2 三元组——contentDigest 为
 *     来源身份要素（进 provenance→草稿 externalRefs，MDL-19 来源可追溯）；
 *     finalPath 文本为 ExternalResourceRecord.absPath 装配面——路径不作
 *     身份）。摘要全零或路径为空＝调用方契约违约（mapXacroExpanded 的
 *     来源记录消费契约无法满足——见 expand 前置校验）。
 *   - dependencyTree：io §6.5 依赖树（io 契约无环——环在 io 侧已以
 *     IO-FORMAT-XML-CYCLE 失败；本服务仍防御性复核以透传 V-09 码面）。
 *     节点可携带 exists=false 缺失叶——include 类缺失对本服务是硬失败
 *     （io.md §6.2 离线依赖：展开所需全部文件必须已在本机），mesh 类
 *     缺失不阻断展开（随树传递给 mapUrdf 走 §6.7 Recorded 软事实）。
 *   - includeBytes：依赖树内各 include 文件的已验证字节（键＝依赖树
 *     relPath（正斜杠、折叠小写——io relPath 约定），值＝io open/snapshot
 *     产物字节）。缺失项不在表中——展开时命中缺失→IO-RES-MISSING（V-09
 *     同族透传）。键不在依赖树节点集＝调用方契约违约。
 *
 * 生命周期/所有权：纯值类型，调用方所有；展开过程只读。线程安全：不可变
 * 共享安全。
 */
struct ValidatedXacroSource {
    std::vector<std::uint8_t> entryBytes;   ///< 入口 .xacro 已验证字节（io 产物——预算内）
    io::ResourceSnapshot entrySnapshot{};   ///< 入口快照（digest＝来源身份；finalPath＝记录路径）
    io::ResourceDependencyTree dependencyTree;  ///< 依赖树（io 契约无环；缺失叶语义见类型注）
    /// include 文件字节表（键＝树 relPath 折叠键；值＝io 已验证字节）
    std::map<std::string, std::vector<std::uint8_t>> includeBytes;
};

// =====================================================================
// 展开环境：XacroSubstitutionMap（§9.4.9 SubstitutionMap 的本单元落位名
// ——Xacro 前缀避免过泛词，XacroProvenance 同款命名先例）
// =====================================================================

/**
 * @brief 展开环境逐条替换表（用户提供的参数代入值——优先级最高的解析层）。
 *
 * 构造序保序（vector-of-pair 而非关联容器——与 XacroProvenance.substitutions
 * 同形，留痕即输入序）：同名键重复时取**首个**（重复本身是调用方输入质量
 * 问题，不经诊断报错；构造序确定性优先）。解析优先级：本表 → 文档属性
 * （xacro:property，定义序后者覆盖前者）→ 宏调用绑定（宏体内最内层）。
 *
 * 生命周期/所有权：纯值类型，调用方所有。
 */
using XacroSubstitutionMap = std::vector<std::pair<std::string, std::string>>;

// =====================================================================
// 护栏常量（io.md §6.2 展开边界在语义引擎侧的执行值——P-MDL-4"护栏 io、
// 语义 modeling"：数值权威在 io 卡/预算表，本单元只按同一值执行）
// =====================================================================

/**
 * 展开递归深度上限＝16。来源：io.md §6.2"展开递归深度 ≤ IncludeDepth
 * （16）"与 io Budget.hpp IncludeDepth 维产品默认值——include 拼接与宏
 * 调用共用同一深度计（两者都是"展开递归"）。超限→IO-SEC-BUDGET-INCLUDE
 * （io 已注册码面透传，比较三要素 actual/limit）。
 */
inline constexpr std::uint32_t kXacroMaxExpansionDepth = 16;

/**
 * 展开产物总字节防御上限＝2 GiB。来源：io Budget.hpp TotalBytes 维产品
 * 默认值（io.md §6.2"展开产物总字节 ≤ TotalBytes"）。io 护栏已限定**输入**
 * 字节；本值限定**产出**（宏递归放大输入——深度护栏之外的第二道纵深）。
 * 超限→IO-SEC-BUDGET-TOTAL。单元测试不触发该值（需 GiB 级产物）——由
 * 深度护栏与代码走查覆盖，此处如实声明。
 */
inline constexpr std::uint64_t kXacroExpandedTotalBytesGuard = 2ull * 1024ull * 1024ull * 1024ull;

// =====================================================================
// 参数列表展示数据（MDL-19"参数列表"——向导页展示面的值载体）
// =====================================================================

/**
 * @brief 参数列表展示条目（向导页"参数列表"的展示数据——定义序＋生效值；
 *        substitutions 逐条亦入本清单，与 XacroProvenance.substitutions
 *        同源）。
 *
 * source 词表两值："substitution"（展开环境代入——span 无定位（0,0））、
 * "property"（文档 xacro:property 定义——span 指向定义处行列）。排列序：
 * substitutions 在前（输入序），properties 随后（文档定义序）——确定性。
 */
struct XacroParameterItem {
    std::string name;        ///< 参数名（xacro:property 的 name/替换键）
    std::string valueText;   ///< 生效值稳定文本（定义处已解析 ${} 后的字面——不改值）
    std::string source;      ///< 来源："substitution" | "property"（上行词表）
    ImportSourceSpan span;   ///< 定义位置（property 定义处；substitution 为无定位）

    bool operator==(const XacroParameterItem& o) const
    {
        return name == o.name && valueText == o.valueText && source == o.source
            && span == o.span;
    }
    bool operator!=(const XacroParameterItem& o) const { return !(*this == o); }
};

// =====================================================================
// 错误面：XacroExpandErrorCode/XacroExpandError（值面——Import.hpp
// ImportError 同形先例：接口局部载体，值面返回不穿越单元边界抛出）
// =====================================================================

/**
 * @brief Xacro 展开错误码（expand 的机器判别面；枚举顺序＝失败族稳定序，
 *        表尾追加纪律同 ImportErrorCode）。
 *
 * 与诊断码的关系（两层面分工——Import.hpp 同款）：diag 是稳定码呈现面
 * （MDL-IMPORT-XACRO-UNRESOLVED＝本单元语义面；IO-*＝io 护栏码透传），
 * error 是调用方判别面。失败一律无展开产物（ExpandOutcome.expandedBytes
 * 为空——无草稿面），报告语义由 diags 承载。
 */
enum class XacroExpandErrorCode : std::uint8_t {
    /// 未定义宏/未定义参数/缺参/多余属性/不支持构造/宏重定义——语义面
    /// 失败（稳定码 MDL-IMPORT-XACRO-UNRESOLVED，宏名/参数名＋源行列定位
    /// ——MDL-19/AT-31）。
    UndefinedSymbol,
    /// include 类依赖缺失（io.md §6.2 离线依赖硬失败；IO-RES-MISSING＋
    /// 缺失清单透传——V-09 同族）。
    DependencyMissing,
    /// include 图有环（io 契约本应拦截——本服务防御性复核并透传
    /// IO-FORMAT-XML-CYCLE＋环路径清单——V-09）。
    IncludeCycle,
    /// 展开护栏超限：递归深度＞16（IO-SEC-BUDGET-INCLUDE）或产物总字节
    /// ＞TotalBytes 防御值（IO-SEC-BUDGET-TOTAL）。
    ExpansionBudgetExceeded,
    /// 输入契约违约（入口字节空/树缺入口/字节表键不在树中/摘要或路径缺/
    /// 已验证字节解析失败——无草稿产出，不抛异常）。
    SourceInconsistent,
};

/**
 * @brief 取展开错误码的稳定 token（枚举成员名原文，如 "UndefinedSymbol"）。
 *
 * 实现侧唯一映射点（switch 全枚举、无 default——新增枚举值漏登记编译告警
 * 暴露，importErrorCodeToken 同款防线）。本 token 是展开接口错误面的判别
 * 串，不是 MDL- 或 IO- 稳定诊断码。
 *
 * @param code [in] 展开错误码（全表 5 值均有 token）
 * @return 稳定 token（静态存储期）。纯函数；线程安全；确定性。
 */
std::string_view xacroExpandErrorCodeToken(XacroExpandErrorCode code) noexcept;

/**
 * @brief Xacro 展开错误值（ImportError 同形——码＋有序参数表＋开发级细节；
 *        值面返回不穿越单元边界抛出）。
 *
 * params 键随码而异（如 UndefinedSymbol→item-kind/symbol；
 * DependencyMissing→missing-count/missing-list；IncludeCycle→cycle-path；
 * ExpansionBudgetExceeded→actual/limit）。detail 仅供内部诊断链，用户
 * 呈现前经 diagnostics 脱敏（敏感值纪律同 ImportError）。
 */
struct XacroExpandError {
    XacroExpandErrorCode code = XacroExpandErrorCode::SourceInconsistent;  ///< 稳定错误码
    std::vector<std::pair<std::string, std::string>> params;  ///< 上下文参数（构造序保序）
    std::string detail;                                       ///< 开发级细节（脱敏前不外泄）
};

// =====================================================================
// 产出值类型：ExpandOutcome（§9.4.9 expand 返回值）
// =====================================================================

/**
 * @brief Xacro 受控展开产出：展开产物＋参数列表＋来源记录＋阻断条件。
 *
 * 四元关系（调用方判定表）：
 *   - expandedBytes 有值：展开成功——产物为 URDF 形态 XML（声明式子集内
 *     的属性/宏/参数全部替换完成）。调用方按 Import.hpp ValidatedSource
 *     契约装配（展开字节＋展开产物摘要快照＋原依赖树）后交
 *     mapXacroExpanded 走与 mapUrdf 相同的映射与安全边界（§6.5）。
 *   - expandedBytes 无值：展开失败（error 必有值）——无草稿面（"展开
 *     失败不产生草稿"，MDL-19/§6.5）；parameters 为失败前已收集部分
 *     （与 ImportReport"恒产出"同风味：报告面不因失败丢失）。
 *   - parameters：参数列表展示数据（MDL-19"参数列表"）——substitutions
 *     在前＋文档属性定义序随后；确定性。
 *   - provenance：来源记录（mapXacroExpanded 的第二输入——digest 进草稿
 *     externalRefs（Recorded）、substitutions 逐条随导入报告留痕，§6.5）。
 *
 * 所有权：值语义归调用方；本服务不落盘、不产生修订、不写任何对象
 * （V-09"无对象写入"——对象写入只发生在命令 prepare 之后，与导入向导
 * 无关，§6.7）。
 */
struct ExpandOutcome {
    std::optional<std::vector<std::uint8_t>> expandedBytes;  ///< 展开产物（URDF 形态；nullopt＝失败无草稿）
    std::vector<XacroParameterItem> parameters;              ///< 参数列表展示数据（定义序；失败时为已收集部分）
    XacroProvenance provenance{};                            ///< 来源记录（mapXacroExpanded 输入——§6.5）
    std::optional<XacroExpandError> error;                   ///< 最严重阻断条件（无错误时为空）
};

// =====================================================================
// 接口：IXacroExpandService（§9.4.9 原文签名）＋无状态实现
// =====================================================================

/**
 * @brief Xacro 受控展开语义服务接口（卡 §9.4.9 概要契约的本单元落位；
 *        纯函数服务——无共享可变状态、可重入、多线程并发安全，卡 §3.4）。
 *
 * 契约要点：
 *   - @pre source 为 io open/snapshot/dependencyTree 产物装配
 *     （ValidatedXacroSource 契约见类型注）；调用方契约违约→值面
 *     SourceInconsistent（无草稿产出），不抛异常。
 *   - @post 同输入字节＋同 substitutions→同展开字节、同参数清单、同
 *     provenance、同诊断顺序（按源文件行序稳定排序——NFR-COR-02）；
 *     不读写文件系统、不产生修订、不落盘。
 *   - 支持的声明式子集（io.md §6.2"Xacro 语言子集按声明式处理"——子集外
 *     一律定位诊断失败，绝不猜测执行）：
 *       ① xacro:include（filename——拼接目标文件根元素的子元素）；
 *       ② xacro:property（name/value——字面值＋${引用} 解析）；
 *       ③ xacro:macro（name/params——params 为空白分隔的 name 或
 *          name:=default，默认值为字面）；
 *       ④ <xacro:Name .../> 宏调用（属性按签名绑定，属性值与元素文本的
 *          ${name} 引用在调用点作用域解析）；
 *       ⑤ 其余元素原样传递（属性/文本做 ${} 替换）。
 *     ${} 内只允许纯标识符引用；表达式（${a+b}）、ROS 替换参数（$(...)）
 *     等一律"不支持构造"定位失败——绝不执行任意代码（§9.4.9）。
 *   - 注释与处理指令不进入展开产物（声明式子集边界——确定性序列化）。
 */
class IXacroExpandService {
public:
    virtual ~IXacroExpandService() = default;

    /**
     * @brief 受控展开（宏/参数/属性替换语义引擎——§9.4.9 原文签名）。
     *
     * @param source       [in] io 已验证产物装配（ValidatedXacroSource）
     * @param substitutions [in] 展开环境代入表（构造序保序——最高优先层）
     * @param diags        [out] 诊断记录输出（追加不清空；按源文件行序稳定
     *                排序；码面＝已注册 MDL-IMPORT-XACRO-UNRESOLVED 与 IO-*
     *                稳定码（护栏码透传），唯一构造点见 DiagCodes.hpp/
     *                io::errorCodeToken——禁字符串拼码）
     *
     * @return ExpandOutcome{expandedBytes, parameters, provenance, error}
     *         （四元关系见类型注）
     */
    virtual ExpandOutcome expand(const ValidatedXacroSource& source,
                                 const XacroSubstitutionMap& substitutions,
                                 std::vector<core::DiagnosticRecord>& diags) const = 0;
};

/**
 * @brief IXacroExpandService 无状态实现（卡 §3.4 总约定 1；ModelImportMapper
 *        同款"接口＋final 实现"落位形态）。
 *
 * DOM 解析经 pugixml（O-40：复用 WP-13-T05 已登记依赖——vcpkg 经典模式
 * 1.16/x64-windows，modeling PRIVATE，不二次引入渠道；有界性由 io
 * BudgetGuard 前置保证：进入本单元的字节已经预算入账，DOM 规模与输入
 * 同阶；本单元不二次读文件、不解析外部实体——无 XXE/代码执行通道）。
 * 展开为单遍流式序列化：源 DOM 只读（定位偏移全程有效），产物直接写入
 * 输出缓冲——替换文本不二次展开（防注入递归，NFR-COR-02 确定性）。
 */
class XacroExpandService final : public IXacroExpandService {
public:
    XacroExpandService() = default;

    ExpandOutcome expand(const ValidatedXacroSource& source,
                         const XacroSubstitutionMap& substitutions,
                         std::vector<core::DiagnosticRecord>& diags) const override;
};

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_XACROEXPAND_HPP
