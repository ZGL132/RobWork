/**
 * @file   Capture.hpp
 * @brief  三维拾取与 TCP 捕获的域侧消费面（§9.8 L-R6/L-R7——WP-14-T06）：
 *         ui View3D 拾取结果/会话只读视图捕获值 → 写回前确认（REQ-08）→
 *         入草稿；会话状态错位→REQ-CAPTURE-STATE-STALE；未应用不失效。
 *
 * 设计依据：
 *   - units/requirements.md §9.8（L-R6"拾取结果→写回前确认→入草稿；
 *     未应用不失效"、L-R7"捕获当前 TCP→确认→构造 Fixed 任务点入草稿
 *     （来源 methodTag=captured-tcp）；会话过期→REQ-CAPTURE-STATE-STALE"、
 *     线程约束行"编辑器仅 UI 线程；确认/拾取经 ui 桥 marshal"）、§9.6
 *     （REQ-CAPTURE-STATE-STALE 行——warning，本头为该码唯一产码消费者）、
 *     §5.1（任务点 source 行：UserProvided（手工/捕获，methodTag=
 *     captured-tcp 等））、§4.6（草稿态——捕获写的是编辑器工作集，非
 *     修订）、§11（WP-14-T06 行）、§14.2 R-REQ-5（会话状态与草稿基线
 *     错位——确认流＋STALE 码＋来源标记缓解）
 *   - units/ui.md §9.2（确认对话桥——域侧只消费确认**结果**；对话装配
 *     与 marshal 归 ui，本单元不重定义确认流）、View3DContract.hpp
 *     （KIN-06/AT-04——拾取/回写类交互不产生修订不触发失效；本头的
 *     "确认前不落草稿"是该红线在域侧写路径上的镜像钉）
 *   - 需求 REQ-08（三维场景写回前必须确认）、REQ-10（几何特征拾取与
 *     TCP 捕获进入草稿；未应用不失效）、AT-23、SA-15（确认凭据语义）
 *   - 任务契约 tasks/foundation/WP-14-T06.json acceptance 3/4
 *
 * 背景说明（域侧消费面的边界——第一读者须知）：
 *   拾取交互（View3D 拾取态）与 TCP 捕获（会话态/runtime 只读视图）的
 *   **采集**归 ui 阶段 B 实现；本头只定义采集结果进入需求草稿的域侧
 *   通道，三条不变量钉死在接口形状上（acceptance 4"接口面审查"的
 *   落实——不需要读实现即可审查）：
 *   ①写回前必须确认：两个写入口（captureTcpAsFixedPoint/
 *     applyPickToOrientation）都把 CaptureConfirmation 作为**必填参数**
 *     ——不存在无确认参数的重载/缺省放行形态；state != Confirmed 一律
 *     零数据变更返回（负向用例面）。确认凭据是 ui 确认对话桥（ui.md
 *     §9.2 三要素呈现）的产出，域侧只读消费、不自行制造（领域服务无
 *     对话能力，也不得伪造凭据——SA-15 主体+UTC 由 ui/project 采集）。
 *   ②草稿是唯一写目标：服务只持有 IRequirementEditor（§4.6 编辑态），
 *     接口面无任何 project 写端口/命令端口参数——捕获/拾取数据在物理
 *     上不存在绕过草稿直达修订的通道（未应用不失效的结构保证；显式
 *     应用才经命令族产生修订——§9.1 D-REQ-9）。
 *   ③来源恒留痕：捕获产物条目 source.methodTag="captured-tcp"（§5.1
 *     来源标记行）——R-REQ-5 缓解的第二半（可追溯重捕：条目自证来自
 *     捕获而非手工输入）。
 *
 * REQ-CAPTURE-STATE-STALE 语义（warning——不阻断）：CapturedTcpPose.
 * sessionRevisionId（捕获时会话基线）≠ editor.draftStatus().
 * baseRevisionId（草稿基线）＝会话状态与草稿基线错位（R-REQ-5）——
 * 登记警告后流程继续（捕获值仍经确认门写草稿：错位由确认对话呈现给
 * 用户知情放行，卡面 L-R7 语义）；重捕可消除（UserRetry 动作族）。
 * 空串口径（实现决策——随卡 §14.6 登记）：编辑器闭包抽象不携带修订
 * 身份，基线未标注时 draftStatus().baseRevisionId 为空串（Editor.hpp
 * 载入注）——严格相等比较下，带会话基线的捕获相对"未标注"基线恒登记
 * STALE（保守知情：基线未标注即无法证明一致；命令提交面补齐基线 id 后
 * 对账自然精确化）。该对账仅 L-R7 承载（§9.6 行语义钉在"TCP 捕获时"
 * ——L-R6 拾取无会话修订键，不扩面）。
 *
 * 线程约束：编辑器仅 UI 线程（§3.4 总约定 2）——本头两服务方法必须在
 * UI 线程调用（拾取/捕获 UI 流程天然在 UI 线程发起）；确认凭据值本身
 * 为纯值无同步需求。
 * 确定性（NFR-COR-01/02）：流程序固定（确认门→参数面→STALE 对账→
 * 构造校验→草稿写入）；同输入（含同凭据值）→同产出（objectId 除外
 * ——身份生成本质随机，与 createPoint 同口径）。
 */

