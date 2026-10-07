/**
 * @file   SystemDefaultPolicy.cpp
 * @brief  系统缺省策略集与其 ④ 端口供给器的实现（契约见同名公共头）。
 *
 * 设计依据：
 *   - ARC-05（唯一默认＝附录 D 第 11 项）／PolicyOriginKind::SystemDefault
 *   - findings F-536／DTB §4.2 O-46 裁决（出路②-scope——F-540 无涉，此
 *     处不复制裁决全文，见头文件注释与 DTB 行）
 *
 * 线程约束：函数局部静态（C++11 线程安全初始化）＋不可变值应答——并发
 * 只读安全（头文件契约行）。
 */

#include <sdurws/ird/policy/SystemDefaultPolicy.hpp>

#include <sdurws/ird/policy/Diagnostics.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>

#include <string_view>
#include <utility>

namespace sdurws::ird::policy {

namespace {

// 系统缺省策略保留身份的规范文本（全 1 最低位——非全零、非项目对象；
// 语义见公共头 systemDefaultPolicyObjectId 注释）。
constexpr std::string_view kSystemDefaultPolicyCanonical =
    "obj-00000000000000000000000000000001";

// 系统缺省集的审计备注（origin.note——审计字段不参与内容身份，D-03）。
constexpr std::string_view kSystemDefaultNote =
    "系统缺省策略集（附录 D 第 11 项唯一冻结默认 4π；其余阈值显式不适用"
    "——P-POL-2 未裁决不发明数值；宿主 ④ 端口缺省源，非项目存储对象）";

// 端口层诊断固定文案（确定性——同条件同文案，NFR-COR-02；与
// PolicyPort.cpp 的端口诊断纪律同款：subject 绑请求对象、无名称上下文）。
constexpr std::string_view kPortContext =
    "④端口策略解析（SystemDefaultPolicyProvider::resolvePolicy）";
constexpr std::string_view kPortAction =
    "系统缺省供给器只应答保留身份（systemDefaultPolicyObjectId）；工程内"
    "真实策略对象的解析经存储背书 PolicyProvider（宿主装配换装——"
    "O-46 裁决的生命周期后续）";

/// 端口层诊断组装（与 PolicyPort.cpp makePortDiag 同构——码面/文案单源
/// 纪律在本文件内的对应组装点）。
core::DiagnosticRecord makeSystemDefaultDiag(core::ObjectId subject,
                                             std::string cause)
{
    return core::DiagnosticRecord::make(
        std::string{policyDiagCode(PolicyDiagCode::ObjectMissing)},
        subject,
        std::nullopt,   // localName：端口层条件定位到对象级
        std::nullopt,   // runtimeName：端口层无名称上下文（§9.7）
        std::string{kPortContext}, std::move(cause),
        std::string{kPortAction});
}

}  // namespace

const core::ObjectId& systemDefaultPolicyObjectId()
{
    // 函数局部静态（C++11 线程安全初始化）；解析失败即抛（字面量违约＝
    // 代码缺陷，fail-fast——进程期不可能走到）。
    static const core::ObjectId kId =
        core::ObjectId::fromCanonical(kSystemDefaultPolicyCanonical);
    return kId;
}

EngineeringPolicySet makeSystemDefaultPolicySet()
{
    // ---- 语义闭包块（参与内容身份的四字段——§4.2/§5.3） ----------------
    // 关节阈值：默认成员初始化＝行程上限 4π（DefaultAppendixD，值字段）
    // ＋nearLimitRatio/conditionNumberWarning nullopt（显式不适用，P-POL-2）
    // ＋travelLimitCheckEnabled=true（④校验执行）。零发明数值。
    const JointThresholds thresholds{};

    // 碰撞规则：disabled（碰撞策略无冻结默认——enabled=true 需
    // safetyClearance 会抛 ThresholdRequiredMissing；false＝显式不适用，
    // O-10 保守口径。④行程校验不消费碰撞面）。
    const CollisionRules collision = CollisionRules::make(
        /*enabled=*/false,
        /*enabledDomains=*/{},
        /*safetyClearance=*/std::nullopt,
        /*excludeAdjacentLinksByDefault=*/true,
        /*mandatoryPairs=*/{},
        /*excludedPairs=*/{});

    // 适用范围：空集＝全部模式/对象适用（§4.2.1 空集语义——缺省源对一切
    // 行程相关 apply 生效）。
    const PolicyApplicability applicability{};

    const std::uint32_t schemaVersion = kPolicySchemaVersionCurrent;
    const std::string anchor{std::string_view{kNumericContractAnchorDefault}};

    // 内容身份：对语义闭包现算（§5.3——发布门「调用方不可申报假身份」的
    // 合规满足；同闭包恒同＝CON-05/06）。
    const core::ContentIdentity identity = PolicyCodec::contentIdentity(
        schemaVersion, collision, thresholds, applicability, anchor);

    // ---- 发布门（§4.5——Valid 态＋SystemDefault 来源） ------------------
    return EngineeringPolicySet::make(
        systemDefaultPolicyObjectId(),
        schemaVersion,
        identity,
        collision,
        thresholds,
        applicability,
        PolicyOrigin{PolicyOriginKind::SystemDefault,
                     std::nullopt,
                     std::string{std::string_view{kSystemDefaultNote}}},
        PolicyValidationState::Valid);
}

PolicyResolution SystemDefaultPolicyProvider::resolvePolicy(
    const PolicyResolutionRequest& request) const
{
    // 第一步：调用方契约复检（fail-fast——错误矩阵第 1 项同款纪律；全零
    // 保留值不可能是合法编址请求，静默转诊断会掩盖装配侧 bug）。
    if (!request.policyObject.isValid()) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "policyObject 身份无效（全零保留值）");
    }

    // 第二步：只应答保留身份。系统缺省集为编译期常量（无存储版本演进），
    // expectedVersion 不消费（头文件契约登记的差异面——非错误矩阵偏离的
    // 隐瞒）；其余身份＝本供给器无此对象（POLICY-OBJECT-MISSING，错误矩
    // 阵第 3 格同码面——诊断 subject 绑请求对象）。
    // EngineeringPolicySet 全 const 成员＝拷贝赋值被删除——PolicyResolution
    // 的 optional 槽位以聚合构造（拷贝构造合法）就位，不走赋值。
    if (request.policyObject != systemDefaultPolicyObjectId()) {
        PolicyResolution resolution{
            std::optional<EngineeringPolicySet>(std::nullopt),
            {makeSystemDefaultDiag(
                request.policyObject,
                "系统缺省供给器仅应答保留身份（systemDefaultPolicyObjectId）；"
                "请求对象无存储背书字节（本供给器非存储形态）")}};
        return resolution;
    }

    // 第三步：应答系统缺省集（函数局部静态缓存——同闭包同值，CON-05 下
    // 缓存即重算；发布门异常＝构造常量违约＝代码缺陷，向上传播不吞错）。
    static const EngineeringPolicySet kDefaultSet = makeSystemDefaultPolicySet();
    return PolicyResolution{std::optional<EngineeringPolicySet>(kDefaultSet), {}};

}

ICollisionEvaluator& SystemDefaultPolicyProvider::collisionEvaluator() const
{
    // 解析半区专用（头文件契约）：④行程校验只走 resolvePolicy；碰撞评估
    // 半区未装配——调用即装配缺陷，fail-fast 不静默（PolicyPort.hpp 装配
    // 契约「注入前调用即契约违约」同码面）。
    throw PolicyError(PolicyErrorCode::PortAssemblyIncomplete,
                      "SystemDefaultPolicyProvider 为解析半区专用（碰撞评估"
                      "器未装配——④行程校验不消费碰撞面）");
}

CollisionBackendDescriptor SystemDefaultPolicyProvider::collisionBackend() const
{
    throw PolicyError(PolicyErrorCode::PortAssemblyIncomplete,
                      "SystemDefaultPolicyProvider 为解析半区专用（碰撞后端"
                      "未装配——④行程校验不消费碰撞面）");
}

}  // namespace sdurws::ird::policy
