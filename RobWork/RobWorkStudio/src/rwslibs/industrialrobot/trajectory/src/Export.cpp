/**
 * @file   Export.cpp
 * @brief  标准轨迹导出的实现（WP-16-T10，§15.13）——前置校验＋可写性预
 *         检＋io ICsvWriter 会话编排（魔数行＋身份块＋表头＋逐时刻逐关节
 *         长表）；失败/取消先前输出完整保留（io 结构性保证）。
 *
 * 设计依据：
 *   - units/trajectory.md §15.13（接口基线全行）、§14.4（TRJ-EXPORT-FAILED
 *     ＝io 通道错误转译——本 TU 把 io 稳定码原文入 outcome.errorToken，
 *     诊断组装面据此出 TRJ-* 素材）、§14.5.1（trajectory.export 命令的
 *     执行面）
 *   - io Csv.hpp §5.7/§9.4（唯一转义点/写出会话协议 open→write→finish，
 *     RAII 放弃语义＝未 finish 析构清理暂存目标不变——失败/取消语义的
 *     结构性承载）、AtomicFile.hpp §4.6（同目录暂存＋同卷原子替换）
 *   - 确定性：样本优先、关节次之的固定行序＋io canonical 写出（to_chars
 *     最短往返/固定方言行/UTF-8 无 BOM）——同请求同 payload 字节级同输
 *     出（NFR-COR-02）
 *
 * 双模式编译（无 gating）：本 TU 对 payload 只做 const 分量级读取（零
 * rw::math::Q 构造/析构点——TrjTypes.cpp"零 Q 构造面"同款先例），io 通
 * 道两模式皆可达——无条件编译（与 Playback.cpp 的 gating 差异＝符号面，
 * 见 CMakeLists 注释）。
 *
 * 线程安全：单线程会话（ICsvWriter 会话单线程——io §9.4）。
 */

#include <sdurws/ird/trajectory/Export.hpp>

#include <sdurws/ird/io/IoDiagnostics.hpp>  // io::errorCodeToken（IoErrorCode→§9.12 稳定 token 唯一映射点）

#include <algorithm>
#include <cmath>
#include <utility>

