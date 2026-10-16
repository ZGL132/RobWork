/**
 * @file   KinHostServicesAssembly.cpp
 * @brief  kinematics 面板服务缝的宿主装配半区实现（KinHostServicesAssembly.hpp
 *         全部落点——KIN-P4 审查 §4"降级服务逐项接通"的宿主侧）。
 *
 * 设计依据：hpp 文件头（各产品入口的转译语义/错误轨/线程约束的权威
 * 声明在本 TU 逐段兑现）。
 */

#include "KinHostServicesAssembly.hpp"

#include <stdexcept>
#include <utility>

#include <QSettings>          // 配置持久化载体（PM-14 用户级通道——ui 宿主面）
#include <QStandardPaths>     // 导出目录缺省解析（产品闭包——测试注入临时目录）

#include <sdurws/ird/core/DiagData.hpp>          // core::DiagnosticRecord/DiagCode（提交回显诊断承载）
#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // AnalysisConfigurationCodec（配置编解码）
#include <sdurws/ird/kinematics/DiagCodes.hpp>   // kKinNoDevice（稳定码——禁字符串拼码）
#include <sdurws/ird/io/AtomicFile.hpp>          // io 原子写出（L-K10 写出语义）
#include <sdurws/ird/modeling/Codec.hpp>         // modeling::RobotDesignCodec（根对象编解码权威）
#include <sdurws/ird/modeling/ObjectTypes.hpp>   // 对象 token/schema 常量（named-pose-set 等）
#include <sdurws/ird/modeling/Parts.hpp>         // PoseSet/PoseSetEntry/kHomeConfigurationPoseKey

#include <variant>  // holds_alternative/get（ObjectVariant 取根——显式依赖显式 include）

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// KinProjectCommandGateway——转译实现
// =====================================================================

KinProjectCommandGateway::KinProjectCommandGateway(
    project::IProjectQueryPort* query, SubmitFn submit)
    : m_query(query)
    , m_submit(std::move(submit))
{
    // 装配违约 fail-fast（Commands.hpp 网关注释同则——缺依赖到达即编程
    // 错误，不产半装配对象）。
    if (m_query == nullptr || !m_submit) {
        throw std::invalid_argument(
            "KinProjectCommandGateway：查询端口或提交闭包缺位（装配违约）");
    }
}

runtime::Expected<kinematics::KinProjectBaseline, kinematics::KinematicsError>
KinProjectCommandGateway::fetchBaseline() const
{
    kinematics::KinProjectBaseline baseline;

    // ---- 分支与 tip（INV-M3 单默认分支——与宿主应用编排同源取数：
    // branchTips 首条；空表＝无分支态，NoDevice 轨——无设备亦无分支）。
    std::vector<project::BranchTip> tips;
    try {
        tips = m_query->branchTips();
        if (!tips.empty()) {
            baseline.branch = tips.front().id;
            baseline.tipRevision = tips.front().tip;
        }
        // ---- HEAD 闭包扫描 robot-design 根（token 常量＝kinematics 公共
        // 头 kRobotDesignObjectTypeToken——与 modeling kRobotDesignObject-
        // Type 同串，测试金标钉住；对象内容版本取引用表值——tryObject 按
        // 内容寻址核对）。
        const project::RevisionView head = m_query->head();
        for (const project::ObjectRef& ref : head.objectRefs) {
            if (ref.objectTypeToken
                == std::string(kinematics::kRobotDesignObjectTypeToken)) {
                baseline.rootObjectId = ref.objectId;
                baseline.rootObjectBytes =
                    m_query->tryObject(ref.objectId, ref.contentVersion)
                        .value_or(std::vector<std::uint8_t>{});
                break;  // R1 单设备数据模型——恰一根对象（首中即取）
            }
        }
    } catch (const std::exception& e) {
        // 端口数据侧异常（StoreError 族）转译为 NoDevice 错误轨＋detail
        // 留痕（门面契约的值面错误轨——异常不穿越门面；detail 属内部
        // 诊断链素材，呈现面经 diagnostics 脱敏——KinematicsError 注）。
        kinematics::KinematicsError err;
        err.code = kinematics::KinematicsErrorCode::NoDevice;
        err.detail = std::string("查询端口取数失败：") + e.what();
        return runtime::Expected<kinematics::KinProjectBaseline,
                                 kinematics::KinematicsError>::err(
            std::move(err));
    }

    // ---- 闭包无根＝"无可用设备"业务出口（§9.6 KIN-NO-DEVICE——不抛
    // 异常，值面错误轨）。
    if (!baseline.rootObjectId.has_value()
        || baseline.rootObjectBytes.empty()) {        kinematics::KinematicsError err;
        err.code = kinematics::KinematicsErrorCode::NoDevice;
        err.detail = "项目闭包无 robot-design 根对象（尚未建模或闭包外）";
        return runtime::Expected<kinematics::KinProjectBaseline,
                                 kinematics::KinematicsError>::err(
            std::move(err));
    }
    return runtime::Expected<kinematics::KinProjectBaseline,
                             kinematics::KinematicsError>::ok(
        std::move(baseline));
}

