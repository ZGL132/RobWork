/**
 * @file   Combination.hpp
 * @brief  组合校核（selection 单元）——候选组合构造（§14.5）、组合身份
 *         （§9.5）、③端口消费的映射事实承载（§9.2 两段管线，P-SEL-1 提议
 *         契约 v1）、惯量比可配置工程规则（§11.3）、多工况候选资格矩阵
 *         （§9.4）、组合校核核心（§9.3 全覆盖清单）与组合校核评估器
 *         sel-combination-check（③端口，§3.1 CombinationCheck 组件）。
 *
 * 设计依据：
 *   - units/selection.md §9（drivetrain/dynamics 工作点和组合校核——数据
 *     流/两段管线/校核清单/资格矩阵/组合身份/payload 契约）、§10.1～10.4
 *     （记录类型复用/空集语义/词表/稳定排序——复用 Screening.hpp 的
 *     T04 承载，同一类型不分叉）、§11.3（O-11/P-POL-3 惯量比阈值的边界
 *     处理——未裁决期间"未判定"显式标记且不整体阻断）、§14.5（
 *     IDeviceCombinationBuilder 接口签名）、§14.0（通用约定——调用方
 *     错误 fail-fast vs 数据/环境类返回诊断）、§3.2（依赖红线——
 *     drivetrain 经③端口消费，零编译链接边）
 *   - 需求 SEL-05（经共享 DriveTrainMappingEvaluator 校核电机—减速器—
 *     负载惯量、组合兼容和每轴工作点；惯量比阈值为可配置工程规则，不作
 *     为普适固定硬约束）、DYN-04（消费——电机侧工作点取唯一映射口径产
 *     出）、SEL-06（组合级记录素材——可行集组装归 WP-19-T06）、ERR-01
 *     （比较型字段齐备）、EVI-02（必验工况全覆盖——不得漏验）、AT-38
 *     （三方映射身份一致——本头只核对身份，不做第二套矩阵验证）
 *   - 任务契约 tasks/foundation/WP-19-T05.json（acceptance 1～3：经共享
 *     映射校核用例；不自建映射实现；惯量比未裁决期间输出"未判定"维度
 *     不整体阻断）
 *
 * ★ SEL-05 红线的本头执行形态（不自建映射实现——R-5 精神）：
 *   1. 本头【零】传动映射公式：不含传动比映射、虚功对偶映射、电机侧力矩/速度/功率
 *      计算、反射惯量折算与交叉耦合的任何表达式（记法词表由契约
 *      测试扫描——注释也不书写映射公式记法，防扫描误报）。电机侧工作点/惯量比/反射惯量一律作为
 *      【数值事实】经 MappingBatchFacts 值传递消费——其唯一产生者是
 *      drivetrain 的 DriveTrainMappingEvaluator（③端口 dt.mapping，
 *      L5 装配注册；ARCH §7.10"统一走 drivetrain，dynamics 与 selection
 *      不各自实现映射"）。红线由契约测试机器可断言（映射公式词表零命中
 *      扫描——contract_test/CombinationCheckContractTest.cpp）。
 *   2. selection 仅有的三类合法动作（卡 §9.1"selection 只能"行）在本头
 *      的落位：①候选传动参数构造（CombinationDriveInput——逐轴 c_j 由
 *      候选减速器速比 n:1 按 drivetrain 卡 §5.3 c＝Δq_joint/Δθ_motor 口径
 *      换算 c＝1/n，η⁺/η⁻/J_rotor/负载惯量取目录/回填值——§9.2 候选组合
 *      集载荷）；②经③端口消费映射（评估器切片 UpstreamResult 条目
 *      dt.mapping＋MappingBatchFacts 提取面）；③按目录能力筛选＋记录候选
 *      与工作点关系（复用 T04 HardConstraintSelector——同一实现不分叉）。
 *   3. 零 drivetrain 编译边：本头不 include 任何 sdurws/ird/drivetrain/
 *      头（卡 §3.2"运行时注入/端口"列——P-SEL-2 推荐端口形态）。映射
 *      批结果进入本域的形态＝MappingBatchFacts（P-SEL-1 提议契约 v1 的
 *      selection 承载，字段面对齐 drivetrain 卡 §11 MotorOperatingPoint
 *      的消费口径：τ/ω/P 峰值/RMS、η、反射惯量、惯量比、完整性）；其
 *      由组装方（L5 装配/测试）从③端口上游评估产出提取填充——drivetrain
 *      卡正式冻结跨域契约后按注册清单收编（R-SEL-1 同源风险登记）。
 *
 * 错误语义（卡 §14.0 两分法）：
 *   - 调用方契约违约（非有限数值/非法阈值/空轴表/切片派发违约/条目载荷
 *     形态不符）＝fail-fast 抛 std::invalid_argument——不返回半结果；
 *   - 数据/环境类（映射批缺失/部分、工作点事实缺失、工况覆盖缺口、对象
 *     字节不可得）＝返回 DataGap/DataInsufficient 素材＋诊断，不抛、不
 *     伪造数值、不默认通过（卡 §7.2 同源纪律）。
 *
 * 确定性（NFR-COR-02）：组合键为规范序列化 SHA-256（同输入同键）；核心
 *   纯函数无时钟/随机源；输出序＝输入组合序（完整稳定排序归 WP-19-T06
 *   FeasibleSet——卡 §10.4 排序键级联在可行集组装段执行）。
 *
 * 线程安全：DeviceCombinationBuilder 与组合校核核心为无状态纯函数对象
 *   （可重入——卡 §14.10"筛选/曲线/组合构造（纯函数）可重入"）；评估器
 *   实例单线程（descriptor.threadSafety 声明——卡 §14.10"每 worker 每任务
 *   一实例；stateless=true"）。全部输入由调用方持有，输出按值返回
 *   （卡 §14.0 所有权约定）。
 */

#ifndef IRD_SELECTION_COMBINATION_HPP
#define IRD_SELECTION_COMBINATION_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Screening.hpp>

