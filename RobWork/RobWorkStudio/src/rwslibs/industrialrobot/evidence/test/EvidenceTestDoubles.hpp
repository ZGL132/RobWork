/**
 * @file   EvidenceTestDoubles.hpp
 * @brief  evidence 单元规范测试替身（§11 测试设施）——可控评估器
 *         ScriptedEvaluator／宿主上下文替身 ScriptedEvaluationContext／
 *         工厂与注册辅助（§11、§9.3 调用约定的可观测承载）。
 *
 * 设计依据：
 *   - units/evidence.md §11 测试设施行：**可控评估器替身**（ScriptedEvaluator：
 *     按脚本返回预设证据/证明/搜索未果/取消抛出，注册于测试内 registry）；
 *     EV-REG-3 行（替身边界声明——见本文件头"替身边界声明"与本目录
 *     README.md）；§9.3（IEngineeringEvaluator/IEvaluationContext/
 *     IEvaluatorFactory 调用约定——替身逐条兑现）
 *   - 任务契约 tasks/foundation/EV-T11.json（≙WP-05-T11）acceptance 1～3：
 *     §11 矩阵用例体经端口路径驱动；替身边界声明在案；替身数据不得冒充
 *     真实证据
 *   - 命名空间与结构先例：runtime/test/RuntimeTestDoubles.hpp
 *     （sdurws::ird::runtime::testdoubles——同款 testdoubles 命名空间纪律）
 *
 * ★ 替身边界声明（EV-REG-3，全文见 test/README.md）：
 *   本头全部类型只存在于测试目标（evidence/test/，不进产品库源码面——
 *   EvaluatorPortSuiteTest 的源码扫描用例机检钉住）；ScriptedEvaluator 的
 *   脚本产出（证据项/证明/搜索未果/违例）是**契约形态数据**，仅用于验证
 *   evidence 的评估器端口、汇总与构造边界契约，**不构成任何 IK/动力学/
 *   碰撞等业务算法正确性证明**，也**不得**被任何产品路径持久化为真实证据
 *   （替身只存活于测试进程内存；descriptor/脚本均带 test 双重标注）。
 *
 * 线程约束：本头全部替身为单线程设施（每实例仅在其所属测试线程使用——
 * §11 替身用例无并发面；并发面归 EvaluatorTest 的 EV-REG-2 用例）。
 * 确定性：脚本按构造序回放，无时间/随机源（NFR-COR-02 同源纪律）。
 */

