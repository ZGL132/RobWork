/**
 * @file   ObjectTypes.hpp
 * @brief  requirements 对象类型登记——五对象 objectTypeToken 常量、
 *         ProcessTag/TemplateKind/ArrayKind 三词表与对象 schema 版本常量。
 *
 * 设计依据：
 *   - units/requirements.md §4.1（对象分解与对象类型登记——五对象 token
 *     权威表："五个 token 均为本单元所有权登记（§14.4）"）、§4.2（根对象
 *     字段表 schemaVersion 行"≥1；需求对象 schema 演进单点"）、§4.3
 *     （processTag 字段——ProcessTag 词表 11 值）、§7.1（TemplateKind 词表
 *     6 值）、§7.2（ArrayKind 四值 Linear/Rectangular/Circular/Polyline）、
 *     §3.3（公共头表 ObjectTypes.hpp 行——T02/T03）、§14.4（新增语义登记
 *     第 1/2 项——token 与词表均为 requirements 所有权内登记）
 *   - ARCHITECTURE.md §3.3（业务域单元二分结构——本头属计算库公共面）、
 *     §7.4（名称语义归 runtime——本头登记的是对象**类型** token，不是
 *     RobWork 名称，两者不同层）
 *   - 需求 ARC-04（对象稳定身份的类型维度——token 是路由键）、CON-01
 *     （对象闭包与类型路由）、NFR-DEP-04（schema 主版本不识别→稳定拒绝
 *     ＋升级指引——版本常量的消费语义）
 *   - 任务契约 tasks/foundation/WP-14-T02.json acceptance 3（ObjectTypes.hpp
 *     落位——五 token/三词表/schema 版本常量）
 *
 * 背景说明（为什么对象类型 token 如此重要）：requirements 拥有的持久化
 * 对象以 objectTypeToken 声明类型，project 修订闭包（RevisionView.objectRefs）
 * 按该 token 路由到对应的命令处理器/解码器——拼写漂移＝路由失联（对象在
 * 闭包里却没人认领）。因此本头是 token 的**登记簿**：五个 token 全部由
 * requirements 登记（§4.1——与 modeling 不同，需求侧没有"根对象 token
 * 归 runtime"的锚定面：req-set 是编译入口对象但其 token 仍是本单元所有
 * 权，§14.4 第 1 项原文）。后续新增对象类型只允许表尾追加并走单元卡
 * 增量修订，不重排既有 token（登记簿纪律—— modeling 同款）。
 *
 * 线程安全：本头全部实体为 constexpr 常量/纯值枚举/纯函数，并发只读安全。
 * 确定性（NFR-COR-02）：token 与词表串均为编译期固定字面量，同值同串、
 * 无 locale 依赖。
 */

#ifndef IRD_REQUIREMENTS_OBJECTTYPES_HPP
#define IRD_REQUIREMENTS_OBJECTTYPES_HPP

#include <cstdint>
#include <optional>
#include <string_view>

