/**
 * @file   Factory.hpp
 * @brief  诊断工厂与跨单元错误转译——IDiagnosticFactory（唯一创建入口＋
 *         校验链＋转译注册面，§9.2 同一接口）、DiagnosticsFactory（实现）
 *         与 ErrorCodeTranslator 命名视图、阶段 A 转译清单。
 *
 * 设计依据：
 *   - units/diagnostics.md §4.2（信封字段的工厂校验——码已注册/subject 边界/
 *     三要素/占位一致）、§8.1（转换总原则四条——不丢根因/禁字符串匹配/唯一
 *     权威分类/不吞异常语义）、§8.2~§8.8（各单元错误→诊断映射的归属）、
 *     §9.2（IDiagnosticFactory 接口签名与行为契约——create/registerTranslation/
 *     translate 同接口）、§4.5（paramSchema 占位一致性——工厂校验）、§4.6
 *     （内置码表——转译目标码全部已注册）
 *   - 需求 ERR-01（诊断字段/绑定对象）、UX-03（三要素）、NFR-MNT-03（码/
 *     文案单一权威——工厂只接受已注册码）
 *   - 任务契约 tasks/foundation/DIAG-T04.json（≙WP-09-T04 部分）：`Factory.*`
 *     （工厂＋ErrorCodeTranslator 骨架与 PRJ/RT/POLICY/EVI/EX 映射——§11
 *     DIAG-T04 行产物）；knownPitfalls CR-08 处置（acceptance 2）
 *
 * 背景说明（为什么"诊断不能经异常文本临时生成"，§4.5）：稳定诊断码是持久化
 * 契约（进入项目历史/报告后永久可解释）——任何"拿一段字符串当码"的路径都会
 * 绕过注册表的码值权威。本工厂是**唯一创建入口**：code 必须已在
 * StableCodeRegistry 登记（未注册→CodeUnknown；废弃→CodeDeprecated）；
 * 异常文本只能经 ErrorCodeTranslator 的**已登记类型映射**转译为稳定码
 * （§8.1 规则 2——按异常类型而非文本匹配，DT-REG-4）。
 *
 * 工厂与转译器的体（实现登记，单元卡 v0.5）：§9.2 将创建与转译合并于**同一
 * 接口**（create/registerTranslation/translate 三方法一张契约表；§8.1"各小节
 * 的映射表即注册进 ErrorCodeTranslator 的数据"）——故实现为单实现类
 * DiagnosticsFactory（转译注册表即其组成，决策 D-14）；卡 §3.1/§11 的
 * ErrorCodeTranslator 命名以类型别名提供（同一类型，"跨单元错误转换注册表"
 * 即工厂的转译面视图）——不设第二个实现体（NFR-MNT-04 禁重复设施）。L5
 * 装配注入同一对象充任两角色（§9.7 尾注 report＝factory.create＋catalog.append
 * 的 factory 即它）。
 *
 * 陷阱处置锚点（契约 knownPitfalls 逐项）：
 *   - CR-08（acceptance 2）：跨单元错误只经已登记类型映射转译为稳定码＋
 *     DiagnosticRecord；**禁字符串匹配**（转换表按 type_index 登记——编译期
 *     类型安全，可静态审计）；**不跨边界抛对方异常类型**（本单元异常轨只有
 *     DiagnosticsError；捕获的对方异常就地转译为诊断条目，绝不重抛/包装——
 *     foundation-contract-review CR-08"已关闭"处置原文）。
 *   - P-DIAG-1：create 入参/条目内嵌的 core::DiagnosticRecord 以 core.md
 *     v0.1 为基线；core 冻结 diff 后增量同步，不私改 core。
 *
 * 五族映射的就位口径（acceptance 2——"PRJ/RT/POLICY/EVI/EX 映射就位"）：
 *   按 §8.8"转换设施归本文、映射数据归域"的分工与 §8 各节归属：
 *   - RT：§8.3 明文"本文转换表登记 std::exception 体系→RT-ROBWORK-ERROR 的
 *     兜底规则"——本任务经 registerStageATranslations() 登记（具体注册，可
 *     translate）；
 *   - POLICY/EVI：§8.4/§8.7——两单元经各自 IPolicyDiagnostics/工厂路径直接
 *     产出 core::DiagnosticRecord，其"映射"＝码表分类/严重元数据（§4.6，
 *     DIAG-T03 已收编）＋分类映射（§4.3/§4.4）——**无异常转译行**，不预建；
 *   - PRJ（§8.2）/EX（§8.5）：映射行按对方错误**枚举值**逐值定向（同类型多
 *     目标码），type_index 注册面（一型一码）无法表达——映射数据归域（§8.8），
 *     随 project/execution 落地任务经其装配清单登记（每值一行直创或派生
 *     注册），本任务交付转译设施＋注册协议；逐值映射行不在本任务预建
 *     （NFR-MNT-04：无消费者的映射行不预建）。
 *
 * 线程安全：create 并发安全（entryId 单调序用原子——§9.2 契约表"线程"行：
 * "并发安全（内部无共享可变状态；entryId 单调序用原子）"）；registerTranslation
 * 装配期单线程约定（seal 后运行期调用→Usage，判据同 StableCodeRegistry::seal
 * 实现口径——evidence EV-T09 先例登记）。
 */