namespace sdurws::ird::selection {

// =====================================================================
// ③端口登记常量（唯一书写点——descriptor/codec/测试共用；drivetrain
// Codec.hpp 同款纪律。评估键词形偏差登记见 makeCombinationCheckDescriptor）
// =====================================================================

/// 组合校核评估键（kebab 形态——evidence isValidEvaluationKey 词形闸门
/// 不含点；卡 §9.2 记法 "sel.combination-check" 的落位偏差与 drivetrain
/// kMappingEvaluationKey 同款先例，随单元卡 §19.3 T05 落位细化登记）。
inline constexpr std::string_view kCombinationCheckEvaluationKey = "sel-combination-check";

/// 组合校核输入/输出契约版本（>0；进 sliceId——CON-04；随契约面演进递增）。
inline constexpr std::uint32_t kCombinationCheckContractVersion = 1;

/// 依赖键：目录锁定版本对象（Object——卡 §9.2 管线②切片条目）。
inline constexpr std::string_view kCatalogLockKey = "catalog.lock";
/// 依赖键：关节侧事实 UpstreamResult（卡 §9.2 管线②切片条目原文；
/// 本域消费面＝AxisFactsBundle——见其类型注的 P-SEL-1 提议契约说明）。
inline constexpr std::string_view kJointSeriesSelKey = "dyn.joint-series";
/// 依赖键：dt.mapping 组合批结果 UpstreamResult（③端口唯一映射口径的
/// 产出引用——SEL-05 红线的切片声明面）。
inline constexpr std::string_view kMappingBatchKey = "dt.mapping";
/// 依赖键：筛选条件 Configuration（卡 §9.2 管线②切片条目——进入切片
/// 身份；T04 ScreeningCriteria 头注"条目化编码归组合校核段"的兑现）。
inline constexpr std::string_view kSelScreeningConfigKey = "config.sel-screening";
/// 依赖键：候选组合集 Configuration（卡 §9.2 管线①切片条目——本卡
/// 细化的组合集载荷，P-SEL-1 跨卡对齐注记；管线①批映射的提交面）。
inline constexpr std::string_view kDtComboSetConfigKey = "config.dt-combo-set";

/// 输出 payload 的域 token（DomainPayload.kindToken——词表归域，非空＋无 NUL）。
inline constexpr std::string_view kCombinationCheckPayloadToken = "sel-combination-check-output-v1";

// =====================================================================
// 组合身份与候选组合构造（卡 §9.5/§14.5）
// =====================================================================

/**
 * @brief 组合身份（卡 §9.5 DeviceCombinationId——规范序列化摘要）。
 *
 * 构造规则单点在 makeDeviceCombinationId：轴序×(motorModelId, gearboxId,
 * catalogVersion) 的规范文本 SHA-256（小写 hex 64 字符）。字符串承载但
 * 语义是内容寻址身份（CON-05）——候选/目录任一变更必得新键（确定性
 * 可复现；同组合集内重复组合以键判等去重——卡 §9.5"重复组合去重"）。
 */
using DeviceCombinationId = std::string;

/**
 * @brief 单轴器件指派（卡 §10.1 AxisDeviceAssignment 基线）。
 *
 * 落位承载与 T04 细化 ① 同口径：候选电机/减速器以 ModelId（目录域身份）
 * 承载（型号无项目对象 ID），轴以 core::ObjectId（modeling 项目对象）
 * 承载。轴序＝组合轴表排列序（关节串联序——与上游模型一致，组装方保证；
 * 本头不做轴序重排——重排等价于改输入）。
 */
struct AxisDeviceAssignment {
    core::ObjectId jointId;  ///< 轴对象 ID（稳定身份——ARC-04，不经名称匹配）
    ModelId motorModelId;    ///< 指派电机型号稳定 ID（包内唯一）
    ModelId gearboxModelId;  ///< 指派减速器型号稳定 ID（包内唯一）

    bool operator==(const AxisDeviceAssignment& o) const
    {
        return jointId == o.jointId && motorModelId == o.motorModelId
            && gearboxModelId == o.gearboxModelId;
    }
    bool operator!=(const AxisDeviceAssignment& o) const { return !(*this == o); }
};

/**
 * @brief 器件组合（卡 §10.1 DeviceCombination 基线＋组合级目录版本）。
 *
 * catalog 为组合级目录身份（§9.3 行 10"组合校核所用目录版本＝映射批
 * 所用候选参数来源版本"的核对载体——组合校核将其与快照身份逐组合
 * 比对，不一致即 SEL-IDENTITY-MISMATCH 语义的 CatalogVersionIncompatible）。
 */
struct DeviceCombination {
    DeviceCombinationId id;                  ///< 组合键（makeDeviceCombinationId 产出）
    CatalogIdentity catalog;                 ///< 组合所用目录版本（候选参数来源）
    std::vector<AxisDeviceAssignment> axes;  ///< 逐轴指派（关节串联序；≥1）

    bool operator==(const DeviceCombination& o) const
    {
        return id == o.id && catalog == o.catalog && axes == o.axes;
    }
    bool operator!=(const DeviceCombination& o) const { return !(*this == o); }
};

/**
 * @brief 单轴候选清单（卡 §14.5 AxisCandidateList——组合构造的 perAxis 入参）。
 *
 * 候选 ID 必须存在于目标目录快照（组合校核对快照内缺失的候选按数据
 * 缺口处理——不静默跳过；构造器对空候选集的轴仍参与笛卡尔积〔该轴仅
 * 一个"空指派"组合面〕，轴映射完整性维度会给出漏轴原因）。
 */
struct AxisCandidateList {
    core::ObjectId jointId;       ///< 轴对象 ID
    std::vector<ModelId> motorIds;   ///< 候选电机型号（去重由构造器执行）
    std::vector<ModelId> gearboxIds; ///< 候选减速器型号（同上）

    bool operator==(const AxisCandidateList& o) const
    {
        return jointId == o.jointId && motorIds == o.motorIds
            && gearboxIds == o.gearboxIds;
    }
    bool operator!=(const AxisCandidateList& o) const { return !(*this == o); }
};

/// 兼容关系表（卡 §14.5 compat 参数——快照 compatibility 表的值传递视图；
/// 零行语义＝包内无预声明兼容对，组合兼容维度按"无记录即不兼容"判定，
/// 卡 §5.2/§9.3 行 1）。
using CompatibilityTable = std::vector<CompatibilityRecord>;

/**
 * @brief 批预算（卡 §14.5 budget/§9.5"批预算随 config.dt-combo-set 声明"
 * ——NFR-PERF-03 分批/流式的 selection 侧承载）。
 *
 * maxCombinationsPerBatch 为每批组合数上限（≥1；0＝调用方契约违约
 * fail-fast）。批切分只影响批边界，不改变组合内容与顺序（组合集整体
 * 序＝构造确定性序，批内保留全局序切片——§10.4"分批并行只影响完成
 * 顺序，汇总前按排序键归并"的构造侧前提）。
 */
struct BatchBudget {
    std::size_t maxCombinationsPerBatch = 0; ///< 每批组合数上限（≥1）

    bool operator==(const BatchBudget& o) const
    {
        return maxCombinationsPerBatch == o.maxCombinationsPerBatch;
    }
};

/**
 * @brief 组合批（卡 §9.5 分批的承载——批内组合＋批序号）。
 */
struct CombinationBatch {
    std::size_t batchIndex = 0;              ///< 批序号（0 起——批间顺序稳定）
    std::vector<DeviceCombination> combinations; ///< 本批组合（全局序切片）

    bool operator==(const CombinationBatch& o) const
    {
        return batchIndex == o.batchIndex && combinations == o.combinations;
    }
};

/**
 * @brief 组合集（卡 §14.5 build 返回——兼容过滤＋去重＋批预算切分的产物）。
 *
 * combinations 为去重后全量组合（构造确定性序：轴清单序×电机候选序×
 * 减速器候选序的字典序展开）；batches 为按预算的批切分视图（合并不重
 * 不漏覆盖 combinations）；duplicateDroppedCount 为被去重丢弃的重复组合
 * 数（观测面——同组合键只算一次，卡 §9.5）。
 */
struct CombinationSet {
    std::vector<DeviceCombination> combinations;   ///< 全量组合（去重后）
    std::vector<CombinationBatch> batches;         ///< 批切分视图
    std::size_t duplicateDroppedCount = 0;         ///< 去重丢弃计数（观测面）

