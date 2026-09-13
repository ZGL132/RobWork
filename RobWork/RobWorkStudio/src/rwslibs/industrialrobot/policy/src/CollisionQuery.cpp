/**
 * @file   CollisionQuery.cpp
 * @brief  碰撞评估唯一实现的评估半区——会话构建的评估装配（buildEvaluationHalf）
 *         与 evaluate 执行序（§6.3 状态机/§6.4 确定性稳定排序/§6.6 调用时序/
 *         §7.4 边界值决策表）的产品实现（POL-T07）。
 *
 * 设计依据：
 *   - units/policy.md §6.2（查询与输出类型——CollisionQuery.hpp 的语义权威）、
 *     §6.3（评估状态机："失败/取消/中断/检测到碰撞"四分——Completed/
 *     Canceled/Failed 的 findings/finalized 语义；框架异常（rw::common::
 *     Exception）必须捕获转
 *     Failed＋诊断，不吞、不崩、不跨进程边界）、§6.4（确定性：evaluate
 *     纯度/无随机源/无归约；稳定排序三入口跨进程逐字节一致——NFR-COR-05/
 *     AT-19）、§6.6（调用时序：setState→逐对 inCollision/distance→取消
 *     检查点→异常捕获→稳定排序输出）、§7.2（评估顺序——必检先于过滤、
 *     过滤全量留痕）、§7.4（距离语义与边界值决策表——d＞m 清晰/d＝m 清晰
 *     〔边界含于安全侧，D-08〕/0≤d＜m 间距不足/后端相交＝碰撞/NaN±Inf
 *     评估失败/m＝0 退化仅碰撞）、§7.5（评估期数值异常/几何缺失/名称
 *     断链/上下文失效的处理行）、§9.3（evaluate 契约表——错误类型/线程
 *     约束/确定性/副作用行）、§12 POL-T07 行（本 TU＝评估实现落位）
 *   - traceability/foundation-api-diff.md CR-04（名称消费仅经固化结果与
 *     场景 Frame——本 TU 不含名称拼接/剥离；R-4/P-POL-8 例外登记措辞）
 *   - 需求 NFR-COR-02/03/05、KIN-02（构型级发现——硬过滤素材）、KIN-05
 *     （无几何≠无碰撞——coverage 显式计数＋告知性诊断；缺检测器≠无碰撞
 *     ——Failed＋DETECTOR-UNAVAILABLE）、TRJ-04（PathSequence 保序＋
 *     pathParameter 段内定位）、TASK-02/CON-04（取消/失败不产正式证据）、
 *     ARC-05（唯一实现——R-POL-1~5 的执行侧落点）
 *
 * 背景说明（第一读者须知）：
 *   本 TU 与 CollisionEvaluator.cpp（构建期半区）、唯一实现绑定姊妹 TU
 *   （零字面量纪律文件——其全名见 policy/CMakeLists.txt 登记与该文件头）
 *   的切分沿用 policy.md §15.4 v0.7 登记：框架命名标识符
 *   集中在姊妹 TU；本 TU 含评估
 *   执行的 rw 基线调用（setQ/worldTframe/inCollision/distance——框架
 *   API 调用而非框架命名标识符定义）与诊断字面量（评估期 POLICY-CLL-*
 *   单点，POL-T10 Diagnostics.hpp 落位前的承载处）。ird_gates IRD-GATE-R4
 *   的跨引号对启发式对本 TU 的触发面＝API 调用名（非字符串拼接行为）——
 *   与 POL-T06 的切分动机一致，误报隔断归属例外登记流程（O-12/P-POL-8）。
 *
 * 评估期诊断码（建议码单点——码值权威归 diagnostics StableCodeRegistry，
 * PA-1；§9.6 CLL 家族清单行，随单元卡 v0.8 补登留痕；POL-T10 落位
 * Diagnostics.hpp 后迁入其码表，本 TU 届时仅消费）：
 *   POLICY-CLL-CONTEXT-EXPIRED      迟到调用拒绝（POL-LATE-1）
 *   POLICY-CLL-DETECTOR-UNAVAILABLE 检测器能力不可用（KIN-05；P-POL-11）
 *   POLICY-CLL-EVALUATION-FAILED    评估内部异常/非有限实测值（§7.4/§7.5）
 *   POLICY-CLL-GEOMETRY-MISSING     作用域对象几何缺口（告知性——KIN-05）
 *
 * 线程安全：evaluate 为会话 const 成员——共享状态只有注入后端实例与其
 * 查询互斥（构造期装配的其余产物只读）；后端查询面（rw 检测器统计
 * 计数器非线程安全）经 backendQueryMutex 串行化——锁不进入输出（§6.4
 * evaluate 纯度：同 (会话, 查询, 上下文存活) → 逐字段等价输出）。确定性：
 * 遍历序＝作用域展开产物规范序；无随机源；无归约。
 */

#include <sdurws/ird/policy/CollisionEvaluator.hpp>

