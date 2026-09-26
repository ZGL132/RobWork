/**
 * @file   Commands.cpp
 * @brief  会话姿态写入口与设默认命令门面的实现（Commands.hpp 契约的
 *         执行面——T08；§9.7"组装载荷＋经①端口提交＋结果回显"）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.2（IKinematicsCommandHandler 两方法执行序与
 *     @pre/@post）、§9.7（D-KIN-5——门面只组装 modeling 命令并提交，处理
 *     器/断言/inverse 归 modeling）、§9.6（KIN-NO-DEVICE/KIN-ROOT-BYTES-
 *     ILLEGAL 产码面——码值经 DiagCodes.hpp 在册常量，禁字符串拼码）、
 *     §9.8（L-K4/L-K9/L-K11 数据流的数据面）
 *   - ARCHITECTURE.md §7.7（会话态零修订/零失效——本 TU 不产生任何写通
 *     道调用）、§7.11（产生修订的命令一律经①命令端口——本 TU 唯一提交
 *     点在 assembleAndSubmit）
 *   - 布局依据（载荷框架字节）：modeling 卡 §9.3 命令载荷 v2 文档化格式
 *     （小端、长度前缀、无填充）——漂移防线见 Commands.hpp 函数注与契约
 *     测试（P-KIN-7 增量同步义务）
 *
 * 线程安全：KinSessionPose 非线程安全（仅 UI 线程）；门面无可变状态。
 * 确定性：框架编码纯位组装（同输入同字节——NFR-COR-02）。
 */

#include <sdurws/ird/kinematics/Commands.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/kinematics/DiagCodes.hpp>  // KIN-* 在册码常量（禁拼码）

