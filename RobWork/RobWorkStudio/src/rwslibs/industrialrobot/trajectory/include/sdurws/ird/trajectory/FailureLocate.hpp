/**
 * @file   FailureLocate.hpp
 * @brief  失败段诊断定位（WP-16-T09）——把 T04~T08 各管线产出的失败素材
 *         （FailedSegmentRecord，TrjTypes.hpp）组装为满足 ERR-01 字段义务
 *         的稳定诊断条目（DiagnosticEntry），定位到具体段落与原因（TRJ-06
 *         的域内唯一出口；units/trajectory.md §14.4 诊断构造的 trajectory
 *         侧执行面）。
 *
 * 设计依据：
 *   - units/trajectory.md §14.4（diagnostics 协作——"诊断构造：经
 *     IDiagnosticFactory.create（码已注册校验、subject/比较型完整性、
 *     DiagContext 必填——评估路径 snapshotId/sliceId 必填、任务路径
 *     task 五元组必填）；产生者仅登记，判定不越权"）；§6.2/§6.7
 *     （FailedSegmentRecord——TRJ-06 定位载体："失败段序号＋reasonToken
 *     ＋段内定位参数 s＋比较型字段（超限类必填）＋cause/recommendedAction
 *     为中文"）；§12.6（超限与失败定位——时间侧产码路径）；§9.2（连续性
 *     破坏/分支跳变诊断示例）；§7.6（无路径素材轨——"IK 无解→段失败定位
 *     （TRJ-NO-PATH 素材）"）；§8.3/§8.5（分支跳变/奇异邻域素材）；§14.1.6
 *     （phase token 词表——失败阶段定位）；§18 T09 行（"失败段定位到段落
 *     与原因码用例"）；§20 追踪矩阵 TRJ-06 行（设计章节 §7.6/§8.5-6/§9.2/
 *     §12.6/§14.4；验证行 V-03/07/16/17/18；任务 T09）
 *   - units/diagnostics.md §4.2/§4.5（信封字段与注册协议——工厂校验链：
 *     码已注册/subject 边界〔用户级码必须携带合法非全零 subjectObjectId〕/
 *     比较型三要素/paramSchema 占位一致；§9.2 IDiagnosticFactory 同接口）
 *   - 需求 TRJ-06（P0："对无路径、分支跳变、奇异邻域和限制超标给出具体
 *     段落与原因"——本头即其 R1 承载面）、ERR-01（"稳定诊断项以
 *     subjectObjectId 绑定对象，并包含局部/运行时名称、原因和建议动作；
 *     比较型校验额外包含实际值、期望值和单位；不适用字段显式标记"）、
 *     NFR-COR-02（确定性——同输入等价输出）、NFR-COR-03（非法输入拒绝，
 *     不钳制不静默）
 *   - 任务契约 tasks/foundation/WP-16-T09.json（acceptance 1——无路径/
 *     分支跳变/奇异邻域/限制超标定位到具体段落与原因用例；acceptance 2
 *     ——诊断字段满足 ERR-01；knownPitfalls P-TRJ-11——本头签名形态复用
 *     T07/T08 既有请求/结果结构体＋自由函数入口形态（SmoothRequest/
 *     SmoothOutcome、TimeParamRequest/TimeParamResult 同款），不发明新
 *     签名风格）
 *
 * 背景说明（本头在轨迹链路中的位置——第一读者须知）：
 *   T04~T08 各管线（Ptp/Sequence/CartesianLine/Continuity/Planner/Smooth/
 *   Recheck/TimeParam）在各自失败语义点产出 **素材**（FailedSegmentRecord：
 *   段序号＋阶段 token＋TRJ-* 稳定码＋段内定位＋中文原因/建议动作＋比较型
 *   字段）。素材不是诊断——诊断实例的产出受 diagnostics 工厂校验链约束
 *   （§14.4：码已注册、subject 边界、比较型完整性、上下文必填）。本头是
 *   这两层之间的**组装器**：把素材逐条经 IDiagnosticFactory.create 转成
 *   可入目录/可入证据的 DiagnosticEntry，并在转换中补齐 ERR-01 要求的
 *   定位上下文描述（对象/上下文/原因/建议动作；比较型含实际值/要求值/
 *   单位）。
 *
 *   定位语义（TRJ-06"具体段落与原因"的落点）：
 *   - **段落**＝FailedSegmentRecord.segmentIndex（0 基段序号；全轨迹级失败
 *     ＝0xFFFFFFFF）＋pathParameter（段内归一化路径参数 s∈[0,1]，可定位到
 *     点时必填）＋phaseToken（失败阶段——§14.1.6 词表）；三者以确定性中文
 *     上下文串写入 DiagnosticRecord.context（ERR-01"上下文"字段）。
 *   - **原因**＝reasonToken（TRJ-* 稳定码，进 DiagnosticRecord.code——
 *     诊断条目的稳定键）＋cause（中文原因，透传）。
 *   - **对象绑定**（ERR-01 subject 边界）：素材 subject 有值直接绑定（路点/
 *     段来源任务点/碰撞对象对——TrjTypes.hpp 注）；素材未绑对象时落到请求
 *     面 subjectFallback（评估装配面提供的被评估对象锚——如序列/轨迹对象）；
 *     两者皆缺＝调用方契约违约（fail-fast——用户级诊断码不允许无对象条目，
 *     diagnostics §4.2 subject 边界）。
 *
 *   本头职责边界（防越权，N7/PA-1）：只做"素材→诊断条目"的字段组装与
 *   校验，**不判定**失败等级是否成立（判定归 evidence 汇总——§14.2.5"失
 *   败/取消/数据不足交给 evidence"）、不把素材轨改判为任务不可行（§7.6
 *   "搜索未果→DataInsufficient 素材，不判不可行"）、不消费 policy（阈值
 *   零触碰）、不产生任何修订（零副作用）。收集各管线产物的编排归评估器
 *   组装面（WP-16-T10）——本头只消费调用方投递的素材序列，不做管线编排
 *   （NFR-MNT-04：不预建无消费者的收集设施）。
 *
 * 头文件依赖纪律（冒烟模式安全——TrjTypes.hpp 同款）：本头对 rw 零消费
 * （素材与诊断条目均为纯值类型）；diagnostics 工厂/注册表经公共头消费
 * （依赖白名单七边内的 diagnostics 边——卡 §3.2）。locateFailures 的定义
 * 在实现 TU（src/FailureLocate.cpp）——**两模式皆可编译链接**（纯值面，
 * 不构造 rw::math::Q 对象，无框架库符号引用；与 TrjTypes.cpp 同款非条件
 * 源）。
 *
 * 线程安全：全部值类型并发只读安全；locateFailures 为纯函数（零副作用/
 * 零修订/零写盘——§15.0），单线程使用；factory.create 的并发语义随实现
 * （DiagnosticsFactory 并发安全——diagnostics 卡 §9.2）。确定性：同请求
 * 等价输出（context 串格式化/字段映射/校验序全确定——NFR-COR-02）；
 * emittedAtUtc/entryId 由工厂分配（会话态字段，不属值等价判定面）。
 */