#include <rw/common/Exception.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/Object.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/CollisionStrategy.hpp>
#include <rw/proximity/DistanceStrategy.hpp>
#include <rw/proximity/ProximityStrategyData.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sdurws::ird::policy {

// =====================================================================
// 匿名命名空间——评估半区的内部实现件（非公共契约）。
// =====================================================================

namespace {

// ---- 评估期诊断建议码单点（见文件头码表说明——POL-T10 前的承载处）----

/// 迟到调用拒绝（POL-LATE-1——§6.1 只读生命周期第二层防护的执行点）。
constexpr std::string_view kCodeContextExpired = "POLICY-CLL-CONTEXT-EXPIRED";
/// 检测器能力不可用（KIN-05"缺检测器≠无碰撞"；距离能力缺口——P-POL-11）。
constexpr std::string_view kCodeDetectorUnavailable = "POLICY-CLL-DETECTOR-UNAVAILABLE";
/// 评估内部异常/非有限实测值（§6.3 框架异常捕获；§7.4 NaN/±Inf 行）。
constexpr std::string_view kCodeEvaluationFailed = "POLICY-CLL-EVALUATION-FAILED";
/// 作用域对象几何缺口（告知性——§7.5"不输出无碰撞结论字段"，KIN-05）。
constexpr std::string_view kCodeGeometryMissing = "POLICY-CLL-GEOMETRY-MISSING";

/**
 * @brief 评估期诊断记录构造（core::DiagnosticRecord::make 的评估语境包装
 *        ——ERR-01 三要素：稳定码/定位/上下文＋建议动作；比较型三要素在
 *        评估期诊断不适用〔阈值比较的输出载体是 CollisionFinding 的
 *        measuredClearance，非 DiagnosticRecord.comparison〕）。
 *
 * @param code         [in] 建议码（本 TU 顶部四值之一）
 * @param subject      [in] 定位对象（对象级缺口/失败给对象 ID；能力级/
 *                     查询级失败为 nullopt——cause 文本承载对定位）
 * @param runtimeName  [in] 定位对象的运行时名（无对象定位时空串→不回填）
 * @param cause        [in] 原因（非空——工厂校验）
 * @param recommendedAction [in] 建议动作（非空——工厂校验）
 * @return 可持久化诊断记录（C-3 校验通过）
 */
core::DiagnosticRecord makeEvaluationDiagnostic(std::string_view code,
                                                const std::optional<core::ObjectId>& subject,
                                                const std::string& runtimeName,
                                                std::string cause,
                                                std::string recommendedAction)
{
    // context 固定串（同码同上下文——诊断的稳定分类面；文案权威归
    // diagnostics/ui——NFR-REL-05，本串仅为执行语境标注）。
    return core::DiagnosticRecord::make(
        std::string{code}, subject,
        std::nullopt,   // localName：评估输出以 runtimeName 定位（对象 ID 为身份——CON-06）
        runtimeName.empty() ? std::optional<std::string>(std::nullopt)
                            : std::optional<std::string>(runtimeName),
        "碰撞评估执行（CollisionEvaluationSession::evaluate）", std::move(cause),
        std::move(recommendedAction));
}

/**
 * @brief 非有限/任意双精度值的原文格式化（%.17g——round-trip 精确；仅进
 *        诊断 cause，非持久化契约面；snprintf "C" locale 数字格式，无本地
 *        小数点——NFR-COR-02）。NaN/±Inf 经 %.17g 输出 "nan"/"inf"/"-inf"
 *        字面量——§7.4"非有限实测值以原文保留"的承载形式。
 */
std::string formatRawDouble(double v)
{
    char buf[40] = {};
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string{buf};
}

/**
 * @brief 查询契约校验（§6.2 kind↔样本数匹配＋设备自由度一致性——调用方
 *        违约 fail-fast 轨，§9.3 错误类型行"样本数/kind 不符→PolicyError"）。
 *
 * 校验序（固定——确定性）：先 kind 匹配契约（SingleState 恰 1 构型且无
 * 路径参数；PathSequence 构型非空且 pathParameters 等长；SampleSet 构型
 * 非空且无路径参数），后构型维度（＝主链设备自由度——Device::setQ 前置
 * 条件 q.size()==getDOF()，先验拒绝优于后端越界）。
 *
 * @throws PolicyError(PolicyErrorCode::QueryInvalid) 任一匹配契约违约
 */
void validateQueryShape(const CollisionQuery& q, std::size_t deviceDof)
{
    // 第一步：kind 与样本数/路径参数（§6.2 成员语义逐行转写）。
    switch (q.kind) {
    case CollisionQueryKind::SingleState:
        if (q.configurations.size() != 1) {
            throw PolicyError(
                PolicyErrorCode::QueryInvalid,
                "SingleState 查询须恰 1 个构型，实际 " + std::to_string(q.configurations.size())
                    + "（§6.2 configurations 语义）");
        }
        if (!q.pathParameters.empty()) {
            throw PolicyError(PolicyErrorCode::QueryInvalid,
                              "SingleState 查询不得携带路径参数（§6.2 pathParameters 为 "
                              "PathSequence 专属）");
        }
        break;
    case CollisionQueryKind::PathSequence:
        if (q.configurations.empty()) {
            // POL-EXC-1"空构型序列（违约）"——空序列无路径语义可言，fail-fast。
            throw PolicyError(PolicyErrorCode::QueryInvalid,
                              "PathSequence 查询构型序列为空（§6.2——空构型序列属调用方违约）");
        }
        if (q.pathParameters.size() != q.configurations.size()) {
            throw PolicyError(
                PolicyErrorCode::QueryInvalid,
                "PathSequence 查询 pathParameters 与 configurations 等长（§6.2）: path="
                    + std::to_string(q.pathParameters.size()) + ", configs="
                    + std::to_string(q.configurations.size()));
        }
        break;
    case CollisionQueryKind::SampleSet:
        if (q.configurations.empty()) {
            throw PolicyError(PolicyErrorCode::QueryInvalid,
                              "SampleSet 查询构型序列为空（§6.2——空构型序列属调用方违约）");
        }
        if (!q.pathParameters.empty()) {
            throw PolicyError(PolicyErrorCode::QueryInvalid,
                              "SampleSet 查询不得携带路径参数（§6.2——样本独立无路径语义）");
        }
        break;
    }
    // 第二步：构型维度＝设备自由度（Device::setQ 前置条件的先验校验——
    // 维度不符不是环境错误而是装配/构造错误，fail-fast 优于后端异常轨）。
    for (std::size_t i = 0; i < q.configurations.size(); ++i) {
        if (q.configurations[i].size() != deviceDof) {
            throw PolicyError(
                PolicyErrorCode::QueryInvalid,
                "构型维度与主链设备自由度不符（§6.2 configurations=设备 Q，单位 rad）: configs["
                    + std::to_string(i) + "].size=" + std::to_string(q.configurations[i].size())
                    + ", deviceDOF=" + std::to_string(deviceDof));
        }
    }
}

}  // namespace