    bool operator==(const CombinationSet& o) const
    {
        return combinations == o.combinations && batches == o.batches
            && duplicateDroppedCount == o.duplicateDroppedCount;
    }
};

/**
 * @brief 构造组合键（卡 §9.5——规范序列化摘要，规则单点）。
 *
 * 规范文本（确定性序）：magic "IRDSCID1"＋目录 (catalogId|version|内容
 * 身份 hex)＋逐轴 (jointId 规范文本|motor|gearbox)（轴表排列序）。SHA-256
 * 摘要的小写 hex（64 字符）即组合键。
 *
 * @param catalog [in] 组合所用目录身份（进键——候选参数来源版本变更必
 *                得新键，§9.5 键定义含 catalogVersion）
 * @param axes    [in] 逐轴指派（排列序进键；空表合法——键仍可计算，
 *                但组合校核的轴映射维度会给出漏轴原因）
 * @return 组合键（64 字符小写 hex；确定性——同输入恒同键，NFR-COR-02）
 *
 * @note 纯函数；可重入。
 */
DeviceCombinationId makeDeviceCombinationId(const CatalogIdentity& catalog,
                                            const std::vector<AxisDeviceAssignment>& axes);

// =====================================================================
// 候选传动参数构造（卡 §9.2 候选组合集载荷——SEL-05 允许的三类动作之一）
// =====================================================================

/**
 * @brief 单轴归一化传动输入（卡 §9.2 候选组合集载荷的逐轴条目——管线①
 *        批映射对组合 k 求值时消费的归一化传动参数）。
 *
 * ★ 候选传动参数构造（不是映射计算）：本结构字段全部来自目录/回填的
 *   【直接取值或单点口径换算】，不含任何工作点映射公式：
 *   - ratioC：归一化传动比 c＝Δq_joint/Δθ_motor（无量纲 rad/rad；带符号
 *     语义保留——R1 目录速比恒正，c＞0 同向）。换算口径单点：候选减速器
 *     速比 n:1 → c＝1/n（drivetrain 卡 §5.3 c 口径；禁第二种未声明约定）。
 *   - etaForward/etaBackward：候选减速器 η⁺/η⁻（无量纲，∈(0,1]——目录
 *     v1 只有单一 efficiency 列，η⁺＝η⁻＝目录值；分方向扩展随目录 schema
 *     演进，卡 §4.1）。
 *   - rotorInertia：候选电机转子惯量（kg·m²，电机轴系；目录 rotor_inertia
 *     列/回填独立字段——未计入 DWC 合成的转子值，防重复计入，SEL-10/
 *     MDL-16）。
 *   - loadInertiaJointSide：壳体合成参考负载惯量（kg·m²，关节轴系——
 *     组装方按 §12.4 合成规则产出的参考值；缺失以 nullopt 显式标记，
 *     不默认零——§11.2"不将缺少证据默认当作零负载"）。
 *
 * 全部 present 值必须有限（非法值＝调用方契约违约，编码/构造入口
 * fail-fast——NFR-COR-03）。
 */
struct AxisDriveInput {
    core::ObjectId jointId;               ///< 轴对象 ID（与组合轴表对位）
    double ratioC = 0.0;                  ///< 归一化传动比 c＝1/n（无量纲；≠0 且有限）
    double etaForward = 0.0;              ///< 驱动方向效率 η⁺（无量纲；∈(0,1]）
    double etaBackward = 0.0;             ///< 再生方向效率 η⁻（无量纲；∈(0,1]）
    double rotorInertia = 0.0;            ///< 电机转子惯量（kg·m²，电机轴系；>0）
    std::optional<double> loadInertiaJointSide; ///< 壳体合成参考负载惯量（kg·m²，
                                                ///   关节轴系；nullopt＝缺失显式标记）

    bool operator==(const AxisDriveInput& o) const
    {
        return jointId == o.jointId && ratioC == o.ratioC
            && etaForward == o.etaForward && etaBackward == o.etaBackward
            && rotorInertia == o.rotorInertia
            && loadInertiaJointSide == o.loadInertiaJointSide;
    }
    bool operator!=(const AxisDriveInput& o) const { return !(*this == o); }
};

/**
 * @brief 单组合归一化传动输入（卡 §9.2 组合集载荷的逐组合条目——管线①
 *        批映射"对同一关节序列按 K 个归一化模型分别求值"的第 k 个模型面）。
 */
struct CombinationDriveInput {
    DeviceCombinationId combinationId;  ///< 所属组合键（与组合集对位）
    std::vector<AxisDriveInput> axes;   ///< 逐轴传动输入（组合轴表序）

    bool operator==(const CombinationDriveInput& o) const
    {
        return combinationId == o.combinationId && axes == o.axes;
    }
};

/**
 * @brief 由目录快照与组合集构造候选组合集载荷（config.dt-combo-set 的
 *        域内承载——§14.5"组合集载荷编码"的前半：值面构造）。
 *
 * 每组合逐轴从快照取候选减速器 ratio（→c＝1/n 单点换算）、efficiency
 * （→η⁺＝η⁻）与候选电机 rotor_inertia；负载惯量参考值由调用方按轴供给
 * （本函数无合成职责——§12.4 合成归回填段，WP-19-T09）。
 *
 * @param snapshot       [in] 目录快照（候选参数来源——组合 catalog 必须
 *                       与快照身份一致，不一致的组合按调用方契约违约
 *                       fail-fast：参数来源版本错配即构造语义破坏）
 * @param combinations   [in] 组合集（build 产物；组合键与轴表逐一对位）
 * @param loadInertiaByJoint [in] 逐轴负载惯量参考值（按 jointId 键控；
 *                       缺键＝该轴 nullopt——显式缺失标记，不默认零）
 * @return 逐组合传动输入（序＝组合集序；确定性）
 *
 * @throws std::invalid_argument 组合 catalog 与快照身份不一致／组合轴在
 *         快照内缺失候选型号／速比/效率/转子惯量值非法（≤0 或非有限）
 *
 * @note 纯函数；确定性。字节编码面＝encodeDtComboSetPayload（本头 §codec）。
 */
std::vector<CombinationDriveInput> makeCombinationDriveInputs(
    const CatalogPackageSnapshot& snapshot,
    const std::vector<DeviceCombination>& combinations,
    const std::vector<AxisDriveInput>& loadInertiaByJoint);

// =====================================================================
// 组合构造器（卡 §14.5 IDeviceCombinationBuilder——接口与唯一产品实现）
// =====================================================================

/**
 * @brief 候选组合构造接口（卡 §14.5 设计基线——签名逐注承载）。
 *
 * 职责（卡 §14.5 @brief 注）：兼容性过滤（compatibility 表零行语义）、
 * 去重（组合键）、批预算切分、组合集载荷编码（载荷值面＝
 * makeCombinationDriveInputs；字节面＝encodeDtComboSetPayload——分离
 * 便于组装方对值面复用）。
 */
class IDeviceCombinationBuilder {
public:
    virtual ~IDeviceCombinationBuilder() = default;