#ifndef IRD_TRAJECTORY_FAILURELOCATE_HPP
#define IRD_TRAJECTORY_FAILURELOCATE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>       // core::DiagnosticRecord/ComparativeFields（诊断记录契约）
#include <sdurws/ird/core/Digest.hpp>         // core::ContentIdentity（评估路径身份——§14.4 必填）
#include <sdurws/ird/core/Identity.hpp>       // core::ObjectId（subject 绑定——ERR-01）
#include <sdurws/ird/diagnostics/Catalog.hpp> // diagnostics::DiagnosticEntry/DiagContext（§4.2 信封）
#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // diagnostics::IDiagnosticRegistry（码表查询面）
#include <sdurws/ird/diagnostics/Factory.hpp>    // diagnostics::IDiagnosticFactory（唯一创建入口——§9.2）
#include <sdurws/ird/trajectory/TrjTypes.hpp> // FailedSegmentRecord（素材载体——§6.2）

namespace sdurws::ird::trajectory {

// =====================================================================
// 域常量（唯一书写点——算法/测试共用，禁第二处字面量）
// =====================================================================

/// 全轨迹级失败的段序号保留值（§6.2 FailedSegmentRecord 注"全轨迹级失败
/// ＝0xFFFFFFFF＋phase 说明"；TrjTypes.hpp 同值缺省——本常量供定位判读与
/// 测试引用，TrjTypes.hpp 已落位面不回改）。
inline constexpr std::uint32_t kWholeTrajectorySegmentIndex = 0xFFFFFFFFu;

/// 诊断上下文的来源单元 token（DiagContext.sourceUnit——diagnostics §4.2
/// 必填 ≤32 字符；trajectory 所有权域＝TRJ 前缀所有者，§4.5 前缀-所有权表）。
inline constexpr char kFailureLocateSourceUnit[] = "trajectory";

/// 诊断上下文的来源接口 token（DiagContext.sourceInterface——diagnostics
/// §4.2 必填 ≤64 字符；"<面>.<通道>"点分形态，"commands.submit" 同款）。
inline constexpr char kFailureLocateSourceInterface[] = "evaluate.failure-locate";

// =====================================================================
// 请求/结果（T07/T08 既有请求-结果结构体形态的 T09 同款——P-TRJ-11 规避）
// =====================================================================

/**
 * @brief 失败段诊断定位请求（素材投递面——全部值语义）。
 *
 * 前置（校验序见 locateFailures；违约一律 TrajectoryError fail-fast——
 * §15.0"调用方契约违约 fail-fast，不转诊断码"）：
 *   - snapshotId/sliceId 必须为合法内容身份（isValid 非全零——§14.4
 *     "评估路径 snapshotId/sliceId 必填"；空身份＝中间态误投，拒绝）；
 *   - records 逐条（见 locateFailures 步骤 3）：
 *     reasonToken 已注册且属 trajectory 所有权、subject（素材值或回退值）
 *     合法非全零、cause/recommendedAction 非空、比较型码素材携带 comparison；
 *   - records 允许为空（合法——本次评估零失败，返回空结果，非错误）。
 *
 * 单位：素材内部物理量单位随素材自带（比较型字段 UnitToken——UX-03 三
 * 要素"实际值/要求值/单位"的单位侧；本头只透传不换算——换算归产生管线
 * 的量纲语义）。值语义纯结构；线程安全（借用引用在调用期存活即可）。
 */
struct FailureLocateRequest {
    /// 待定位素材序列（T04~T08 各管线产物原样投递；**输入序＝输出序**——
    /// 调用方按管线编排序投递即得确定性输出，本头不重排〔管线编排序本身
    /// 是失败链条语义的一部分，排序会丢信息〕）。
    std::vector<FailedSegmentRecord> records;
    /// 绑定快照内容身份（CON-01——§14.4 评估路径诊断必填；isValid 必须）。
    core::ContentIdentity snapshotId;
    /// 绑定切片内容身份（CON-05——同上必填；进 DiagContext.sliceId）。
    core::ContentIdentity sliceId;
    /// 对象绑定回退（可空）：素材 subject 未绑对象时落到本值（评估装配面
    /// 提供的被评估对象锚——如被评估序列/轨迹的对象 ID；ARC-04 追溯链的
    /// 端点）。素材与回退皆缺→TrajectoryError（ERR-01 subject 边界——
    /// 用户级码不允许无对象条目，diagnostics §4.2）。
    std::optional<core::ObjectId> subjectFallback;
    /// 局部名投影（可空——ERR-01"局部名称"；工程模型内的显示名。无名称
    /// 映射时保持 nullopt——不伪造，NFR-COR-03；投影作用于本批全部条目
    /// 〔名称解析归 runtime ⑥端口/评估装配面，域内不逐素材解析〕）。
    std::optional<std::string> subjectLocalName;
    /// 运行时名投影（可空——ERR-01"运行时名称"；R-4：名称解析归 runtime，
    /// 本头只透传装配面已解析值，零拼接/剥离）。
    std::optional<std::string> subjectRuntimeName;

