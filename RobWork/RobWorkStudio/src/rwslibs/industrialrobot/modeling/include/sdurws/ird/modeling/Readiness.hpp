/**
 * @file   Readiness.hpp
 * @brief  IModelReadinessChecker——模型就绪校验器（§8.2 分层检查表＋§9.4.4
 *         签名落位）：L0→L11 逐层评估，结果 blockers/warnings/confirmables
 *         三组＋逐层明细；物理合法性判定共用 CommandHandlers.hpp 的
 *         AssertionSuite（D-MDL-11/NFR-MNT-04——两套判定不得出现）。
 *
 * 设计依据：
 *   - units/modeling.md §8.2（模型就绪校验——分层检查表 L0→L11、级别语义
 *     Blocking/Warning/Confirmable、就绪状态图 NotReady/ReadyWithNotes/
 *     Ready、四轴状态正交关系表、结果流向、"不可行模型≠未就绪模型"边界）
 *     ；§9.4.4（IModelReadinessChecker 签名——check 纯函数、CheckContext
 *     三字段、"与 prepare 断言的关系：两处共用同一断言实现（AssertionSuite）
 *     不得出现两套判定"、"非法调用：ctx.resolvedPolicy 为空且工作集含有限
 *     限位旋转关节→L11 报 Blocking'策略不可解析'（不静默跳过行程校验）"）
 *     ；§9.3（行程上限确认放行——④端口阈值唯一来源）；§3.3（公共头表
 *     Readiness.hpp 行——"IModelReadinessChecker、分层检查与
 *     ModelReadinessReport"）；§4.10（I-MDL 不变量——L 层数据依据）
 *   - units/policy.md §9.4（IJointLimitEvaluator 消费契约——行程比较型
 *     结果供处理器构造 ConfirmableFinding）、§4.4（JointThresholds 阈值
 *     唯一来源）、§7.4（T＞L 才超限——比较边界）
 *   - units/project.md §5.2（RevisionView/ObjectRef——闭包引用事实）
 *   - 需求 MDL-06（分层就绪校验＋断言分域）、MDL-12（continuous 工程
 *     工作范围）、SA-15（可确认诊断放行）、ARC-05（阈值唯一权威）、
 *     NFR-MNT-04（判定不重复）、NFR-COR-02（稳定排序）、REQ-06（输入
 *     未完成不运行正式评估——就绪校验是其在 modeling 侧的守门语义）
 *   - 任务契约 tasks/foundation/WP-13-T08.json acceptance 1（落位＋分层
 *     UT＋共用 AssertionSuite）、acceptance 2/3（断言分域反例与行程确认
 *     的检查器侧载体）
 *
 * 背景说明（checker 是"预检视图"——§9.4.4 原文）：就绪校验回答"模型输入
 * 是否完备合法、能否构造与编译"，**不回答**"工程上是否可行"（可达性/
 * 碰撞/动力学裕度归各评估域 EngineeringStatus——不可行≠未就绪，modeling
 * 不输出 Feasible/Infeasible 结论）。它对编辑态与草稿态即时求值（预检），
 * 对已应用修订在命令 prepare 内重估（防基线漂移/TOCTOU）——两处共用
 * AssertionSuite（CommandHandlers.hpp 内部组件），本头不携带任何第二份
 * 物理判定逻辑。
 *
 * 级别语义（§8.2 原文）：Blocking＝应用被阻止（就地、精确定位到对象）；
 * Warning＝可应用但登记（未固化资源/物性缺失预告）；Confirmable＝策略
 * 校验超限类（行程上限），不就地阻断、经显式确认放行（SA-15）。
 *
 * 复用说明（v0.9 增量口径，§14.6 登记）：§9.4.4 冻结的 CheckContext 三
 * 字段（resolvedPolicy/writable/baseRevision）逐字落位；闭包事实（L1 引用
 * 存在性/L7 tcpKey 存在性/L9 传动合法性）经 ModelingWorkingSet 的 v0.9
 * 增量字段（rootObjectId＋partObjects——Template.hpp）进入，check 签名与
 * 上下文形状不变。
 *
 * 线程安全：checker 实例构造后只读、check 为 const 纯函数——并发只读可
 * 重入（§3.4 总约定 1）。确定性：同 (工作集, 上下文) 输入必得逐字段相等
 * 输出；结果组排序稳定（层号→对象 id 字典序——NFR-COR-02）；不读时钟/
 * 环境/locale/文件系统（L6 的"缺失/变化探测"是 I/O，归 io 护栏与 runtime
 * 编译复核——本层只承载资源状态机事实，见 checkL6 注）。
 */