#ifndef SDURWS_IRD_EVIDENCE_EVIDENCETESTDOUBLES_HPP
#define SDURWS_IRD_EVIDENCE_EVIDENCETESTDOUBLES_HPP

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdurws::ird::evidence::testdoubles {

// =====================================================================
// 脚本步骤（§11"按脚本"的承载——每步一次 evaluate 的预设行为）
// =====================================================================

/**
 * @brief 单步脚本行为（一次 evaluate() 消费一步——按构造序回放）。
 *
 * 行为优先级（evaluate 内固定）：throwIfCancellationRequested 且宿主报取消
 * → 抛 EvidenceError（取消抛出轨）；否则 throwEvidenceError → 抛出；
 * 否则返回 output（预设产出轨）。三轨覆盖 §11 替身行文的全部行为词表
 * （"返回预设证据/证明/搜索未果/取消抛出"）。
 *
 * 值语义；线程约束：仅构造线程与所属评估器实例的调用线程使用（单线程）。
 */
struct ScriptedStep {
    /// 预设产出（默认轨——证据/证明/搜索未果/违例/载荷/诊断的契约形态数据）。
    EvaluationOutput output;
    /// 抛出轨开关：true＝本步不返回产出，抛 EvidenceError（域自选错误轨的
    /// 契约演练——§9.3"抛 EvidenceError 或返回诊断，域自选"）。
    bool throwEvidenceError = false;
    /// 抛出码面（域自选轨的演练值——默认取证据缺失族码面，无域语义）。
    EvidenceErrorCode throwCode = EvidenceErrorCode::EvidenceMissing;
    /// 抛出细节（人读定位——测试断言消息面）。
    std::string throwDetail;
    /// 取消抛出轨开关：true＝宿主 context.cancellationRequested() 为真时
    /// 抛 EvidenceError（§9.3 契约"观测到取消后自主选择退出路径"的
    /// 抛出支路——evidence 不捕获域异常，D-15 的可观测面）。
    bool throwIfCancellationRequested = false;
    /// 取消抛出码面/细节（同上两字段——取消支路的承载）。
    EvidenceErrorCode cancelCode = EvidenceErrorCode::EvidenceMissing;
    std::string cancelDetail;
    /// 域侧对照数据（**不进入**聚合输入——C8 语义：构型碰撞解仅记录在
    /// filteredSolutions、不上升为任务级证明；有有效解时评估器不产出
    /// searchRecord，本字段仅供测试断言"域侧行为与聚合输入的分离"）。
    std::optional<SearchExhaustedRecord> domainSideFilterRecord;
    /// 对象读取步开关：true＝本步经 context.tryObjectBytes 读取
    /// readObjectId@readContentVersion（§9.3 对象读取路径的演练——worker
    /// 物化场景的契约面；读取结果计入 context 侧调用记录）。
    bool readObjectBytes = false;
    core::ObjectId readObjectId;                 ///< 读取步目标对象（须非零）
    core::ContentVersion readContentVersion;     ///< 读取步期望内容版本（须非零）
};

// =====================================================================
// 宿主上下文替身（IEvaluationContext 的测试实现——§9.3 上下文契约的
// 可观测面；实现归宿主〔execution〕，测试以替身承载）
// =====================================================================

/**
 * @brief 可控评估上下文替身（§11 测试设施——取消查询/进度上报/对象读取
 *        三通道的可观测承载）。
 *
 * 行为：
 *   - cancellationRequested()：返回 cancelRequested 标志（测试用例置位）；
 *   - reportProgress()：记录（percent, phase）序列（测试断言上报面）；
 *   - tryObjectBytes()：按（对象规范文本→内容版本→字节）预置表回答；
 *     表外请求返回 nullopt（"对象不可得"契约支路），并记录调用次数。
 *
 * ★ 边界：本替身回答的"对象字节"是契约形态数据（测试脚本预置），不代表
 *   project 侧真实对象存储——EV-REG-3 同源。
 *
 * 线程约束：单线程（每测试实例私用）。
 */
class ScriptedEvaluationContext final : public IEvaluationContext {
public:
    /// 预置对象字节表键（对象规范文本＋"@"＋内容版本规范文本——表内唯一）。
    using ObjectKey = std::string;

    /// 生成预置表键（测试侧构表辅助——规范文本拼装单点）。
    static ObjectKey objectKey(core::ObjectId objectId, core::ContentVersion version)
    {
        return objectId.toCanonical() + "@" + version.toCanonical();
    }

    /// 预置一个可读对象（worker 物化形态——tryObjectBytes 将回答该字节）。
    void provideObject(core::ObjectId objectId, core::ContentVersion version,
                       std::vector<std::uint8_t> bytes)
    {
        m_objects.insert_or_assign(objectKey(objectId, version), std::move(bytes));
    }

    // ---- IEvaluationContext（§9.3 三通道——行为见类注释） ----

    bool cancellationRequested() const override { return m_cancelRequested; }

    void reportProgress(std::uint8_t percent, std::string_view phase) override
    {
        m_progress.emplace_back(percent, std::string{phase});
    }

    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId objectId, core::ContentVersion contentVersion) const override
    {
        ++m_objectLookups;   // 调用计数（mutable——接口为 const）
        const auto it = m_objects.find(objectKey(objectId, contentVersion));
        if (it == m_objects.end()) {
            return std::nullopt;   // 表外＝对象不可得（§9.3 契约支路）
        }
        return it->second;
    }

    // ---- 测试观测/驱动面（非接口契约——仅测试线程使用） ----

    /// 置位/清除取消标志（EV-VER 替身行的"取消抛出"驱动器）。
    void setCancelRequested(bool requested) { m_cancelRequested = requested; }

    /// 进度上报记录（percent, phase 按序）。
    const std::vector<std::pair<std::uint8_t, std::string>>& progressLog() const
    {
        return m_progress;
    }

    /// tryObjectBytes 累计调用次数（对象读取路径的可观测计数）。
    int objectLookupCount() const { return m_objectLookups; }

private:
    bool m_cancelRequested = false;               ///< 取消标志（测试置位）
    std::vector<std::pair<std::uint8_t, std::string>> m_progress; ///< 进度记录
    std::map<ObjectKey, std::vector<std::uint8_t>> m_objects;     ///< 预置对象表
    mutable int m_objectLookups = 0;              ///< 对象读取计数（const 路径递增）
};

// =====================================================================
// 可控评估器替身（ScriptedEvaluator——§11 点名的替身类型名）
// =====================================================================