#ifndef SDURWS_IRD_DIAGNOSTICS_FACTORY_HPP
#define SDURWS_IRD_DIAGNOSTICS_FACTORY_HPP

#include <atomic>
#include <exception>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>       // core::DiagnosticRecord（P-DIAG-1 基线）
#include <sdurws/ird/diagnostics/Catalog.hpp>   // DiagContext/DiagnosticEntry/IClock
#include <sdurws/ird/diagnostics/DiagCodes.hpp> // IDiagnosticRegistry（码表查询面）
#include <sdurws/ird/diagnostics/Errors.hpp>    // DiagnosticsError（本单元唯一异常类型）

namespace sdurws::ird::diagnostics {

// =====================================================================
// IDiagnosticFactory（§9.2 接口——创建/校验/关联 + ErrorCodeTranslator 面）
// =====================================================================

/**
 * @brief 转译源输入（translate 模板对异常对象的非泛型投影——类型索引＋类型
 *        名＋消息原料）。
 *
 * 为什么需要投影：translate<E> 在头文件内联（模板），泛型体只做三件事——
 * 取 typeid、取异常类型名、取 what() 消息（仅当 E 派生 std::exception）；
 * 其余全部派发到非虚实现（.cpp），保持模板体零逻辑（可审计）。
 *
 * 确定性（NFR-COR-02）：typeName 取 typeid 的编译器拼写（MSVC 下为
 * "class X" 形态）——同一二进制内稳定；跨编译器不承诺同串（转译条目为会话态
 * 开发诊断字段，不进入持久化身份）。
 */
struct TranslationInput {
    std::type_index type;      ///< 静态类型索引（注册查找键——§9.2 registerTranslation）
    std::string typeName;      ///< 异常类型名（§8.1 规则 2：保留原始来源）
    std::string message;       ///< 异常消息（仅 std::exception 体系可得；其余为空——
                               ///       转译条目按"消息不可得"登记，不伪造）
    bool isRuntimeError = false;  ///< 静态类型派生 std::runtime_error（兜底查找序②——
                                  ///  编译期判定：注册/查找均在调用方静态类型上进行）
    bool isException = false;     ///< 静态类型派生 std::exception（兜底查找序③——
                                  ///  含②；§8.3"std::exception 体系兜底"的判定面）
};

/**
 * @brief 诊断工厂接口（§9.2 原文签名——唯一创建入口＋转译注册面同接口）。
 *
 * 行为契约（§9.2 签名注释逐条）：
 *   - create：前置——code 已注册且未废弃；用户级码 subject 合法；比较型码
 *     comparison 完整（core C-1 强化）；来源码的必填上下文齐备（execution→
 *     task——§8.10 锚定规则；评估/命令路径的逐码上下文清单未由卡给出规范性
 *     枚举，阶段 A 只机械执行 §8.10 一条，登记于单元卡 v0.5）。后置——返回
 *     不可变 DiagnosticEntry（分类/严重取自码表；entryId/dedupKey/orderKey
 *     已赋；同输入同输出，时间戳来自注入 IClock）。错误——CodeUnknown /
 *     CodeDeprecated / SubjectMissing / ComparisonMissing / ParamSchemaMismatch
 *     / ContextMissing / Usage。副作用——**无**（不入目录——入目录是 sink 的
 *     显式行为，创建与记录分离便于测试替身，D-18）。
 *   - registerTranslation：转换规则登记（装配期；同错误类型重复登记→
 *     DuplicateCode）。
 *   - translate：错误转换（按登记的类型映射，非字符串匹配）；产出目标码条目
 *     并 causedBy 根因条目（root 为空则标注原始来源，不丢根因）；未登记类型
 *     →产出 DIAG-REGISTRY-UNKNOWN-CODE 条目（保留来源类型名＋安全摘要），
 *     **不抛**。
 */
class IDiagnosticFactory {
public:
    virtual ~IDiagnosticFactory() = default;

