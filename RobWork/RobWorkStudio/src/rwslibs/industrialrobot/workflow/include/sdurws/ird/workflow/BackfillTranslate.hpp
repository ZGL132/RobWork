/**
 * @file   BackfillTranslate.hpp
 * @brief  P-SEL-9 转译编排——selection 回填记录（sel-device-backfill）到
 *         modeling 权威写入（apply-drivetrain-design 命令链）的跨命令
 *         编排核（宿主收口批次 ASM-WF 落位）。
 *
 * 设计依据：
 *   - units/selection.md §19.1 P-SEL-9（登记原文要点：回填记录对象与权威
 *     robot-drivetrain 对象写入的跨命令编排对齐——selection 回填命令落
 *     记录面；权威 robot-drivetrain 对象字节归 modeling Codec 唯一实现，
 *     selection 零 modeling 编译边、自拼字节＝第二实现；**L5 装配层把
 *     回填字段转译编排为 modeling apply-drivetrain-design 提交**；
 *     "逐轴回填记录与 modeling.md §4.7 单值 catalogBackfill schema 的
 *     逐轴缺口由该编排形态承载"）、§12.3 T09 落位注记（不冒用 modeling
 *     robot-drivetrain token——该 token 是修订闭包解析器路由键）、
 *     §19.3 T09 落位细化②
 *   - units/workflow.md §3.2 依赖白名单八边（R-1：workflow 不直链
 *     modeling/selection 等业务域单元——本头以两端口接缝承载跨域消费，
 *     optimization Applier"注入缝＋canonical 字节为通道载荷"同款手法）、
 *     §9 协作表 modeling 行（"不直链——模板参数化与 Model Diff 均经公共
 *     数据契约/命令端口"）
 *   - 需求 SEL-10/MDL-16（选型回填）、AT-30（回填后四域复算提示）、
 *     PA-1（权威唯一——robot-drivetrain canonical 字节唯一实现归
 *     modeling Codec）、NFR-MNT-03（防第二实现）
 *   - 先例：optimization Applier.hpp（kApplyRobotDesignCommandToken——
 *     "只引用不注册"的 token 字面常量手法；组装与执行分离）；本单元
 *     Lifecycle.hpp RelinkFlow（检测→确认→提交的编排核形态）
 *
 * 背景说明（第一读者须知——为什么编排核在 workflow、字节在两端）：
 *   selection 的回填命令只写**记录面**对象（sel-device-backfill——SEL-10
 *   语义自足：目录引用/安装关系/合成物性/复算提示），权威 robot-drivetrain
 *   对象的 canonical 字节唯一实现归 modeling Codec（selection 自拼＝第二
 *   实现，NFR-MNT-03 禁止）；两端之间的"字段转译＋权威提交"编排是 L5
 *   装配层义务（P-SEL-9 登记原文）。本单元是编排单元（ARCH §3.4）且持有
 *   ①命令端口消费先例（Lifecycle.cpp 提交面），故编排核落位 workflow；
 *   两端的领域知识（selection IRDSBFV1 解码／modeling DrivetrainDesign
 *   组装与 Codec 编码）经两端口接缝注入（R-1：workflow 零业务域头）——
 *   缝实现归宿主装配层（可同时 include 两边公共头），契约测试以测试
 *   等价物钉扎编排语义（真实字段映射对账随宿主装配批次，见文件尾边界
 *   登记）。
 *
 * "selection 侧记录对象不动"的结构性保证：本编排核对记录对象**只读**
 * （tryObject 深拷贝值语义——PA-3），全部写路径只有①端口的
 * apply-drivetrain-design 一次提交（recordPort/translatePort 端口契约
 * 明文零写入）；编排核签名不接收任何记录写面。
 *
 * 线程约束：静态编排核（无共享状态）；参数对象按 §10.3 会话内单线程
 * 纪律使用（store/端口非线程共享）。
 * 错误语义（§10.3）：调用方错误 fail-fast（WorkflowError）；环境/对端
 * 错误走值轨道呈现（Outcome.failure——UX-03 三字段；对端诊断原样透传
 * ——D-WF-7 零加工零归码）；无记录/转译拒绝不是错误（诚实业务分支）。
 */

