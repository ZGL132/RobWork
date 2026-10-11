/**
 * @file   FailureLocate.cpp
 * @brief  失败段诊断定位实现（WP-16-T09）——locateFailures 的执行序落地
 *         （请求面校验→逐素材五步组装→工厂创建；执行序与错误语义见
 *         FailureLocate.hpp 函数注）。
 *
 * 设计依据（摘要——完整链条见头文件注）：
 *   - units/trajectory.md §14.4（诊断构造经 IDiagnosticFactory.create；
 *     评估路径 snapshotId/sliceId 必填；产生者仅登记判定不越权）、§6.2/
 *     §6.7（FailedSegmentRecord 字段义务）、§12.6/§9.2/§7.6/§8.3/§8.5
 *     （四类失败的产码路径——本 TU 只组装不产码）
 *   - units/diagnostics.md §4.2/§4.5（subject 边界〔用户级码必须携带合法
 *     非全零 subjectObjectId〕/paramSchema 占位一致〔TRJ 码 paramSchema=
 *     "[]"→params 必须空 map〕/码已注册）；core DiagData.hpp（级别映射
 *     "severity==Error ⇒ level==Error，其余 ⇒ Warning"——注册表强制映射
 *     的域内落值面）
 *   - 需求 TRJ-06/ERR-01/NFR-COR-02（确定性——上下文串格式化/校验序全
 *     确定）/NFR-COR-03（非法输入拒绝不代填）
 *   - 任务契约 tasks/foundation/WP-16-T09.json（acceptance 1/2）
 *
 * 实现登记（DTB §5.4——设计未定处的实现补全，与单元卡增量修订同批）：
 *   1. 定位上下文串格式（§14.4"上下文"字段的中文呈现语义，域内定格式）：
 *      - 段级＋可定位到点：  "轨迹段 <N>（阶段 <phaseToken>）路径参数 s=<x.xxxxxx>"
 *      - 段级不可定位到点：  "轨迹段 <N>（阶段 <phaseToken>）"
 *      - 全轨迹级：          "全轨迹级（阶段 <phaseToken>）"
 *      其中 <N> 为 0 基段序号（十进制）；<x.xxxxxx> 为段内归一化路径参数
 *      s∈[0,1]（无量纲），printf "%.6f" 固定六位小数——确定性格式化
 *      （NFR-COR-02：同输入同串，不经 locale）；<phaseToken> 为 §14.1.6
 *      词表 token 原文（plan-ptp/plan-line/plan-avoid/smooth/recheck/
 *      time-param——不翻译，稳定可判别）。
 *   2. 对象绑定的回退链（素材 subject 可空面与诊断 subject 必填面之间的
 *      桥——FailureLocateRequest.subjectFallback 注）：素材值→回退值→
 *      TrajectoryError（两级皆缺＝调用方契约违约）。
 *   3. 比较型素材的域内预检（UX-03 三要素完整性）：码描述符
 *      requiresComparison==true 而素材 comparison 缺失→TrajectoryError。
 *      工厂侧 ComparisonMissing 同语义（diagnostics §4.2 校验④）——两道
 *      防线报错类型不同（域内 TrajectoryError vs 工厂 DiagnosticsError）
 *      但均 fail-fast 不吞错；域内预检使"上游素材违约"与"工厂实现缺陷"
 *      可区分（前者是调用方错误面——§15.0 错误二分）。
 *   4. 非比较型码携带 comparison：**不拒绝**（透传——ERR-01 只强制"比较型
 *      校验额外包含"，不禁止非比较型条目携带补充数值事实；工厂同样不校验
 *      该方向）。
 *
 * 线程安全：纯函数零共享可变状态（单线程使用约定随 §15.0）；确定性来源：
 * snprintf 固定格式（locale 无关）＋固定校验序＋固定字段映射。
 */

#include <sdurws/ird/trajectory/Errors.hpp>  // TrajectoryError（调用方契约违约 fail-fast 载体）
#include <sdurws/ird/trajectory/FailureLocate.hpp>

#include <cstdio>    // std::snprintf——路径参数固定六位小数格式化（locale 无关）
#include <string>
#include <utility>
#include <vector>