    bool operator==(const FailureLocateRequest& o) const
    {
        return records == o.records && snapshotId == o.snapshotId
            && sliceId == o.sliceId && subjectFallback == o.subjectFallback
            && subjectLocalName == o.subjectLocalName
            && subjectRuntimeName == o.subjectRuntimeName;
    }
    bool operator!=(const FailureLocateRequest& o) const { return !(*this == o); }
};

/**
 * @brief 失败段诊断定位结果（值语义纯结构；线程安全）。
 *
 * 字段联动（实现保证）：entries 与 locatedRecords **一一对应且同序**——
 * entries[i] 是 locatedRecords[i] 经工厂产出的诊断条目（回放面：审查者
 * 可从条目反查原始素材的段序号/路径参数/阶段，核对定位语义未被组装改写）。
 * 空输入→两向量皆空（合法结局——零失败零诊断）。
 */
struct FailureLocateResult {
    /// 定位诊断条目（经 IDiagnosticFactory.create 产出——码已注册/subject
    /// 边界/比较型完整性/上下文必填均经工厂校验链；entryId/emittedAtUtc/
    /// 分类/严重度由工厂与码表分配——产生者仅登记，判定不越权，§14.4）。
    std::vector<diagnostics::DiagnosticEntry> entries;
    /// 与 entries 一一对应的原始素材回放（定位面——同序同长，实现保证）。
    std::vector<FailedSegmentRecord> locatedRecords;