#ifndef SDURWS_IRD_WORKFLOW_BACKFILLTRANSLATE_HPP
#define SDURWS_IRD_WORKFLOW_BACKFILLTRANSLATE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>        // core::BranchId/RevisionId（分支与修订身份——core 强类型）
#include <sdurws/ird/project/ProjectStore.hpp> // project::ProjectStore（①端口宿主＋查询面——白名单边）
#include <sdurws/ird/workflow/Lifecycle.hpp>   // NewProjectFailure（UX-03 三字段同型——失败呈现值承载复用）
#include <sdurws/ird/workflow/Types.hpp>       // workflow::WorkflowError（调用方错误 fail-fast）

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 冻结 token 的字面引用（引用不注册——optimization kApplyRobotDesign-
// CommandToken 同款先例；词形权威在登记单元，本常量只是编排核的引用面）
// =====================================================================

/**
 * @brief 回填记录对象类型 token（"sel-device-backfill"——selection
 *        kBackfillRecordObjectToken 的字面引用）。
 *
 * 为什么是字面而不是 include：selection 是业务域单元，R-1 禁止 workflow
 * 直链（白名单八边不含 selection）；该 token 是修订闭包 objectRefs 的
 * 路由键（域登记稳定 token），编排核只按它**查找**记录对象、不解释其
 * 语义（解码权威在 selection——经解码缝）。字面漂移由契约测试与两侧
 * 卡的 SA-12 同步义务钉住（同 modeling 值面词形对照先例）。
 */
inline constexpr std::string_view kSelBackfillRecordObjectToken =
    "sel-device-backfill";

/**
 * @brief modeling 域命令 token（"apply-drivetrain-design"——modeling
 *        kCmdApplyDrivetrainDesign 的字面引用；**权威写入的路由键**）。
 *
 * "modeling token 为路由键"（P-SEL-9 登记口径）：编排核组装的
 * CommandEnvelope.commandType 取本 token——project 命令服务按它路由到
 * modeling ApplyDrivetrainDesignHandler（requiresDualCompile=true 的
 * 双编译编排在该处理器注册面与 project S5，本编排核零双编译知识——
 * PA-1 不越权）。"只引用不注册"（optimization Applier 同案）：本单元
 * 不注册新命令 token、不自拼 robot-drivetrain canonical 字节。
 */
inline constexpr std::string_view kApplyDrivetrainDesignCommandToken =
    "apply-drivetrain-design";

/**
 * @brief 连杆坐标系参考系 token（"link-frame"——selection
 *        kBackfillFrameLink 的字面引用）。
 *
 * v1 冻结参考系词表（§12.4"按明确参考系合成；记录参考系"）——编排核
 * 复核中立值面 referenceFrameToken 时对照（词表外值＝解码缝/数据侧
 * 异常，转译拒绝；复核是编排面的最小防御，词表权威在 selection）。
 */
inline constexpr std::string_view kSelBackfillLinkFrameToken = "link-frame";

// =====================================================================
// 中立值面（两缝之间的编排载体——workflow 自有类型，零业务域头）
// =====================================================================

/**
 * @brief 单轴回填记录的中立承载（解码缝产出→转译缝消费的逐轴值面）。
 *
 * 字段集＝P-SEL-9"回填字段转译"所需的最小面：身份（轴对象 obj- 词形）、
 * 权威写入值（传动比——DrivetrainDesign.ratioPerJoint 的逐关节取值）、
 * 物性合成记录（selection §12.4 合成产物——权威模型物性面的回填来源
 * 记录，随 catalogBackfill 语义承载）、目录引用词面（追溯面）。全部
 * 字段由解码缝自 selection 记录对象**搬运**（零加工——编排核与转译缝
 * 都不重算合成、不解释目录语义，PA-1/N8）。
 *
 * 数值单位（与 selection AxisBackfillEntry 同口径）：传动比无量纲；
 * 质量 kg；质心/位置 m；惯量 kg·m²（绕合成质心、连杆坐标系姿态）。
 * 数值合法性（有限/正定/三角不等式）已在 selection 合成与解码门断言，
 * 本值面不重复断言（对端权威——转译缝按对端口径复核）。
 *
 * 线程安全：纯值类型。
 */