namespace sdurws::ird::requirements {

// =====================================================================
// 五对象 objectTypeToken（§4.1 权威表；表行序＝本头声明序——登记簿纪律：
// 后续新增对象类型只允许表尾追加并走单元卡增量修订，不重排既有 token）
// =====================================================================

/// @brief req-set 对象的类型 token（§4.1 表第 1 行——TaskRequirementSet
///        根/聚合对象，每需求集一个；编译入口对象、修订闭包锚）。
inline constexpr std::string_view kReqSetObjectType = "req-set";

/// @brief req-point-set 对象的类型 token（§4.1 表第 2 行——任务点集合对象
///        （单对象内条目数组），每需求集至多一份；任务点可达数千条
///        （NFR-PERF-03 规模口径），逐点建对象会爆炸闭包——集合对象一条
///        依赖键即可表达"任务点变更"失效面）。
inline constexpr std::string_view kReqPointSetObjectType = "req-point-set";

/// @brief req-region-set 对象的类型 token（§4.1 表第 3 行——区域集合对象，
///        每需求集至多一份；区域少量、与点集对称）。
inline constexpr std::string_view kReqRegionSetObjectType = "req-region-set";

/// @brief req-condition-set 对象的类型 token（§4.1 表第 4 行——工况集合
///        对象，每需求集至多一份；工况变更独立失效面（不影响点位几何
///        切片的字节））。
inline constexpr std::string_view kReqConditionSetObjectType = "req-condition-set";

/// @brief req-plan-set 对象的类型 token（§4.1 表第 5 行——采样计划集合
///        对象，每需求集至多一份；计划变更独立失效面（对应 evidence
///        SamplingPlanRef.planContentIdentity 的内容身份来源——§4.1 原文））。
inline constexpr std::string_view kReqPlanSetObjectType = "req-plan-set";

// =====================================================================
// 对象 schema 版本常量（§4.2 字段表 schemaVersion 行"≥1；需求对象 schema
// 演进单点（NFR-DEP-04：主版本不识别→稳定拒绝＋升级指引，升级器归
// project 口径）"；四个集合对象结构对称、同版演进——§4.2"四个集合对象
// 结构对称"行）
//
// 版本语义（project.md §4.8 canonical 编码契约同口径，modeling T02 同款）：
//   - 主版本变更＝破坏性变更（旧字节须走升级器——升级器归 project 口径，
//     §4.2 schemaVersion 行原文；REQ-SCHEMA-UNSUPPORTED 稳定码承载拒绝面，
//     见 DiagCodes.hpp）；
//   - 表尾追加可选字段＝次版本兼容（编解码格式版本 FormatVersion 的
//     minor 承载——随 WP-14-T03 Codec.hpp 落位）。
//
// 单一权威：每个对象的 schemaVersion 字段只允许经本表取值，禁止在结构体
// 初始化处写字面量（与五 token 同款"登记簿"纪律——拼写/数值漂移在源头
// 被切断，NFR-COR-02）。
// =====================================================================

/// @brief req-set 对象 schema 主版本（§4.2 字段表 schemaVersion 行——根
///        对象，需求 schema 演进单点）。
inline constexpr std::uint32_t kReqSetSchemaVersion = 1;

/// @brief req-point-set 对象 schema 主版本（§4.2 尾段——集合对象与根同版
///        演进；条目字段表见 §4.3）。
inline constexpr std::uint32_t kReqPointSetSchemaVersion = 1;

/// @brief req-region-set 对象 schema 主版本（§4.2 尾段；条目字段表见 §4.4）。
inline constexpr std::uint32_t kReqRegionSetSchemaVersion = 1;

/// @brief req-condition-set 对象 schema 主版本（§4.2 尾段；条目字段表见
///        §4.5）。
inline constexpr std::uint32_t kReqConditionSetSchemaVersion = 1;

/// @brief req-plan-set 对象 schema 主版本（§4.2 尾段；条目＝SamplingPlan，
///        §5.2）。
inline constexpr std::uint32_t kReqPlanSetSchemaVersion = 1;

// =====================================================================
// ProcessTag——任务点工艺标签词表（§4.3 processTag 字段；词表 11 值，
// 模板/报告消费；§14.4 登记制——requirements 所有权）
// =====================================================================

/**
 * @brief 任务点工艺标签（§4.3 processTag 字段词表 11 值；REQ-07 模板语义
 *        载体——§2.5 旧 ProcessType 11 类工艺语义的承接行）。
 *
 * 所有权口径（§14.4 第 2 项原文）：词表为 **requirements 所有权内登记**
 * （登记制）——本枚举即需求侧的正式登记；消费方（模板匹配 §7.1、报告
 * 分组、下游注释性提示 D-REQ-7）以 token 串为界消费。旧代码的 ProcessType
 * 词表（§2.5 对照行"旧词表，REQ-07 模板语义载体"）由本词表承接，值序＝
 * §4.3 字段表括注原文序（Generic 先行、工艺类随后）；持久化契约面纪律：
 * 只允许表尾追加并走单元卡增量修订。
 *
 * 枚举值不承载任何工程判定语义（D-REQ-7：构型硬过滤归 KIN-02，
 * processTag 仅作注释性提示/模板/报告消费——不在本单元做标签驱动的
 * 行为分支）。
 *
 * 线程安全：纯值枚举；确定性：同值同 token（NFR-COR-02）。
 */
enum class ProcessTag {
    Generic,         ///< "Generic"——通用任务点（无特定工艺语义）
    Pick,            ///< "Pick"——拾取
    Place,           ///< "Place"——放置
    MachineLoad,     ///< "MachineLoad"——机床上下料·装载
    MachineUnload,   ///< "MachineUnload"——机床上下料·卸载
    Inspect,         ///< "Inspect"——检测
    WeldStart,       ///< "WeldStart"——焊接·起弧段
    WeldEnd,         ///< "WeldEnd"——焊接·收弧段
    ToolChange,      ///< "ToolChange"——换工具
    SafeStandby,     ///< "SafeStandby"——安全待机
    Handover,        ///< "Handover"——交接
};

/**
 * @brief 工艺标签的稳定 token（词表串转发表）。
 *
 * token＝词表串（§4.3 括注 11 串，如 "Generic"/"MachineLoad"）——模板/
 * 报告/下游消费共用同一串面。返回值指向静态存储期字面量。
 *
 * @param tag [in] 工艺标签（全表 11 值均有 token——switch 全枚举、无
 *              default，新增枚举值未登记表项时编译器告警暴露遗漏）
 * @return 词表串（静态存储期；模板匹配与报告分组共用）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同标签同串）。
 */
std::string_view processTagToken(ProcessTag tag) noexcept;

/**
 * @brief 词表串 → 工艺标签（try 轨——词表外返回 nullopt 不猜测）。
 *
 * @param token [in] 待核对的标签串（项目对象字节/导入表头承载的串）；
 *              精确等值比较（无大小写折叠、无前后空白剥离——ARC-04
 *              "不猜测"纪律：拼写变体一律词表外；导入侧的表头别名归
 *              §7.3 字段字典，不经本函数）
 * @return 对应标签；词表外（含空串/大小写变体/未知串）→ nullopt。
 *         nullopt 的业务含义由调用方按其所在域处置（导入映射→映射报告
 *         不支持项；编辑器→就地错误呈现），本函数不自行产出诊断
 *         （PA-1：诊断文案权威归 core/diagnostics，产码唯一经工厂）。
 *
 * 纯函数；线程安全；确定性（NFR-COR-02）。
 */
std::optional<ProcessTag> tryProcessTag(std::string_view token) noexcept;

// =====================================================================
// TemplateKind——工艺模板词表（§7.1；词表 6 值，requirements 所有权登记
// ——§14.4 第 2 项）
// =====================================================================

/**
 * @brief 工艺模板类别（§7.1 词表 6 值；REQ-07——承接旧六类模板，§2.5）。
 *
 * 所有权口径：requirements 所有权登记（§7.1"requirements 所有权登记
 * （§14.4）"原文）；模板参数数值为设计默认（黄金数据集锁定，同 modeling
 * T-MDL-1 模式——参数表随 WP-14-T07 落位，本词表 T02 先行登记）。
 * 枚举顺序＝§7.1 词表原文序；持久化契约面纪律：只允许表尾追加并走单元
 * 卡增量修订。
 *
 * 线程安全：纯值枚举；确定性：同值同 token（NFR-COR-02）。
 */
enum class TemplateKind {
    BinPicking,      ///< "BinPicking"——拆垛/拣选（料箱拾取）
    MachineTending,  ///< "MachineTending"——机床上下料
    Palletizing,     ///< "Palletizing"——码垛
    Inspection,      ///< "Inspection"——检测
    ToolChange,      ///< "ToolChange"——换工具
    Handover,        ///< "Handover"——交接
};

/**
 * @brief 模板类别的稳定 token（§7.1 词表串转发表）。
 *
 * @param kind [in] 模板类别（全表 6 值均有 token——switch 全枚举）
 * @return 词表串（静态存储期；模板服务 applyTemplate 与命令载荷共用）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同类别同串）。
 */
std::string_view templateKindToken(TemplateKind kind) noexcept;

/**
 * @brief 词表串 → 模板类别（try 轨——词表外返回 nullopt 不猜测）。
 *
 * @param token [in] 待核对的类别串（命令载荷/模板登记表承载的串）；
 *              精确等值比较（同 tryProcessTag 口径）
 * @return 对应类别；词表外→nullopt（调用方按其所在域处置——命令解码→
 *         载荷拒绝；本函数不自行产出诊断，PA-1 同上）。
 *
 * 纯函数；线程安全；确定性（NFR-COR-02）。
 */
std::optional<TemplateKind> tryTemplateKind(std::string_view token) noexcept;

// =====================================================================
// ArrayKind——批量阵列词表（§7.2；词表 4 值，requirements 所有权登记
// ——§14.4 第 2 项）
// =====================================================================

/**
 * @brief 批量阵列类别（§7.2 派生关系图"阵列：Linear/Rectangular/
 *        Circular/Polyline 参数"行——REQ-11 批量阵列四构型）。
 *
 * 所有权口径：requirements 所有权登记（§14.4 第 2 项"ArrayKind 4 值
 * 词表"）；四构型的参数面（方向/数量/间距/角度/半径/折线点列——§7.2
 * 原文）随 WP-14-T07 ArrayParams 落位，本词表 T02 先行登记。枚举顺序＝
 * §7.2 原文序（Linear→Rectangular→Circular→Polyline）；持久化契约面
 * 纪律：只允许表尾追加并走单元卡增量修订。
 *
 * 线程安全：纯值枚举；确定性：同值同 token（NFR-COR-02）。
 */
enum class ArrayKind {
    Linear,        ///< "Linear"——线性阵列（方向/数量/间距）
    Rectangular,   ///< "Rectangular"——矩形阵列（两正交方向/行×列/间距）
    Circular,      ///< "Circular"——圆形阵列（圆心/半径/数量/角度）
    Polyline,      ///< "Polyline"——三维折线阵列（折线点列/数量）
};

/**
 * @brief 阵列类别的稳定 token（§7.2 词表串转发表）。
 *
 * @param kind [in] 阵列类别（全表 4 值均有 token——switch 全枚举）
 * @return 词表串（静态存储期；阵列服务 applyArray 与命令载荷共用）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同类别同串）。
 */
std::string_view arrayKindToken(ArrayKind kind) noexcept;

/**
 * @brief 词表串 → 阵列类别（try 轨——词表外返回 nullopt 不猜测）。
 *
 * @param token [in] 待核对的类别串（命令载荷/生成溯源 parameters 承载
 *              的串）；精确等值比较（同 tryProcessTag 口径）
 * @return 对应类别；词表外→nullopt（调用方按其所在域处置；本函数不
 *         自行产出诊断，PA-1 同上）。
 *
 * 纯函数；线程安全；确定性（NFR-COR-02）。
 */
std::optional<ArrayKind> tryArrayKind(std::string_view token) noexcept;

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_OBJECTTYPES_HPP