    /**
     * @brief 唯一创建入口（§9.2 原文签名——诊断不能经异常文本临时生成的
     *        机制承载）。
     *
     * @param record  [in] core 诊断记录（P-DIAG-1 基线：句法已由 core 工厂
     *                校验；本工厂追加码已注册/subject 边界/三要素/占位校验）
     * @param context [in] 关联身份块（sourceUnit/sourceInterface 必填非空且
     *                在 token 长度边界内；params 键集须与码 paramSchema 一致）
     * @return 不可变条目（entryId 单调；分类/严重自码表；dedupKey/orderKey
     *         已计算；contractVersions 已追加码表版本标注）
     *
     * @throws DiagnosticsError 见类注释错误面（首错即停，detail 指明字段——
     *         检查序登记于 DiagnosticsFactory::create 实现注释）
     */
    virtual DiagnosticEntry create(const core::DiagnosticRecord& record,
                                   const DiagContext& context) = 0;

    /**
     * @brief 登记一条类型→目标码转译规则（§9.2 原文签名；装配期）。
     *
     * @param errType    [in] 错误类型索引（translate 的查找键——精确类型；
     *                    基类兜底见 DiagnosticsFactory::translateDispatch 实现注释）
     * @param targetCode [in] 目标稳定码（必须已注册——未注册码的转译规则即
     *                    装配错误，CodeUnknown 拒绝）
     *
     * @throws DiagnosticsError DuplicateCode（同错误类型重复登记）；CodeUnknown
     *         （targetCode 未注册）；Usage（seal 后运行期调用）
     */
    virtual void registerTranslation(std::type_index errType,
                                     std::string_view targetCode) = 0;