struct BackfillAxisRecord {
    /// 轴（关节）对象身份 obj- 规范词形（与权威模型对齐键——ARC-04
    /// 稳定身份；词形透传，强类型还原归转译缝侧消费方）。
    std::string jointIdCanonical;
    /// 回填传动比（无量纲；>0 且有限——I-MDL-11 消费口径，与关节序
    /// 对应的"该轴"取值；有序承载＝中立值面向量序，不重排）。
    double appliedRatio = 0.0;
    /// 合成质量（单位 kg——连杆＋两壳体之和；转子不计入，§12.4）。
    double synthesisMassKg = 0.0;
    /// 合成质心（单位 m；连杆坐标系下三分量 [x,y,z]）。
    double synthesisComM[3] = {0.0, 0.0, 0.0};
    /// 合成惯量张量（单位 kg·m²；绕合成质心、连杆系姿态——行主序 3×3
    /// 九分量 [xx,xy,xz,yx,yy,yz,zx,zy,zz]，对称阵由对端断言保证）。
    double synthesisInertia[9] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    /// 转子等效惯量（单位 kg·m²，电机轴系）——**独立登记不参与合成**
    /// （§12.2 纪律 5：映射侧 J·i² 显式计入的唯一位置，防重复计入的
    /// 分轨记录面）；转译缝把它编入 modeling 侧对应承载（逐轴缺口由
    /// 该编排形态承载——P-SEL-9 登记原文）。
    double rotorInertiaKgM2 = 0.0;
    /// 目录版本词面（CatalogIdentity.version 同值——追溯面透传）。
    std::string catalogVersion;
    /// 电机型号稳定 ID 词面（目录包内唯一键——追溯面透传）。
    std::string motorModelId;
    /// 减速器型号稳定 ID 词面（同上）。
    std::string gearboxModelId;
    /// 安装关系词表值（CompatibilityRecord.mountKind 同值——§12.2 纪律 4
    /// 的记录面承载）。
    std::string mountKind;

    /// 逐字段相等（浮点位模式精确比较——值搬运面要求逐位一致，零容差；
    /// 数值来自对端 canonical 解码，同源必同位模式）。
    bool operator==(const BackfillAxisRecord& o) const noexcept
    {
        if (jointIdCanonical != o.jointIdCanonical
            || appliedRatio != o.appliedRatio
            || synthesisMassKg != o.synthesisMassKg
            || rotorInertiaKgM2 != o.rotorInertiaKgM2
            || catalogVersion != o.catalogVersion
            || motorModelId != o.motorModelId
            || gearboxModelId != o.gearboxModelId
            || mountKind != o.mountKind) {
            return false;
        }
        for (std::size_t i = 0; i < 3; ++i) {
            if (synthesisComM[i] != o.synthesisComM[i]) {
                return false;
            }
        }
        for (std::size_t i = 0; i < 9; ++i) {
            if (synthesisInertia[i] != o.synthesisInertia[i]) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const BackfillAxisRecord& o) const noexcept
    {
        return !(*this == o);
    }
};

/**
 * @brief 回填记录的中立事实集（解码缝产出——一次回填命令的记录面全量）。
 *
 * referenceFrameToken 契约：v1 词表冻结 kSelBackfillLinkFrameToken
 * （"link-frame"）——编排核复核词表（词表外＝记录面异常，转译拒绝）。
 * axes 契约：非空（零轴回填无业务意义——selection 组装面已拒绝，此处
 * 为防御复核）；序＝selection 声明序透传（编码按声明序不重排——对端
 * canonical 纪律同源）。
 *
 * 线程安全：纯值类型。
 */
struct SelBackfillRecordFacts {
    /// 参考系声明（随命令留痕——§12.4；词表见类型注）。
    std::string referenceFrameToken;
    /// 逐轴回填记录（≥1——见类型注；序＝声明序透传）。
    std::vector<BackfillAxisRecord> axes;

