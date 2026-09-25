/**
 * @file   Editor.hpp
 * @brief  IRequirementEditor——需求编辑器（§9.3 行原文契约的落位）：基线
 *         修订闭包＋未应用编辑差值的演算视图，含局部撤销/重做与变更
 *         摘要。非线程安全（仅 UI 线程——§3.4 总约定 2）。
 *
 * 设计依据：
 *   - units/requirements.md §3.3（公共头表 Editor.hpp 行——T03）、§9.3
 *     （IRequirementEditor 行原文签名：loadBaseline/workingSet/applyEdit/
 *     undoLocal〔局部撤销〕/redoLocal/buildChangeSummary/draftStatus；
 *     @pre"closure 含 req-set（token 路由）；解码失败→RequirementError
 *     (SchemaVersionUnsupported)"；@post"接受：工作集本地版本 +1（局部
 *     撤销入栈）＋就绪增量重估；拒绝：字节不变＋逐项诊断"）、§4.6
 *     （编辑态/草稿态/已应用修订态三态——编辑器是编辑态的承载）、§5.1
 *     （删除引用保护——编辑边界拒绝）、§9.1/§9.5（D-REQ-9：命令族仅
 *     两条，细粒度由编辑器局部撤销承载）、§3.4（编辑器仅 UI 线程）
 *   - modeling/CanonicalBridge.hpp（ObjectClosureView 闭包域字节源抽象
 *     ——P-RT-5 注入形态采纳的 requirements 侧同构）
 *   - 任务契约 tasks/foundation/WP-14-T03.json acceptance 4（构造边界
 *     ——编辑器拒绝面复用服务校验）、acceptance 5（O-36：条目经正式
 *     分配后跨修订稳定；编辑器不重分配已有条目 id）
 *
 * 背景说明（编辑器的角色——§4.6 三态中的"编辑态"）：编辑器持基线闭包
 * 解码出的工作集＋编辑差值；草稿落盘/命令提交归 DraftService/命令处理
 * 器（T05——本单元不自行提交）；局部撤销零修订（与项目级撤销分离，
 * REQ-11）。所有编辑先过服务同款校验（validateXxx＋集合唯一性）——
 * 编辑态里的工作集恒为"可编码"形态（canonical 前提，I-REQ-1）。
 *
 * 线程约束（§3.4 总约定 2 原文）：**非线程安全，仅 UI 线程访问**——工作
 * 集/撤销栈为可变共享状态，不做任何加锁（UI 线程串行是唯一使用形态）。
 * 确定性：同基线＋同编辑序列→同工作集（摘要文本含条目名/id——确定性
 * 拼装）。
 */

#ifndef IRD_REQUIREMENTS_EDITOR_HPP
#define IRD_REQUIREMENTS_EDITOR_HPP

#include <sdurws/ird/requirements/Codec.hpp>          // 解码闸口＋FormatVersion
#include <sdurws/ird/requirements/ObjectTypes.hpp>    // 五对象 token
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // 值模型＋校验层

#include <string>
#include <variant>
#include <vector>

namespace sdurws::ird::requirements {

// =====================================================================
// 闭包域字节源（§9.3 loadBaseline 入参的 requirements 侧契约面——
// modeling/CanonicalBridge.hpp 同构注入形态；实现由 L5/调用方提供）
// =====================================================================

/// 闭包内对象的一次取回产出（类型 token＋canonical 字节；值语义纯结构）。
struct RequirementClosureObject {
    std::string objectTypeToken;             ///< 存储登记的对象类型 token（路由/复核面）
    RequirementBytes bytes;                  ///< 对象 canonical 字节（Codec.decode 输入）
};

/**
 * @brief 闭包域字节源抽象（§9.3 loadBaseline @pre"closure 含 req-set
 *        （token 路由）"的取回面）。
 *
 * 实现方约束（与 modeling ObjectClosureView 同款）：并发只读安全；同键
 * 重复取回同字节或稳定 nullopt；闭包域纪律（只应答目标修订闭包内对象）。
 * 生命周期：非 owning——调用方持有并保证 loadBaseline 调用期存活。
 */
class RequirementObjectClosureView {
public:
    virtual ~RequirementObjectClosureView() = default;

