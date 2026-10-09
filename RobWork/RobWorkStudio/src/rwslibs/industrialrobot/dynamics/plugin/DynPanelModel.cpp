/**
 * @file   DynPanelModel.cpp
 * @brief  dynamics 插件面板模型层流的实现翻译单元（L-D1~L-D6——零 Qt、
 *         零动力学计算：全部为数据重组、词表受理、缝归一与查表转发）。
 *
 * 设计依据：DynPanelModel.hpp 文件头（流清单与语义锚）；
 * 单元卡 §9.5 红线在本 TU 的执行面：零评估器/统计器消费、零排序/归约
 * 算法符号（契约测试全文词表扫描钉住）；错误语义两轨见各函数注。
 */

#include "DynPanelModel.hpp"

#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::dynamics {

namespace {

/// 通道键冻结词表（DynChannelId 枚举序——dynChannelKey 的查表源；
/// 小写连字符形态与 §3.5 键词形一致，改词即呈现契约破坏）。
constexpr const char* kChannelKeys[kDynChannelCount] = {
    "position", "velocity", "acceleration", "generalized-force",
    "mechanical-power",
};

}  // namespace

// =====================================================================
// 曲线通道词表（头内函数的实现——查表＋判型，零计算）。
// =====================================================================

std::string dynChannelKey(DynChannelId id)
{
    // 词表越界＝调用方错误（程序缺陷非用户输入——fail-fast 不静默）。
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kDynChannelCount) {
        throw std::invalid_argument("dynChannelKey: 通道序越界（0..4 之外）");
    }
    return kChannelKeys[index];
}

std::string dynChannelUnit(DynChannelId id, DynJointType jointType)
{
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kDynChannelCount) {
        throw std::invalid_argument("dynChannelUnit: 通道序越界（0..4 之外）");
    }
    // 移动关节＝长度系量纲（m 系）；转动/连续关节＝角度系（rad 系）——
    // 卡 §4.5 量纲表的呈现面分派（D-DYN-4 类型化纪律的标签半区）。
    const bool prismatic = jointType == DynJointType::Prismatic;
    switch (id) {
        case DynChannelId::Position:
            return prismatic ? "m" : "rad";
        case DynChannelId::Velocity:
            return prismatic ? "m/s" : "rad/s";
        case DynChannelId::Acceleration:
            return prismatic ? "m/s^2" : "rad/s^2";
        case DynChannelId::GeneralizedForce:
            // 类型化红线（DYN-03）：转动 N·m／移动 N 不得混用——标签
            // 分派即该红线在呈现面的落点。
            return prismatic ? "N" : "N·m";
        case DynChannelId::MechanicalPower:
            return "W";  // 功率两型同量纲（P=τ·q̇，量纲合成后同型）
    }
    return "";  // 不可达（switch 全枚举覆盖——防御性返回空标签）
}

const std::vector<double>& dynChannelValues(const JointCurves& curves,
                                            DynChannelId id)
{
    // 五字段按通道序对位（JointCurves 契约——字段序即通道序）；switch
    // 返回成员引用（零拷贝视图——绘线/点数共用同一数据源）。
    switch (id) {
        case DynChannelId::Position:          return curves.q;
        case DynChannelId::Velocity:          return curves.qd;
        case DynChannelId::Acceleration:      return curves.qdd;
        case DynChannelId::GeneralizedForce:  return curves.generalizedForce;
        case DynChannelId::MechanicalPower:   return curves.mechanicalPower;
    }
    throw std::invalid_argument("dynChannelValues: 通道序越界（0..4 之外）");
}

// =====================================================================
// L-D1 就绪投影合成（透传——零判定）。
// =====================================================================

DynReadinessRow readinessProjection(const DynModuleSessionState& session)
{
    DynReadinessRow row;
    // 域注册键恒 "dynamics"（§6.5 域注册词表——本域唯一书写点）。
    row.domainKey = "dynamics";
    // 四事实字段逐项透传（权威在域就绪校验/execution/evidence——插件
    // 不复制判定逻辑，防第二真值）。
    row.verdict = session.verdict;
    row.inputComplete = session.inputComplete;
    row.missingItemKeys = session.missingItemKeys;
    row.hasActiveTask = session.hasActiveTask;
    return row;
}

// =====================================================================
// L-D2 曲线通道行集重组（数据重组零统计——行序稳定）。
// =====================================================================