    /**
     * @brief 错误转译（§9.2 原文模板签名——按登记的类型映射，非字符串匹配）。
     *
     * 查找序与根因链接的实现契约见 DiagnosticsFactory::translateDispatch
     * （本模板只做泛型投影后派发）。要点：
     *   - 未登记类型→DIAG-REGISTRY-UNKNOWN-CODE 兜底条目（Dev 级，保留来源
     *     类型名＋消息截断摘要，不抛——§8.1 规则 2/§9.2 错误行）；
     *   - root 非空→产出条目 causedBy=root->entryId（§6.3 不丢根因）；
     *   - 不接管、不重抛 err（CR-08：不跨边界抛对方异常类型——转译是显式
     *     调用的行为，不改变调用方控制流）。
     *
     * @param E        [模板] 错误类型（可为任意类型——非 std::exception 体系
     *                 的未登记类型同样落入兜底条目，typeName 保留、message 置空）
     * @param err      [in] 错误对象（就地读取类型与消息）
     * @param context  [in] 关联身份块（同 create 前置）
     * @param root     [in] 根因条目（可空；§6.3 原因链——指向目录内已记录条目）
     * @return 转译产出的条目（不入目录——调用方显式 append）
     */
    template <class E>
    DiagnosticEntry translate(const E& err, const DiagContext& context,
                              const DiagnosticEntry* root = nullptr)
    {
        // 模板体只做泛型投影（类型/类型名/消息/静态继承旗标），逻辑在
        // translateDispatch——见 TranslationInput 注释。typeid(E) 取静态类型
        // （§9.2 注册面语义：按 errType 登记的即调用方的静态错误类型——查找
        // 与注册同一判据，语义自洽）。
        TranslationInput input{std::type_index(typeid(E)),
                               typeid(E).name(),
                               exceptionMessageOf(err),
                               std::is_base_of_v<std::runtime_error, E>,
                               std::is_base_of_v<std::exception, E>};
        return translateDispatch(input, context, root);
    }

protected:
    /**
     * @brief 转译派发（translate 模板的非泛型实现面；实现于 DiagnosticsFactory）。
     */
    virtual DiagnosticEntry translateDispatch(const TranslationInput& input,
                                              const DiagContext& context,
                                              const DiagnosticEntry* root) = 0;

private:
    /// 取异常消息（仅 std::exception 体系；编译期双轨——非异常类型返回空串，
    /// "消息不可得"不伪造）。
    template <class E>
    static std::string exceptionMessageOf(const E& err)
    {
        if constexpr (std::is_base_of_v<std::exception, E>) {
            return std::string(err.what());
        } else {
            return {};
        }
    }
};

// =====================================================================
// DiagnosticsFactory（§9.2 实现——创建/校验链/条目组装/转译注册表单点）
// =====================================================================

/**
 * @brief 诊断工厂实现（create 校验链＋条目组装＋转译注册表——§9.2 同一接口
 *        的单实现体；ErrorCodeTranslator 命名视图见文件尾别名）。
 *
 * create 检查序（固定，首错即停并指明字段——detail 携带字段名/码值，§4.5
 * "任一失败即拒绝并指明字段"；检查序登记于单元卡 v0.5）：
 *   ①码已注册（find 未命中→CodeUnknown）；
 *   ②码未废弃（deprecated→CodeDeprecated——§4.5.1 构造侧闸门）；
 *   ③context 来源 token（sourceUnit/sourceInterface 非空且 ≤32/≤64→Usage）；
 *   ④比较型三要素（requiresComparison 且缺 comparison→ComparisonMissing）；
 *   ⑤subject 边界（用户级码缺/非法 subject→SubjectMissing；任一级携带非法
 *     值同样拒绝——Catalog.hpp 头注"subject 边界口径"节）；
 *   ⑥paramSchema 占位一致（params 键集≠schema 参数名集→ParamSchemaMismatch，
 *     §4.5"工厂校验占位一致性"）；
 *   ⑦来源码必填上下文（execution 域码〔ownerUnit=="execution"〕缺
 *     context.task→ContextMissing，§8.10 锚定规则）。
 *
 * 组装（§9.2 后置）：entryId＝原子单调自增（≥1）；分类/严重自码表描述符；
 * emittedAtUtc＝注入时钟；dedupKey 派生（Task/Object/None——Catalog.hpp
 * ScopeKind 注释口径）；orderKey＝{时间戳计数, entryId, code}；
 * contractVersions 追加 {"diag-code", registryVersion}（§4.5——报告侧检测
 * 文案/语义演进）；threadTag/workerId/redactedContextSnapshot 阶段 A 置空
 * （采集点/脱敏接线分别随产码路径与 DIAG-T07/T08 登记）。
 *
 * translateDispatch 查找序（实现口径，登记于单元卡 v0.5；§9.2 未定义基类
 * 派生码的判据）：①typeid(E) 精确命中→按登记目标码转译；②未命中且静态类型
 * 派生 std::runtime_error（TranslationInput.isRuntimeError——编译期旗标）且
 * 登记过 typeid(std::runtime_error)→按其目标码；③仍未命中且静态类型派生
 * std::exception（isException）且登记过 typeid(std::exception)→按其
 * 目标码（§8.3"std::exception 体系兜底"的机制承载——类型匹配而非文本匹配）；
 * ④全部未命中→DIAG-REGISTRY-UNKNOWN-CODE 兜底条目（Dev 级；保留来源类型名
 * ＋消息截断摘要——512 字节上限与 §7.6 异常消息摘要口径一致，"安全摘要"的
 * 脱敏级整备随 DIAG-T08 接线）。兜底路径恒不抛（目标码为 Dev 级——subject
 * 边界天然满足）；已登记路径经 create 全链（用户级目标码遵守 subject 边界：
 * root 非空时继承 root.subject——作用对象域一致；root 空且目标用户级→
 * SubjectMissing，调用方须提供根因条目或换用 Dev 级目标码——fail-fast）。
 *
 * 线程安全：create/translate 并发安全（entryId 原子；registry 运行期只读；
 * 转译规则表运行期只读——seal 后无写入）。生命周期：进程级；registry/clock
 * 为非拥有引用（调用方持有——L5 装配注入）。
 */
class DiagnosticsFactory final : public IDiagnosticFactory {
public:
    /**
     * @brief 构造（L5 装配期注入依赖）。
     *
     * @param registry [in] 稳定码注册表（码元数据唯一权威；须已 seal——本
     *                 工厂按"运行期只读"使用，未 seal 的装配期查询亦合法）
     * @param clock    [in] 时钟（emittedAtUtc 来源；测试注入 ManualClock
     *                 形态替身——Catalog.hpp IClock 注释）
     */
    DiagnosticsFactory(const IDiagnosticRegistry& registry, const IClock& clock);

    // 禁拷贝/禁移动：entryId 原子计数器与转译注册表的进程级单例语义（引用
    // 注入面要求地址稳定——同 StableCodeRegistry 纪律）。
    DiagnosticsFactory(const DiagnosticsFactory&) = delete;
    DiagnosticsFactory& operator=(const DiagnosticsFactory&) = delete;

