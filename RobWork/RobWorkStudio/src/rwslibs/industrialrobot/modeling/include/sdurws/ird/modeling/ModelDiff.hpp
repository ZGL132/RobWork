/**
 * @file   ModelDiff.hpp
 * @brief  IModelDiffService——Model Diff 差异增量表数据实体（MDL-08）：两个
 *         ModelingWorkingSet 输入、结构/参数（DH、轴线、限位）/物性三组
 *         增量条目、逐条携带对象定位（供 UX-13 点击定位消费）。
 *
 * 设计依据：
 *   - units/modeling.md §9.4.9（IModelDiffService 签名行——"Model Diff 数据
 *     实体（MDL-08；两工作集输入，稳定排序增量表）；ModelDiffReport
 *     diff(const ModelingWorkingSet& baseline, const ModelingWorkingSet&
 *     candidate) const；分组：结构/参数（DH、轴线、限位）/物性；条目含
 *     对象定位（供 UX-13 点击）"）、§3.3（公共头表 ModelDiff.hpp 行——
 *     "IModelDiffService、差异增量表数据实体（MDL-08）"）、§3.4（纯函数
 *     服务总约定——无共享可变状态、可重入、确定性）
 *   - units/modeling.md §4.3/§4.3-A/§4.3-B（根对象字段表与字段声明序——
 *     组内"字段序"排序键的唯一权威）、§4.8（身份/版本——集合规范化语义：
 *     引用表与资源清单在 canonical 字节中按字典序规范化）、§14.2
 *     D-MDL-5（派生字段不入编码身份——差异面同口径）
 *   - 需求 MDL-08（数据层基线与候选差异比较——不直接覆盖原始基线；呈现
 *     归 UX-13，M-5 分工）、AT-12（数据侧：比较型差异逐项可观察）、
 *     NFR-COR-02（同输入→同报告；稳定排序）、ARC-04（ObjectId 跨修订
 *     稳定——条目对象定位的身份锚）
 *   - 任务契约 tasks/foundation/WP-13-T14.json acceptance 1～4
 *
 * 背景说明（本服务在产品里的位置）：MDL-08 是"方案比较"的第一段——
 * 七阶段工作流中用户会在多个候选方案（各为一个 ModelingWorkingSet）之间
 * 徘徊，本服务产出**数据层**的差异增量表；画面呈现（分组视图、差异高亮、
 * 点击定位跳转）归 WP-22-T11/UX-13（V15-02 补登记），采用候选为另一独立
 * 需求（OPT-08）。本头只拥有差异**数据实体与比较语义**（M-5 分工红线）：
 * 接口无任何 UI 类型依赖、零 Qt（L2 计算内核红线，BuildRedLineTest 与
 * ird_gates 双重复核）。
 *
 * 比较面与范围（MDL-08 原文口径——"两个 RobotDesign 之间的……差异增量
 * 表"）：diff 逐字段比较两个工作集的 design（RobotDesign 根对象），输入
 * 类型取 ModelingWorkingSet（§9.4.9 签名行字面——工作集是编辑态方案的
 * 演算结果载体）。范围边界三项，均为语义决策而非遗漏：
 *   ① 根对象引用表（toolRefs/sceneRefs/poseSetRef/drivetrainRef/defaultTcp）
 *      的变化入表（结构组）——引用是根对象字段；**部件对象内容**（工具
 *      物性、场景位姿等）不入本表：MDL-08 的比较面是"两个 RobotDesign"，
 *      部件对象内容的差异比较随消费方任务（WP-22-T11）按需扩展；
 *   ② 工作集 changes 变更摘要日志不参与比较——它是编辑过程的派生记录
 *      （§4.9 编辑态行），不是模型内容；
 *   ③ selfCollisionHints 不入表——不入根对象编码权威语义（§4.3-B 行
 *      原文，Codec 不编码），与 D-MDL-5"身份外字段不作差异项"同一口径。
 *
 * 派生只读字段规则（acceptance 2/D-MDL-5，本头最重要的语义决策）：
 *   权威受管字段（§7.3 C-1/C-2 的字段轴）只在**两侧同为权威**时比较：
 *   - axis/origin：两侧 authority 均为 Explicit 时比较（权威一等字段，
 *     MDL-09）；任一侧为 StandardDH 即为派生只读——不产生独立差异项
 *     （其值随 DH 权威字段确定性重算，报告它会伪造"独立变化"）；
 *   - dhDerived：两侧 authority 均为 StandardDH 时比较（权威）；任一侧
 *     为 Explicit 即为派生展示值——不产生独立差异项；
 *   - 两侧 authority 不同：受管三字段全部跳过（每字段在至少一侧是派生
 *     值），authority 开关本身的差异以一条结构组条目承载——权威表述的
 *     切换是两模型间真实且唯一需要报告的语义事实。
 *   该规则与 Codec 编码身份严格同构（D-MDL-5：Explicit 态不编码
 *   dhDerived、StandardDH 态不编码 axis/origin）——差异面=身份面，不存在
 *   "字节相同而报告有差"或"字节不同而报告无差"的受管字段组合。
 *
 * 确定性（NFR-COR-02，acceptance 2）：同输入重复 diff 输出逐字段相等的
 * 报告；每组条目按（对象 id 规范文本 → 字段序 → 定位路径 → 变化态）稳定
 * 排序，其中"字段序"＝§4.3/§4.3-A/§4.3-B 字段表行序（声明序，实现内以
 * 固定序数承载，用例以期望条目序列机械钉住）；值摘要文本经 classic locale
 * ＋17 位有效数字渲染，不读 locale/环境/时钟（§3.4 总约定 1）。
 *
 * 线程安全：diff 为 const 纯函数、实现无状态——实例可并发只读复用
 * （§3.4 总约定 1）。错误语义：§9.4.9 卡面签名**无错误轨**（直返报告，
 * 非 Expected）——输入合法性（I-MDL-2 身份唯一等）由构造/编辑边界保证
 * （§4.10 处置原则）；对违反前置的输入本函数不做校验也不静默修复，按
 * 首次出现身份匹配确定性处理（产出内容未定义语义的调用属调用方数据
 * 破损，其拦截归 checkInvariants 消费边界，NFR-COR-03 不在本层重复设防）。
 */