    /**
     * @brief 由逐轴候选构造 DeviceCombination 集合（卡 §14.5）。
     *
     * 构造序（确定性，NFR-COR-02）：perAxis 清单序×电机候选首现序×减速器
     * 候选首现序的字典序展开；兼容过滤在展开时执行（无兼容记录的对不
     * 生成组合——组合兼容维度的"无记录即不兼容"在构造面已过滤，校核段
     * 的兼容维度对【构造产物】恒通过〔双保险：构造过滤＋校核核对，卡
     * §9.3 行 1〕）。
     *
     * @param perAxis [in] 逐轴候选清单（≥1 轴——空表＝调用方契约违约）
     * @param compat  [in] 兼容关系表（快照 compatibility 表值传递）
     * @param budget  [in] 批预算（maxCombinationsPerBatch ≥1）
     * @param catalog [in] 组合级目录身份（写入每组合——候选参数来源版本）
     * @return 组合集（去重后全量＋批切分视图＋去重计数）
     *
     * @throws std::invalid_argument perAxis 为空／budget.maxCombinationsPerBatch
     *         为 0／同一 perAxis 清单内 jointId 重复（轴歧义——组合轴表
     *         必须逐轴恰一指派的前提破坏，卡 §9.3 行 2 的构造侧防线）
     *
     * @note 纯函数；确定性；可重入。R2 直线传动组合扩展（卡 §14.5 注）
     *       随 SEL-09-S1（不在阶段 C 实现——§17.2）。
     */
    virtual CombinationSet build(const std::vector<AxisCandidateList>& perAxis,
                                 const CompatibilityTable& compat,
                                 const BatchBudget& budget,
                                 const CatalogIdentity& catalog) const = 0;
};

/**
 * @brief 组合构造器唯一产品实现（无状态纯函数对象——可重入，卡 §14.10）。
 */
class DeviceCombinationBuilder final : public IDeviceCombinationBuilder {
public:
    CombinationSet build(const std::vector<AxisCandidateList>& perAxis,
                         const CompatibilityTable& compat,
                         const BatchBudget& budget,
                         const CatalogIdentity& catalog) const override;
};

// =====================================================================
// ③端口消费的映射事实承载（P-SEL-1 提议契约 v1——selection 域内值类型）
// =====================================================================

/**
 * @brief 完整性两态（selection 域内承载）。
 *
 * 对齐 drivetrain 卡 §11.1 quality 字段口径（Complete/Partial——Partial
 * 附缺失清单）；不引入第三态（Estimated 以 missingItems＋标记位承载，
 * 卡 §10.7 估算限定语经 P-SEL-1 收编后对齐）。域内独立定义的原因：零
 * drivetrain 编译边（卡 §3.2）——不 include 他单元值类型头（P-SEL-2
 * 推荐端口形态），收编时按注册清单同步（R-SEL-1）。
 */
enum class CompletenessKind : std::uint8_t {
    Complete, ///< 完整（无缺失输入）
    Partial,  ///< 部分（缺失清单非空——不伪造完整，§10.2 空集语义表）
};

/**
 * @brief 组合批内的单组合指派表（dt.mapping 组合批结果携带的组合身份面
 *        ——管线②自足性：组合校核所需的轴×候选指派随批结果值传递，
 *        P-SEL-1 提议契约 v1；字段与 DeviceCombination 同构）。
 */
struct MappingCombinationFact {
    DeviceCombinationId combinationId;      ///< 组合键（§9.5）
    CatalogIdentity catalog;                ///< 组合所用目录身份（一致性核对载体）
    std::vector<AxisDeviceAssignment> axes; ///< 逐轴指派（关节串联序）

    bool operator==(const MappingCombinationFact& o) const
    {
        return combinationId == o.combinationId && catalog == o.catalog
            && axes == o.axes;
    }
};

/**
 * @brief 逐组合逐轴的映射事实（dt.mapping 组合批结果的提取面——P-SEL-1
 *        提议契约 v1；字段面对齐 drivetrain 卡 §11 MotorOperatingPoint
 *        的 selection 消费口径：τ/ω/P 峰值/RMS＋惯量比＋反射惯量＋效率
 *        降级标记＋峰值定位）。
 *
 * ★ 组合口径纪律：电机侧工作点依赖组合（不同 c ⇒ 不同电机侧值）——
 *   本结构按 (组合,轴) 供给电机侧事实；同一关节序列在 K 个组合下各自
 *   求值（§9.2"映射核心按 K 个归一化模型分别求值"）。关节侧事实（与
 *   组合无关的 DYN-03 口径）由 AxisFactsBundle 按 (轴,工况) 供给。
 *
 * ★ 全部数值为【映射产出的数值事实】（P-DT-2：drivetrain 只输出数值，
 *   阈值判定归 selection）——本头消费数值、不自算（红线见文件头）：
 *   - motor* 六量：电机侧峰值/RMS（N·m/rad/s/W——§11.1 消费口径）；
 *     nullopt＝该量缺失（对应电机筛选维度数据不足，不伪造零值）。
 *   - peakDuration：峰值段持续时长（s——过载持续时间维度，卡 §7.1）。
 *   - peakAtTime/peakSegmentId：峰值工作点定位（s/段 ID——淘汰原因
 *     定位面，§10.3）。
 *   - inertiaRatio：惯量比数值（无量纲；nullopt＝负载折算惯量缺失→
 *     不适用——ERR-01 显式标记，映射侧 §10.7 降级语义的提取面）。
 *   - reflectedInertia：反射惯量 J_rotor/c²（kg·m²，关节轴系）——参考
 *     呈现面（§9.3 行 4"组合校核以映射事实为准"；组合校核不重复计入、
 *     不重算——转子惯量的唯一显式计入位置在映射侧，D-DT-7）。
 *   - efficiencyApplied：效率是否可用（false＝该轴功率/能量降级——
 *     映射侧 η 缺失降级的提取面；组合校核据此记数据缺口，不以 η＝1
 *     静默替代，卡 §10.1）。
 */
struct MappingAxisFact {
    DeviceCombinationId combinationId; ///< 所属组合键（组合批逐组合对位）
    core::ObjectId jointId;            ///< 轴对象 ID（与组合轴表对位）
    CaseId caseId;                     ///< 工况 ID（组合×轴×工况三键对位）
    // ---- 电机侧工作点六量（映射事实——dt.mapping 唯一口径产出）----
    std::optional<double> motorTorqueRms;   ///< 电机侧 RMS 转矩（N·m）
    std::optional<double> motorTorquePeak;  ///< 电机侧峰值转矩（N·m）
    std::optional<double> motorSpeedPeak;   ///< 电机侧峰值角速度（rad/s）
    std::optional<double> motorSpeedRms;    ///< 电机侧 RMS 角速度（rad/s）
    std::optional<double> motorPowerPeak;   ///< 电机侧峰值功率（W）
    std::optional<double> motorPowerRms;    ///< 电机侧 RMS 功率（W）
    std::optional<double> peakDuration;     ///< 峰值段持续时长（s）
    double peakAtTime = 0.0;                ///< 峰值工作点时间（s——定位面）
    std::string peakSegmentId;              ///< 峰值所在轨迹段 ID（定位面）
    // ---- 惯量与效率事实 ----
    std::optional<double> inertiaRatio;     ///< 惯量比数值（无量纲；nullopt＝不适用）
    std::optional<double> reflectedInertia; ///< 反射惯量（kg·m²，关节轴系；参考面）
    bool efficiencyApplied = false;         ///< 效率是否可用（false＝降级）