kinematics::CommandSubmission KinProjectCommandGateway::submit(
    const kinematics::KinCommandEnvelope& envelope)
{
    // ---- 同构转译（KinCommandEnvelope → project::CommandEnvelope：
    // 分支/期望基线/token/版本/载荷字节逐字段直投——两结构字段语义
    // 逐字对应，见 kinematics Commands.hpp §6.4 注）。
    project::CommandEnvelope out;
    out.branch = envelope.branch;
    out.expectedRevision = envelope.expectedRevision;
    out.commandType = envelope.commandType;
    out.payloadFormatVersion = envelope.payloadFormatVersion;
    out.payloadCanonical = envelope.payloadCanonical;

    const project::CommandResult result = m_submit(std::move(out));

    // ---- 四态两态投影（Committed 携新修订；其余一律 NotCommitted——
    // 对端 diagnostics 有记录则原样透传（码面权威在对端——不复制第二
    // 份词表）；对端无记录（如纯词表拒绝）时以 status 词表 token 的
    // 透明诊断承载原因文本。code 承载对端 reason 的规范化 token（reject
    // 词表——project 权威），非本网关私造码；处置随本包登记于
    // units/kinematics.md §14.6（诊断透传的 code 语义＝对端 reason）。
    kinematics::CommandSubmission submission;
    if (result.status.kind == project::CommandStatus::Kind::Committed) {
        submission.kind = kinematics::CommandSubmission::Kind::Committed;
        submission.newRevision = result.newRevision;
        return submission;
    }
    submission.kind = kinematics::CommandSubmission::Kind::NotCommitted;
    if (!result.diagnostics.empty()) {
        submission.diagnostics = result.diagnostics;  // 对端诊断原样透传
        return submission;
    }
    // 对端无诊断记录——词表 reason token 透传（四态细分：Rejected 的
    // rejection 词表/Aborted 的 abort 词表/Failed 的 error 码面）。
    std::string reasonToken;
    std::string causeText;
    switch (result.status.kind) {
        case project::CommandStatus::Kind::Rejected:
            reasonToken = "rejected";
            causeText = "对端拒绝（rejection 词表项 "
                        + std::to_string(static_cast<int>(
                              result.status.rejection))
                        + "）";
            break;
        case project::CommandStatus::Kind::Aborted:
            reasonToken = "aborted";
            causeText = "对端中止（abort 词表项 "
                        + std::to_string(static_cast<int>(
                              result.status.abort))
                        + "）";
            break;
        case project::CommandStatus::Kind::Failed:
            reasonToken = "failed";
            causeText = "对端失败（环境/数据侧错误）";
            break;
        case project::CommandStatus::Kind::Committed:
            break;  // 不可达（上方已返回）
    }
    submission.diagnostics.push_back(core::DiagnosticRecord::make(
        core::DiagCode(reasonToken),        // 对端词表 token 透传（登记随单元卡 §14.6）
        std::nullopt, std::nullopt, std::nullopt,
        "设默认命令提交未产生修订（①端口四态投影——非 Committed）",
        causeText,
        "请核对项目可写性与基线状态后重试"));
    return submission;
}