/**
 * @brief 可控评估器替身（§11：按脚本返回预设证据/证明/搜索未果/取消抛出；
 *        注册于测试内 registry——配 ScriptedEvaluatorFactory 经
 *        EvaluatorRegistry 注册，走真实注册期校验与 create 路径）。
 *
 * 行为契约（全部来自 §9.3 调用约定——替身是契约的"合规被调方"样本）：
 *   - 每次 evaluate() 消费脚本下一步（构造序回放；脚本耗尽＝测试程序
 *     缺陷，抛 std::logic_error fail-fast——不静默重复最后一步）；
 *   - 周期性查询 context.cancellationRequested()（§9.3 长评估义务——
 *     本替身每次 evaluate 恰查询一次，查询点在行为分发前）；
 *   - 每次评估上报一次进度（50%，"scripted-solve"——进度通道演练；
 *     percent 无量纲，§9.1 约定 0～100）；
 *   - 收到的全部 request 逐份记录（receivedRequests——断言调用方装配面：
 *     切片/工况子集/模式到达评估器的内容）；
 *   - descriptor() 返回构造时持有的描述符（注册期已验证的同一值）。
 *
 * ★ 替身边界（EV-REG-3）：脚本产出为契约形态数据——仅验证 evidence 契约，
 *   不构成业务算法正确性证明（见文件头；test/README.md 全文）。
 *
 * 线程约束：单线程（descriptor.threadSafety=SingleThread 与行为一致）。
 */
class ScriptedEvaluator final : public IEngineeringEvaluator {
public:
    /**
     * @brief 构造（描述符＋脚本）。
     *
     * @param descriptor [in] 评估器描述符（注册期验证对象；值拷贝持有——
     *                   descriptor() 须指向稳定存储，§9.3 契约）
     * @param script     [in] 脚本步骤序列（按构造序回放；可为空——空脚本
     *                   的 evaluate 即测试缺陷，logic_error fail-fast）
     */
    ScriptedEvaluator(EvaluatorDescriptor descriptor, std::vector<ScriptedStep> script)
        : m_descriptor(std::move(descriptor)), m_script(std::move(script))
    {
    }

    const EvaluatorDescriptor& descriptor() const override { return m_descriptor; }

    EvaluationOutput evaluate(const EvaluationRequest& request,
                              IEvaluationContext& context) override
    {
        m_requests.push_back(request);   // 调用方装配面记录（断言用）

        // §9.3 周期性取消查询（本替身的查询点：行为分发前恰一次）。
        const bool cancelled = context.cancellationRequested();

        // 进度上报演练（§9.1 通道——percent 50 无量纲，phase 为固定短语）。
        context.reportProgress(50, "scripted-solve");

        // 脚本耗尽＝测试程序缺陷（脚本步数与调用次数不匹配）——fail-fast，
        // 不静默重复（重复回放会把脚本错序伪装成"通过"）。
        if (m_next >= m_script.size()) {
            throw std::logic_error("ScriptedEvaluator: 脚本已耗尽（evaluate 调用 "
                                   "超出脚本步数——测试装配缺陷）");
        }
        ScriptedStep& step = m_script[m_next++];

        // 取消抛出轨（§9.3"观测到取消后自主选择退出路径"的抛出支路）。
        if (step.throwIfCancellationRequested && cancelled) {
            throw EvidenceError(step.cancelCode, step.cancelDetail);
        }
        // 域自选错误轨（§9.3"抛 EvidenceError 或返回诊断，域自选"）。
        if (step.throwEvidenceError) {
            throw EvidenceError(step.throwCode, step.throwDetail);
        }
        // 对象读取步（§9.3 tryObjectBytes 路径演练——读取结果由 context
        // 记录；本替身不把字节内容搬进产出，读取行为本身即被测面）。
        if (step.readObjectBytes) {
            (void)context.tryObjectBytes(step.readObjectId, step.readContentVersion);
        }
        // 默认轨：返回预设产出（契约形态数据——文件头边界声明）。
        return step.output;
    }

    /// 已收到的请求序列（逐份拷贝——断言调用方装配面：切片条目/工况子集/
    /// 模式/任务身份到达评估器的内容；快照不可变值拷贝语义）。
    const std::vector<EvaluationRequest>& receivedRequests() const { return m_requests; }

    /// 已消费的脚本步数（回放进度断言——EV-VER-2 双步回放观测面）。
    std::size_t consumedSteps() const { return m_next; }

private:
    EvaluatorDescriptor m_descriptor;        ///< 描述符（构造时值拷贝——稳定存储）
    std::vector<ScriptedStep> m_script;      ///< 脚本（按序消费）
    std::size_t m_next = 0;                  ///< 下一步下标（回放游标）
    std::vector<EvaluationRequest> m_requests; ///< 收到的请求记录
};

// =====================================================================
// 工厂替身与描述符/注册辅助
// =====================================================================

/**
 * @brief 脚本评估器工厂（IEvaluatorFactory 的测试实现——注册表持工厂、
 *        create() 产独立实例；每实例获得脚本的全量副本独立回放）。
 *
 * create() 线程安全：本替身按 §9.4"create() 线程安全"承诺实现（只读
 * 共享脚本模板，实例各持副本）——但测试面按单线程使用（EV-REG-2 的并发
 * 面在 EvaluatorTest 以独立替身承载）。
 */