    bool operator==(const SelBackfillRecordFacts& o) const
    {
        return referenceFrameToken == o.referenceFrameToken && axes == o.axes;
    }
    bool operator!=(const SelBackfillRecordFacts& o) const { return !(*this == o); }
};

/**
 * @brief 转译产物（转译缝产出——①端口提交信封的字节面材料）。
 *
 * payloadCanonical 语义：modeling apply-drivetrain-design 命令的域
 * canonical 负载字节（robot-drivetrain 对象字节的**唯一实现**在转译缝
 * ——经 modeling DrivetrainDesign 值面＋IRobotDesignCodec::encode 产出；
 * 本编排核不解释、不修改、不自拼——P-SEL-9/PA-1 红线）。targetObject
 * 语义：权威 robot-drivetrain 对象的 obj- 词形（空＝新建——基线闭包
 * 尚无该对象；非空＝基线既有对象改版）；编排核对它的消费＝编排结果
 * 登记面（词形透传），信封组装不需要它（对象定位随 payload 承载——
 * modeling handler 解码门按 payload 内显式 oid 走改版路径）。
 *
 * 线程安全：纯值类型。
 */
struct DrivetrainCommandDraft {
    /// 载荷格式版本（modeling 处理器自有演进版本戳——不受理的历史版本
    /// 由对端 Rejected(invalid-payload) 拒绝，透传不猜测，NFR-DEP-04）。
    std::uint32_t payloadFormatVersion = 0;
    /// modeling 命令 canonical 负载字节（转译缝唯一产出——见类型注）。
    std::vector<std::uint8_t> payloadCanonical;
    /// 权威对象定位词形（obj-；空＝新建——登记面，见类型注）。
    std::string targetObjectCanonical;
    /// 中文编排摘要（轴数/目录版本——呈现与日志面；不是命令摘要——
    /// 命令摘要随修订持久化，由对端 handler 组装，PA-1）。
    std::string summary;

    bool operator==(const DrivetrainCommandDraft& o) const
    {
        return payloadFormatVersion == o.payloadFormatVersion
            && payloadCanonical == o.payloadCanonical
            && targetObjectCanonical == o.targetObjectCanonical
            && summary == o.summary;
    }
    bool operator!=(const DrivetrainCommandDraft& o) const
    {
        return !(*this == o);
    }
};

// =====================================================================
// 两端口接缝（实现归 L5 装配层——R-1 下编排面与领域面的解耦点）
// =====================================================================

/**
 * @brief 回填记录解码端口（记录字节→中立事实集的接缝）。
 *
 * 谁实现：L5 装配层——桥接 selection 公共契约
 * decodeBackfillRecordObject（IRDSBFV1 严格解码——分结构/版本两轨）与
 * 中立值面搬运（BackfillAxisRecord 逐字段搬运，零加工）。workflow 零
 * selection 头（R-1），缝即接缝。
 *
 * 错误语义：解码失败（Malformed/UnsupportedVersion/词表外）＝数据侧
 * 拒绝，走 Decode.ok=false＋detail 中文定位（查询轨两态——对端纪律同
 * 源），编排核转 Failed 呈现；实现方不抛异常（对端 decode 不抛——
 * 同款查询轨）。
 *
 * 线程约束：主线程会话内调用（编排核所在调用线程）。
 */
class ISelBackfillRecordPort {
public:
    /// 虚析构：经接口引用多态消费的常规保障。
    virtual ~ISelBackfillRecordPort() = default;

    /// 解码结果（两态——ok=false 时 detail 承载中文定位，facts 无效）。
    struct Decode {
        bool ok = false;                  ///< true＝解码成功（facts 有效）
        SelBackfillRecordFacts facts;     ///< 中立事实集（ok=true 有效）
        std::string detail;               ///< 失败定位（中文——诊断素材）
    };