namespace sdurws::ird::trajectory {

// =====================================================================
// 内部辅助（匿名命名空间——实现私有，不入公共契约；R-2 纪律）
// =====================================================================

namespace {

/**
 * @brief 组装单条素材的确定性中文定位上下文串（TRJ-06"具体段落"的承载——
 *        ERR-01 DiagnosticRecord.context 字段）。
 *
 * 格式登记见本 TU 文件头"实现登记 1"（三种形态：段级可定位到点/段级不
 * 可定位/全轨迹级）。确定性要点：snprintf "%.6f" 固定格式（不消费全局
 * locale——同输入跨进程同串）；段序号十进制无填充；phaseToken 原文不翻译。
 *
 * @param segmentIndex  [in] 失败段序号（0 基；0xFFFFFFFF＝全轨迹级）
 * @param phaseToken    [in] 失败阶段 token（§14.1.6 词表；调用方已保证非空
 *                      ——见 locateFailures 步骤 3 文案面校验）
 * @param pathParameter [in] 段内归一化路径参数 s∈[0,1]（可定位到点时必填；
 *                      nullopt＝不可定位到点——如段级整体失败）
 * @return 定位上下文串（中文＋稳定 token；进 DiagnosticRecord.context）
 */
std::string buildLocateContext(std::uint32_t segmentIndex,
                               const std::string& phaseToken,
                               const std::optional<double>& pathParameter)
{
    // 全轨迹级失败（segmentIndex=0xFFFFFFFF 保留值——§6.2"全轨迹级失败
    // ＝0xFFFFFFFF＋phase 说明"）：不渲染段序号（该值不是合法段序号，
    // 渲染"段 4294967295"会误导用户），以"全轨迹级"词面表达。
    if (segmentIndex == kWholeTrajectorySegmentIndex) {
        return "全轨迹级（阶段 " + phaseToken + "）";
    }
    // 段级失败：段序号必渲染（TRJ-06"具体段落"的最小定位面）。
    std::string context = "轨迹段 " + std::to_string(segmentIndex)
                          + "（阶段 " + phaseToken + "）";
    // 段内可定位到点时追加固定位（无量纲 s∈[0,1]；%.6f＝微米级路径分辨
    // 率下的足够精度——定位是给审查者读的坐标，不参与数值判定）。
    if (pathParameter.has_value()) {
        char buffer[32];
        // "%.6f" 固定六位小数：snprintf 与 locale 无关（C 标准保证 %f 使
        // 用 C locale 的小数点），返回值 <32 恒成立（s∈[0,1] 最多
        // "0."＋6 位＋结束符）——缓冲区溢出不可能（防御性断言面）。
        const int written = std::snprintf(buffer, sizeof(buffer), "%.6f",
                                          *pathParameter);
        if (written > 0 && static_cast<std::size_t>(written) < sizeof(buffer)) {
            context += " 路径参数 s=";
            context += buffer;
        }
        // written 异常（理论不可达）→省略固定位——定位串仍含段级信息，
        // 不因格式化边角失败而丢弃整条定位（fail-open 仅限呈现细节，
        // 不影响诊断事实——与"错误语义"分类：呈现层降级非数据违约）。
    }
    return context;
}

}  // namespace

// =====================================================================
// 入口（执行序见 FailureLocate.hpp 函数注）
// =====================================================================

FailureLocateResult locateFailures(const FailureLocateRequest& request,
                                   const diagnostics::IDiagnosticRegistry& registry,
                                   diagnostics::IDiagnosticFactory& factory)
{
    // ---- 步骤 1：请求面身份校验（§14.4"评估路径 snapshotId/sliceId 必填"
    // ——空身份（全零保留值）＝中间态误投：无评估上下文的诊断条目无法
    // 追溯到快照/切片（CON-01/CON-05），属调用方契约违约，fail-fast）。
    if (!request.snapshotId.isValid()) {
        throw TrajectoryError("trajectory/failure-locate/snapshot-missing",
                              "snapshotId 为空（全零保留值）——评估路径诊断"
                              "构造必须绑定快照身份（§14.4）");
    }
    if (!request.sliceId.isValid()) {
        throw TrajectoryError("trajectory/failure-locate/slice-missing",
                              "sliceId 为空（全零保留值）——评估路径诊断"
                              "构造必须绑定切片身份（§14.4/CON-05）");
    }

    FailureLocateResult result;

    // ---- 步骤 2：空素材序列＝零失败零诊断（合法结局——非错误；取消/
    // 无失败的评估路径都会走到这里）。
    if (request.records.empty()) {
        return result;
    }

    // ---- 步骤 3：逐素材组装（输入序遍历——"输入序＝输出序"，确定性
    // 由同输入保障；本头不重排，管线编排序语义保留）。
    result.locatedRecords.reserve(request.records.size());
    result.entries.reserve(request.records.size());
    for (const FailedSegmentRecord& record : request.records) {
        // ---- 3a. 码面校验：reasonToken 非空＋已注册＋属 trajectory 所有权。
        // 空码＝上游素材构造违约（TrjTypes.hpp 注"reasonToken 恒取
        // DiagCodes.hpp 的 TRJ-* 码常量，禁字符串拼码"——空值不可能合法）。
        if (record.reasonToken.empty()) {
            throw TrajectoryError("trajectory/failure-locate/code-missing",
                                  "素材 reasonToken 为空——失败素材必须携带"
                                  " TRJ-* 稳定码（DiagCodes.hpp 常量）");
        }
        // 注册查询：未命中＝未注册码投递（码值权威归 StableCodeRegistry——
        // NFR-MNT-03；未注册码进不了工厂，域内先行拒绝并给出可读原因）。
        const diagnostics::CodeDescriptor* descriptor =
            registry.find(record.reasonToken);
        if (descriptor == nullptr) {
            throw TrajectoryError(
                "trajectory/failure-locate/code-unregistered",
                "素材 reasonToken「" + record.reasonToken
                    + "」未注册——TRJ-* 码须经 registerTrajectoryCodes 在"
                      "装配期注册后方可产出诊断（码值权威归注册表）");
        }
        // 所有权校验：TRJ 前缀归 trajectory（diagnostics §4.5 前缀-所有权
        // 表）——他域码（RT-/PRJ-/…）不应出现在轨迹失败素材里；命中即
        // 上游串域投递违约（防素材管道混线，fail-fast）。
        if (descriptor->ownerUnit != kFailureLocateSourceUnit) {
            throw TrajectoryError(
                "trajectory/failure-locate/code-foreign-owner",
                "素材 reasonToken「" + record.reasonToken + "」所有者单元为「"
                    + descriptor->ownerUnit + "」——轨迹失败素材只允许"
                      " TRJ-*（trajectory 所有权）码");
        }

        // ---- 3b. 对象绑定：素材 subject 有值直接绑定；缺则回退值；两级
        // 皆缺＝调用方违约（ERR-01 subject 边界：用户级码必须携带合法非全零
        // subjectObjectId——diagnostics §4.2；TRJ 码全表用户级，无一豁免）。
        core::ObjectId subject = record.subject.value_or(
            request.subjectFallback.value_or(core::ObjectId{}));
        if (!subject.isValid()) {
            throw TrajectoryError(
                "trajectory/failure-locate/subject-missing",
                "素材（码「" + record.reasonToken + "」段 "
                    + std::to_string(record.segmentIndex)
                    + "）未绑定对象且请求面无 subjectFallback——用户级诊断"
                      "码必须绑定合法对象（ERR-01/diagnostics §4.2 subject"
                      " 边界）");
        }

        // ---- 3c. 文案面校验：cause/recommendedAction 非空（ERR-01 必填
        // 字段；空文案＝上游素材违约——不代填占位文案，不伪造建议动作
        // ——NFR-COR-03"非法输入拒绝，不钳制不静默"）。
        if (record.cause.empty() || record.recommendedAction.empty()) {
            throw TrajectoryError(
                "trajectory/failure-locate/text-missing",
                "素材（码「" + record.reasonToken + "」段 "
                    + std::to_string(record.segmentIndex)
                    + "）cause/recommendedAction 存在空串——ERR-01 两字段"
                      "必填，上游素材构造违约");
        }

        // ---- 3d. 比较型校验：码声明 requiresComparison 而素材未携带
        // 比较型三要素＝上游违约（UX-03"比较型校验额外包含实际值、要求值
        // 和单位"——缺任一侧即不完整；工厂侧 ComparisonMissing 同语义，
        // 本预检使错误归类落在"调用方素材违约"面）。反向（非比较型码携带
        // comparison）不拒绝——透传（见实现登记 4）。
        if (descriptor->requiresComparison && !record.comparison.has_value()) {
            throw TrajectoryError(
                "trajectory/failure-locate/comparison-missing",
                "素材码「" + record.reasonToken
                    + "」为比较型码而素材未携带 comparison——实际值/要求值/"
                      "单位三要素必须齐备（UX-03/ERR-01）");
        }

        // ---- 3e-i. 级别映射（core DiagData.hpp 登记的注册表强制映射：
        // "severity==Error ⇒ level==Error，其余 ⇒ Warning"——阻断轴级别
        // 由码表严重度机械推导，域内零自判；诊断条目的 severity/category
        // 本身由工厂自码表分配，不随本映射漂移）。
        const core::DiagnosticLevel level =
            (descriptor->severity == diagnostics::DiagnosticSeverity::Error)
                ? core::DiagnosticLevel::Error
                : core::DiagnosticLevel::Warning;

        // ---- 3e-ii. 诊断记录组装：定位上下文串（TRJ-06 段落落点）＋
        // 素材字段透传（cause/action/comparison 语义不改写——组装器只补
        // ERR-01 缺口，不改素材事实）。
        core::DiagnosticRecord diagRecord = core::DiagnosticRecord::make(
            record.reasonToken,                       // 稳定码（已注册——3a）
            subject,                                  // 对象绑定（3b）
            request.subjectLocalName,                 // 局部名投影（可空不伪造）
            request.subjectRuntimeName,               // 运行时名投影（可空）
            buildLocateContext(record.segmentIndex, record.phaseToken,
                               record.pathParameter), // 定位上下文（TRJ-06）
            record.cause,                             // 中文原因（透传）
            record.recommendedAction,                 // 中文建议动作（透传）
            record.comparison,                        // 比较型三要素（透传）
            level);                                   // 阻断轴级别（码表映射）

        // ---- 3e-iii. 关联身份块：评估路径 snapshotId/sliceId 必填（§14.4）
        // ＋来源 token（"trajectory"+"evaluate.failure-locate"——诊断目录
        // 按此归组到轨迹评估通道）；params 空 map＝TRJ 码 paramSchema "[]"
        // 的占位一致（diagnostics §4.5"不私造参数名"——工厂校验键集相等，
        // 多传即 ParamSchemaMismatch）；contractVersions 省略（码表版本由
        // 工厂自动追加 {"diag-code", registryVersion}——域内无额外契约
        // 版本需要标注）。
        diagnostics::DiagContext context;
        context.snapshotId = request.snapshotId;
        context.sliceId = request.sliceId;
        context.sourceUnit = kFailureLocateSourceUnit;
        context.sourceInterface = kFailureLocateSourceInterface;

        // ---- 3e-iv. 工厂创建（唯一创建入口——§9.2；码已注册/subject 边界/
        // 占位一致/比较型完整性经工厂复核）。工厂拒绝（DiagnosticsError）
        // 不捕获——域内预检通过后仍被拒＝工厂与码表状态不一致的实现缺陷，
        // 原样传播（fail-fast 不吞错；不转译不包装——CR-08 纪律）。
        diagnostics::DiagnosticEntry entry = factory.create(diagRecord, context);

        // ---- 3e-v. 收集（同序成对——entries[i]↔locatedRecords[i] 的
        // 回放联动是实现保证的后置，FailureLocateResult 注）。
        result.entries.push_back(std::move(entry));
        result.locatedRecords.push_back(record);
    }

    return result;
}

}  // namespace sdurws::ird::trajectory