    bool operator==(const MappingAxisFact& o) const
    {
        return combinationId == o.combinationId && jointId == o.jointId
            && caseId == o.caseId && motorTorqueRms == o.motorTorqueRms
            && motorTorquePeak == o.motorTorquePeak
            && motorSpeedPeak == o.motorSpeedPeak
            && motorSpeedRms == o.motorSpeedRms
            && motorPowerPeak == o.motorPowerPeak
            && motorPowerRms == o.motorPowerRms
            && peakDuration == o.peakDuration && peakAtTime == o.peakAtTime
            && peakSegmentId == o.peakSegmentId
            && inertiaRatio == o.inertiaRatio
            && reflectedInertia == o.reflectedInertia
            && efficiencyApplied == o.efficiencyApplied;
    }
    bool operator!=(const MappingAxisFact& o) const { return !(*this == o); }
};

/**
 * @brief dt.mapping 组合批结果事实包（P-SEL-1 提议契约 v1 的 selection
 *        消费承载——组装方从③端口上游评估产出提取填充）。
 *
 * 身份块（§9.3 行 11"drivetrain 映射版本一致性"的核对载体）：
 *   - mappingContractVersion/mappingAlgorithmVersion：映射批的契约/算法
 *     版本（评估器路径与切片声明核对；两者须 >0——0＝未登记非法）；
 *   - upstreamSliceId：映射批上游切片身份（与切片条目
 *     UpstreamResultRef.upstreamSliceId 逐字节核对——AT-38 三方同口径）。
 *     ★ 负值语义：expectedMappingSliceId 由评估器路径从切片条目取出；
 *     直调路径（模型测试/组装方直用核心）须自行保证一致——核心对
 *     upstreamSliceId 为全零的映射批按"无身份"数据缺口处理（不伪造）。
 *
 * 失败与完整性（§10.2 空集语义表"drivetrain 映射失败"行）：mappingFailed
 * =true 表示映射批整体失败（上游失败——诊断透传、不伪装成候选淘汰、
 * 不自动判整机不可行）；completeness==Partial 表示部分降级
 * （missingItems 全量列出——逐条转数据缺口）。
 */
struct MappingBatchFacts {
    std::uint32_t mappingContractVersion = 0; ///< 映射契约版本（>0 合法）
    std::uint32_t mappingAlgorithmVersion = 0; ///< 映射算法版本（>0 合法）
    core::ContentIdentity upstreamSliceId;    ///< 映射批上游切片身份（全零＝无身份）
    CompletenessKind completeness = CompletenessKind::Complete; ///< 批完整性
    bool mappingFailed = false;               ///< 映射批整体失败（上游失败透传面）
    std::vector<std::string> missingItems;    ///< 缺失清单（Partial 必非空）
    std::vector<std::string> diagnosticCodes; ///< 映射批诊断码引用面（DT-*，产出序透传）
    std::vector<MappingCombinationFact> combinations; ///< 组合批的组合指派表
                                                      ///   （校核遍历序＝本表序）
    std::vector<MappingAxisFact> axes;        ///< 逐组合逐轴事实（组合集序）

    bool operator==(const MappingBatchFacts& o) const
    {
        return mappingContractVersion == o.mappingContractVersion
            && mappingAlgorithmVersion == o.mappingAlgorithmVersion
            && upstreamSliceId == o.upstreamSliceId
            && completeness == o.completeness && mappingFailed == o.mappingFailed
            && missingItems == o.missingItems
            && diagnosticCodes == o.diagnosticCodes
            && combinations == o.combinations && axes == o.axes;
    }
};

/**
 * @brief 目录锁定引用载荷（catalog.lock Object 条目的对象字节形态）。
 *
 * 条目载荷（ObjectDependencyPayload）携带 lockObjectId＋contentVersion；
 * 本结构是其对象字节的域内形态（identity 进字节以支持"摘要不符拒绝"
 * ——卡 §14.1 load 契约的同语义核对）。评估器读取字节后经注入的
 * ICatalogProvider 解析快照（provider 装配契约见评估器类注）。
 */
struct CatalogLockPayload {
    CatalogIdentity identity;      ///< 锁定版本的目录身份（核对载体）
    core::ObjectId lockObjectId;   ///< 锁定对象 ID（与条目载荷一致——第二道核对）

    bool operator==(const CatalogLockPayload& o) const
    {
        return identity == o.identity && lockObjectId == o.lockObjectId;
    }
};

/**
 * @brief 轴工作点事实包（dyn.joint-series UpstreamResult 条目在本域的
 *        消费字节形态——P-SEL-1 提议契约 v1）。
 *
 * ★ 消费面边界（组合口径纪律）：组合校核消费本包条目的【关节侧三量＋
 *   需求侧字段（保持/外载荷/安装）＋关节类型 jointKind（WP-19-T08
 *   ——SEL-09 范围外阻断：Prismatic 轴不进入旋转传动判定）】——这些量
 *   与组合无关（DYN-03 关节侧口径）；电机侧六量在组合校核中以
 *   MappingBatchFacts.axes 为权威
 *   （电机侧工作点依赖组合 c——见 MappingAxisFact 注），本包条目的
 *   motor* 字段在组合校核路径被忽略（直调组装方无须填充）。
 *
 * ★ 与 drivetrain 消费的差异（诚实登记）：同键双消费者——drivetrain
 *   消费 dynamics 的逐样本序列（JointSeriesView 编码）；本域消费的是
 *   【组装方按 DYN-03 口径提取后的轴级工作点事实】（T04 AxisWorkpointFacts
 *   列表）。物化锚单点＝selUpstreamAnchor（selection 域内派生规则，与
 *   drivetrain 域锚互不混用——两域字节形态不同，锚空间按域隔离）。
 *   dynamics 卡产出后按其卡收编（R-SEL-1）。
 */
using AxisFactsBundle = std::vector<AxisWorkpointFacts>;

/**
 * @brief 物化锚派生（selection 域内单点——上游结果字节在宿主对象空间的
 *        读取锚）。
 *
 * 派生规则与 drivetrain jointSeriesAnchor 同形（ObjectId.bytes＝上游切片
 * 身份前 16 字节；确定性无随机；版本参数用保留值＝版本一致性由上游切片
 * 身份承载）——但规则单点在各自域（字节形态不同，禁跨域混用锚）。
 *
 * @param upstreamSliceId [in] 上游结果切片身份（须非全零——全零为调用方
 *                        契约违约，fail-fast）
 * @return 物化锚对象 ID
 *
 * @throws std::invalid_argument upstreamSliceId 为全零保留值
 */
core::ObjectId selUpstreamAnchor(const core::ContentIdentity& upstreamSliceId);

// =====================================================================
// 惯量比可配置工程规则（SEL-05；O-11/P-POL-3 边界——卡 §11.3）
// =====================================================================

/**
 * @brief 惯量比工程规则（可配置通道——阈值来自调用方显式供给，不写死
 *        任何默认值：P-SEL-4"不写死默认阈值"、P-DT-2"阈值判定归
 *        selection 且不内嵌数字"）。
 *
 * ★ 未裁决期间的保守行为（卡 §11.3 原文行"未裁决前行为"）：本头不提供
 *   "已裁决工程约束"形态——referenceMaxRatio 是【参考阈值】（呈现/排序
 *   参考面，卡 §11.3 Quick 研究态"用户可临时参考值排序候选——明确标注
 *   非工程约束、不入结果身份"）：
 *   - nullopt（R1 默认态，P-POL-3 未裁决）→ 该维度输出显式"未判定"
 *     （InertiaRatioPolicyUnsettled 语义）——不作为淘汰原因、不作为可行
 *     依据、Verified 结论不因此整体阻断（该维度不是需求 §8.1 表 4 选型
 *     域必需项）；
 *   - 有值 → 逐轴与映射事实惯量比比较（参考语义）：超参考与否进入
 *     InertiaRatioCheck（呈现素材），同样不产生淘汰原因、不改变 verdict
 *     （词表无惯量比超限 token——裁决后经 Policy 条目进入切片的正式
 *     工程约束形态随 P-SEL-4 裁决＋单元卡增量修订启用，本头不预建）。
 */
struct InertiaRatioRule {
    /// 参考阈值（无量纲；nullopt＝未裁决/未配置→"未判定"；有值须有限>0
    /// ——违约 fail-fast）。
    std::optional<double> referenceMaxRatio;
    /// 阈值来源（策略条目引用/筛选条件条目文本——呈现与追溯；未配置时
    /// 应为空串）。
    std::string source;