// =====================================================================
// 根对象补丁缝（modeling 编解码权威保持）
// =====================================================================

std::optional<std::vector<std::uint8_t>> patchRootDesignDefaultTcp(
    const std::vector<std::uint8_t>& baselineRootBytes,
    const kinematics::TcpRef& defaultTcp)
{
    // ---- 解码（modeling::RobotDesignCodec——外部字节进入类型化世界的
    // 唯一 model 侧闸口；失败＝数据侧 nullopt，门面转诊断）。
    const modeling::RobotDesignCodec codec;
    const auto decoded =
        codec.decode(baselineRootBytes, modeling::kCurrentFormatVersion);
    if (!decoded.ok()) {
        return std::nullopt;
    }
    // ---- 变体取根（variant 备择序 0＝RobotDesign——非根形态即基线
    // 字节违约，同轨拒绝）。
    if (!std::holds_alternative<modeling::RobotDesign>(decoded.get())) {
        return std::nullopt;
    }
    modeling::RobotDesign design =
        std::get<modeling::RobotDesign>(std::move(decoded.get()));
    // ---- 写 defaultTcp（仅覆盖该字段——其余字段逐位保留）。
    design.defaultTcp = modeling::TcpRef{defaultTcp.toolObject, defaultTcp.tcpKey};
    // ---- 重编码（canonical 字节——同权威编解码器，零第二套编码）。
    const auto encoded =
        codec.encode(modeling::ObjectVariant{std::move(design)},
                     modeling::kCurrentFormatVersion);
    if (!encoded.ok()) {
        return std::nullopt;
    }
    return encoded.get();
}

// =====================================================================
// 导出写出缝（io AtomicFile 装配）
// =====================================================================

std::function<bool(const std::string&, const std::string&)>
makeKinExportWriter(std::function<std::filesystem::path()> dirProvider)
{
    if (!dirProvider) {
        throw std::invalid_argument(
            "makeKinExportWriter：目录解析闭包缺位（装配违约）");
    }
    // 闭包（值捕获 dirProvider——每次导出现取目录：环境变量变化即时
    // 生效；目录代建——io AtomicFile 契约"父目录必须已存在"的装配层
    // 补偿，create_directories 幂等）。
    return [dirProvider = std::move(dirProvider)](
               const std::string& fileName,
               const std::string& content) -> bool {
        try {
            const std::filesystem::path dir = dirProvider();
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (ec) {
                return false;  // 目录不可建＝写出失败（如实——旧文件语义由 io 层保证）
            }
            const std::filesystem::path target = dir / fileName;
            // io 原子写出（prepare→write→commit——commit 成功即目标在
            // 介质上完整；失败轨 abort 幂等清暂存，目标零污染）。
            const io::IAtomicFileWriterPtr writer = io::makeAtomicFileWriter();
            io::IoResult<io::AtomicTarget> prepared =
                writer->prepare(target, io::ReplacePolicy::OverwriteAtomic);
            if (!prepared) {
                return false;  // prepare 失败（目录缺失/权限——如实布尔）
            }
            io::AtomicTarget session = std::move(prepared.value);
            const io::IoResult<void> written =
                session.write(content);  // 内容一次投递（本包量级远低于 1 MiB 分块界）
            if (!written) {
                (void)writer->abort(session);
                return false;
            }
            const io::IoResult<void> committed = writer->commit(session);
            if (!committed) {
                (void)writer->abort(session);
                return false;
            }
            return true;
        } catch (const std::exception&) {
            return false;  // 写出失败（目录解析/文件系统异常——如实布尔，不吞状态）
        }
    };
}