#ifndef IRD_REQUIREMENTS_CAPTURE_HPP
#define IRD_REQUIREMENTS_CAPTURE_HPP

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Vector3D.hpp>  // rw::math::Vector3D<double>——位置/欧拉角（SI：m/rad）

#include <sdurws/ird/core/DiagData.hpp>                  // DiagnosticRecord/ConfirmationState/ConfirmationCredential
#include <sdurws/ird/requirements/Editor.hpp>            // IRequirementEditor——草稿唯一写目标（§4.6 编辑态）
#include <sdurws/ird/requirements/Errors.hpp>            // RequirementError——值面拒绝
#include <sdurws/ird/requirements/Readiness.hpp>         // CheckContext——闭包元数据（拾取目标浅核对）
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // TaskPoint/ToleranceSpec/OrientationFeature 等

namespace sdurws::ird::requirements {

// =====================================================================
// 确认凭据与输入值（ui 产出 → 域侧只读消费；纯值）
// =====================================================================

/**
 * @brief 写回确认（REQ-08"写回前必须确认"的域侧承载——ui §9.2 确认
 *        对话桥的**结果**值，域侧只读消费）。
 *
 * 不变量（C-2 同构——core::ConfirmableFinding 的确认语义对齐）：
 *   - state==Confirmed ⇔ credential.principal 非空（Confirmed 工厂强制
 *     非空主体；凭据的 confirmedAtUtc 由 ui/project 采集——域侧不制造
 *     时刻值）；Rejected 工厂不携凭据（取消/关闭拾取态＝用户拒绝写回
 *     ——零数据变更）。
 *   - state==Pending 在域侧视为未确认（与 Rejected 同走零数据变更——
 *     Pending 只是对话未决的中间态，到达域服务即对话已关）。
 *
 * 线程安全：纯值。确定性：同值同判。
 */
struct CaptureConfirmation {
    core::ConfirmationState state = core::ConfirmationState::Rejected;  ///< 确认状态（缺省 Rejected＝保守不放行）
    core::ConfirmationCredential credential{};                          ///< 确认凭据（仅 Confirmed 有语义——主体＋UTC）

    /// @brief Confirmed 工厂（principal 非空强制——违约抛
    ///        std::invalid_argument，fail-fast：伪造凭据是调用方契约违约）。
    static CaptureConfirmation confirmed(std::string principal,
                                         std::chrono::system_clock::time_point confirmedAtUtc);

    /// @brief Rejected 工厂（取消/关闭拾取态——零数据变更语义的输入面）。
    static CaptureConfirmation rejected();

    /// @brief 不变量核查（Confirmed ⇔ principal 非空；见结构体注）。
    bool wellFormed() const noexcept;