    /**
     * @brief 解码回填记录字节（sel-device-backfill 对象负载）。
     * @param bytes [in] 对象负载字节（编排核自 store.tryObject 深拷贝
     *              取得——值语义透传）
     * @return 解码结果（不抛异常——查询轨）
     */
    virtual Decode decode(const std::vector<std::uint8_t>& bytes) = 0;
};

/**
 * @brief 回填转译端口（中立事实集→modeling 命令字节面的接缝）。
 *
 * 谁实现：L5 装配层——消费 selection 中立事实集，组装 modeling
 * DrivetrainDesign 值面（ratioPerJoint 逐关节序/catalogBackfill 单值
 * schema 的逐轴缺口承载——P-SEL-9 登记原文）并经 modeling
 * IRobotDesignCodec::encode 产出权威 canonical 字节。workflow 零
 * modeling 头（R-1），缝即接缝。
 *
 * 错误语义：转译拒绝（对端组装/编码校验不过）＝数据侧拒绝，走
 * Translation.ok=false＋cause/action（UX-03 半区）；实现方不抛异常
 * （对端 Expected 两态同源）。
 *
 * 线程约束：主线程会话内调用。
 */
class ISelBackfillTranslatePort {
public:
    /// 虚析构：经接口引用多态消费的常规保障。
    virtual ~ISelBackfillTranslatePort() = default;

    /// 转译结果（两态——ok=false 时 cause/action 承载 UX-03 半区）。
    struct Translation {
        bool ok = false;                  ///< true＝转译成功（draft 有效）
        DrivetrainCommandDraft draft;     ///< 转译产物（ok=true 有效）
        std::string cause;                ///< 失败原因（UX-03 半区二）
        std::string action;               ///< 建议动作（UX-03 半区三）
    };

    /**
     * @brief 转译回填事实集为 modeling 命令字节面。
     * @param facts [in] 中立事实集（编排核复核后的解码产物）
     * @return 转译结果（不抛异常——查询轨）
     */
    virtual Translation translate(const SelBackfillRecordFacts& facts) = 0;
};

// =====================================================================
// 编排请求与结果
// =====================================================================

/**
 * @brief 转译编排请求（权威写入的目标分支与并发基线）。
 *
 * expectedRevision 语义与 project CommandEnvelope 同源：nullopt＝分支
 * tip（提交期解析）；显式值失配 → Rejected(stale-revision)（并发校验
 * 兜底——编排面只做预期透传）。
 *
 * 线程安全：纯值类型。
 */
struct SelBackfillTranslateRequest {
    /// 目标分支（brn- 规范文本；须 isValid——编排核 @pre）。
    core::BranchId branch{};
    /// 期望基线修订（nullopt＝分支 tip——见类型注）。
    std::optional<core::RevisionId> expectedRevision;
};

/**
 * @brief 转译编排结果（run 的唯一返回通道）。
 *
 * 不变量：result==Submitted ⇔ revisionId 有值且 isValid（①端口
 * Committed——权威写入产生恰一新修订）；NoRecord/Failed ⇔ revisionId
 * 无值（无记录零提交——"selection 侧记录对象不动"；失败不带病报成功
 * ）。failure 仅 Failed 有值（UX-03 三字段——对端 detail/诊断透传）。
 * summary 为编排摘要（轴数/对象词形——呈现与日志面，所有路径尽力
 * 填充，NoRecord 时可为空）。
 */
struct SelBackfillTranslateOutcome {
    /**
     * @brief 编排结果三值（Submitted 语义具名——完成态即"权威写入已
     *        提交并产生新修订"；NoRecord 为检测无记录的早退态）。
     */
    enum class Result : std::uint8_t {
        Submitted = 0, ///< 权威写入已提交（新修订产生——①端口 Committed）
        NoRecord = 1,  ///< 基线闭包无回填记录（零提交——无转译事实）
        Failed = 2,    ///< 失败（failure 有值——记录异常/转译拒绝/提交拒绝）
    };

    Result result = Result::NoRecord;///< 编排结果（见枚举注）
    /// 新修订身份（Submitted 时有值——①端口产物）。
    std::optional<core::RevisionId> revisionId;
    /// 编排摘要（轴数/对象词形——呈现与日志面）。
    std::string summary;
    /// 失败呈现（Failed 时有值——UX-03 三字段；复用 NewProjectFailure
    /// 同型值承载——context/cause/recommendedAction 语义一致）。
    std::optional<NewProjectFailure> failure;
};

// =====================================================================
// 转译编排核（O4 同型静态编排面——P-SEL-9 的执行点）
// =====================================================================

/**
 * @brief P-SEL-9 转译编排核（selection 记录面 → modeling 权威写入的
 *        ①端口提交编排）。
 *
 * 全静态接口（无会话状态——同 RelinkFlow/SaveAsFlow 先例：宿主事件
 * 驱动的编排核，纯面可契约测试直调）。
 */
class SelBackfillTranslateFlow {
public:
    SelBackfillTranslateFlow() = delete;