    /**
     * @brief 按对象类型 token 取唯一对象（根对象路由——req-set）。
     * @return 命中＝token＋字节；nullopt＝闭包内无该 token 对象
     */
    virtual std::optional<RequirementClosureObject>
        tryObjectByToken(std::string_view objectTypeToken) const = 0;

    /**
     * @brief 按对象身份取对象（四集合解引用——§4.1 根对象引用表）。
     * @return 命中＝token＋字节；nullopt＝不在闭包
     */
    virtual std::optional<RequirementClosureObject>
        tryObject(const core::ObjectId& objectId) const = 0;
};

// =====================================================================
// 工作集与编辑差值（§4.6 编辑态承载）
// =====================================================================

/**
 * @brief 需求工作集（§9.3 workingSet——基线解码结果＋已应用编辑的演算
 *        视图；五对象齐全〔根＋四集合〕）。
 *
 * 不变量：四集合条目恒按 ObjectId 字典序（I-REQ-1——编辑器插入即排序）
 * ＋集合内名称唯一（I-REQ-3）＋跨集合 id 唯一（I-REQ-2）——applyEdit 的
 * 拒绝面维护。
 */
struct RequirementWorkingSet {
    RequirementSet root{};        ///< 需求集根对象（§4.2）
    PointSet points{};            ///< 任务点集合（req-point-set）
    RegionSet regions{};          ///< 区域集合（req-region-set）
    ConditionSet conditions{};    ///< 工况集合（req-condition-set）
    PlanSet plans{};              ///< 采样计划集合（req-plan-set）

    bool operator==(const RequirementWorkingSet& o) const
    {
        return root == o.root && points == o.points && regions == o.regions
            && conditions == o.conditions && plans == o.plans;
    }
    bool operator!=(const RequirementWorkingSet& o) const { return !(*this == o); }
};

/**
 * @brief 编辑差值（§9.3 RequirementEdit——字段级/条目增删的值承载；
 *        D-REQ-9：细粒度编辑在编辑器局部撤销承载，不产生逐字段命令）。
 *
 * 变体备择序＝编辑种类登记序（表尾追加纪律）；Add/Update 携完整条目值
 * （Update 按objectId 整体替换——编辑器无字段级合并语义，"字段级"指
 * 调用方在值上做字段修改后整体提交）；Remove 携条目 id。
 */
using RequirementEdit = std::variant<
    TaskPoint,              ///< Add/Update：目标集合内已有同 id＝Update，否则 Add（upsert 语义）
    WorkRegion,             ///< 同上（区域）
    OperatingCondition,     ///< 同上（工况）
    SamplingPlan,           ///< 同上（计划）
    std::pair<std::string, std::string>,  ///< SetRootHeader：{name, note}（根对象头编辑）
    std::pair<core::ObjectId, int>>;      ///< Remove：{条目 id, 目标集合种类——WorkingSetMember 枚举值}

/// Remove 编辑的目标集合种类（RequirementEdit 第二备择的 int 载荷语义；
/// 登记序＝workingSet 成员序——表尾追加纪律）。
enum class WorkingSetMember : int {
    Points = 0,      ///< 任务点集合（TaskPoint 条目）
    Regions = 1,     ///< 区域集合（WorkRegion 条目）
    Conditions = 2,  ///< 工况集合（OperatingCondition 条目）
    Plans = 3,       ///< 计划集合（SamplingPlan 条目）
};

/// Remove 编辑的便捷工厂（类型安全——免手拼 pair；variant 装载非 constexpr
/// 可表达，普通 inline 函数）。
inline RequirementEdit removeEdit(core::ObjectId id, WorkingSetMember member)
{
    return RequirementEdit{std::in_place_type<std::pair<core::ObjectId, int>>,
                           id, static_cast<int>(member)};
}

/// SetRootHeader 编辑的便捷工厂。
inline RequirementEdit rootHeaderEdit(std::string name, std::string note)
{
    return RequirementEdit{std::in_place_type<std::pair<std::string, std::string>>,
                           std::move(name), std::move(note)};
}

/**
 * @brief 编辑应用产出（§9.3 EditOutcome——"接受：工作集本地版本 +1；
 *        拒绝：字节不变＋逐项诊断"的值承载；诊断经 error/detail 值面，
 *        本单元无已登记诊断码——Errors.hpp 阶段纪律）。
 */
struct EditOutcome {
    bool accepted = false;         ///< true＝工作集已更新＋撤销入栈；false＝字节不变
    RequirementError error{};      ///< 拒绝面：首个违例（DuplicateName/MalformedPayload/…）
    std::string changeSummary;     ///< 接受面：本次编辑的人读中文摘要（并入 buildChangeSummary）
};

/// 载入产出（§9.3 LoadOutcome——基线解码失败携 RequirementError）。
struct RequirementLoadOutcome {
    bool ok = false;               ///< true＝workingSet 有效；false＝error 有效
    RequirementError error{};      ///< 失败面：SchemaVersionUnsupported/MalformedPayload
};

/// 草稿状态（§9.3 RequirementDraftStatus——{dirty, baseRevisionId, edits}）。
struct RequirementDraftStatus {
    bool dirty = false;            ///< 自基线以来有未应用提交的编辑
    std::string baseRevisionId;    ///< 基线修订 id 规范文本（未载入＝空）
    std::uint64_t edits = 0;       ///< 已应用编辑数（撤销/重做不改变该计数——差值语义）
};

// =====================================================================
// IRequirementEditor——接口（§9.3 行原文契约；仅 UI 线程）
// =====================================================================

class IRequirementEditor {
public:
    virtual ~IRequirementEditor() = default;