#ifndef IRD_MODELING_MODELDIFF_HPP
#define IRD_MODELING_MODELDIFF_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>     // ObjectId（条目对象定位锚）
#include <sdurws/ird/modeling/Template.hpp> // ModelingWorkingSet（两输入载体——§9.4.9）

namespace sdurws::ird::modeling {

// =====================================================================
// 差异增量表数据实体（MDL-08——分组/变化态/条目/报告）
// =====================================================================

/**
 * @brief 差异分组（§9.4.9 行"分组：结构/参数（DH、轴线、限位）/物性"——
 *        三值词表；枚举序＝卡面词序，表尾追加纪律同 AuthorityMode）。
 *
 * 组归属映射（逐字段决策，实现与用例同源钉住）：
 *   - Structure：模型构成——根对象元字段（schemaVersion/displayName/
 *     notes）、权威模式开关、基座安装（preset/customEaa/basePosition）、
 *     关节/连杆集合成员与链序、localName/type、几何引用（visual/collision）、
 *     引用表（defaultTcp/toolRefs/sceneRefs/poseSetRef/drivetrainRef）、
 *     资源清单；
 *   - Parameters：关节运动学参数——axis/origin（仅两侧 Explicit）、
 *     zeroOffset、bounds、workingRange、dhDerived（仅两侧 StandardDH）
 *     （MDL-08 括注"DH、轴线、限位"为本组代表项；origin 是 MDL-09 与
 *     axis 并列的权威一等字段、zeroOffset/workingRange 是 §4.3-A 登记
 *     的关节参数字段，同组承载）；
 *   - Properties：物性——连杆 body.mass/centerOfMass/inertia/material
 *     （MDL-05 分层的物性面；kg/m/kg·m²/材料引用）。
 */
enum class ModelDiffGroup {
    Structure,   ///< 结构：模型构成与拓扑（见类注映射表）
    Parameters,  ///< 参数：DH、轴线、限位等关节运动学参数（见类注映射表）
    Properties,  ///< 物性：质量/质心/惯量/材料（见类注映射表）
};

/**
 * @brief 分组稳定 token（"structure"/"parameters"/"properties"——卡面词表
 *        小写承载；呈现与测试判别用）。
 * @param group [in] 差异分组（switch 全枚举、无 default——新增组漏登记时
 *              编译器告警暴露）
 * @return 静态存储期串。纯函数；线程安全；确定性（NFR-COR-02）。
 */
std::string_view modelDiffGroupToken(ModelDiffGroup group) noexcept;

/**
 * @brief 变化三态（acceptance 2"增/删/改"）。
 *
 * 方向语义（acceptance 3"报告方向字段"的条目面）：所有条目恒以
 * "candidate 相对 baseline"为观察方向——
 *   - Added：对象/引用仅在 candidate 存在（baseline→candidate 视角新增）；
 *   - Removed：对象/引用仅在 baseline 存在（baseline→candidate 视角移除）；
 *   - Modified：两侧均存在但字段面不同。
 * 交换两输入（baseline↔candidate）后 Added 与 Removed 逐一互换、Modified
 * 保持三态不变且两侧值文本互换（镜像性质，DirectionSemantics 用例钉住）。
 */
enum class ModelDiffChangeKind {
    Added,     ///< 增：仅 candidate 存在
    Removed,   ///< 删：仅 baseline 存在
    Modified,  ///< 改：两侧均存在、字段面不同
};

/**
 * @brief 变化态稳定 token（"added"/"removed"/"modified"）。纯函数；确定性。
 * @param kind [in] 变化态（switch 全枚举、无 default——同上）
 * @return 静态存储期串。
 */
std::string_view modelDiffChangeKindToken(ModelDiffChangeKind kind) noexcept;

/**
 * @brief 一条差异增量条目（§9.4.9 行"条目含对象定位（供 UX-13 点击）"）。
 *
 * 数据面构成（全部为值语义，无行为、无 UI 类型——M-5 分工红线）：
 *   - 定位四元组：group（呈现分组）＋kind（三态）＋objectId（点击定位的
 *     身份锚——ARC-04 跨修订稳定）＋subjectPath（值模型内字段定位路径，
 *     如 "joints[2].axis"——数组下标按观察方向在对象存在侧计数：Added/
 *     Modified 取 candidate 下标、Removed 取 baseline 下标；链序位变化
 *     条目（chainIndex）的两侧位次由值摘要文本承载，路径取 candidate
 *     下标）；
 *   - field：叶字段名（如 "axis"/"mass"/"chainIndex"——呈现列键；整对象
 *     增删条目为 "joint"/"link"/"resource"）；
 *   - 变化面标记：valueChanged（值/状态面不同——含四态迁移与链序位变化）
 *     与 provenanceChanged（ValueProvenance 来源标记不同——仅 SourcedValue
 *     字段在 Provided 态参与来源比较，与 core SourcedValue 相等语义同
 *     基准：NotProvided/NotApplicable 无来源载荷、Invalid 态只比原串）。
 *     acceptance 2"权威字段变更与来源标记变更均入表"：两标记独立成列，
 *     仅来源变化（值不变）的条目 valueChanged=false／provenanceChanged=
 *     true，呈现面可据此区分"数值改了"与"只是来源标记变了"；
 *   - 两侧值摘要：baselineText/candidateText 为**确定性文本渲染**（固定
 *     模板＋classic locale＋17 位有效数字；空串＝该侧不存在——Added 的
 *     baseline 侧／Removed 的 candidate 侧）。摘要仅承载呈现辅助与逐字节
 *     确定性证据，精确取值经（objectId, subjectPath）回模型定位——文本
 *     不含单位后缀，单位语义由字段定义承载（§4.3 表：rad/m/kg/kg·m²），
 *     呈现侧按 field 词表自行标注。
 *
 * 相等＝七数据字段全等（运算子逐字段定义，供确定性断言与测试）。
 */
struct ModelDiffEntry {
    ModelDiffGroup group = ModelDiffGroup::Structure;  ///< 差异分组（三值词表）
    ModelDiffChangeKind kind = ModelDiffChangeKind::Modified;  ///< 变化三态