// =====================================================================
// 评估半区装配（构造函数末段调用——声明见 CollisionEvaluator.hpp）。
// =====================================================================

void CollisionEvaluationSession::buildEvaluationHalf(const IPolicyNameContext& names)
{
    // ---- ②（先行）：后端查询互斥缺省自建 ----
    // 共享形态由装配方（评估器）经构造参数传入（跨会话安全）；缺省自建
    // 覆盖单会话形态（直构会话/测试）——两种形态下"同一后端实例的全部
    // 访问经同一把锁"的不变式都成立。
    if (!m_backendQueryMutex) {
        m_backendQueryMutex = std::make_shared<std::mutex>();
    }

    // ---- ①：主链设备固化（evaluate 的 setState 消费）----
    // POL-T06 第三步已验证 primaryDevice 可解析到 workcell 内设备——此处
    // 重新解析并持有 Device 指针（防御复检同码：解析链与构建期事实不一
    // 致＝装配违约面，宁可失败也不在评估期裸奔）。
    const std::optional<std::string> deviceName = names.tryRuntimeName(m_scene.primaryDevice);
    if (!deviceName.has_value()) {
        throw PolicyError(PolicyErrorCode::NameUnresolved,
                          "评估半区装配：primaryDevice 经名称上下文不可解析（§7.5——"
                          "不猜测，ARC-04; obj=" + m_scene.primaryDevice.toCanonical() + "）");
    }
    m_device = m_scene.workcell->findDevice(*deviceName);
    if (m_device.isNull()) {
        throw PolicyError(PolicyErrorCode::NameUnresolved,
                          "评估半区装配：primaryDevice 解析名在 workcell 内无对应设备"
                          "（§7.5 名称不可解析——场景 Frame↔对象 ID 断链）: name='"
                              + *deviceName + "'");
    }

    // ---- ③：作用域内对象固化（规范序＝objectKey 字节升序）----
    // 对象集＝作用域展开产物中必检/域默认对的端点（被过滤对不经评估——
    // 其留痕由 scope 直接导出，无需 Frame/几何）。
    std::set<core::ObjectId> inScopeObjects;
    for (const ScopedCollisionPair& p : m_scope.pairs) {
        if (p.status == ScopePairStatus::Mandatory
            || p.status == ScopePairStatus::InScopeDefault) {
            inScopeObjects.insert(p.objectA);
            inScopeObjects.insert(p.objectB);
        }
    }
    // 场景条目索引（声明几何事实源——§6.1 场景事实，调用方装配）。
    std::map<core::ObjectId, const SceneObjectEntry*> sceneEntries;
    for (const SceneObjectEntry& entry : m_scene.objects) {
        sceneEntries.emplace(entry.objectId, &entry);
    }
    // 编译产物 Object 索引（几何承载——rw 以 Object 持有几何并挂接
    // Frame；按基帧指针匹配，不按名称猜配——ARC-04）。
    std::map<const rw::kinematics::Frame*, rw::core::Ptr<rw::models::Object>> objectByFrame;
    for (const rw::core::Ptr<rw::models::Object>& obj : m_scene.workcell->getObjects()) {
        if (obj->getBase() != nullptr) {
            // getBase() 返回裸 Frame*（RW_USE_PTR 未定义形态——Object.hpp
            // 条件编译；构建树实测无此宏）——直接作键。
            objectByFrame.emplace(obj->getBase(), obj);
        }
    }

    for (const core::ObjectId& id : inScopeObjects) {
        SceneObjectEvaluation record;
        record.object = id;
        // 声明几何事实（场景事实——KIN-05 coverage 缺口的声明侧来源）。
        const auto entryIt = sceneEntries.find(id);
        record.declaredGeometry =
            entryIt != sceneEntries.end() && entryIt->second->hasCollisionGeometry;
        // 名称解析＋Frame 定位（两级——§7.5"名称不可解析"行：映射缺失与
        // 编译产物断链同码拒绝；不猜测——ARC-04）。
        const std::optional<std::string> runtimeName = names.tryRuntimeName(id);
        if (!runtimeName.has_value()) {
            throw PolicyError(PolicyErrorCode::NameUnresolved,
                              "评估半区装配：作用域对象经名称上下文不可解析（§7.5——"
                              "不猜测，ARC-04; obj=" + id.toCanonical() + "）");
        }
        record.runtimeName = *runtimeName;
        rw::kinematics::Frame* frame = m_scene.workcell->findFrame(*runtimeName);
        if (frame == nullptr) {
            throw PolicyError(PolicyErrorCode::NameUnresolved,
                              "评估半区装配：作用域对象解析名在 workcell 内无对应 Frame"
                              "（§7.5 场景 Frame↔对象 ID 断链）: name='" + *runtimeName
                                  + "'（obj=" + id.toCanonical() + "）");
        }
        record.frame = frame;
        // 几何注册（声明有几何才尝试；全程持互斥——共享后端实例的构建期
        // 注册与其他会话的评估查询串行化）。注册核对以 hasModel 为准——
        // "声明有几何"而产物无 Object/无几何 → registeredGeometry=false：
        // 评估期以 coverage 缺口＋POLICY-CLL-GEOMETRY-MISSING 显式化
        // （§7.5 几何缺失行——不伪装已检、不输出无碰撞结论，KIN-05），
        // 不在此 fail-fast（几何缺失是数据覆盖事实，非装配异常）。
        record.registeredGeometry = false;
        if (record.declaredGeometry) {
            std::lock_guard<std::mutex> lock(*m_backendQueryMutex);
            const auto objIt = objectByFrame.find(frame);
            if (objIt != objectByFrame.end() && !m_strategy->hasModel(frame)) {
                // 幂等注册（hasModel 先核对——同一后端实例可能已被同 Frame
                // 的其他会话注册；重复 addModel 会叠加几何，破坏确定性）。
                m_strategy->addModel(objIt->second);
            }
            record.registeredGeometry = m_strategy->hasModel(frame);
        }
        m_evaluationObjects.push_back(std::move(record));
    }

    // 对象→固化记录下标（对记录以下标引用端点——值语义，拷贝安全）。
    std::map<core::ObjectId, std::size_t> objectIndex;
    for (std::size_t i = 0; i < m_evaluationObjects.size(); ++i) {
        objectIndex.emplace(m_evaluationObjects[i].object, i);
    }

    // ---- ④：作用域对固化（仅必检/域默认对；保持 scope 规范相对序）----
    for (const ScopedCollisionPair& p : m_scope.pairs) {
        if (p.status != ScopePairStatus::Mandatory
            && p.status != ScopePairStatus::InScopeDefault) {
            continue;   // 被过滤对不经评估（§7.2②——跳过并留痕，留痕见 evaluate 预填）
        }
        ScopedPairEvaluation record;
        record.objectA = p.objectA;
        record.objectB = p.objectB;
        const std::size_t indexA = objectIndex.at(p.objectA);
        const std::size_t indexB = objectIndex.at(p.objectB);
        record.endAIndex = indexA;
        record.endBIndex = indexB;
        record.runtimeNameA = m_evaluationObjects[indexA].runtimeName;
        record.runtimeNameB = m_evaluationObjects[indexB].runtimeName;
        record.frameA = m_evaluationObjects[indexA].frame;
        record.frameB = m_evaluationObjects[indexB].frame;
        record.bothEndsGeometry =
            m_evaluationObjects[indexA].registeredGeometry
            && m_evaluationObjects[indexB].registeredGeometry;
        // 发现级别（§6.2 level 语义）：必检对＝其规则的 authored 级别；
        // 域默认对＝Must（§6.2"无规则命中=域默认 Must"）。
        record.level = (p.status == ScopePairStatus::Mandatory)
                           ? m_policy.collision.mandatoryPairs.at(p.ruleIndex).level
                           : PolicyRuleLevel::Must;
        m_evaluationPairs.push_back(std::move(record));
    }

    // ---- ⑤：距离能力探测（P-POL-11 保守口径的判定源）----
    // 注入后端若同时实现 DistanceStrategy（rw 多接口策略惯用法——
    // 策略类可同时继承碰撞与距离接口），间距检查/最小距离查询可用；否则
    // 显式不可用（需要距离的评估 → Failed＋POLICY-CLL-DETECTOR-UNAVAILABLE
    // ——§6.3"检测器不可用"触发器，KIN-05：不伪造、不静默收窄）。
    // 现内置后端（ProximityStrategyRW）为二值碰撞策略——探测为无能力；
    // WP-24 冻结 NFR-DEP-05 后端基线后如换装距离能力后端，本探测自适应，
    // evaluate 语义零变化（单元卡 §15.3 P-POL-11 登记待裁决）。
    m_distanceStrategy = dynamic_cast<rw::proximity::DistanceStrategy*>(m_strategy.get());
    m_distanceCapable = m_distanceStrategy != nullptr;
}