    /**
     * @brief 载入基线（修订闭包解码）并返回可变工作集句柄（§9.3 行原文）。
     *
     * @pre closure 含 req-set（token 路由）；四集合对象经根引用表解引用
     *      可达（缺失/异 token＝闭包违约拒绝）。解码失败→
     *      RequirementError(SchemaVersionUnsupported)（§9.3 @pre 行原文
     *      ——版本面）或 MalformedPayload（结构/不变量面——decode 校验链）。
     * @post 载入成功＝工作集/撤销栈/重做栈/编辑计数全部重置为基线态
     *       （dirty=false）；载入失败＝编辑器保持原状（不半更新——
     *       NFR-COR-03）。
     *
     * @param closure [in] 闭包域字节源（只读；调用期存活——非 owning）
     * @return ok＝载入成功；err＝解码/闭包违约错误
     *
     * 线程约束：仅 UI 线程。
     */
    virtual RequirementLoadOutcome loadBaseline(const RequirementObjectClosureView& closure) = 0;

    /**
     * @brief 只读工作集（§9.3 行原文——含 RequirementProfile 派生档的
     *        演算视图本体；派生档按需经 deriveRequirementProfile 重算，
     *        §4.8"不入 canonical 编码"）。
     * @return 工作集 const 引用（生命周期随编辑器）
     *
     * 线程约束：仅 UI 线程。
     */
    virtual const RequirementWorkingSet& workingSet() const noexcept = 0;