    bool operator==(const InertiaRatioRule& o) const
    {
        return referenceMaxRatio == o.referenceMaxRatio && source == o.source;
    }
};

/**
 * @brief 单轴惯量比维度判定结果（§9.3 行 3 的承载——数值核对，三态显式）。
 */
struct InertiaRatioCheck {
    bool settled = false;   ///< 规则是否已配置参考阈值（false＝"未判定"态）
    /// 规则配置的参考阈值（无量纲；settled 时必有值——与规则同源镜像）。
    std::optional<double> referenceThreshold;
    /// 映射事实惯量比是否可得（false＝映射侧负载惯量缺失——数据缺口面）。
    bool ratioPresent = false;
    /// 数值事实惯量比（ratioPresent 时镜像——呈现素材）。
    std::optional<double> actualRatio;
    /// 是否不超过参考阈值（settled 且 ratioPresent 时有效；true/false 均
    /// 为参考语义——不构成淘汰原因，卡 §11.3）。
    std::optional<bool> withinReference;

    bool operator==(const InertiaRatioCheck& o) const
    {
        return settled == o.settled && referenceThreshold == o.referenceThreshold
            && ratioPresent == o.ratioPresent && actualRatio == o.actualRatio
            && withinReference == o.withinReference;
    }
};

// =====================================================================
// 多工况候选资格矩阵素材与组合校核产出（卡 §9.4/§9.6）
// =====================================================================

/**
 * @brief 资格矩阵单格判定（卡 §9.4——每格绑定组合×工况；判定来源轴/
 *        时刻/段随格携带——淘汰原因定位面）。
 *
 * verdict 三态语义（卡 §9.4 矩阵图例）：Pass＝该工况全部已判定维度通过；
 * Fail＝该工况存在轴级淘汰原因（定位字段指向首条原因的工作点）；Data-
 * Insufficient＝该工况存在数据缺口（缺失项全量列出——组合整体转
 * DataInsufficient，卡 §9.4"任一工况数据不足→组合整体素材"）。
 */
struct CaseCoverageEntry {
    DeviceCombinationId combinationId; ///< 组合键（矩阵行）
    CaseId caseId;                     ///< 工况 ID（矩阵列——必验工况逐格，不得漏验）
    VerdictKind verdict = VerdictKind::DataInsufficient; ///< 格判定（复用 T04 三态）
    core::ObjectId axisId;             ///< 定位轴（Fail 时的原因轴；Pass 时全零）
    double atTime = 0.0;               ///< 定位工作点时间（s；Fail 时有效）
    std::string segmentId;             ///< 定位轨迹段（Fail 时有效；无段上下文空串）
    std::string note;                  ///< 判定摘要（呈现素材——如失败维度 token 文本；
                                       ///   惯量比未判定态在此携带 "inertia-ratio-
                                       ///   policy-unsettled" 标注——§10.3 词表文本）

    bool operator==(const CaseCoverageEntry& o) const
    {
        return combinationId == o.combinationId && caseId == o.caseId
            && verdict == o.verdict && axisId == o.axisId
            && atTime == o.atTime && segmentId == o.segmentId && note == o.note;
    }
};

/**
 * @brief 单组合校核产出（组合级 FeasibilityRecord＋资格矩阵素材＋参考面）。
 *
 * record 复用 T04 FeasibilityRecord（同一类型不分叉——T04 落位细化 ①：
 * 组合级记录 id 承载组合键）；组合级身份承载约定：candidateModelId＝空串
 * （组合无单候选——逐候选定位在 RejectionReason.candidateModelId）、
 * axisId＝全零（组合级无单轴）、deviceKind＝Combination（表尾追加值，
 * T04 落位细化登记）。inputSliceId/mappingId 在本结构真实回填（T04 头注
 * 边界：评估器路径由 T05 回填——直调路径由调用方供给，无切片时全零）。
 */
struct CombinationCheckOutcome {
    FeasibilityRecord record;                 ///< 组合级记录（id＝组合键）
    std::vector<CaseCoverageEntry> coverage;  ///< 资格矩阵素材（必验工况逐格）
    bool inertiaRatioUnsettled = false;       ///< 惯量比维度存在"未判定"轴（O-11 显式标记）
    double totalMass = 0.0;                   ///< 组合质量（kg；Σ 各轴电机＋减速器质量——
                                              ///   目录必填字段，恒可得；SEL-06 素材面）

    bool operator==(const CombinationCheckOutcome& o) const
    {
        return record == o.record && coverage == o.coverage
            && inertiaRatioUnsettled == o.inertiaRatioUnsettled
            && totalMass == o.totalMass;
    }
};

/**
 * @brief 组合校核总产出（§9.6 SelectionCheckPayload 的域内形态——评估器
 *        payload 的编码对象；reporting 选型章节与 WP-19-T06 可行集组装
 *        的消费面）。
 *
 * identity 面按卡 §9.6"目录/映射身份块"承载：catalog（组合级目录身份）、
 * mappingSliceId（映射批上游切片身份——直调无映射身份时全零）、
 * inputSliceId（评估器路径切片身份——直调路径全零，诚实标记）。
 */
struct SelectionCheckResult {
    std::vector<FeasibilityRecord> records;  ///< 逐组合记录（输入组合序）
    std::vector<CaseCoverageEntry> coverage; ///< 资格矩阵素材（组合序×工况序）
    CatalogIdentity catalog;                 ///< 目录身份块（§9.6）
    core::ContentIdentity mappingSliceId;    ///< 映射批身份块（§9.6；全零＝直调无映射）
    core::ContentIdentity inputSliceId;      ///< 切片身份（评估器路径回填；直调全零）
    CompletenessKind completeness = CompletenessKind::Complete; ///< 批完整性
    std::vector<std::string> missingItems;   ///< 缺失清单（映射批 Partial 的批级素材）
    std::vector<std::string> diagnosticCodes;///< 映射批诊断码透传（§10.2 上游诊断不吞）