class ScriptedEvaluatorFactory final : public IEvaluatorFactory {
public:
    ScriptedEvaluatorFactory(EvaluatorDescriptor descriptor,
                             std::vector<ScriptedStep> script)
        : m_descriptor(std::move(descriptor)), m_script(std::move(script))
    {
    }

    const EvaluatorDescriptor& descriptor() const override { return m_descriptor; }

    std::unique_ptr<IEngineeringEvaluator> create() const override
    {
        // 每实例独立回放（脚本全量副本——EV-VER-2 双步回放的实例语义）。
        return std::make_unique<ScriptedEvaluator>(m_descriptor, m_script);
    }

private:
    EvaluatorDescriptor m_descriptor;      ///< 描述符（注册期验证对象）
    std::vector<ScriptedStep> m_script;    ///< 脚本模板（create 时拷贝）
};

/**
 * @brief 标准脚本评估器描述符（测试域形状——kin 域最小合法 descriptor）。
 *
 * 字段口径（§9.2/§9.4 注册期校验面）：
 *   - key＝"kin-batch-ik"（评估键不含点——isValidEvaluationKey 词形）；
 *   - contractVersion＝7（>0；进入 sliceId——CON-04）；
 *   - inputs＝两条 Required 声明（模型对象＋求解配置——无条件，无适用
 *     条件故不涉 referencedKeys 闭包；snapshotFactKeys 可为空表）；
 *   - profile＝(kin, 1.0.0) 声明引用且 contentIdentity 置保留值（域不可
 *     申报——§9.5 实现口径 R-3：申报非零即注册拒绝）；
 *   - supportedModes＝{Quick, Verified}（非空、无重复——表 1 模式效力）；
 *   - stateless=false（每次 create 独立实例）、threadSafety=SingleThread
 *     （与替身单线程行为一致——声明即契约，虚报属实现缺陷）。
 *
 * 确定性：纯函数（同入参恒同值——NFR-COR-02 测试面）。
 */
inline EvaluatorDescriptor makeScriptedKinDescriptor()
{
    EvaluatorDescriptor d;
    d.key = "kin-batch-ik";
    d.contractVersion = 7;

    // 依赖声明两条：模型对象（Object/Required）＋求解配置（Configuration/
    // Required）——声明闭包校验（§4.2.3①）的最小合法集。
    DependencyDeclaration model;
    model.key = "model.robot-design";
    model.kind = DependencyKind::Object;
    model.requiredness = DependencyRequiredness::Required;
    DependencyDeclaration config;
    config.key = "solve.ik-config";
    config.kind = DependencyKind::Configuration;
    config.requiredness = DependencyRequiredness::Required;
    d.inputs = {model, config};

    // Profile 声明引用：contentIdentity 置零（域不可申报——§9.5/R-3；
    // 权威值由 EvidenceProfileRegistry 注册时计算）。
    d.profile.profileId = "kin";
    d.profile.version = "1.0.0";

    // 模式效力（EVI-01 表 1）：Preview 不入正式评估——不声明。
    d.supportedModes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};
    d.stateless = false;
    d.threadSafety = ThreadSafety::SingleThread;
    return d;
}

/**
 * @brief 组装注册辅助：把脚本评估器按标准形状注册进真实注册表对
 *        （§11"注册于测试内 registry"——注册路径过 §9.4/§9.5 全部注册期
 *        校验，注册失败即抛——测试装配错误显性暴露）。
 *
 * 调用序约束（§13 接入顺序）：Profile 先注册、评估器后注册（注册期验证
 * 解析 Profile——顺序颠倒即 ProfileUnresolvable 拒绝）。
 *
 * @param registry      [in,out] 评估器注册表（L5 装配面——测试内实例）
 * @param profileRegistry [in] Profile 注册表（须已注册对应 (kin,1.0.0)）
 * @param script        [in] 脚本步骤（工坊模板——每实例全量副本）
 * @param snapshotFactKeys [in] 快照事实键（本描述符无条件声明——可传空表）
 *
 * @throws EvidenceError 注册期校验拒绝（EvaluatorDuplicate/
 *         EvaluatorDescriptorInvalid/DeclarationInvalid——测试装配缺陷）
 */
inline void registerScriptedEvaluator(EvaluatorRegistry& registry,
                                      const EvidenceProfileRegistry& profileRegistry,
                                      std::vector<ScriptedStep> script,
                                      const std::vector<std::string>& snapshotFactKeys
                                      = {})
{
    registry.registerEvaluator(
        std::make_unique<ScriptedEvaluatorFactory>(makeScriptedKinDescriptor(),
                                                   std::move(script)),
        snapshotFactKeys);
}

}  // namespace sdurws::ird::evidence::testdoubles

#endif  // SDURWS_IRD_EVIDENCE_EVIDENCETESTDOUBLES_HPP