    /// 对象定位锚（UX-13 点击定位消费——ARC-04）。根对象字段条目为全零
    /// ObjectId（isValid()==false；字节序下最小，稳定排于对象条目之前）；
    /// 引用表条目为**被引用对象**的 id（点击定位到工具/场景/位姿集/传动
    /// 对象本身，而非根对象）。
    core::ObjectId objectId;

    std::string subjectPath;  ///< 字段定位路径（值模型内路径，UTF-8——见类注下标口径）
    std::string field;        ///< 叶字段名（呈现列键——词表见类注）

    bool valueChanged = false;       ///< 值/状态面不同（含四态迁移与链序位变化）
    bool provenanceChanged = false;  ///< 来源标记不同（仅 SourcedValue 字段 Provided 态）

    std::string baselineText;  ///< 基线侧确定性值摘要（空串＝该侧不存在）
    std::string candidateText; ///< 候选侧确定性值摘要（空串＝该侧不存在）

    bool operator==(const ModelDiffEntry& o) const
    {
        return group == o.group && kind == o.kind && objectId == o.objectId
            && subjectPath == o.subjectPath && field == o.field
            && valueChanged == o.valueChanged && provenanceChanged == o.provenanceChanged
            && baselineText == o.baselineText && candidateText == o.candidateText;
    }
    bool operator!=(const ModelDiffEntry& o) const { return !(*this == o); }
};

/**
 * @brief 差异增量表报告（§9.4.9 ModelDiffReport 落位——MDL-08 数据实体）。
 *
 * 方向字段（acceptance 3"报告方向字段"）：baselineObjectId/candidateObjectId
 * 承载两侧工作集根对象身份（自 ModelingWorkingSet.rootObjectId 原样带入；
 * nullopt＝该侧为尚无 project 身份的模板草稿）——报告的观察方向由结构
 * 本身声明，交换输入后两 id 互换、条目呈镜像（见 ModelDiffChangeKind 注）。
 *
 * 排序契约（acceptance 2"NFR-COR-02 稳定排序——分组内按对象 id→字段序"）：
 * 三组条目各自按（objectId 规范文本字典序 → 字段序〔§4.3 表行序——见
 * ModelDiffEntry 注〕→ subjectPath 字典序 → kind 枚举序）升序稳定排列；
 * 同输入重复 diff 输出逐字段相等（byte-stable 由确定性文本渲染保证）。
 *
 * 空差集语义（acceptance 3）：两工作集逐字段全等 → 三组均为空表（报告
 * 仍是合法值对象——"空报告非 null"：无条目、无错误、无需特殊判别态）。
 *
 * 线程安全：纯值类型。
 */
struct ModelDiffReport {
    /// 基线侧工作集根对象身份（nullopt＝模板草稿尚无 project 身份）。
    std::optional<core::ObjectId> baselineObjectId;
    /// 候选侧工作集根对象身份（nullopt＝模板草稿尚无 project 身份）。
    std::optional<core::ObjectId> candidateObjectId;

