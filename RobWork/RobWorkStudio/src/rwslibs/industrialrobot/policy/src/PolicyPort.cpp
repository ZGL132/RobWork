/**
 * @file   PolicyPort.cpp
 * @brief  ④策略端口唯一实现——PolicyProvider 的取数/解码/解析/记忆化流程
 *         （POL-T05 产物）。
 *
 * 设计依据：
 *   - units/policy.md §9.1（IPolicyProvider 契约表——错误类型行的六项错误
 *     矩阵与记忆化后置条件的唯一权威）、§3.1（PolicyPort.hpp/.cpp 归属）、
 *     §5.1（七段解析管线——本文件经其唯一入口 resolvePolicy 调用，不复制
 *     管线逻辑）、§5.3（PolicyCodec::decode——字节层契约门）、§6.5（装配
 *     线与防旁路）、§12 POL-T05 行（完成条件＝注入接口与端口契约用例通过；
 *     POL-ID-1 缓存一致性）
 *   - 需求 ARC-05（策略单一权威）、CON-05/06（内容编址与内容身份——记忆化
 *     键的确定性前提）、NFR-COR-02/03（确定性；不静默、不吞错）、UX-03/ERR-01
 *     （诊断定位要素：subject 绑定请求对象、cause 携带原文）
 *   - 任务契约 tasks/foundation/POL-T05.json acceptance 1～2（端口契约用例；
 *     缓存一致 POL-ID-1 联动）
 *
 * 实现纪律（与 PolicyPort.hpp 契约注释一一对应，不引入第二口径）：
 *   1. 错误矩阵六项（头文件类注释）逐项落点见 resolvePolicy 各步骤注释——
 *      调用方契约违约 fail-fast 抛 PolicyError；环境/输入条件一律诊断轨
 *      （空 policy＋全量诊断，不抛——§9.1 错误类型行"不抛"）。
 *   2. 解析唯一经 ::resolvePolicy 七段管线（POL-T04 单点——本文件零校验
 *      逻辑重复）；字节解码唯一经 PolicyCodec::decode（§5.3 单点）。
 *   3. 记忆化键＝(ObjectId, ContentVersion)（CON-05 内容编址）；成功与失败
 *      都入表（键下结果为进程内纯函数值——头文件类注释的记忆化语义）。
 *   4. 缓存互斥量只保护查表/入表，解析与取数在锁外执行（不持锁调用外部
 *      依赖——避免锁传播到宿主适配器）。
 *   5. 诊断构造的码面/文案单源（本文件匿名命名空间——与 PolicyParsing.cpp
 *      同款纪律）；诊断码值权威归 diagnostics StableCodeRegistry，本文件
 *      只产出建议码原文（PA-1；POLICY-OBJECT-MISSING 为 §9.6 清单表尾补登
 *      建议值——单元卡 v0.6 登记）。
 *
 * 线程安全：resolvePolicy 为 const＋memo 表经互斥量保护（§9.1 线程行
 * "缓存内部同步"）；并发未命中允许重复计算（解析纯函数——结果逐字段一致，
 * 先入为准）。确定性：无 I/O、无时间/随机源；诊断文本只含规范 id 串与
 * 固定文案（NFR-COR-02）。
 */

#include <sdurws/ird/policy/PolicyPort.hpp>

#include <sdurws/ird/policy/PolicyInput.hpp>

#include <utility>