    bool operator==(const SelectionCheckResult& o) const
    {
        return records == o.records && coverage == o.coverage
            && catalog == o.catalog && mappingSliceId == o.mappingSliceId
            && inputSliceId == o.inputSliceId && completeness == o.completeness
            && missingItems == o.missingItems && diagnosticCodes == o.diagnosticCodes;
    }
};

// =====================================================================
// 组合校核核心（纯函数——模型测试直调 NFR-MNT-01；评估器经同一实现调用）
// =====================================================================

/**
 * @brief 组合校核核心输入（直调与评估器路径共用的值面——卡 §9.2 管线②
 *        切片条目的域内对位：快照〔catalog.lock 解析产物〕＋轴事实〔dyn.
 *        joint-series 消费面〕＋映射批〔dt.mapping 消费面——组合指派表
 *        与逐轴事实的唯一来源〕＋筛选条件〔config.sel-screening 消费面〕）。
 */
struct CombinationCheckCoreInput {
    const CatalogPackageSnapshot* snapshot = nullptr; ///< 目录快照（调用方持有；
                                                      ///   非空——调用方契约）
    std::vector<AxisWorkpointFacts> axisFacts;        ///< 轴×工况工作点事实（关节侧
                                                      ///   ＋电机侧——电机侧须来自
                                                      ///   dt.mapping 批，§9.1）
    MappingBatchFacts mappingBatch;                   ///< 映射批事实（P-SEL-1 提议 v1
                                                      ///   ——combinations 表＝校核
                                                      ///   遍历序的唯一来源）
    ScreeningCriteria criteria;                       ///< 筛选条件（进入切片身份的面）
    InertiaRatioRule inertiaRule;                     ///< 惯量比工程规则（可配置；
                                                      ///   评估器路径 R1 恒未配置态
                                                      ///   ——P-SEL-4 裁决后经 Policy
                                                      ///   条目增登）
    core::ContentIdentity inputSliceId;               ///< 切片身份（评估器路径回填；
                                                      ///   直调无切片＝全零诚实标记）
};

/**
 * @brief 组合校核核心（§9.3 全覆盖清单的执行体——纯函数直调面）。
 *
 * 逐组合执行（维度序＝卡 §9.3 清单行序；候选能力维度不短路——全部独立
 * 维度执行完毕才汇总，卡 §10.2）：
 *  ①目录版本一致性：组合 catalog ≠ 快照身份 → CatalogVersionIncompatible
 *    原因（逐项定位，不整批短路——身份错配是候选级事实）；
 *  ②组合兼容：任一轴 (motor, gearbox) 在兼容表无记录 → ComboIncompatible
 *    （构造面已过滤的前提下的双保险核对）；
 *  ③轴映射完整性：事实轴集 ⊄ 组合轴集（缺轴）→ AxisMappingIncomplete；
 *  ④SEL-09 范围外阻断（WP-19-T08——卡 §2.2 R1 纪律/D-SEL-15）：组合含
 *    移动关节轴（jointKind==Prismatic）→ 逐范围外轴恰一条数据缺口
 *    （dimension="axis-out-of-scope"，diagCode＝SEL-INPUT-AXIS-OUT-OF-
 *    SCOPE，caseId 空——轴级边界事实与工况无关）；该轴不查询工作点/
 *    映射事实、不调用 T04 旋转传动筛选（不静默套用旋转传动、不伪造
 *    电机工作点）；含范围外轴的资格矩阵格不得判 Pass（全移动链时格
 *    聚合无轴记录可依，兜底为 DataInsufficient＋note="axis-out-of-
 *    scope"）；混合链中旋转轴的既有 Rejected 判定保持原样（范围外是
 *    数据/边界类事实——不覆盖淘汰原因，也不升级整机不可行，判定权在
 *    evidence 汇总）；
 *  ⑤轴级能力判定（复用 T04 HardConstraintSelector——同一实现不分叉）：
 *    按 (轴×工况) 先对全部候选筛选一次（R-SEL-2 缓解——逐轴先筛选），
 *    组合装配时取本组合候选的记录；电机侧维度消费映射事实（调用方供给
 *    的 motor* 字段），关节侧维度消费 dynamics 事实；
 *  ⑥惯量比维度（§11.3）：规则未配置→显式未判定（inertiaRatioUnsettled
 *    标记，不产生原因/缺口、不影响 verdict）；配置且映射事实缺失→
 *    DataGap；配置且有值→withinReference 参考判定（不产生淘汰原因）；
 *  ⑦映射完整性：mappingFailed→DataGap（上游失败透传——不伪装淘汰）；
 *    Partial→missingItems 逐条转 DataGap；
 *  ⑧效率降级：映射事实 efficiencyApplied=false 的轴→DataGap（不以
 *    η＝1 静默替代）；
 *  ⑨多工况资格矩阵：每（组合×工况）格按该工况轴级判定聚合（全部启用
 *    必验工况通过方可行——EVI-02 不得漏验）；
 *  ⑩质量核算：Σ 各轴电机＋减速器质量（kg）；
 *  ⑪verdict 汇总（T04 细化 ⑦ 同规则）：reasons 非空→Rejected；gaps
 *    非空→DataInsufficient；否则 Feasible。
 *
 * @param input [in] 核心输入（snapshot 须非空；mappingBatch.combinations
 *              为校核遍历序的唯一来源；调用方持有其存活期）
 * @param ctx   [in] 取消查询（可空 nullptr；查询点＝组合边界＋轴×工况
 *              筛选批次边界——观测到取消即停止处理剩余组合，返回已完成
 *              产出〔截断语义——调用方以产出数对比组合数感知，T04 ④
 *              同款口径〕）
 * @return 逐组合产出（序＝映射批 combinations 表序；已取消截断时为
 *         前缀——不发布未校核组合的任何产出，§13.4"取消不发布完整
 *         可行集"）
 *
 * @throws std::invalid_argument snapshot 为空指针／组合 catalog 非有限/
 *         criteria 非法（同 T04 契约——非有限值/安全系数<1/最低效率∉(0,1]/
 *         速比范围非良构）／inertiaRule.referenceMaxRatio 有值但非有限
 *         或 ≤0／映射批版本字段为 0（未登记非法——调用方契约违约）
 *
 * @note 纯函数；同输入恒同输出（NFR-COR-02）；可重入。
 */
std::vector<CombinationCheckOutcome> checkCombinations(
    const CombinationCheckCoreInput& input,
    const evidence::IEvaluationContext* ctx);

// =====================================================================
// canonical 字节编解码（域内协议——组装方协议面；drivetrain Codec 同款
// 协议风格：小端定宽、8 字节 magic＋u32 codec 版本、string＝u32 长度＋
// UTF-8、vector＝u32 元素数＋逐元素、id＝16 字节原始字节、摘要＝32 字节、
// optional＝u8 有无标志＋有值时 T、枚举＝u8 底层值；解码严格校验——
// magic/版本/长度违约抛 std::invalid_argument；非有限 double 拒绝编码
// ——NFR-COR-03）
// =====================================================================

/// 编码目录锁定引用（catalog.lock 条目对象字节；magic "IRDSLK1"）。
std::vector<std::uint8_t> encodeCatalogLockPayload(const CatalogLockPayload& payload);
/// 解码目录锁定引用（解码违约＝调用方契约违约 fail-fast——字节形态错误
/// 属组装协议破坏，不静默降级；下同）。
CatalogLockPayload decodeCatalogLockPayload(const std::vector<std::uint8_t>& bytes);

/// 编码筛选条件（config.sel-screening 条目字节；magic "IRDSLB1"——T04
/// ScreeningCriteria 头注"条目化编码归组合校核段"的兑现）。
std::vector<std::uint8_t> encodeScreeningCriteria(const ScreeningCriteria& criteria);
/// 解码筛选条件。
ScreeningCriteria decodeScreeningCriteria(const std::vector<std::uint8_t>& bytes);

/// 编码轴工作点事实包（dyn.joint-series 消费面字节；magic "IRDSAF1"）。
std::vector<std::uint8_t> encodeAxisFactsBundle(const AxisFactsBundle& bundle);
/// 解码轴工作点事实包。
AxisFactsBundle decodeAxisFactsBundle(const std::vector<std::uint8_t>& bytes);

/// 编码映射批事实包（dt.mapping 消费面字节；magic "IRDSMB1"——P-SEL-1
/// 提议契约 v1 的字节面）。
std::vector<std::uint8_t> encodeMappingBatchFacts(const MappingBatchFacts& facts);
/// 解码映射批事实包。
MappingBatchFacts decodeMappingBatchFacts(const std::vector<std::uint8_t>& bytes);

/// 编码组合校核总产出（评估 payload canonical 字节；magic "IRDSCC1"）。
std::vector<std::uint8_t> encodeSelectionCheckResult(const SelectionCheckResult& result);
/// 解码组合校核总产出。
SelectionCheckResult decodeSelectionCheckResult(const std::vector<std::uint8_t>& bytes);

/// 编码候选组合集载荷（config.dt-combo-set 条目字节；magic "IRDSDC1"——
/// 管线①提交面；P-SEL-1 跨卡对齐注记的字节承载）。
std::vector<std::uint8_t> encodeDtComboSetPayload(
    const std::vector<CombinationDriveInput>& inputs);
/// 解码候选组合集载荷。
std::vector<CombinationDriveInput> decodeDtComboSetPayload(
    const std::vector<std::uint8_t>& bytes);

// =====================================================================
// 组合校核评估器（sel-combination-check，③端口——卡 §3.1 CombinationCheck）
// =====================================================================

/**
 * @brief 构造组合校核评估器描述符（字段落值＝卡 §9.2 管线②切片的声明面）。
 *
 * 字段落值（逐字段出处）：
 *   - key＝kCombinationCheckEvaluationKey（"sel-combination-check"——kebab
 *     词形偏差同 drivetrain kMappingEvaluationKey 先例：卡面 "sel.
 *     combination-check" 记法与 evidence 评估键词形闸门（不含点）冲突，
 *     按③端口所有者词形权威执行）；
 *   - contractVersion＝kCombinationCheckContractVersion（进 sliceId）；
 *   - inputs＝{ {catalog.lock, Object, Required}, {dyn.joint-series,
 *     UpstreamResult, Required}, {dt.mapping, UpstreamResult, Required},
 *     {config.sel-screening, Configuration, Required} }（卡 §9.2 管线②
 *     四条目；P-SEL-4 裁决后按卡 §11.3"descriptor 增加 Policy 依赖声明"
 *     增登——本版不预声明 Policy 条目）；
 *   - profile＝{profileId:"sel", version:"1", contentIdentity:保留值}
 *     （sel 域 Profile——drivetrain 评估器偏差 2 已绑定同版本；内容权威
 *     归本卡，正式 RequiredEvidenceProfile 注册随 WP-19-T06 EVI-01 行）；
 *   - supportedModes＝{Quick, Verified}（卡 §11.2——R1 评估器不含
 *     Preview，保守同 drivetrain D-DT-14）；
 *   - stateless＝true；threadSafety＝SingleThread（卡 §14.10）。
 *
 * @return 描述符（值语义）
 */
evidence::EvaluatorDescriptor makeCombinationCheckDescriptor();

/**
 * @brief 组合校核评估器（③端口被调方——管线②的执行形态）。
 *
 * evaluate() 流程（drivetrain 评估器适配层同款五步）：
 *   ①切片核对：evaluationKey/evaluatorContractVersion 与登记值不符＝
 *     派发违约（fail-fast）；
 *   ②必需条目提取：四条目缺失＝运行期第二道防线 fail-fast；
 *   ③字节读取与解码：catalog.lock Object 条目经 tryObjectBytes 读取
 *     CatalogLockPayload 字节（与条目载荷第二道核对）后经注入的
 *     ICatalogProvider 解析快照（load 抛出的引用完整性破坏＝数据类，
 *     返回诊断＋空 payload 不抛）；两条 UpstreamResult 条目按
 *     selUpstreamAnchor 物化锚读取并解码（不可得＝数据类诊断）；
 *     config.sel-screening Configuration 条目直接携带 canonicalBytes
 *     （evidence §4.2.1 Configuration 载荷——无锚）；
 *   ④批级身份预检（§10.2 校验边界快速拒绝）：映射批契约/算法版本非零、
 *     批 upstreamSliceId 与切片条目声明逐字节一致——不一致→
 *     SEL-IDENTITY-MISMATCH 诊断＋空 payload（拒绝评估，不以版本不符
 *     的数据继续计算——AT-38 纪律）；
 *   ⑤核心调用＋输出装配：checkCombinations（取消经 context 适配——
 *     组合边界查询；观测到取消返回已完成前缀产出＋截断）→ payload
 *     （encodeSelectionCheckResult＋SHA-256 摘要）＋EvidenceItem
 *     （"sel.combination-check"——有完整产出即 Satisfied）＋映射批诊断
 *     码透传面（DT-* 引用——selection 不吞上游诊断，卡 §10.2）。
 *
 * @note 装配契约：构造注入 ICatalogProvider（selection 域内供给——T03
 *       InMemoryCatalogProvider 或 T07 存储适配；零跨单元边）。注册时序
 *       ＝"sel Profile 注册在前、评估器注册在后"（evidence §13；Profile
 *       实例登记随 WP-19-T06）。
 */
class CombinationCheckEvaluator final : public evidence::IEngineeringEvaluator {
public:
    /**
     * @brief 构造（绑定目录快照供给——装配期一次性注入）。
     *
     * @param catalogProvider [in] 目录快照供给（长期存活——评估器不接管
     *                        所有权；评估期内只读调用 load）
     */
    explicit CombinationCheckEvaluator(const ICatalogProvider& catalogProvider);