    std::vector<ModelDiffEntry> structure;   ///< 结构组增量（排序契约见类注）
    std::vector<ModelDiffEntry> parameters;  ///< 参数组增量（排序契约见类注）
    std::vector<ModelDiffEntry> properties;  ///< 物性组增量（排序契约见类注）

    bool operator==(const ModelDiffReport& o) const
    {
        return baselineObjectId == o.baselineObjectId
            && candidateObjectId == o.candidateObjectId
            && structure == o.structure && parameters == o.parameters
            && properties == o.properties;
    }
    bool operator!=(const ModelDiffReport& o) const { return !(*this == o); }
};

// =====================================================================
// IModelDiffService——接口与唯一实现（§9.4.9）
// =====================================================================

/**
 * @brief Model Diff 服务（§9.4.9 原文契约）：两工作集输入、稳定排序增量表、
 *        条目含对象定位；纯函数（不覆盖基线——MDL-08"不直接覆盖原始基线"
 *        的函数面形态：diff 只读两输入、无副作用、无输出参数）。
 *
 * @post 不修改两输入工作集（const 引用＋纯函数——acceptance 3"输入工作集
 *       只读（post 不修改输入）"；用例经 canonical 编码字节前后对照留证）。
 * @错误 无错误轨（§9.4.9 卡面签名直返 ModelDiffReport——见文件头"错误
 *       语义"；不抛越过单元边界的异常）。
 *
 * 线程安全：并发只读安全（§3.4 总约定 1）。
 */
class IModelDiffService {
public:
    virtual ~IModelDiffService() = default;

    /**
     * @brief 生成基线→候选方向的差异增量表（§9.4.9 原文签名）。
     *
     * @param baseline  [in] 基线工作集（只读——@post 不修改；其 design 为
     *                  比较面，rootObjectId 进报告方向字段）
     * @param candidate [in] 候选工作集（只读——同上）
     * @return 差异增量表（三组稳定排序；同输入同输出——NFR-COR-02；
     *         空差集→三组空表的合法报告）
     *
     * 纯函数；线程安全；确定性（NFR-COR-02）。
     */
    virtual ModelDiffReport diff(const ModelingWorkingSet& baseline,
                                 const ModelingWorkingSet& candidate) const = 0;
};

/**
 * @brief IModelDiffService 唯一产品实现（无状态纯函数服务——可默认构造，
 *        拷贝/移动平凡；无注入依赖，比较语义全部内聚于本单元值模型之上）。
 */
class ModelDiffService final : public IModelDiffService {
public:
    ModelDiffService() = default;

    /// @copydoc IModelDiffService::diff
    ModelDiffReport diff(const ModelingWorkingSet& baseline,
                         const ModelingWorkingSet& candidate) const override;
};

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_MODELDIFF_HPP