// =====================================================================
// evaluate——评估执行序（§6.3/§6.4/§6.6/§7.4；契约表见 CollisionEvaluator.hpp）。
// =====================================================================

CollisionEvaluation
CollisionEvaluationSession::evaluate(const CollisionQuery& q, const IPolicyCallContext& ctx) const
{
    // ---- ① 查询契约校验（唯一抛出点——调用方违约 fail-fast，§9.3）----
    // 设备在评估半区装配恒非空（装配失败即无会话）；防御性判空仅为免于
    // 假设（会话若以空设备存在则自由度按 0 处理——构型维度校验将拒绝）。
    validateQueryShape(q, m_device.isNull() ? std::size_t{0} : m_device->getDOF());

    // ---- 输出基座预填（全部出口共用——身份绑定＋静态覆盖事实，确定性）----
    CollisionEvaluation out;
    // 缺省态＝Failed（非终态）：任何提前出口不得虚标 Completed——逐出口显式覆写。
    out.status = CollisionEvaluationStatus::Failed;
    out.applicability = ScopeApplicability::Applicable;
    out.finalized = false;
    // 判定依据绑定（§8.4 素材——policy 只供给，任务级绑定归调用方/evidence）。
    out.policyContentIdentity = m_policy.contentIdentity;
    out.sceneIdentity = m_scene.sceneContentIdentity;
    out.nameMapIdentity = m_nameMapIdentity;
    // 静态覆盖事实（§6.2 计数语义——作用域展开产物的直接投影；pairsEvaluated
    // 随评估推进如实累计，其余三计数为会话静态事实，Failed/Canceled 出口
    // 也如实保留——"没检＝无碰撞"的防伪由显式计数承担，KIN-05）。
    out.coverage.pairsInScope = static_cast<std::uint64_t>(m_scope.inScopePairCount());
    out.coverage.pairsExcludedByRule = static_cast<std::uint64_t>(m_scope.excludedPairCount());
    for (const ScopedPairEvaluation& p : m_evaluationPairs) {
        if (p.bothEndsGeometry) {
            ++out.coverage.pairsWithGeometry;
        }
    }
    // 被过滤关系全量留痕（§7.2③——不因首个短路；规范序由 scope 保证；
    // ruleOrigin＝§7.2②"规则引用＋理由"——显式排除对引用策略承载槽位并
    // 附理由原文，默认相邻过滤为冻结字面量）。
    for (const ScopedCollisionPair& p : m_scope.pairs) {
        if (p.status == ScopePairStatus::ExcludedByRule) {
            AppliedFilterRecord record;
            record.objectA = p.objectA;
            record.objectB = p.objectB;
            record.ruleOrigin = "policy.excludedPairs[" + std::to_string(p.ruleIndex)
                                    + "].reason=" + p.filterReason;
            out.appliedFilters.push_back(std::move(record));
        }
        else if (p.status == ScopePairStatus::ExcludedByAdjacencyDefault) {
            AppliedFilterRecord record;
            record.objectA = p.objectA;
            record.objectB = p.objectB;
            record.ruleOrigin = "default.adjacent-links";
            out.appliedFilters.push_back(std::move(record));
        }
    }

    // ---- ② 迟到调用拒绝（§6.1/POL-LATE-1——输出永不入正式证据）----
    if (!ctx.alive()) {
        out.diagnostics.push_back(makeEvaluationDiagnostic(
            kCodeContextExpired, std::nullopt, std::string{},
            "调用上下文已失效（快照/运行已结束——迟到调用拒绝，POL-LATE-1）",
            "重新发起携带存活上下文的评估；失效上下文的输出不得进入正式证据"));
        return out;   // status=Failed，finalized=false
    }

    // ---- ③ 适用性判定（§6.2 ScopeApplicability——两者皆非"无碰撞"结论）----
    if (!m_collisionEnabled) {
        // 策略禁用被误调用（§4.3 总开关）——如实应答，finalized=true
        // （"禁用"本身是已完成评估的确定性事实；消费方据 applicability
        // 与空 coverage 识别，不得读作"无碰撞"）。
        out.status = CollisionEvaluationStatus::Completed;
        out.applicability = ScopeApplicability::CollisionDisabledByPolicy;
        out.finalized = true;
        return out;
    }
    if (m_evaluationPairs.empty()) {
        // 作用域解析为空（域全禁/场景无对象/仅剩被过滤对）——§7.1 空作用
        // 域行：EmptyScope≠"无碰撞"，KIN-05 口径由 coverage（全零在检
        // 计数）表达。被过滤对的留痕（appliedFilters）仍全量输出。
        out.status = CollisionEvaluationStatus::Completed;
        out.applicability = ScopeApplicability::EmptyScope;
        out.finalized = true;
        return out;
    }

    // ---- ④ 距离能力核对（§6.3"检测器不可用"→Failed；KIN-05/P-POL-11）----
    // 间距检查的激活条件＝策略 safetyClearance＞0（发布门保证 enabled 时
    // 必有值，§5.2 行 5——nullopt 分支为防御；m=0 时间距检查退化为空，
    // §7.4"m＝0"行）。距离查询的激活条件＝间距检查激活或调用方显式请求
    // 最小距离（§6.2 requestMinDistance"间距检查需要时"）。需要而不可得
    // →Failed＋DETECTOR-UNAVAILABLE：碰撞证据缺失归 DataInsufficient
    // （消费方口径），绝不静默降级为"仅碰撞"输出。
    const bool hasClearance = m_policy.collision.safetyClearance.has_value();
    const double clearance = hasClearance ? m_policy.collision.safetyClearance->siValue() : 0.0;
    const bool marginNeeded = hasClearance && clearance > 0.0;
    const bool distanceNeeded = marginNeeded || q.requestMinDistance;
    if (distanceNeeded && !m_distanceCapable) {
        out.diagnostics.push_back(makeEvaluationDiagnostic(
            kCodeDetectorUnavailable, std::nullopt, std::string{},
            "注入后端无距离查询能力（DistanceStrategy 探测为否——内置二值碰撞后端；"
            "间距检查" + std::string{marginNeeded ? "激活（safetyClearance＞0）" : "未激活"}
                + "、最小距离请求" + std::string{q.requestMinDistance ? "是" : "否"}
                + "——§6.3 检测器不可用/P-POL-11 登记口径）",
            "以距离能力后端重建会话（待 WP-24 冻结 NFR-DEP-05 后端基线）或将查询改至"
            "不依赖间距/最小距离的口径；本输出不入正式证据（KIN-05：缺检测器≠无碰撞）"));
        return out;   // status=Failed，finalized=false
    }

    // ---- ⑤ 样本循环（§6.6 时序）----
    if (q.requestMinDistance) {
        out.minDistances = std::vector<SampleDistanceSummary>{};
    }
    // 查询缓存（每次评估独立持有——并发 evaluate 互不共享；碰撞与距离分
    // 开两只：ProximityStrategyData 的缓存槽单所有者，混用会反复重建）。
    rw::proximity::ProximityStrategyData collisionData;
    rw::proximity::ProximityStrategyData distanceData;
    // 逐对"已查询"标记（coverage.pairsEvaluated＝实际被后端查询过的对数
    // ——至少查询一次即计入；stopAtFirstFinding/中途失败时如实反映）。
    std::vector<char> pairQueried(m_evaluationPairs.size(), 0);
    // 几何缺口对象的告知性诊断去重集（每对象每次评估至多一条——下标集）。
    std::set<std::size_t> geometryMissingReported;

    const std::size_t sampleCount = q.configurations.size();
    for (std::size_t sample = 0; sample < sampleCount; ++sample) {
        // 样本边界：协作取消检查点（§6.3 Canceled 行/TASK-02/UX-03）——
        // 取消≠失败：部分 findings 保留、无错误诊断、finalized=false。
        if (ctx.cancellationRequested()) {
            out.status = CollisionEvaluationStatus::Canceled;
            return out;
        }

        try {
            // 基准态克隆→setState（§6.6：构型单位 rad；缺省基准态＝编译
            // 产物默认态——§6.2 baseState"缺省=场景基准状态"）。
            rw::kinematics::State state = q.baseState.has_value()
                                              ? *q.baseState
                                              : m_scene.workcell->getDefaultState();
            m_device->setQ(q.configurations[sample], state);

            // 逐对评估（遍历序＝作用域展开产物规范序——确定性；输出顺序
            // 因此与生成序一致，最终仍经稳定排序兜底，§6.4）。
            std::optional<double> sampleBestDistance;
            std::optional<core::ObjectId> sampleBestA;
            std::optional<core::ObjectId> sampleBestB;
            bool sampleHasFinding = false;

            for (std::size_t pairIndex = 0; pairIndex < m_evaluationPairs.size(); ++pairIndex) {
                const ScopedPairEvaluation& pair = m_evaluationPairs[pairIndex];
                const SceneObjectEvaluation& endA = m_evaluationObjects[pair.endAIndex];
                const SceneObjectEvaluation& endB = m_evaluationObjects[pair.endBIndex];

                // 几何缺口对：跳过（coverage.pairsWithGeometry 已显式缺口
                // ——"没检≠无碰撞"由计数承担）；每对象至多一条告知性诊断
                // （KIN-05：缺口是数据事实不是错误——不改变评估状态）。
                if (!pair.bothEndsGeometry) {
                    for (const std::size_t end : {pair.endAIndex, pair.endBIndex}) {
                        const SceneObjectEvaluation& missing = m_evaluationObjects[end];
                        if (missing.registeredGeometry
                            || geometryMissingReported.find(end)
                                   != geometryMissingReported.end()) {
                            continue;   // 该端有几何（对级缺口的另一端）或已报告过
                        }
                        geometryMissingReported.insert(end);
                        out.diagnostics.push_back(makeEvaluationDiagnostic(
                            kCodeGeometryMissing, missing.object, missing.runtimeName,
                            std::string{"作用域对象无有效碰撞几何（声明="}
                                + (missing.declaredGeometry ? "有" : "无")
                                + "，编译产物注册核对=" + (missing.declaredGeometry ? "无" : "-")
                                + "）——该对象参与的对不计入已检（KIN-05）",
                            "补全编译产物碰撞几何或将该对象移出碰撞域后重评；coverage 缺口"
                            "由消费方按数据不足处理"));
                    }
                    continue;
                }

                // 当前样本下两端的世界变换（R_world_base 随编译产物内置
                // ——MDL-22：policy 不做任何二次旋转，直接消费编译事实）。
                const rw::math::Transform3D<> wTa =
                    rw::kinematics::Kinematics::worldTframe(pair.frameA, state);
                const rw::math::Transform3D<> wTb =
                    rw::kinematics::Kinematics::worldTframe(pair.frameB, state);

                // 碰撞检测（后端二值语义——含接触；§7.4"后端报告相交"行）。
                bool colliding = false;
                {
                    std::lock_guard<std::mutex> lock(*m_backendQueryMutex);
                    colliding = m_strategy->inCollision(pair.frameA, wTa, pair.frameB, wTb,
                                                        collisionData);
                }
                if (pairQueried[pairIndex] == 0) {
                    pairQueried[pairIndex] = 1;
                    ++out.coverage.pairsEvaluated;
                }

                if (colliding) {
                    // §7.4 行 4：碰撞发现——penetrationDepth 后端不可提供
                    // ＝显式空（不伪造）；碰撞对不做距离查询（碰撞判定已由
                    // 二值语义给出，穿透深度无二值后端来源）。
                    CollisionFinding finding;
                    finding.objectA = pair.objectA;
                    finding.objectB = pair.objectB;
                    finding.runtimeNameA = pair.runtimeNameA;
                    finding.runtimeNameB = pair.runtimeNameB;
                    finding.sampleIndex = sample;
                    finding.pathParameter = (q.kind == CollisionQueryKind::PathSequence)
                                                ? std::optional<double>(q.pathParameters[sample])
                                                : std::nullopt;
                    finding.kind = CollisionFindingKind::Collision;
                    finding.measuredClearance = std::nullopt;
                    finding.penetrationDepth = std::nullopt;
                    finding.level = pair.level;
                    out.findings.push_back(std::move(finding));
                    sampleHasFinding = true;
                    if (q.stopAtFirstFinding) {
                        break;   // 提前终止（§6.2——筛选/淘汰场景；coverage 如实）
                    }
                }
                else if (marginNeeded || q.requestMinDistance) {
                    // 最小距离查询（d＝SI m；仅非碰撞对——见上）。
                    double measured = 0.0;
                    {
                        std::lock_guard<std::mutex> lock(*m_backendQueryMutex);
                        measured = m_distanceStrategy
                                       ->distance(pair.frameA, wTa, pair.frameB, wTb,
                                                  distanceData)
                                       .distance;
                    }
                    // §7.4 行 5：d 为 NaN/±Inf → 评估失败（非有限实测值以
                    // 原文保留；不伪造 0、不入 findings——NFR-COR-03）。
                    // §7.5 行"NaN/±Inf 实测值"：该对评估 Failed＋诊断——
                    // 评估级 Failed（继续评估将产出部分可信的间距判定）。
                    if (!std::isfinite(measured)) {
                        out.diagnostics.push_back(makeEvaluationDiagnostic(
                            kCodeEvaluationFailed, std::nullopt, std::string{},
                            "实测最小距离非有限（原文=" + formatRawDouble(measured)
                                + "，SI m）: objA=" + pair.objectA.toCanonical()
                                + ", objB=" + pair.objectB.toCanonical() + ", sample="
                                + std::to_string(sample) + "——§7.4/§7.5 不伪造、不入 findings",
                            "排查检测器数值状态与输入构型后重评；失败输出不入正式证据"));
                        out.status = CollisionEvaluationStatus::Failed;
                        return out;   // finalized=false
                    }
                    // §7.4 行 3：0 ≤ d ＜ m → 间距不足（非碰撞）；d＝m 边界
                    // 含于安全侧（D-08 冻结——"安全间距"语义＝d ≥ m 即满足）。
                    if (marginNeeded && measured < clearance) {
                        CollisionFinding finding;
                        finding.objectA = pair.objectA;
                        finding.objectB = pair.objectB;
                        finding.runtimeNameA = pair.runtimeNameA;
                        finding.runtimeNameB = pair.runtimeNameB;
                        finding.sampleIndex = sample;
                        finding.pathParameter =
                            (q.kind == CollisionQueryKind::PathSequence)
                                ? std::optional<double>(q.pathParameters[sample])
                                : std::nullopt;
                        finding.kind = CollisionFindingKind::SafetyMarginViolation;
                        finding.measuredClearance = measured;
                        finding.penetrationDepth = std::nullopt;
                        finding.level = pair.level;
                        out.findings.push_back(std::move(finding));
                        sampleHasFinding = true;
                        if (q.stopAtFirstFinding) {
                            break;
                        }
                    }
                    // 逐样本最小距离追踪（仅 requestMinDistance——摘要承诺
                    // 的是"已实测对"的最小，碰撞对未实测不参与）。
                    if (q.requestMinDistance
                        && (!sampleBestDistance.has_value() || measured < *sampleBestDistance)) {
                        sampleBestDistance = measured;
                        sampleBestA = pair.objectA;
                        sampleBestB = pair.objectB;
                    }
                }
            }

            if (q.requestMinDistance) {
                SampleDistanceSummary summary;
                summary.sampleIndex = sample;
                summary.minDistance = sampleBestDistance;
                summary.nearestToA = sampleBestA;
                summary.nearestToB = sampleBestB;
                out.minDistances->push_back(std::move(summary));
            }
            if (sampleHasFinding && q.stopAtFirstFinding) {
                break;   // 提前终止于样本边界（后续样本不评——coverage 如实）
            }
        }
        // 异常捕获（§6.3：框架异常〔rw::common::Exception 及其他〕在
        // 评估实现内必须捕获并转为 Failed＋诊断——不吞、不崩、异常不跨进
        // 程边界"。捕获点在样本粒度：异常前已产出的 findings 为非终态部分
        // 保留（§6.3 Failed 行）；pol::PolicyError 不可能自此抛出（查询
        // 校验在循环外），std::logic_error 等替身边界违约同样转 Failed）。
        catch (const rw::common::Exception& e) {
            // cause 文案不含框架品牌子串（IRD-GATE-R4 启发式的字面量隔断
            // ——与本单元唯一实现绑定 TU 的零字面量切分同动机；异常类型
            // rw::common::Exception 在代码面自明）。
            out.diagnostics.push_back(makeEvaluationDiagnostic(
                kCodeEvaluationFailed, std::nullopt, std::string{},
                std::string{"基线框架异常（rw::common::Exception，评估样本 "}
                    + std::to_string(sample) + "）: " + e.what(),
                "排查检测器/几何资源与输入构型后重评；失败输出不入正式证据"));
            out.status = CollisionEvaluationStatus::Failed;
            return out;   // finalized=false
        }
        catch (const std::exception& e) {
            out.diagnostics.push_back(makeEvaluationDiagnostic(
                kCodeEvaluationFailed, std::nullopt, std::string{},
                std::string{"评估内部异常（样本 "} + std::to_string(sample) + "）: " + e.what(),
                "排查检测器/几何资源与输入构型后重评；失败输出不入正式证据"));
            out.status = CollisionEvaluationStatus::Failed;
            return out;
        }
        catch (...) {
            out.diagnostics.push_back(makeEvaluationDiagnostic(
                kCodeEvaluationFailed, std::nullopt, std::string{},
                std::string{"评估内部未知异常（样本 "} + std::to_string(sample)
                    + "）——不吞、不崩，转稳定诊断",
                "排查检测器/几何资源与输入构型后重评；失败输出不入正式证据"));
            out.status = CollisionEvaluationStatus::Failed;
            return out;
        }
    }

    // ---- ⑥ 稳定排序＋终态化（§6.4——三入口跨进程逐字节一致，NFR-COR-05/
    //      AT-19）----
    // findings 键序：(sampleIndex 升序, objectA/objectB 对字典序, kind)——
    // 生成序已与键序同向（样本升序×规范对序），stable_sort 为契约兜底
    // （对同键重复元保持生成序——无同键重复的可能，防御性声明）。
    std::stable_sort(out.findings.begin(), out.findings.end(),
                     [](const CollisionFinding& l, const CollisionFinding& r) {
                         if (l.sampleIndex != r.sampleIndex) {
                             return l.sampleIndex < r.sampleIndex;
                         }
                         if (l.objectA != r.objectA) {
                             return l.objectA < r.objectA;
                         }
                         if (l.objectB != r.objectB) {
                             return l.objectB < r.objectB;
                         }
                         return static_cast<int>(l.kind) < static_cast<int>(r.kind);
                     });
    // minDistances 按样本序（生成序即升序——循环序保证）；appliedFilters
    // 按对象对字典序（scope 展开产物规范序保证）——两者无第二排序点。
    out.status = CollisionEvaluationStatus::Completed;
    out.applicability = ScopeApplicability::Applicable;
    out.finalized = true;
    return out;
}

}  // namespace sdurws::ird::policy