    /// 描述符（注册期已验证；运行期只读——返回引用指向实例成员稳定存储）。
    const evidence::EvaluatorDescriptor& descriptor() const override;

    /**
     * @brief 执行一次组合校核评估（evidence §9.3 调用约定；流程与错误轨
     *        见类注）。
     *
     * @param request [in] 评估请求（调用方持有，调用期间有效）
     * @param context [in] 宿主上下文（取消/进度/对象读取——实例不持久持有）
     * @return 评估产出（envelope 由调用侧组装——§9.3）
     *
     * @throws std::invalid_argument 切片派发违约/必需条目缺失/条目载荷
     *         形态不符（调用方错误 fail-fast）
     *
     * 线程约束：实例单线程（descriptor.threadSafety 声明）。
     */
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest& request,
                                        evidence::IEvaluationContext& context) override;

private:
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor() 引用所指）
    const ICatalogProvider* m_catalogProvider;  ///< 目录快照供给（非拥有——装配期注入）
};

/**
 * @brief 组合校核评估器工厂（§9.3 IEvaluatorFactory——L5 装配注册；
 *        create() 线程安全〔仅构造无状态对象＋拷贝引用成员〕）。
 */
class CombinationCheckEvaluatorFactory final : public evidence::IEvaluatorFactory {
public:
    /**
     * @brief 构造（绑定目录快照供给——create() 产出的实例共享同一供给）。
     *
     * @param catalogProvider [in] 目录快照供给（长期存活——须超过全部
     *                        产出实例的生命周期；本工厂不接管所有权）
     */
    explicit CombinationCheckEvaluatorFactory(const ICatalogProvider& catalogProvider);

    /// 工厂描述符（多次调用返回同值——注册表按首次返回值登记）。
    const evidence::EvaluatorDescriptor& descriptor() const override;

    /// 创建评估器实例（所有权随 unique_ptr 转移；保证非空）。
    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override;

private:
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor() 引用所指）
    const ICatalogProvider* m_catalogProvider;  ///< 目录快照供给（非拥有——装配期注入）
};

/// sel 域绑定 Profile 的登记版本（与 makeCombinationCheckDescriptor 的
/// profile 字段同源；drivetrain kMappingProfileId 同值——同一 sel 域
/// Profile 实例，注册时序由 L5 装配执行，evidence §13）。
inline constexpr std::string_view kSelProfileId = "sel";
inline constexpr std::string_view kSelProfileVersion = "1";

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_COMBINATION_HPP