namespace sdurws::ird::kinematics {

namespace {

// =====================================================================
// 局部工具：定宽小端写入（modeling 载荷框架 §9.3 布局的兼容编码原语）
// =====================================================================

/// 追加 u32（小端——框架布局"小端、长度前缀、无填充"的字节序面）。
void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

/// 追加长度前缀字节块（u32 长度域＋原始字节——ASCII 文本与对象字节共用）。
void putBytes(std::vector<std::uint8_t>& out, const std::uint8_t* data, std::size_t n)
{
    putU32(out, static_cast<std::uint32_t>(n));
    out.insert(out.end(), data, data + n);
}

/// 追加长度前缀文本（UTF-8/ASCII 原样——不做任何字符变换）。
void putString(std::vector<std::uint8_t>& out, std::string_view s)
{
    putBytes(out, reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

// =====================================================================
// 局部工具：门面自产诊断（码值经 DiagCodes.hpp 在册常量——禁拼码；
// Evaluators.cpp makeKinDiag 同款形态，单元内局部不跨 TU 复用）
// =====================================================================

/// 基础诊断组装（subject 仅在语义对象存在时携带——不伪造；C-3 校验在
/// core::DiagnosticRecord::make 内执行，字段缺失即抛＝实现缺陷显性化）。
core::DiagnosticRecord makeKinDiag(std::string_view code,
                                   std::optional<core::ObjectId> subject,
                                   const char* context,
                                   const std::string& cause,
                                   const char* recommendedAction)
{
    return core::DiagnosticRecord::make(std::string(code), std::move(subject),
                                        std::nullopt,   // localName 不伪造（R-4 名称归⑥端口）
                                        std::nullopt,   // runtimeName——⑥端口消费随名称面任务
                                        context, cause, recommendedAction);
}

/// 无设备根诊断（KIN-NO-DEVICE——闭包无 robot-design 根，设默认无意义）。
core::DiagnosticRecord noDeviceDiag()
{
    return makeKinDiag(kKinNoDevice,
                       std::nullopt,  // 无根对象可指——subject 缺席（不伪造身份）
                       "设默认 TCP/设备（KIN-14 命令门面）",
                       "项目闭包无 robot-design 根对象（无可用设备）",
                       "先经建模命令创建设备模型后再设默认");
}

/// 根字节补丁失败诊断（KIN-ROOT-BYTES-ILLEGAL——数据侧，零修订）。
core::DiagnosticRecord rootBytesIllegalDiag(const core::ObjectId& root)
{
    return makeKinDiag(kKinRootBytesIllegal, root,
                       "设默认 TCP（KIN-14 命令门面）",
                       "根对象字节补丁失败（基线根不可解码或补丁产物编码失败"
                       "——数据侧错误，本次提交未发出）",
                       "核对项目存储完整性；如项目可打开请重新编辑设备后重试");
}

// =====================================================================
// 局部工具：有限性校验（NFR-COR-03——非有限拒绝入会话态，不钳制不置零）
// =====================================================================

/// 全部分量有限的判定（空向量恒 true——"未含任何数值"不含非有限事实）。
bool allFinite(const std::vector<double>& q)
{
    return std::all_of(q.begin(), q.end(),
                       [](double v) {
                           return std::isfinite(v);
                       });
}

}  // namespace

// =====================================================================
// KinSessionPose——会话姿态写入口（L-K4；结构保证见头文件类注）
// =====================================================================

void KinSessionPose::setJointConfiguration(std::vector<double> q)
{
    // 非有限拒绝（NFR-COR-03）：会话态是 UI 显示与回填默认值的来源，非
    // 有限值一旦入态会沿"以当前 TCP 为目标"回填（L-K5）污染目标编辑器
    // ——在写入口 fail-fast（调用方错误轨），不入态不钳制。
    if (!allFinite(q)) {
        throw std::invalid_argument(
            "KinSessionPose：会话姿态含非有限分量（rad/m——NaN/±∞ 拒绝入态）");
    }
    m_q = std::move(q);
    m_set = true;
}

void KinSessionPose::resetToHome(const std::vector<double>& homeQ)
{
    // 复位 Home＝一次显式写入（L-K4 第三入口的分立命名——行为等价
    // setJointConfiguration；Home 值由调用方从命名位姿集保留键读取，
    // 本单元不解析位姿集对象——MDL-17/V-27 建模侧同则）。
    setJointConfiguration(homeQ);
}

void KinSessionPose::clear()
{
    m_q.clear();
    m_set = false;
}

// =====================================================================
// encodeKinApplyRobotDesignPayload——载荷框架编码（v2 布局——头文件函数
// 注 @code 块为权威布局图；漂移防线＝契约测试独立解码器＋金标字节）
// =====================================================================

std::vector<std::uint8_t> encodeKinApplyRobotDesignPayload(
    const core::ObjectId& rootObjectId, const std::vector<std::uint8_t>& rootObjectBytes)
{
    // 保留容量（magic 7＋版本/模式/计数 12＋槽头与三长度域＋removals 计
    // 数——避免中段扩容重排；纯性能语义，不影响布局）。
    std::vector<std::uint8_t> out;
    out.reserve(64 + rootObjectBytes.size());

    // ---- 段 1：magic（7 字节 ASCII "IRDMCP2"——版本尾数与载荷版本同步，
    //      modeling v2 框架标识；v1 拒收＝NFR-DEP-04 的对端语义）----
    const std::uint8_t magic[] = {'I', 'R', 'D', 'M', 'C', 'P', '2'};
    out.insert(out.end(), magic, magic + sizeof(magic));

    // ---- 段 2：载荷格式版本（kModelingCommandPayloadVersion＝2——值面
    //      权威 modeling kCommandPayloadVersion，测试金标钉住）----
    putU32(out, kModelingCommandPayloadVersion);

    // ---- 段 3：模式（0＝Apply 正向应用；Restore=1 是①端口撤销/重做逆
    //      放专用形态，门面恒正向——§6.9 逆命令组装归 modeling）----
    putU32(out, 0U);

    // ---- 段 4：对象槽数（设默认增量＝恰好一个根槽——§9.7"单根槽增量"）----
    putU32(out, 1U);

    // ---- 段 5：对象槽（allocateNew=0 既有对象替换；oid/token/字节三段
    //      长度前缀——ObjectId 规范文本与 modeling 载荷逐字节兼容）----
    out.push_back(0U);  // allocateNew = false（根对象身份稳定，非新建）
    putString(out, rootObjectId.toCanonical());
    putString(out, kRobotDesignObjectTypeToken);
    putBytes(out, rootObjectBytes.data(), rootObjectBytes.size());

    // ---- 段 6：removals 空段（v2 新增尾段——设默认零引用移除；缺失该
    //      段的载荷会被 modeling v2 解码器拒收，故空段计数必须显式写出）----
    putU32(out, 0U);

    return out;
}

// =====================================================================
// KinematicsCommandHandler——门面实现（执行序见头文件接口注）
// =====================================================================

KinematicsCommandHandler::KinematicsCommandHandler(IKinProjectCommandGateway* gateway,
                                                   RootDesignPatcher patcher)
    : m_gateway(gateway), m_patcher(std::move(patcher))
{
    // 装配违约 fail-fast：网关为空＝构造出不可用对象（两方法首步即崩溃
    // 面），构造期拒绝优于使用期崩溃（Collision.hpp 空会话同款取舍）。
    if (m_gateway == nullptr) {
        throw std::invalid_argument("KinematicsCommandHandler：网关未注入（装配违约）");
    }
}

CommandSubmission KinematicsCommandHandler::setProjectDefaultTcp(const TcpRef& tcp)
{
    // ---- 步骤 1：补丁缝装配核查（缺依赖到达即装配违约——fail-fast；
    //      设备路径不消费补丁，故只在本方法前置）----
    if (!m_patcher) {
        throw std::invalid_argument(
            "KinematicsCommandHandler：根对象补丁缝未装配（setProjectDefaultTcp "
            "需要 RootDesignPatcher——装配违约）");
    }

    // ---- 步骤 2：基线事实取数（②端口投影）——闭包无设备根＝"无可用
    //      设备"业务出口（KIN-NO-DEVICE），零提交零修订。
    const auto baseline = m_gateway->fetchBaseline();
    if (!baseline.ok()) {
        CommandSubmission out;  // 缺省 NotCommitted——零修订安全缺省
        out.diagnostics.push_back(noDeviceDiag());
        return out;
    }

    // ---- 步骤 3：根身份在场核查（防御面——适配器契约中 rootObjectId
    //      缺失与"无设备"同语义轨；ok 态携空根视为取数面数据违约，不
    //      静默组装空槽）----
    if (!baseline.get().rootObjectId.has_value()) {
        CommandSubmission out;
        out.diagnostics.push_back(noDeviceDiag());
        return out;
    }

    // ---- 步骤 4：内容字节补丁（modeling 编解码权威经注入缝执行）——
    //      nullopt＝基线根不可解码/补丁产物编码失败（数据侧错误），
    //      KIN-ROOT-BYTES-ILLEGAL 诊断，零提交零修订。
    const std::optional<std::vector<std::uint8_t>> patched =
        m_patcher(baseline.get().rootObjectBytes, tcp);
    if (!patched.has_value()) {
        CommandSubmission out;
        out.diagnostics.push_back(rootBytesIllegalDiag(*baseline.get().rootObjectId));
        return out;
    }

    // ---- 步骤 5：组装信封并经①端口提交（回显透传——Committed 携带新
    //      修订；NotCommitted 透传对端诊断如 PRJ-* 只读/过期门卫码与
    //      MDL-* 断言定位码）。提交失败亦零修订（①端口四态非 Committed
    //      恒零修订——project §5.3.1 后置）。
    return assembleAndSubmit(baseline.get(), std::move(*patched));
}

CommandSubmission KinematicsCommandHandler::setProjectDefaultDevice(
    const core::ObjectId& robotOid)
{
    // ---- 基线事实取数（无设备根→KIN-NO-DEVICE，零提交——同 TCP 路径
    //      步骤 2/3 的语义轨）----
    const auto baseline = m_gateway->fetchBaseline();
    if (!baseline.ok() || !baseline.get().rootObjectId.has_value()) {
        CommandSubmission out;
        out.diagnostics.push_back(noDeviceDiag());
        return out;
    }

    // ---- 设备身份核对（R1 单设备模型：项目设备＝robot-design 根本身；
    //      传非根身份＝调用方契约违约——编程错误 fail-fast，无业务出口，
    //      不产诊断不产提交。语义登记见头文件方法注/单元卡 §14.6 v0.8）----
    if (!(robotOid == *baseline.get().rootObjectId)) {
        throw std::invalid_argument(
            "KinematicsCommandHandler：setProjectDefaultDevice 的对象不是本项目"
            " robot-design 根（R1 每分支恰一个设备根——跨项目/部件对象误传）");
    }

    // ---- 组装并提交（槽字节＝基线根字节原样——设备指定修订零内容变更，
    //      既有引用零触碰：KIN-14"不破坏既有引用"的平凡成立面＋modeling
    //      断言双保险）----
    return assembleAndSubmit(baseline.get(), baseline.get().rootObjectBytes);
}

CommandSubmission KinematicsCommandHandler::assembleAndSubmit(
    const KinProjectBaseline& baseline, std::vector<std::uint8_t> rootObjectBytes)
{
    // ---- 信封组装（载荷三元组的门面取值——token/版本/框架编码的唯一
    //      书写点；expectedRevision＝基线 tip，过期由①端口 S2 拒绝）----
    KinCommandEnvelope envelope;
    envelope.branch = baseline.branch;
    envelope.expectedRevision = baseline.tipRevision;
    envelope.commandType.assign(kFacadedModelingCommand);
    envelope.payloadFormatVersion = kModelingCommandPayloadVersion;
    envelope.payloadCanonical =
        encodeKinApplyRobotDesignPayload(*baseline.rootObjectId, rootObjectBytes);

    // ---- ①端口提交（两态回显**原样透传**——细分理由、新修订与诊断
    //      都在对端语义内；本方法不追加不改写不拆包重组对端结果，PA-1
    //      权威纪律。非 Committed 态恒零修订＝①端口后置的透传保证）----
    return m_gateway->submit(envelope);
}

}  // namespace sdurws::ird::kinematics