    /// @brief 装配完成原语（幂等；seal 后 registerTranslation → Usage——
    ///        §9.2"运行期 registerTranslation"违约面）。
    void seal() noexcept;

    /// @brief 是否已进入运行期（装配自检与测试观测用）。
    bool sealed() const noexcept { return m_sealed; }

    // ---- IDiagnosticFactory（§9.2 三方法——行为契约见接口/类注释）----
    DiagnosticEntry create(const core::DiagnosticRecord& record,
                           const DiagContext& context) override;
    void registerTranslation(std::type_index errType,
                             std::string_view targetCode) override;

    /**
     * @brief 已登记的转译规则清单（装配审计面——"映射就位"的可观测证据）。
     *
     * @return {错误类型名, 目标码} 对的清单（按类型名升序——确定性观测面；
     *         纯查询，无副作用）
     */
    std::vector<std::pair<std::string, std::string>> registeredTranslations() const;

protected:
    DiagnosticEntry translateDispatch(const TranslationInput& input,
                                      const DiagContext& context,
                                      const DiagnosticEntry* root) override;

private:
    const IDiagnosticRegistry* m_registry;      ///< 码表（非拥有——码元数据权威）
    const IClock* m_clock;                      ///< 时钟（非拥有——emittedAtUtc 来源）
    std::atomic<DiagEntryId> m_nextEntryId{0};  ///< entryId 单调分配器（§9.2 并发安全）
    /// 类型→目标码注册表（§8.1"映射表……装配期登记；运行期不可变"；typeName
    /// 为 registeredTranslations 的稳定输出冗余——map 键用 type_index）。
    std::map<std::type_index, std::pair<std::string, std::string>> m_rules;
    /// 运行期标志（seal 后置位——登记路径 Usage 拒绝判据）。
    bool m_sealed = false;
};

// =====================================================================
// ErrorCodeTranslator（§3.1/§11 卡内命名——跨单元错误转换注册表的视图名）
// =====================================================================

/**
 * @brief 跨单元错误转换注册表（卡 §3.1 头表/§11 DIAG-T04 行的命名实体）。
 *
 * 实现登记（单元卡 v0.5）：§9.2 将创建与转译合并于同一接口/同一契约表，
 * 故 ErrorCodeTranslator 与 DiagnosticsFactory 为**同一类型**（别名视图）——
 * 转译注册表即工厂组成（决策 D-14"错误转换＝装配期类型映射表"），不设第二
 * 实现体（NFR-MNT-04 禁重复设施）。L5 装配注入同一对象充任"工厂"与"转译器"
 * 两角色。CR-08 处置语义（类型映射禁字符串匹配/不跨边界抛对方异常）见
 * DiagnosticsFactory 类注释与 registerStageATranslations。
 */
using ErrorCodeTranslator = DiagnosticsFactory;

/**
 * @brief 登记阶段 A 锚定的转译规则（装配清单的承载——§11 DIAG-T04 行
 *        "ErrorCodeTranslator 骨架与 PRJ/RT/POLICY/EVI/EX 映射就位"）。
 *
 * 当前清单（依据逐条）：
 *   - typeid(std::exception) → RT-ROBWORK-ERROR：§8.3 明文"本文转换表登记
 *     std::exception 体系→RT-ROBWORK-ERROR 的兜底规则"（RT 族映射——类型
 *     匹配而非文本匹配；translateRobWorkError 的 runtime 侧实现保持在其单元，
 *     本规则为转译器侧兜底）。注意目标码为用户级 Error：rootless 转译按
 *     subject 边界以 SubjectMissing 拒绝——调用方须携带根因条目（编译失败
 *     场景的根因诊断先于兜底条目存在，§6.3 链序）。
 *   - PRJ/POLICY/EVI/EX：无阶段 A 锚定的 type 级映射行（PRJ/EX 逐值映射归域
 *     装配——文件头"五族映射的就位口径"；POLICY/EVI 直接产码无异常转译行）
 *     ——不预建（NFR-MNT-04）。
 *
 * @param translator [in,out] 目标工厂/转译器（须未 seal——本函数即装配清单）；
 *                   调用方持有
 * @throws DiagnosticsError 逐条登记的注册错误（内置清单与既有登记冲突即抛，
 *         不静默跳过）
 */
void registerStageATranslations(IDiagnosticFactory& translator);

}  // namespace sdurws::ird::diagnostics

#endif  // SDURWS_IRD_DIAGNOSTICS_FACTORY_HPP