// =====================================================================
// 求解配置持久化（QSettings×canonical 编解码）
// =====================================================================

std::function<bool(const kinematics::AnalysisConfiguration&)>
makeKinConfigPersist(QSettings* settings)
{
    if (settings == nullptr) {
        throw std::invalid_argument(
            "makeKinConfigPersist：设置载体缺位（装配违约）");
    }
    return [settings](const kinematics::AnalysisConfiguration& config) -> bool {
        // canonical 编码（域设施唯一实现点——65 字节定长 v1；非法配置
        // 在 encode 前置校验即抛——保存缝以 false 如实承载）。
        try {
            const kinematics::AnalysisConfigurationCodec codec;
            const std::vector<std::uint8_t> bytes = codec.encode(config);
            // base64 文本承载（QSettings 无原生字节面——字节串原样存取
            // 的等价载体；AnalysisConfig.hpp §4.4"用户级设置存储"口径）。
            QByteArray raw(reinterpret_cast<const char*>(bytes.data()),
                           static_cast<qsizetype>(bytes.size()));
            settings->setValue(kKinConfigSettingsKey,
                               QString::fromLatin1(raw.toBase64()));
            settings->sync();  // 即时落盘（保存语义——不依赖进程退出拍）
            return settings->status() == QSettings::NoError;
        } catch (const std::exception&) {
            return false;  // 非法配置/编码失败（调用方如实呈现"保存失败"）
        }
    };
}

std::optional<kinematics::AnalysisConfiguration> restoreKinConfig(
    QSettings* settings)
{
    if (settings == nullptr) {
        return std::nullopt;  // 恢复不可用（载体缺位——如实空态）
    }
    const QVariant stored =
        settings->value(kKinConfigSettingsKey);
    if (!stored.isValid() || !stored.canConvert<QString>()) {
        return std::nullopt;  // 无持久化配置（首次使用——回退缺省基线）
    }
    const QByteArray raw =
        QByteArray::fromBase64(stored.toString().toLatin1());
    try {
        const kinematics::AnalysisConfigurationCodec codec;
        const std::vector<std::uint8_t> bytes(raw.cbegin(), raw.cend());
        // decode 内含结构校验＋合法域校验双段（非法即抛——半损坏配置
        // 绝不静默钳制，NFR-COR-03；调用方回退缺省基线）。
        return codec.decode(bytes);
    } catch (const std::exception&) {
        return std::nullopt;  // 字节损坏/非法值域——回退缺省（留痕归调用方）
    }
}

// =====================================================================
// 具名 Home 来源缝（named-pose-set 保留键解析）
// =====================================================================

std::function<std::optional<std::vector<double>>()> makeKinHomePoseProvider(
    KinPoseSetLoader loader)
{
    if (!loader.entries) {
        throw std::invalid_argument(
            "makeKinHomePoseProvider：位姿集加载闭包缺位（装配违约）");
    }
    return [entries = std::move(loader.entries)]()
               -> std::optional<std::vector<double>> {
        // ---- 位姿集现取（零缓存——面板每次点击现调；闭包无位姿集＝
        // nullopt，面板给"未定义 Home"明确提示——绝不以当前姿态充当）。
        const std::optional<std::vector<std::vector<double>>> all =
            entries();
        if (!all.has_value()) {
            return std::nullopt;
        }
        // 保留键 homeConfiguration 的载体约定：条目集首元素＝Home（加载
        // 闭包已按 modeling::kHomeConfigurationPoseKey 过滤——见宿主
        // UiPlugin 装配闭包；本缝零键面知识，保持 modeling 权威单点）。
        if (all->empty() || all->front().empty()) {
            return std::nullopt;
        }
        return all->front();  // rad|m 链序 SI 真值（位姿集条目原值）
    };
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