    /**
     * @brief 应用一次编辑（§9.3 行原文——字段级/条目增删/批量变体/模板
     *        镜像阵列批次；T03 落位面＝条目 upsert/删除/根头编辑，模板/
     *        镜像阵列批次随 T07 EditBatch 落位复用本入口）。
     *
     * 校验链（拒绝＝字节不变＋逐项诊断——@post 行原文）：
     *   ①条目级不变量（validateTaskPoint 等——服务/解码同源）；
     *   ②集合级唯一性：同集合名称唯一（I-REQ-3——Add 时与既有名核对；
     *     Update 时除自身外核对）＋同集合 id 唯一（I-REQ-2 集合内）；
     *   ③跨集合 id 唯一（I-REQ-2 跨集合半区——checkCrossSetIdUniqueness）；
     *   ④删除引用保护（§5.1）：删除被 appliesTo/events.stationRef/顺序键
     *     引用的任务点、被 regionRef 引用的区域、被计划引用存在性的区域
     *     ——编辑边界拒绝＋定位（MalformedPayload 携 subject）。
     * @post 接受：工作集本地版本 +1（局部撤销入栈）＋重做栈清空＋编辑
     *       计数 +1；拒绝：工作集/栈全部不变。
     *
     * @param edit [in] 编辑差值（值语义——编辑器接管拷贝）
     * @return 接受/拒绝＋摘要
     *
     * 线程约束：仅 UI 线程。
     */
    virtual EditOutcome applyEdit(const RequirementEdit& edit) = 0;

    /**
     * @brief 局部撤销一步（§9.3 undoLocal——零修订；编辑器工作集回退，
     *        项目级撤销归 project 逆命令——两套撤销分离，REQ-11）。
     * @return true＝撤销成功（工作集回退一步）；false＝无可撤销编辑
     *         （栈空——工作集不变）
     *
     * 线程约束：仅 UI 线程。
     */
    virtual bool undoLocal() noexcept = 0;

    /**
     * @brief 局部重做一步（§9.3 redoLocal——重放最近被撤销的编辑）。
     * @return true＝重做成功；false＝无可重做编辑
     *
     * 线程约束：仅 UI 线程。
     */
    virtual bool redoLocal() noexcept = 0;

    /**
     * @brief 自基线以来的人读中文变更摘要（§9.3 行原文——命令提交时并入
     *        命令摘要）。
     * @return 逐编辑摘要行拼接（编辑序；空＝无编辑）
     *
     * 线程约束：仅 UI 线程（读工作集/栈——串行保证一致性）。
     */
    virtual std::string buildChangeSummary() const = 0;

    /// @brief 草稿状态（§9.3 RequirementDraftStatus——{dirty, baseRevisionId, edits}）。
    virtual RequirementDraftStatus draftStatus() const = 0;
};

/// IRequirementEditor 的产品实现（仅 UI 线程——非线程安全；局部撤销栈
/// 为深拷贝快照栈——工作集规模数千条目时撤销一步 O(集合)拷贝，编辑粒
/// 度频繁但集合规模有限，R-REQ-3 同源取舍：实测超标再增量差值化）。
class RequirementEditor final : public IRequirementEditor {
public:
    RequirementLoadOutcome loadBaseline(const RequirementObjectClosureView& closure) override;
    const RequirementWorkingSet& workingSet() const noexcept override { return ws_; }
    EditOutcome applyEdit(const RequirementEdit& edit) override;
    bool undoLocal() noexcept override;
    bool redoLocal() noexcept override;
    std::string buildChangeSummary() const override { return summary_; }
    RequirementDraftStatus draftStatus() const override;

private:
    /// 撤销栈一步＝快照＋摘要前缀（深拷贝——见类注）。
    struct UndoStep {
        RequirementWorkingSet snapshot;  ///< 编辑前工作集全量快照
        std::string summaryLine;         ///< 该步编辑的摘要行
    };

    RequirementWorkingSet ws_;                        ///< 演算视图（编辑态）
    std::string baseRevisionId_;                      ///< 基线修订 id 规范文本
    std::vector<UndoStep> undoStack_;                 ///< 局部撤销栈（编辑序）
    std::vector<UndoStep> redoStack_;                 ///< 局部重做栈（撤销序）
    std::string summary_;                             ///< 自基线以来的变更摘要
    std::uint64_t editCount_ = 0;                     ///< 已应用编辑数（差值语义）
    bool loaded_ = false;                             ///< 是否已成功载入基线
};

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_EDITOR_HPP