namespace sdurws::ird::policy {

namespace {

// =====================================================================
// 诊断构造辅助（本文件唯一的 DiagnosticRecord 组装点——码面/文案单源，
// 与 PolicyParsing.cpp 匿名命名空间同款纪律）。
// =====================================================================

/// 端口存储侧条件码（§9.6 清单表尾补登建议值——单元卡 v0.6 登记；码值
/// 权威归 diagnostics StableCodeRegistry，PA-1）。覆盖同一错误矩阵项族：
/// 对象缺失（存储无字节）与版本未指定（不可编址取数）——cause 文案区分。
constexpr std::string_view kObjectMissingCode = "POLICY-OBJECT-MISSING";

/**
 * @brief 组装端口层诊断（subject 恒绑请求对象——ERR-01 稳定诊断绑定对象；
 *        端口层无名称上下文，runtimeName 恒空——会话侧诊断才携带）。
 *
 * @param code    [in] 稳定诊断码（建议码原文——句法 ^[A-Z0-9]+(-[A-Z0-9]+)*$）
 * @param subject [in] 主体对象（＝请求的 policyObject）
 * @param context [in] 上下文描述（非空——core C-3）
 * @param cause   [in] 原因（非空——core C-3；含原文/转发文本）
 * @param action  [in] 建议动作（非空——core C-3）
 */
core::DiagnosticRecord makePortDiag(std::string_view code, core::ObjectId subject,
                                    std::string context, std::string cause,
                                    std::string action)
{
    return core::DiagnosticRecord::make(
        std::string{code}, subject,
        std::nullopt,   // localName：端口层条件定位到对象级，无字段路径可标
        std::nullopt,   // runtimeName：端口层无⑥端口名称上下文（§9.7）
        std::move(context), std::move(cause), std::move(action));
}

/// 端口层诊断的固定上下文/动作文案（确定性——同条件同文案，NFR-COR-02）。
constexpr std::string_view kPortContext =
    "④端口策略解析（PolicyProvider::resolvePolicy）";
constexpr std::string_view kPortAction =
    "核对策略对象身份与内容版本是否来自同一修订闭包；经 project ②端口重新"
    "取得对象引用后重试；字节层码面按对应诊断码处置（编码损坏→核对字节"
    "完整性，版本越代→按升级指引迁移 schema）";

}  // namespace

// =====================================================================
// PolicyProvider——装配构造与三方法实现。
// =====================================================================

PolicyProvider::PolicyProvider(const IPolicyBytesSource& bytesSource,
                               const IPolicyValidationContext& validationContext,
                               std::shared_ptr<ICollisionEvaluator> evaluator,
                               CollisionBackendDescriptor backend)
    : m_bytesSource(&bytesSource)
    , m_validationContext(&validationContext)
    , m_evaluator(std::move(evaluator))
    , m_backend(std::move(backend))
{
    // 构造只存依赖、不校验（头文件契约：装配分步语义下"部分装配"是合法
    // 中间态）；存活期契约（宿主保证依赖存活期覆盖端口）与评估器半区注入
    // 前的使用违约分别在 resolvePolicy/collisionEvaluator 的使用点 fail-fast。
}

PolicyResolution PolicyProvider::resolvePolicy(const PolicyResolutionRequest& request) const
{
    // -----------------------------------------------------------------
    // 第一步：调用方契约复检（fail-fast，不入诊断轨——错误矩阵第 1 项）。
    // 全零保留值不可能是合法编址请求（CON-01 身份包络：对象身份由 project
    // 分配、内容版本由对象字节摘要编址），静默转诊断会掩盖装配侧/调用侧
    // bug——与解析管线对 RawPolicyInput.policyObject 的复检同款纪律（POL-T04）。
    // -----------------------------------------------------------------
    if (!request.policyObject.isValid()) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "resolvePolicy 请求的 policyObject 为全零保留值"
                          "（调用方契约违约——对象身份须由 project 分配）");
    }
    if (request.expectedVersion.has_value() && !request.expectedVersion->isValid()) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "resolvePolicy 请求的 expectedVersion 为全零保留值"
                          "（调用方契约违约——内容版本按对象字节摘要编址，"
                          "全零无编址意义）");
    }

    // -----------------------------------------------------------------
    // 第二步：版本未指定 → 不可编址取数（错误矩阵第 2 项——诊断轨不抛）。
    // ④端口只服务内容编址解析（CON-05）：无版本即无缓存键、无取数地址；
    // 这是可恢复的输入条件（调用方补版本后重试），按 §9.1 错误类型行
    // "版本不符→空 policy＋诊断（不抛）"族处置。注意：本分支无缓存键，
    // 不入 memo 表（每次构造同文案诊断——确定性不受影响）。
    // -----------------------------------------------------------------
    if (!request.expectedVersion.has_value()) {
        return PolicyResolution{
            std::nullopt,
            {makePortDiag(
                kObjectMissingCode, request.policyObject,
                std::string{kPortContext},
                "请求未携带期望内容版本（expectedVersion=nullopt）——④端口仅"
                "服务内容编址解析（CON-05）：调用方须先经 project ②端口取得"
                "策略对象引用 (对象, 内容版本) 再发起请求（§10.1 流程）",
                std::string{kPortAction})}};
    }

    // -----------------------------------------------------------------
    // 第三步：记忆化查取（错误矩阵之外的后置条件主通道——POL-ID-1）。
    // 命中直接返回缓存值：同键重复调用逐字段相等由"同一份缓存值"平凡保证
    // （§9.1 后置行）。持锁范围仅查表（解析在锁外——不持锁调用外部依赖）。
    // -----------------------------------------------------------------
    const CacheKey key{request.policyObject, *request.expectedVersion};
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        const auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            return it->second;
        }
    }

    // -----------------------------------------------------------------
    // 第四步：缓存未命中——取对象字节（错误矩阵第 3 项：对象缺失→诊断轨）。
    // bytesSource 返回 nullopt＝存储侧无该 (对象, 版本) 字节：环境事实而非
    // 异常（对象不存在或版本不在存储闭包内）——空 policy＋诊断，不抛。
    // -----------------------------------------------------------------
    const std::optional<std::vector<std::uint8_t>> bytes =
        m_bytesSource->tryObjectBytes(key.first, key.second);
    if (!bytes.has_value()) {
        PolicyResolution resolution{
            std::nullopt,
            {makePortDiag(
                kObjectMissingCode, key.first,
                std::string{kPortContext},
                "存储侧无该 (对象, 内容版本) 的对象字节：object=" + key.first.toCanonical()
                    + ", version=" + key.second.toCanonical()
                    + "（对象不存在或版本不在存储闭包内——CR-03 适配面应答）",
                std::string{kPortAction})}};
        // 失败结果同样入表（键下结果恒定——内容编址；头文件类注释"成功与
        // 失败都缓存"）。并发重复取数只会多付一次查询，结果逐字段一致。
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache.emplace(key, resolution);
        return resolution;
    }

    // -----------------------------------------------------------------
    // 第五步：解码（错误矩阵第 4 项：字节层条件→同码面诊断转发）。
    // decode 的 PolicyError（EncodingInvalid＝字节损坏；SchemaVersionFuture/
    // Unknown＝字节层版本越代）在此转译为诊断而非传播：它们描述的是"存储
    // 里的字节内容"这一环境事实，不是本调用方（域插件/execution）的契约
    // 违约——端口查询语义下"一个坏对象不崩整轮查询"（UX-08 精神），错误
    // 全文（what()＝"<token>: <detail>"）原样保留进 cause，不吞错
    // （NFR-COR-03）。这也是 §9.1 错误行"POLICY-SCHEMA-* 转发"的字节层
    // 落点：未来/未知代的策略对象在端口层同样得到"空 policy＋POLICY-
    // SCHEMA-* 诊断"，与解析管线的版本门处置语义一致（不前向猜测解析）。
    // -----------------------------------------------------------------
    RawPolicyInput input;
    try {
        input = PolicyCodec::decode(*bytes);
    }
    catch (const PolicyError& e) {
        PolicyResolution resolution{
            std::nullopt,
            {makePortDiag(
                std::string_view{registryCode(e.code())}, key.first,
                std::string{kPortContext},
                "对象字节解码失败，错误原文转发：" + std::string{e.what()}
                    + "（object=" + key.first.toCanonical()
                    + ", version=" + key.second.toCanonical() + "）",
                std::string{kPortAction})}};
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache.emplace(key, resolution);
        return resolution;
    }

    // -----------------------------------------------------------------
    // 第六步：七段解析管线（错误矩阵第 5 项：内容非法→管线全量诊断转发）。
    // 解析为纯函数（POL-T04 契约）：输入非法走诊断轨全量收集不短路——
    // 端口只做转发，不复制任何校验逻辑（单点纪律）；调用方契约违约类异常
    // 在此不可达（解码产物的 policyObject 来自字节、numericContractAnchor/
    // 规则结构由 decode 精确耗尽契约保证——若仍触发即实现缺陷，按第 6 项
    // 语义传播）。
    // sourceVersion 填充：POL-T04 登记的 PA-1 落点——"该字段由④端口
    // Provider 在记忆化键 (policyObject, ContentVersion) 处填充"；端口结果
    // 本体（§9.1 冻结两字段）不携带版本，版本信息经诊断文案定位。
    // -----------------------------------------------------------------
    // 注意：本成员与七段管线自由函数同名（§9.1 与 §5.1 的设计命名），成员名
    // 遮蔽命名空间作用域名字——必须全限定调用解析管线唯一入口（POL-T04 单点）。
    PolicyParseResult parsed = ::sdurws::ird::policy::resolvePolicy(input,
                                                                    *m_validationContext);
    parsed.sourceVersion = key.second;

    // -----------------------------------------------------------------
    // 第七步：组装端口结果并入表（错误矩阵第 1/5 项的成功与失败出口）。
    // EngineeringPolicySet 全 const（赋值编译期删除）——optional 以 emplace
    // 就地构造承载（POL-T04 v0.3 ③登记的消费约束）。
    // -----------------------------------------------------------------
    PolicyResolution resolution;
    if (parsed.policy.has_value()) {
        resolution.policy.emplace(std::move(*parsed.policy));
    }
    resolution.diagnostics = std::move(parsed.diagnostics);

    // 并发未命中：两线程可能同时重算（解析纯函数——结果逐字段一致），
    // 入表先入为准，双方返回值一致（§9.1 线程行"缓存内部同步"的实现口径）。
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_cache.emplace(key, resolution);
    return resolution;
}

ICollisionEvaluator& PolicyProvider::collisionEvaluator() const
{
    // 评估器半区装配复检（§9.1 前置行的 fail-fast 载体——错误码表尾追加
    // PortAssemblyIncomplete 的唯一触发点）：空实例＝宿主装配未完成即对外
    // 服务（装配期遗漏），属调用方契约违约——fail-fast，绝不解引用空指针。
    // 正常装配后每次返回同一实例引用（§9.1 后置行——POL-SHARE-1 实例同一
    // 性断言的基础）。
    if (!m_evaluator) {
        throw PolicyError(PolicyErrorCode::PortAssemblyIncomplete,
                          "collisionEvaluator：评估器半区尚未注入（宿主装配"
                          "契约违约——L5/worker 装配期须经唯一构造入口创建并"
                          "注入 ICollisionEvaluator 后方可对外服务，§6.5）");
    }
    return *m_evaluator;
}

CollisionBackendDescriptor PolicyProvider::collisionBackend() const
{
    // 复现要素按值返回（§9.1 原文签名；值在装配期注入后只读——推荐与
    // evaluator->backend() 同源，保证 sessionIdentity 组成一致，见构造注释）。
    return m_backend;
}

}  // namespace sdurws::ird::policy