namespace sdurws::ird::trajectory {

namespace {

/// 域错误便捷构造（调用方契约违约 fail-fast 轨——§15.0 错误二分第一类；
/// token 形如 "trajectory/export/<违约点>"——导出面统一段前缀）。
TrajectoryError domainError(const std::string& subToken, const std::string& message)
{
    return TrajectoryError("trajectory/export/" + subToken, message);
}

/// 表头列名（§15.13 R1 字段表——固定 6＋2 列序；唯一书写点）。
constexpr char kColT[] = "t_s";
constexpr char kColJointIndex[] = "joint_index";
constexpr char kColQ[] = "q";
constexpr char kColQd[] = "qd";
constexpr char kColQdd[] = "qdd";
constexpr char kColJointType[] = "joint_type";
constexpr char kColSegmentIndex[] = "segment_index";
constexpr char kColSourceTaskPoint[] = "source_task_point";

}  // namespace

std::string_view exportJointTypeToken(TrajectoryExportJointType kind)
{
    switch (kind) {
        case TrajectoryExportJointType::Revolute:
            return "revolute";
        case TrajectoryExportJointType::Prismatic:
            return "prismatic";
    }
    // 封闭词表穷尽分支（两值）——不可达位；防御性返回空视图由调用侧
    // 校验拦截（词表外值在请求校验期即 fail-fast，见执行序 1）。
    return {};
}

TrajectoryExportOutcome exportStandardTrajectory(const TrajectoryExportRequest& request,
                                                 const TrajectoryExportIoPorts& ports)
{
    // ---- 执行序 1：前置校验（违约序固定——NFR-COR-02/COR-03）----
    // 写出通道缝：空工厂＝调用方装配违约（缺省构造恒非空，显式置空才是
    // 违约——测试替身注入面误用拦截）。
    if (!ports.csvWriterFactory) {
        throw domainError("factory-missing", "CSV 写出器工厂缺席——装配违约");
    }
    if (request.targetPath.empty()) {
        throw domainError("target-empty", "导出目标路径为空");
    }
    if (request.payload == nullptr) {
        // §15.13 前置：数据源＝已归档 Completed payload，不从会话态导出
        // ——空指针＝无数据源，直接拒绝。
        throw domainError("payload-missing", "导出数据源为空——须为已归档 Completed 结果的 payload");
    }
    const TimeParameterization& tp = *request.payload;
    if (tp.samples.empty()) {
        throw domainError("payload-empty", "payload 样本表为空——无导出语义");
    }
    // 样本时刻非递减＋端点样本在位（§6.2 采样不变量；与 Playback 查表同
    // 口径——payload 完整性是导出保真前提）。
    for (std::size_t i = 1; i < tp.samples.size(); ++i) {
        if (tp.samples[i].t < tp.samples[i - 1].t) {
            throw domainError("payload-time-order",
                              "样本时刻非递减（下标 " + std::to_string(i) + "）——payload 损坏");
        }
    }
    if (tp.samples.front().t != 0.0 || tp.samples.back().t != tp.totalDurationS) {
        throw domainError("payload-endpoints",
                          "缺端点事件对齐样本（首 t=" + std::to_string(tp.samples.front().t)
                              + "，末 t=" + std::to_string(tp.samples.back().t) + "，total="
                              + std::to_string(tp.totalDurationS) + "）——§6.2 采样不变量");
    }
    // 关节维与 joint_type 词表投影。
    const std::size_t dim = tp.samples.front().q.size();
    if (dim == 0) {
        throw domainError("payload-dim", "样本关节维数为 0——payload 损坏");
    }
    if (request.jointTypes.size() != dim) {
        throw domainError("joint-types-dim",
                          "jointTypes 维度 " + std::to_string(request.jointTypes.size())
                              + " ≠ 样本关节维 " + std::to_string(dim));
    }
    // 全值有限：非有限值若到达写出行会被 io 防御性拒绝（IO-FORMAT-INTERNAL），
    // 提前拦截给可定位的本域错误（NFR-COR-03——不静默）。
    for (const TimedSample& s : tp.samples) {
        if (s.q.size() != dim || s.qd.size() != dim || s.qdd.size() != dim) {
            throw domainError("payload-dim", "样本 q/qd/qdd 维度不一致——payload 损坏");
        }
        for (std::size_t j = 0; j < dim; ++j) {
            if (!std::isfinite(s.q[j]) || !std::isfinite(s.qd[j])
                || !std::isfinite(s.qdd[j])) {
                throw domainError("payload-non-finite",
                                  "样本含非有限分量（轴 " + std::to_string(j) + "，t="
                                      + std::to_string(s.t) + "）——payload 损坏");
            }
        }
    }
    // 段映射列开关（两列同开同关）＋段归投影自洽。
    const bool mappingColumns = !request.segmentAttribution.empty()
                             || !request.segmentDurationsS.empty();
    if (mappingColumns
        && (request.segmentAttribution.empty() || request.segmentDurationsS.empty())) {
        throw domainError("mapping-half-open",
                          "段映射列投影只给了半区（segmentAttribution="
                              + std::to_string(request.segmentAttribution.size()) + " 条/"
                              "segmentDurationsS=" + std::to_string(request.segmentDurationsS.size())
                              + " 条）——两列须同开同关（§15.13 成对出现）");
    }
    if (mappingColumns) {
        double sum = 0.0;
        if (request.segmentAttribution.size() != request.segmentDurationsS.size()) {
            throw domainError("mapping-mismatch",
                              "段归条目数与段时长数不一致——投影不同源");
        }
        for (std::size_t i = 0; i < request.segmentDurationsS.size(); ++i) {
            if (!(request.segmentDurationsS[i] > 0.0)
                || !std::isfinite(request.segmentDurationsS[i])) {
                throw domainError("mapping-duration",
                                  "段时长投影含非正值（下标 " + std::to_string(i) + "）");
            }
            if (request.segmentAttribution[i].segmentIndex
                != static_cast<std::uint32_t>(i)) {
                throw domainError("mapping-order",
                                  "段归条目 segmentIndex 与下标不对齐（下标 "
                                      + std::to_string(i) + "）");
            }
            sum += request.segmentDurationsS[i];
        }
        // 自洽判定（R3 审计 A2 修复，2026-10-15——与 Playback 构造查表
        // 同批同口径）：原位级 != 会拒绝生产面 TimeParam 的合法产物（含
        // 内部结点的运动段按 L1 比例分摊结点时刻，Σ段时长与 totalDurationS
        // 位级不等的概率约三成）。改相对容差：容限＝1e-9·max(1, 总时长)，
        // 远小于真实"不同源"错装的段时长量级偏差，仅拦截真混装。
        const double sumDiff = std::abs(sum - tp.totalDurationS);
        const double sumTol = 1e-9 * std::max(1.0, std::abs(tp.totalDurationS));
        if (!(sumDiff <= sumTol)) {
            throw domainError("mapping-sum",
                              "Σ段时长=" + std::to_string(sum) + " ≠ totalDurationS="
                                  + std::to_string(tp.totalDurationS)
                                  + "（|差|=" + std::to_string(sumDiff)
                                  + " 超容差 " + std::to_string(sumTol)
                                  + "）——段归投影与 payload 不同源（§12.4 自洽）");
        }
    }
    // 身份块完整性（复现要素 NFR-COR-04——五文本字段非空；planningSeed
    // 由 payload 身份块保证 >0，本域透传不校验数值语义）。
    if (request.identity.snapshotId.empty() || request.identity.sliceId.empty()
        || request.identity.policyContentIdentity.empty()
        || request.identity.algorithmVersion.empty()
        || request.identity.robworkBaselineVersion.empty()) {
        throw domainError("identity-incomplete",
                          "身份块五文本字段须非空（snapshotId/sliceId/policyContent"
                          "Identity/algorithmVersion/robworkBaselineVersion）——复现要"
                          "素完整性（NFR-COR-04）");
    }

    // ---- 执行序 2：可写性预检（§15.13"不覆盖未经确认文件"——在写会话
    // 建立之前拒绝，目标零触碰；错误码复用 io 词表单点 IO-PACK-TARGET-
    // EXISTS——同语义同码防双词表漂移）。
    std::error_code existsEc;
    const bool targetExists = std::filesystem::exists(request.targetPath, existsEc);
    if (targetExists && !request.overwriteConfirmed) {
        TrajectoryExportOutcome refused;
        refused.status = TrajectoryExportOutcome::Status::TargetNotConfirmed;
        refused.errorToken = "IO-PACK-TARGET-EXISTS";
        refused.errorMessage = "导出目标已存在且未确认覆盖（overwriteConfirmed=false）"
                               "——§15.13 不覆盖未经确认文件；目标未触碰";
        return refused;
    }

    // ---- 执行序 3：取消轮询（预检后、写会话前——命中零触碰）。
    if (request.cancel && request.cancel()) {
        TrajectoryExportOutcome canceled;
        canceled.status = TrajectoryExportOutcome::Status::Canceled;
        return canceled;
    }

    // ---- 执行序 4：建立写会话（工厂创建→open 文件目标；io 于 open 即建
    // 同目录暂存文件——失败在此暴露且目标不变）。
    std::unique_ptr<io::ICsvWriter> writer = ports.csvWriterFactory();
    if (writer == nullptr) {
        // 工厂返回空＝替身装配违约（产品工厂恒非空——io §9.11）。
        TrajectoryExportOutcome failed;
        failed.status = TrajectoryExportOutcome::Status::Failed;
        failed.errorToken = "IO-FORMAT-INTERNAL";
        failed.errorMessage = "CSV 写出器工厂返回空实例——装配违约";
        return failed;
    }
    io::CsvWriteOptions options;  // io 缺省方言＋emitDialectMarker=true（标识
                                  // 行自写——§15.13 头部行 1）
    const io::IoResult<void> opened =
        writer->open(io::CsvOutputTarget::file(request.targetPath), options);
    if (!opened) {
        TrajectoryExportOutcome failed;
        failed.status = TrajectoryExportOutcome::Status::Failed;
        failed.errorToken = std::string(io::errorCodeToken(opened.error.code));
        failed.errorMessage = "CSV 写出会话建立失败：" + opened.error.detail;
        return failed;  // writer 析构＝放弃＝暂存清理、目标不变（io RAII）
    }

    // ---- 执行序 5：头部块（魔数行＋身份块六行——单 Text 单元格行；首字
    // 符 '#' 不触发 io 可逆转义，原样落盘）。
    const std::size_t identityLineCount = 1U + 6U;  // 魔数 1＋身份块 6
    std::size_t writtenRows = 0;
    auto writeSingleCellRow = [&writer, &writtenRows](
                                  const std::string& line,
                                  TrajectoryExportOutcome::Status& failStatus,
                                  std::string& failToken, std::string& failMessage) -> bool {
        const io::IoResult<void> r =
            writer->writeRow({io::CsvCell::fromText(io::IoString(line))});
        if (!r) {
            failStatus = TrajectoryExportOutcome::Status::Failed;
            failToken = std::string(io::errorCodeToken(r.error.code));
            failMessage = "头部块写出失败：" + r.error.detail;
            return false;
        }
        ++writtenRows;
        return true;
    };

    TrajectoryExportOutcome failCarrier;
    if (!writeSingleCellRow(kTrajectoryExportFormatMagic, failCarrier.status,
                            failCarrier.errorToken, failCarrier.errorMessage)) {
        return failCarrier;
    }
    // 身份块行序（§15.13 字段表六要素固定序——读回解析锚）。
    if (!writeSingleCellRow("# snapshotId=" + request.identity.snapshotId, failCarrier.status,
                            failCarrier.errorToken, failCarrier.errorMessage)) {
        return failCarrier;
    }
    if (!writeSingleCellRow("# sliceId=" + request.identity.sliceId, failCarrier.status,
                            failCarrier.errorToken, failCarrier.errorMessage)) {
        return failCarrier;
    }
    if (!writeSingleCellRow("# policyContentIdentity=" + request.identity.policyContentIdentity,
                            failCarrier.status, failCarrier.errorToken,
                            failCarrier.errorMessage)) {
        return failCarrier;
    }
    if (!writeSingleCellRow("# algorithmVersion=" + request.identity.algorithmVersion,
                            failCarrier.status, failCarrier.errorToken,
                            failCarrier.errorMessage)) {
        return failCarrier;
    }
    if (!writeSingleCellRow("# planningSeed=" + std::to_string(request.identity.planningSeed),
                            failCarrier.status, failCarrier.errorToken,
                            failCarrier.errorMessage)) {
        return failCarrier;
    }
    if (!writeSingleCellRow("# robworkBaselineVersion=" + request.identity.robworkBaselineVersion,
                            failCarrier.status, failCarrier.errorToken,
                            failCarrier.errorMessage)) {
        return failCarrier;
    }

    // ---- 执行序 6：表头行（6 列或 8 列——列名经 io 转义后引号化判定，
    // 本组列名均为安全词形，落盘即原文）。
    std::vector<io::IoString> headerNames{kColT,          kColJointIndex, kColQ,
                                          kColQd,         kColQdd,        kColJointType};
    if (mappingColumns) {
        headerNames.push_back(kColSegmentIndex);
        headerNames.push_back(kColSourceTaskPoint);
    }
    {
        const io::IoResult<void> r = writer->writeHeader(headerNames);
        if (!r) {
            TrajectoryExportOutcome failed;
            failed.status = TrajectoryExportOutcome::Status::Failed;
            failed.errorToken = std::string(io::errorCodeToken(r.error.code));
            failed.errorMessage = "表头写出失败：" + r.error.detail;
            return failed;
        }
    }

    // ---- 执行序 7：数据长表（样本优先、关节次之——行序固定＝确定性；
    // 驻留行显式：样本表已含驻留常值段样本，qd/qdd 按样本原值 0 落盘，
    // 不删行不补行——§15.13"驻留行显式（qd=qdd=0）"）。
    const std::size_t columnCount = headerNames.size();
    std::vector<io::CsvCell> row;
    row.reserve(columnCount);
    for (const TimedSample& s : tp.samples) {
        // 取消检查点＝样本批边界（每样本一行组——§15.1"循环边界轮询"；
        // 命中即返回，writer 析构放弃＝目标不变，UX-03 零错误素材）。
        if (request.cancel && request.cancel()) {
            TrajectoryExportOutcome canceled;
            canceled.status = TrajectoryExportOutcome::Status::Canceled;
            return canceled;
        }
        for (std::size_t j = 0; j < dim; ++j) {
            row.clear();
            row.push_back(io::CsvCell::fromReal(s.t));                       // t_s
            row.push_back(io::CsvCell::fromInt(static_cast<std::int64_t>(j)));  // joint_index
            row.push_back(io::CsvCell::fromReal(s.q[j]));                    // q（rad|m）
            row.push_back(io::CsvCell::fromReal(s.qd[j]));                   // qd（rad/s|m/s）
            row.push_back(io::CsvCell::fromReal(s.qdd[j]));                  // qdd（rad/s²|m/s²）
            row.push_back(io::CsvCell::fromText(io::IoString(
                std::string(exportJointTypeToken(request.jointTypes[j])))));  // joint_type
            if (mappingColumns) {
                // 段归定位：累积边界半开区间 [cum_i, cum_{i+1})，t==total
                // 落末段——与 Playback 查表段归同一口径（§6.3 共用定位键）。
                double cum = 0.0;
                std::size_t seg = request.segmentDurationsS.size() - 1U;
                for (std::size_t i = 0; i < request.segmentDurationsS.size(); ++i) {
                    const double next = cum + request.segmentDurationsS[i];
                    if (s.t >= cum && s.t < next) {
                        seg = i;
                        break;
                    }
                    cum = next;
                }
                row.push_back(io::CsvCell::fromInt(static_cast<std::int64_t>(seg)));
                row.push_back(io::CsvCell::fromText(
                    io::IoString(request.segmentAttribution[seg].sourceTaskPoint)));
            }
            const io::IoResult<void> r = writer->writeRow(row);
            if (!r) {
                TrajectoryExportOutcome failed;
                failed.status = TrajectoryExportOutcome::Status::Failed;
                failed.errorToken = std::string(io::errorCodeToken(r.error.code));
                failed.errorMessage = "数据行写出失败（t=" + std::to_string(s.t)
                                          + "，关节 " + std::to_string(j) + "）：" + r.error.detail;
                return failed;  // writer 析构＝放弃＝目标不变（io RAII）
            }
            ++writtenRows;
        }
    }

    // ---- 执行序 8：finish（flush＋同卷原子替换——目标就位；成功后先前
    // 输出被确认替换，失败→清理暂存目标不变）。
    const io::IoResult<void> finished = writer->finish();
    if (!finished) {
        TrajectoryExportOutcome failed;
        failed.status = TrajectoryExportOutcome::Status::Failed;
        failed.errorToken = std::string(io::errorCodeToken(finished.error.code));
        failed.errorMessage = "导出提交失败：" + finished.error.detail;
        return failed;
    }

    TrajectoryExportOutcome ok;
    ok.status = TrajectoryExportOutcome::Status::Ok;
    ok.identityLineCount = identityLineCount;
    ok.dataRowCount = writtenRows - identityLineCount;  // 数据行＝总行−头部块
    return ok;
}

}  // namespace sdurws::ird::trajectory