    bool operator==(const CaptureConfirmation& o) const noexcept
    {
        return state == o.state && credential == o.credential;
    }
    bool operator!=(const CaptureConfirmation& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 几何特征拾取结果（ui View3D 拾取态产出——L-R6 域侧输入值；
 *        REQUIREMENTS REQ-10"从三维视图几何特征拾取参考对象（夹具/
 *        零件 Frame）"）。
 *
 * target 语义：Frame 拾取（参考对象是坐标系）→ ModelFrame（robot-design
 * 承载对象）或 SceneObject（场景对象坐标系），feature 空 → 产出
 * AlignFrame 规则；几何特征拾取 → SceneObject＋feature 非空 → 产出
 * AlignGeometryNormal 规则（MDL-15 场景对象浅引用——引用以 ObjectId
 * 承载，卡 §2.5 ♻ 行"拾取契约归 ui View3D；引用以 ObjectId 承载"）。
 *
 * 生命周期：纯值（ui 拾取态的一次产出快照）；域侧不回写拾取态。
 */
struct PickedFeature {
    RequirementReference target;  ///< 拾取目标（ModelFrame/SceneObject——浅引用，ObjectId 承载）
    std::optional<OrientationFeature> feature;  ///< 几何特征（Frame 拾取＝空；特征拾取必填）
    bool invertNormal = false;                  ///< 法向取反（仅特征拾取有语义）

    bool operator==(const PickedFeature& o) const
    {
        return target == o.target && feature == o.feature && invertNormal == o.invertNormal;
    }
    bool operator!=(const PickedFeature& o) const { return !(*this == o); }
};

/**
 * @brief TCP 捕获值（ui 会话态/runtime 只读视图产出——L-R7 域侧输入值；
 *        KIN-06 会话语义：会话只读视图的当前 TCP 位姿，零模型写入）。
 *
 * 位姿为 refFrame 系下表达（与任务点 refFrame 同语义——§4.3 头行）；
 * Z-Y-X 欧拉序与 Fixed 规则参数字面同构（捕获→Fixed 任务点无换算——
 * 数值直存，NFR-COR-03 不改写捕获值）。sessionRevisionId 是捕获瞬间
 * 会话权威基线的修订 id 规范文本（STALE 对账键——R-REQ-5）。
 */
struct CapturedTcpPose {
    rw::math::Vector3D<double> position{0.0, 0.0, 0.0};  ///< TCP 位置（m，refFrame 系）
    rw::math::Vector3D<double> rpy{0.0, 0.0, 0.0};       ///< TCP 姿态（rad，Z-Y-X 欧拉序，refFrame 系）
    RequirementReference refFrame;                       ///< 捕获值所属参考系（forScene 槽——World 缺省合法）
    std::string sessionRevisionId;                       ///< 捕获时会话基线修订 id（STALE 对账键；空串＝会话无基线）

    bool operator==(const CapturedTcpPose& o) const
    {
        return position == o.position && rpy == o.rpy && refFrame == o.refFrame
            && sessionRevisionId == o.sessionRevisionId;
    }
    bool operator!=(const CapturedTcpPose& o) const { return !(*this == o); }
};

/**
 * @brief TCP 捕获写回请求（L-R7 域侧输入面——值语义，调用方所有）。
 *
 * confirmation 必填（无缺省放行形态——接口面审查锚①）；siblingNames
 * 由编辑器从工作集填充（I-REQ-3 集合内名称唯一核对）。
 */
struct CaptureTcpRequest {
    CapturedTcpPose captured;                 ///< 捕获值（本请求的写回数据源）
    std::string pointName;                    ///< 新任务点语义名（I-REQ-3）
    std::optional<RequirementReference> tcpRef;  ///< 捕获所用工具/TCP 引用（可选——浅引用）
    ToleranceSpec tolerance{};                ///< 容差（要求值；缺省＝设计默认，§4.3）
    CaptureConfirmation confirmation;         ///< 写回确认（必填——REQ-08）
    std::vector<std::string> siblingNames{};  ///< 集合既有条目名（I-REQ-3 边界核对）
};

/**
 * @brief 拾取写回请求（L-R6 域侧输入面）。
 *
 * 写回语义：把拾取结果装配为姿态规则（Frame→AlignFrame／特征→
 * AlignGeometryNormal），**更新工作集中既有的目标任务点**（targetPointId
 * ——拾取面板对既有条目行发起；新建条目走手工/createPoint 路径，不在
 * 拾取流扩面）。
 */
struct ApplyPickRequest {
    PickedFeature picked;               ///< 拾取结果（本请求的写回数据源）
    core::ObjectId targetPointId;       ///< 目标任务点（工作集内既有条目——O-36 模型内锚）
    CaptureConfirmation confirmation;   ///< 写回确认（必填——REQ-08）
};

// =====================================================================
// 产出（值语义）
// =====================================================================

/**
 * @brief 拾取/捕获写回产出（两写入口共用的值承载）。
 *
 * 三态互斥（调用方按序判读）：
 *   - accepted==true：草稿已更新（编辑器编辑数 +1、局部撤销入栈）——
 *     draftEntry 为入草稿条目（捕获条目带 methodTag=captured-tcp）；diags
 *     可携 STALE 警告（写回成功但会话错位——R-REQ-5 缓解语义）；
 *   - rejectedUnconfirmed==true：确认门拒绝（state!=Confirmed／凭据
 *     违约）——**零数据变更**（工作集/撤销栈/编辑计数全部不变），diags
 *     空（取消是正常流，不产诊断——Canceled 轴非错误）；
 *   - error.code 有效（前两态皆否）：参数/引用失败拒绝——零数据变更，
 *     error 为域错误值、diags 为可定位诊断（回指条目——acceptance 2
 *     同款定位面）。
 */
struct CaptureOutcome {
    bool accepted = false;              ///< true＝草稿已更新
    bool rejectedUnconfirmed = false;   ///< true＝确认门拒绝（零数据变更）
    TaskPoint draftEntry{};             ///< accepted：入草稿/已更新条目
    std::vector<core::DiagnosticRecord> diags;  ///< 登记面诊断（STALE 警告等）
    RequirementError error{};           ///< 拒绝面：域错误值（accepted/rejectedUnconfirmed 时无效）

    bool operator==(const CaptureOutcome& o) const;
    bool operator!=(const CaptureOutcome& o) const { return !(*this == o); }
};

// =====================================================================
// IRequirementCaptureService——接口（L-R6/L-R7 域侧唯一写入口族）
// =====================================================================

/**
 * @brief 三维拾取/TCP 捕获域侧服务（§9.8 L-R6/L-R7 的域侧消费契约）。
 *
 * ★ 接口面审查锚（acceptance 4——"域侧接口不存在任何绕过确认直接写
 * 草稿的入口"）：本接口仅两方法，均强制 CaptureConfirmation 必填参数
 * 且确认门先于一切写路径；本单元无第三个能把拾取/捕获数据写入草稿的
 * 公开入口（编辑器 applyEdit 是通用编辑面，不承载拾取/捕获语义——
 * 拾取/捕获数据只有经本服务的确认门才能成形为条目值）。
 *
 * 线程约束：仅 UI 线程（编辑器非线程安全——§3.4 总约定 2）。
 */
class IRequirementCaptureService {
public:
    virtual ~IRequirementCaptureService() = default;

    /**
     * @brief L-R7：TCP 捕获 → 确认 → 构造 Fixed 任务点入草稿。
     *
     * 流程序（固定——确定性来源；短路）：
     *   ①确认门：!request.confirmation.wellFormed() 或 state!=Confirmed
     *     → rejectedUnconfirmed＝true 返回（零数据变更——取消/关闭拾取
     *     态的负向语义）；
     *   ②捕获值参数面：position/rpy 含非有限值 → error（IllegalTolerance
     *     族值面——不产诊断码：入草稿前的输入值面拒绝，构造边界纪律）
     *     返回；
     *   ③STALE 对账：request.captured.sessionRevisionId 与
     *     editor.draftStatus().baseRevisionId 不一致 → REQ-CAPTURE-STATE-
     *     STALE（warning）登记入 diags——**不阻断**（确认门已过＝用户
     *     将在知情下写回；R-REQ-5 缓解语义）；
     *   ④条目构造＋校验：装配 TaskPointSpec（Fixed 规则＝捕获 rpy 字面、
     *     全约束掩码、source=UserProvided＋methodTag="captured-tcp"），
     *     经 TaskPointService.createPoint 单点校验（I-REQ-3/5 全链——
     *     NFR-MNT-04）→ 失败：error 返回（零数据变更）；
     *   ⑤草稿写入：editor.applyEdit(TaskPoint) → 拒绝：error 返回（编辑
     *     器校验链同源——零数据变更由编辑器保证）；接受：accepted＝
     *     true，draftEntry＝入草稿条目。
     *
     * @param editor  [in,out] 需求编辑器（草稿唯一写目标；须已 loadBaseline
     *                ——未载入由编辑器拒绝面兜底）
     * @param request [in] 捕获写回请求（确认门必过）
     * @param closure [in] 闭包 objectRefs 元数据（refFrame/tcpRef 浅核对
     *                ——§8.1；World/DefaultTcp 缺省恒过）
     * @return 三态产出（见 CaptureOutcome 注）
     *
     * @throws std::invalid_argument confirmation Confirmed 态 principal
     *         为空（工厂已拦——防御面）
     *
     * 线程约束：仅 UI 线程。确定性：objectId 除外同输入同产出。
     */
    virtual CaptureOutcome captureTcpAsFixedPoint(IRequirementEditor& editor,
                                                  const CaptureTcpRequest& request,
                                                  const CheckContext& closure) const = 0;

    /**
     * @brief L-R6：拾取结果 → 确认 → 目标任务点姿态规则更新入草稿。
     *
     * 流程序（固定；短路）：
     *   ①确认门（同上——零数据变更拒绝）；
     *   ②拾取目标装配：feature 空 → AlignFrame{targetFrame=picked.target}
     *     ；feature 非空 → target 须为 SceneObject（几何特征挂在场景
     *     对象——违约：参数面 error）→ AlignGeometryNormal{targetScene-
     *     Object, feature, invertNormal}；
     *   ③拾取目标浅核对：picked.target 对 closure 浅核对（存在＋token
     *     匹配——§8.1；拾取自过期场景的悬空结果在此拦截）→ 失败：error
     *     ＋REQ-READY-REF-MISSING 可定位诊断（回指目标任务点——诊断
     *     subject=目标条目 id/localName=条目名）；
     *   ④目标任务点存在性：工作集点集内定位 targetPointId → 缺失：
     *     error（MalformedPayload——调用方引用了不存在的条目，值面拒绝
     *     与编辑器 Remove 缺失同轨）；
     *   ⑤草稿写入：拷贝目标任务点、以新规则替换 pose.orientation、
     *     editor.applyEdit(upsert) → 拒绝：error 返回；接受：accepted＝
     *     true，draftEntry＝更新后条目（其余字段原样保留——拾取只动
     *     姿态规则，NFR-COR-03 不顺带改写）。
     *
     * @param editor  [in,out] 需求编辑器（同上）
     * @param request [in] 拾取写回请求
     * @param closure [in] 闭包 objectRefs 元数据（拾取目标浅核对）
     * @return 三态产出（STALE 不在本流——§9.6 行语义钉在 TCP 捕获）
     *
     * 线程约束：仅 UI 线程。确定性：同输入同产出。
     */
    virtual CaptureOutcome applyPickToOrientation(IRequirementEditor& editor,
                                                  const ApplyPickRequest& request,
                                                  const CheckContext& closure) const = 0;
};

/// 产品实现（无状态——可默认构造；校验复用 TaskPointService 单点，
/// NFR-MNT-04；§3.4 总约定 1）。
class RequirementCaptureService final : public IRequirementCaptureService {
public:
    CaptureOutcome captureTcpAsFixedPoint(IRequirementEditor& editor,
                                          const CaptureTcpRequest& request,
                                          const CheckContext& closure) const override;
    CaptureOutcome applyPickToOrientation(IRequirementEditor& editor,
                                          const ApplyPickRequest& request,
                                          const CheckContext& closure) const override;
};

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_CAPTURE_HPP