std::vector<DynChannelRow> curveChannelRows(const CurveProjection& projection)
{
    std::vector<DynChannelRow> rows;
    // 空投影（无 Ok 行）→空行集（Empty 显式语义——不伪造行；呈现侧
    // 以"无数据"空态呈现，NFR-COR-03）。
    rows.reserve(projection.joints.size() * static_cast<std::size_t>(kDynChannelCount));
    for (const JointCurves& joint : projection.joints) {
        for (int channel = 0; channel < kDynChannelCount; ++channel) {
            const auto id = static_cast<DynChannelId>(channel);
            DynChannelRow row;
            row.jointIndex = joint.jointIndex;
            row.jointType = joint.jointType;
            row.channelId = channel;
            row.channelKey = dynChannelKey(id);
            row.unitToken = dynChannelUnit(id, joint.jointType);
            // 点数＝该通道数组长度（直拷透传——不校验五通道等长，那
            // 是投影器产出的既有契约）；剔除计数逐关节透传。
            row.pointCount = dynChannelValues(joint, id).size();
            row.nonOkCount = joint.nonOkCount;
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

// =====================================================================
// L-D3 会话命令受理流（§10.7 受理＋会话记录——零修订）。
// =====================================================================

CommandOutcome applySessionCommand(DynModuleSessionState& session,
                                   std::string_view commandToken,
                                   const CommandPayload& payload)
{
    // 第 1 步：领域适配器受理（词表查表＋负载交叉校验——§10.7 语义；
    // 无状态纯函数，插件本地零命令判定）。
    const DynamicsCommandHandler handler;
    const CommandOutcome outcome = handler.handle(commandToken, payload);

    // 第 2 步：会话记录追加（呈现缓冲——token/受理/拒绝/修订位直拷）。
    DynCommandRecord record;
    record.commandToken = std::string(commandToken);
    record.accepted = outcome.accepted;
    record.rejectionToken = outcome.rejectionToken;
    record.producesRevision = outcome.producesRevision;
    session.recentCommands.push_back(std::move(record));
    // 缓冲截断：超上限从头丢最旧（呈现缓冲语义——完整受理史归
    // execution 任务清单，本缓冲只服务工作流页"最近命令"清单）。
    if (session.recentCommands.size() > kDynCommandRecordCapacity) {
        const std::size_t excess =
            session.recentCommands.size() - kDynCommandRecordCapacity;
        session.recentCommands.erase(session.recentCommands.begin(),
                                     session.recentCommands.begin()
                                         + static_cast<std::ptrdiff_t>(excess));
    }

    // 第 3 步：受理结果透传（accepted=false＝用户可见不受理——非异常）。
    return outcome;
}

// =====================================================================
// L-D4 峰值定位流（缝归一——三态显式）。
// =====================================================================

std::optional<DynPeakJump> locatePeakJump(const DynPanelServices& services,
                                          std::uint32_t jointIndex,
                                          int tokenIndex,
                                          std::string* outReason)
{
    // 空因回传初始化（受理＝空串；调用方不关心可传 nullptr）。
    if (outReason != nullptr) {
        outReason->clear();
    }
    // 缝未装配→nullopt＋"not-assembled"（与"无峰值"显式区分——呈现侧
    // 各自呈现，不混同）。
    if (!services.peakLocate) {
        if (outReason != nullptr) {
            *outReason = "not-assembled";
        }
        return std::nullopt;
    }
    // 缝委托（组装侧消费计算库统计行集后翻译——插件面零统计符号）；
    // 缝返回 nullopt＝该关节/通道无峰值（合法空态——"no-peak"）。
    std::optional<DynPeakJump> jump = services.peakLocate(jointIndex, tokenIndex);
    if (!jump.has_value() && outReason != nullptr) {
        *outReason = "no-peak";
    }
    return jump;
}

// =====================================================================
// L-D5 回放查表流（域设施消费——转发计算库投影器）。
// =====================================================================

std::optional<ReplaySample> replaySampleAt(const ReplayData& data, double t)
{
    // 无状态投影器（T08 契约——纯查表零环境依赖）；越界/空集空态与
    // 非有限 fail-fast 语义全部继承计算库契约（见头注），本层零包装
    // 逻辑——"查表唯一实现点"纪律。
    const DynamicsReplayProjector projector;
    return projector.sampleAt(data, t);
}

// =====================================================================
// L-D6 文案解析流（UX-02——缝解析＋键名兜底＋哈希形态守卫）。
// =====================================================================

namespace {

/// 哈希形态检测（64 位十六进制串——ARC-04 内容摘要的呈现泄漏形态；
/// UX-02"不显示哈希"守卫的判定器，逐字符大小写不敏感十六进制集）。
bool looksLikeContentDigest(const std::string& text)
{
    if (text.size() != 64) {
        return false;  // 摘要固定 64 字符（SHA-256 十六进制——长度即滤网）
    }
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(std::toupper(
            static_cast<unsigned char>(ch)));
        const bool hexDigit = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        if (!hexDigit) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string resolvePanelText(const DynPanelServices& services,
                             const std::string& titleKey,
                             bool* outFellBack)
{
    if (outFellBack != nullptr) {
        *outFellBack = false;
    }
    // 缝空→键名原文兜底（kinematics 同纪律——开发态可见缺口）。
    if (!services.textResolver) {
        if (outFellBack != nullptr) {
            *outFellBack = true;
        }
        return titleKey;
    }
    const std::string resolved = services.textResolver(titleKey);
    // 解析结果哈希形态→按泄漏处置回退键名（呈现宁可键名也不可哈希
    // ——UX-02 红线；宿主文案资源缺陷经 outFellBack 可观测）。
    if (looksLikeContentDigest(resolved)) {
        if (outFellBack != nullptr) {
            *outFellBack = true;
        }
        return titleKey;
    }
    return resolved;
}

}  // namespace sdurws::ird::dynamics