    /**
     * @brief 执行转译编排（定位→读取→解码→复核→转译→①端口提交）。
     *
     * 编排序（每段的失败/早退语义独立成立）：
     *   1 前置校验：request.branch 无效（!isValid）→ WorkflowError
     *     （调用方契约违约 fail-fast）。
     *   2 记录定位（只读）：store.query().tryRevision(request.
     *     expectedRevision 或分支 tip) → 修订闭包 objectRefs 按
     *     kSelBackfillRecordObjectToken 扫描——恰一 → 取 oid/cv；
     *     零个 → NoRecord（selection 记录面未落，无转译事实——早退）；
     *     多个 → Failed（记录面异常：每分支闭包应恰一记录对象——
     *     selection "每命令恰一对象＋继承 oid 改版"契约的编排面复核，
     *     多个即对端违约，如实呈现不带病转译）；查询异常（tryRevision
     *     抛 StoreError）→ Failed（cause 透传）。
     *   3 记录取数：store.query().tryObject(oid, cv)（深拷贝——PA-3）；
     *     nullopt（关闭/损坏）→ Failed（cause 登记——环境侧）。
     *   4 解码（缝一）：recordPort.decode——!ok → Failed（detail 透传
     *     入 cause；selection 解码权威，零加工）。
     *   5 编排复核（最小防御面）：facts.axes 为空、或 referenceFrame-
     *     Token 非 kSelBackfillLinkFrameToken（v1 冻结词表）→ Failed
     *     （数据侧异常呈现——转译前提不成立，不 fail-fast：记录字节是
     *     环境面事实不是调用方错误）。
     *   6 转译（缝二）：translatePort.translate——!ok → Failed（cause/
     *     action 透传）；ok → draft。
     *   7 ①端口提交（权威写入）：组装 project::CommandEnvelope{branch,
     *     expectedRevision, commandType=kApplyDrivetrainDesignCommandToken,
     *     payloadFormatVersion=draft.payloadFormatVersion, payloadCanonical
     *     =draft.payloadCanonical} → store.commands().submit(envelope)。
     *     requiresDualCompile=true 的双编译编排归 modeling handler 注册
     *     面＋project S5（本编排核零双编译知识——PA-1）。
     *   8 结果折叠（D-WF-7 零加工）：Committed → Submitted{revisionId}
     *     （AT-21 同型"新修订"观测点）；Rejected/Aborted/Failed →
     *     Failed（cause 自 status 词形＋diagnostics 首条诊断透传组装、
     *     error.what() 兜底——零归码）。
     *
     * @param store       [in] 当前项目存储上下文（非 owning——只读查询
     *                   ＋①端口提交；只读会话提交由 S1 not-writable 拒绝
     *                   并透传 Failed——权限裁决归 project，PA-1）
     * @param request     [in] 编排请求（分支＋并发基线）
     * @param recordPort  [in] 记录解码端口（L5 缝——非 owning）
     * @param translatePort [in] 转译端口（L5 缝——非 owning）
     * @return 编排结果（见 SelBackfillTranslateOutcome 不变量）
     *
     * @throws WorkflowError request.branch 无效（调用方契约违约——fail-fast）
     *
     * @threadSafe 无共享状态（静态函数）；store/端口按会话内单线程纪律。
     * @determinism 无确定性承诺（涉磁盘与命令事务）；同成功路径的状态
     *              事实可复核（revisionId＋store 修订链）。
     */
    static SelBackfillTranslateOutcome run(project::ProjectStore& store,
                                           const SelBackfillTranslateRequest& request,
                                           ISelBackfillRecordPort& recordPort,
                                           ISelBackfillTranslatePort& translatePort);
};

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_WORKFLOW_BACKFILLTRANSLATE_HPP