#ifndef IRD_MODELING_READINESS_HPP
#define IRD_MODELING_READINESS_HPP

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>     // DiagnosticRecord/ConfirmableFinding（结果组元素）
#include <sdurws/ird/core/Identity.hpp>     // RevisionId（CheckContext.baseRevision）
#include <sdurws/ird/modeling/CommandHandlers.hpp>  // AssertionSuite——就绪校验与 prepare 共用的唯一断言实现
#include <sdurws/ird/modeling/Template.hpp>  // ModelingWorkingSet（被检输入——v0.9 含闭包部件视图）
#include <sdurws/ird/policy/PolicySet.hpp>  // policy::EngineeringPolicySet（④端口解析结果）

namespace sdurws::ird::modeling {

// =====================================================================
// 分层与结果值类型（§8.2 分层检查表/级别语义的值化承载）
// =====================================================================

/**
 * @brief 就绪校验分层（§8.2 分层检查表 L0→L11——枚举序＝层号序，短路
 *        优先级由层号表达：高层依赖低层通过）。
 *
 * 枚举顺序＝§8.2 表行序（持久化契约面纪律：只允许表尾追加并走单元卡
 * 增量修订——R2 的耦合扩展层随 T18 追加，不重排）。
 */
enum class ReadinessLayer {
    L0Structure,        ///< L0 结构完整——链连通/单串联无环/计数关系/Fixed 位置（I-MDL-1）
    L1References,       ///< L1 引用完整——引用表指向闭包存在对象、token 匹配（I-MDL-9 闭包半段）
    L2UnitsFinite,      ///< L2 单位/数值合法——一切 Provided 值有限（I-MDL-3；单位合法性由构造层保证）
    L3JointAxis,        ///< L3 关节轴有效——可动关节 axis 非零有限可归一化（I-MDL-6）
    L4LimitOrder,       ///< L4 限位有序——qmin<qmax；continuous 范围已确认有限（I-MDL-4/MDL-06④/MDL-12）
    L5InertiaPhysical,  ///< L5 惯量合法——已提供 m>0/SPD/三角（I-MDL-5；缺失→Warning 预告）
    L6ResourceState,    ///< L6 资源存在——状态机一致性＋未固化提示（I-MDL-10/CON-03；Warning 不阻断）
    L7ToolTcp,          ///< L7 工具与 TCP 完整——defaultTcp 已设且 tcpKey 存在；工具物性合法（I-MDL-9/KIN-14）
    L8BasePlacement,    ///< L8 基座姿态合法——custom 必填 customEaa；正交容差（I-MDL-7/P-RT-4）
    L9Drivetrain,       ///< L9 传动可用——ratio 有限>0；R1 无 coupling（I-MDL-11/12）
    L10CanonicalReady,  ///< L10 可构造 CanonicalModel——schema 版本受支持、权威字段集完整（§9.1 映射）
    L11CompileRequestable,  ///< L11 可请求编译——writable/策略可解析/应用路径基线==tip（project/policy）
};

/**
 * @brief 取层稳定 token（"L0"～"L11"——层号原文；呈现与测试判别用）。
 * @param layer [in] 就绪层（switch 全枚举、无 default——新增层漏登记时
 *              编译器告警暴露）
 * @return 静态存储期串。纯函数；线程安全；确定性（NFR-COR-02）。
 */
std::string_view readinessLayerToken(ReadinessLayer layer) noexcept;

/**
 * @brief 就绪状态（§8.2 就绪状态图三态）。
 */
enum class ReadinessStatus {
    NotReady,        ///< 任一 Blocking——应用被阻止（blockers 列表定位）
    ReadyWithNotes,  ///< 无 Blocking 但有 Warning/待确认/缺项预告
    Ready,           ///< 无 Warning 无待确认（可提交命令；提交后再经 prepare 断言/确认/双编译）
};

/**
 * @brief 取状态稳定 token（"NotReady"/"ReadyWithNotes"/"Ready"——状态图
 *        节点名原文）。纯函数；确定性。
 */
std::string_view readinessStatusToken(ReadinessStatus status) noexcept;

/**
 * @brief 呈现级层结论（无已登记稳定码的层检查结果——§8.2"结果流向"中
 *        走 UI DomainReadinessItem 缺项数据/呈现通道的面，不产诊断记录）。
 *
 * 为什么需要独立于 DiagnosticRecord 的承载：§9.5 登记簿只给八类事实发了
 * 码（断言①②③④/行程/引用/资源/schema），而分层表中还有"构造层已保证"
 * 的域（L2 单位——非法单位在构造层不可能出现）与"缺项预告"域（L5 物性
 * 缺失→DataInsufficient 预告——走 UI missingItemKeys 而非诊断目录）、
 * L11 的呈现级子项（writable——§9.4.4 CheckContext 注释原文"只读模式
 * 提示（L11 呈现级）"）。这些结论若强行套用登记码即"改义"（登记簿纪律：
 * 一经注册不改义），故以本类型承载：定位路径＋人读中文结论＋是否阻断。
 *
 * 确定性：同输入同串（summary 为固定中文模板——无 locale 依赖）。
 */
struct ReadinessNote {
    ReadinessLayer layer = ReadinessLayer::L0Structure;  ///< 所属层
    std::string subjectPath;  ///< 定位路径（值模型内路径，如 "joints[2].axis"——UTF-8）
    std::string summary;      ///< 人读中文一句话（定位＋原因＋建议；固定模板产出）
    bool blocking = false;    ///< true＝参与 NotReady 判定（呈现级阻断——如 L2 非有限值防御面）