    bool operator==(const FailureLocateResult& o) const
    {
        return entries == o.entries && locatedRecords == o.locatedRecords;
    }
    bool operator!=(const FailureLocateResult& o) const { return !(*this == o); }
};

// =====================================================================
// 失败段诊断定位入口（§14.4 诊断构造的 trajectory 侧执行面）
// =====================================================================

/**
 * @brief 把失败素材逐条组装为稳定诊断条目（TRJ-06"具体段落与原因"的
 *        唯一出口；ERR-01 字段义务的执行点）。
 *
 * 执行序（每步语义见行内注释）：
 *   1. 请求面校验（snapshotId/sliceId isValid——§14.4 评估路径必填；违约
 *      →TrajectoryError fail-fast，token "trajectory/failure-locate/..."）；
 *   2. 空素材序列→返回空结果（合法结局，非错误）；
 *   3. 逐素材组装（输入序；每条五步）：
 *      a. 码面校验：reasonToken 非空、registry.find 命中（未注册码＝上游
 *         产码违约——禁字符串拼码纪律的执行点）、ownerUnit=="trajectory"
 *         （TRJ 前缀所有权——防串域码投递）；
 *      b. 对象绑定：subject＝素材值，缺则回退值；仍缺→TrajectoryError
 *         （ERR-01 subject 边界）；
 *      c. 文案面校验：cause/recommendedAction 非空（ERR-01 必填字段——
 *         空文案＝上游素材违约，不代填不伪造）；
 *      d. 比较型校验：码描述符 requiresComparison==true 且素材缺
 *         comparison→TrajectoryError（UX-03 三要素完整性——比较型码的
 *         条目必须携带实际值/要求值/单位；工厂侧 ComparisonMissing 的域内
 *         同义预检，双道防线同语义）；
 *      e. 记录构造＋工厂创建：级别映射（severity==Error→DiagnosticLevel::
 *         Error，其余→Warning——core DiagData.hpp 登记的注册表强制映射，
 *         域内按码表描述符落值）＋确定性中文定位上下文串（段落＋阶段＋
 *         段内参数——TRJ-06"具体段落"落点）→core::DiagnosticRecord::make
 *         →factory.create（DiagContext 携带 snapshotId/sliceId＋来源
 *         token；params 空 map——TRJ 码 paramSchema="[]"，占位一致性）。
 *   4. 工厂拒绝（DiagnosticsError：CodeUnknown/CodeDeprecated/Usage 等）
 *      不捕获不转译——经本头域内预检后工厂仍拒绝＝实现缺陷，原样传播
 *      （fail-fast，不吞错——AGENTS §3 错误处理红线）。
 *
 * 取消：本函数为纯内存组装（无长计算），零取消面（§15.0 取消行不适用）。
 *
 * @param request  [in] 定位请求（FailureLocateRequest 前置见其注）
 * @param registry [in] 稳定码注册表（码元数据唯一权威——TRJ-* 码须已注册，
 *                 装配期经 registerTrajectoryCodes 注册；非拥有引用，调用
 *                 期存活即可）
 * @param factory  [in,out] 诊断工厂（唯一创建入口——§9.2；本函数只调
 *                 create，不触碰转译注册面；非拥有引用）
 *
 * @return 定位结果（entries 与 locatedRecords 一一对应——FailureLocateResult 注）
 *
 * @throws TrajectoryError 调用方契约违约（token "trajectory/failure-locate/
 *         ..."——身份缺 失/码未注册/码非本域/对象缺失/文案缺失/比较型缺失）
 * @throws diagnostics::DiagnosticsError 工厂校验链拒绝（域内预检通过后的
 *         拒绝＝实现缺陷——原样传播不吞）
 *
 * 纯函数（零副作用/零修订/零写盘）；线程安全（单线程使用）；确定性（同
 * 请求等价输出——context 串格式化/校验序/映射全确定；entryId/emittedAtUtc
 * 为工厂会话态分配，不属值等价判定面——NFR-COR-02）。
 */
FailureLocateResult locateFailures(const FailureLocateRequest& request,
                                   const diagnostics::IDiagnosticRegistry& registry,
                                   diagnostics::IDiagnosticFactory& factory);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_FAILURELOCATE_HPP