    bool operator==(const ReadinessNote& o) const noexcept
    {
        return layer == o.layer && subjectPath == o.subjectPath
            && summary == o.summary && blocking == o.blocking;
    }
    bool operator!=(const ReadinessNote& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 逐层明细（§9.4.4"结果含……与逐层明细"的承载——每层一条，
 *        数组下标＝层号）。
 */
struct LayerDetail {
    bool passed = false;   ///< 该层无阻断结论（Warning/预告/待确认不影响 passed）
    std::string note;      ///< 层结论说明（通过时为固定短语；失败时含定位摘要——中文）

    bool operator==(const LayerDetail& o) const noexcept
    {
        return passed == o.passed && note == o.note;
    }
    bool operator!=(const LayerDetail& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 就绪校验报告（§9.4.4 ModelReadinessReport 落位）。
 *
 * 三组结果的排序契约（acceptance 1——"三组稳定排序——按层号→对象 id
 * 字典序"）：blockers/warnings/confirmables 均按 (层号升序 → subject
 * ObjectId 规范文本字典序 → code 字典序) 稳定排序；notes 按 (层号 →
 * subjectPath 字典序)。同输入重复 check 输出逐字段相等（NFR-COR-02）。
 *
 * 结果流向（§8.2 原文，消费方须知）：UI——经 DomainReadinessItem 投影
 * （ui 汇聚不判定）；project——Blocking/Confirmable 在命令 prepare 内
 * **重估**（本报告不替代 prepare 现场断言——防 TOCTOU）；诊断目录——
 * 仅 Warning/Confirmable 明细经 IDiagnosticSink 留痕（blockers 是阻止
 * 应用的定位面，随命令拒绝经 CommandResult.diagnostics 回传）。
 *
 * 线程安全：纯值类型。
 */
struct ModelReadinessReport {
    ReadinessStatus status = ReadinessStatus::NotReady;  ///< 三态汇总
    /// 有登记码的就地阻断（L1/L4/L5/L10/L11 族——subject 精确定位对象；
    /// 码均已登记——产码点唯一为 AssertionSuite）。
    std::vector<core::DiagnosticRecord> blockers;
    /// 有登记码的警告（L6 资源未固化——可应用但登记）。
    std::vector<core::DiagnosticRecord> warnings;
    /// 待确认集（L4 策略域行程上限——SA-15；确认放行/拒绝在命令流）。
    std::vector<core::ConfirmableFinding> confirmables;
    /// 呈现级层结论（缺项预告/构造层保证域/L11 呈现级——见 ReadinessNote 注）。
    std::vector<ReadinessNote> notes;
    /// 逐层明细（下标 0..11 ↔ L0..L11——静态断言钉住数组长度与枚举数一致）。
    std::array<LayerDetail, 12> layers;

    bool operator==(const ModelReadinessReport& o) const
    {
        return status == o.status && blockers == o.blockers && warnings == o.warnings
            && confirmables == o.confirmables && notes == o.notes && layers == o.layers;
    }
    bool operator!=(const ModelReadinessReport& o) const { return !(*this == o); }
};

// =====================================================================
// CheckContext（§9.4.4 冻结三字段——逐字落位）
// =====================================================================

/**
 * @brief 就绪校验上下文（§9.4.4 原文三字段）。
 *
 * 字段语义（§9.4.4 注释原文）：
 *   - resolvedPolicy：④端口解析结果（L11/行程比较）。为空指针且工作集
 *     含有限限位旋转关节→L11 报 Blocking"策略不可解析"（**不静默跳过**
 *     行程校验——非法调用行的原文语义）；非空时行程上限比较以其
 *     JointThresholds 为唯一阈值来源（ARC-05——本地无第二常量）。
 *   - writable：只读模式提示（L11 **呈现级**——§9.4.4 字段注释原文；
 *     只读下的写阻断强制点在 project S1 not-writable，本层只呈现）。
 *   - baseRevision：应用预检用（L11）。"baseRevision==tip"的强制点在
 *     project S2 并发校验（PRJ-STALE-REVISION-REJECTED）与 prepare 防御性
 *     复核——checker 无 tip 事实（纯函数不触 ②端口），携带值仅为应用
 *     预检语义标记（有值＝本次是"应用前预检"口径，L11 明细注记之）。
 *
 * 线程安全：纯值聚合（指针非 owning——调用方保证 check 调用期存活）。
 */
struct CheckContext {
    /// ④端口解析结果（L11/行程比较）；空＝策略不可解析（L11 Blocking）。
    const policy::EngineeringPolicySet* resolvedPolicy = nullptr;
    /// 只读模式提示（L11 呈现级——写阻断强制点在 project S1）。
    bool writable = true;
    /// 应用预检的基线修订（有值＝应用预检口径——tip 相等性强制在 project
    /// S2/prepare 复核；nullopt＝编辑态/草稿态预检）。
    std::optional<core::RevisionId> baseRevision;
};

// =====================================================================
// IModelReadinessChecker——接口与唯一实现（§9.4.4）
// =====================================================================

/**
 * @brief 模型就绪校验器（§9.4.4 原文契约）：纯函数；逐层 L0→L11（§8.2
 *        分层表，顺序即短路优先级——高层依赖低层通过）；结果三组＋逐层
 *        明细，稳定排序。
 *
 * @post 不修改输入工作集；不产生修订；不写诊断目录（呈现归调用方——
 *       §9.4.4 原文）。
 * @错误 不抛异常（§9.4.4 原文"不抛异常"；策略评估器对装配违约的
 *       fail-fast 异常不在此列——本层装配查询表时已保证查询契约，见
 *       AssertionSuite::evaluateTravelLimits 注）。
 *
 * 层实现与数据依据（§8.2 表逐行——判定逻辑唯一落在 AssertionSuite 与
 * RobotDesign/Parts 的既有不变量层，本类只做编排与层归属）：
 *   - L0/L3/L8：checkInvariants(RobotDesign) 的 I-MDL-1/2/6/7/8 违例
 *     （构造层保证域——防御性呈现级阻断，解码门已先行强制）；
 *   - L1：引用表/poseSetRef/drivetrainRef 对工作集闭包视图的存在性＋
 *     token 匹配（MDL-READINESS-REF-MISSING）；defaultTcp∈toolRefs 半段
 *     由 I-MDL-9 覆盖（L0 呈现）；
 *   - L2：I-MDL-3 违例（构造层保证域——防御性呈现级）；
 *   - L4：AssertionSuite::assertJointLimitIntervals（MDL-ASSERT-LIMIT-
 *     INTERVAL/MDL-ASSERT-RANGE-NOT-FINITE——Blocking）；
 *   - L5：AssertionSuite::assertBodyPhysical（连杆——MDL-ASSERT-* 硬断
 *     言）＋缺失预告（Warning 级 notes——DataInsufficient 预告）；
 *   - L6：资源状态机事实（Recorded 未固化→MDL-READINESS-RESOURCE-STATE
 *     Warning——不阻断；Solidified 通过；缺失/变化探测归 io/runtime）；
 *   - L7：工具物性（AssertionSuite::assertBodyPhysical——工具）＋defaultTcp
 *     的 tcpKey 在被引工具 tcpList 中的存在性（闭包视图可判半段）；
 *   - L9：checkInvariants(DrivetrainDesign, R1Locked)（I-MDL-11/12——
 *     构造层保证域＋传动缺失预告）；stage 恒 R1Locked（R2 启用随 T18）；
 *   - L10：schema 主版本受支持（MDL-READINESS-SCHEMA-UNSUPPORTED）＋
 *     Explicit 权威下可动关节 axis/origin 已提供（Description 构造的字段
 *     集完整半段——缺失为呈现级阻断；DH 权威的派生重算归 T09）；
 *   - L11：resolvedPolicy 为空且存在有限限位旋转关节→Blocking"策略不可
 *     解析"（携带行程校验未执行的事实——不静默跳过）；策略可用时执行
 *     行程上限比较（MDL-06-TRAVEL-LIMIT Confirmable——SA-15）；writable/
 *     baseRevision 为呈现级注记。
 *
 * 线程安全：实例构造后只读；check 并发只读安全。
 */
class IModelReadinessChecker {
public:
    virtual ~IModelReadinessChecker() = default;

    /**
     * @brief 全量分层校验（§9.4.4 原文签名）。
     *
     * @param ws  [in] 被检工作集（只读——@post 不修改；闭包视图取其
     *            v0.9 增量字段）
     * @param ctx [in] 上下文（§9.4.4 三字段——见 CheckContext 注）
     * @return 报告（三组＋逐层明细；稳定排序；同输入同输出）
     *
     * 纯函数；线程安全；确定性（NFR-COR-02）。
     */
    virtual ModelReadinessReport check(const ModelingWorkingSet& ws,
                                       const CheckContext& ctx) const = 0;
};

/**
 * @brief IModelReadinessChecker 唯一产品实现（无状态——断言判定委托
 *        AssertionSuite 单例面；可默认构造，拷贝/移动平凡）。
 *
 * 为什么需要 AssertionSuite 注入而非内部构造：就绪校验与 prepare 断言
 * 共用同一判定（D-MDL-11/NFR-MNT-04——两套判定不得出现），而套件的
 * 策略消费面（行程评估器＋名称上下文）由 L5 装配注入（R-4：名称解析
 * 归 runtime，modeling 不自建名称映射）。
 */
class ModelReadinessChecker final : public IModelReadinessChecker {
public:
    /// 构造（注入共用断言套件；引用非 owning——调用方保证存活期覆盖）。
    explicit ModelReadinessChecker(const AssertionSuite& suite) noexcept;

    /// @copydoc IModelReadinessChecker::check
    ModelReadinessReport check(const ModelingWorkingSet& ws,
                               const CheckContext& ctx) const override;

private:
    const AssertionSuite* m_suite;  ///< 共用断言套件（非 owning——NFR-MNT-04 单一判定）
};

/**
 * @brief 唯一实现的装配入口（makeJointLimitEvaluator 同款对称性——供
 *        L5/测试装配，产出只读校验器实例）。
 *
 * @param suite [in] 共用断言套件（引用须覆盖实例使用期）
 * @return 校验器实例（调用方经 unique_ptr 持有）
 */
std::unique_ptr<IModelReadinessChecker> makeModelReadinessChecker(const AssertionSuite& suite);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_READINESS_HPP
