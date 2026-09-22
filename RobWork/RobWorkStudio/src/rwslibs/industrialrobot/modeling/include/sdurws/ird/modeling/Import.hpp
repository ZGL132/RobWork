/**
 * @file   Import.hpp
 * @brief  URDF 导入字段映射与链型判定——IModelImportMapper 纯函数服务、
 *         ValidatedSource 输入值类型、ImportOutcome/ImportReport 导入报告
 *         值类型（卡 §6.1～§6.4/§6.7/§9.4.3；实现按 DTB 规模列拆两提交：
 *         解析映射＋链型判定，本头为两提交的完整公共面）。
 *
 * 设计依据：
 *   - units/modeling.md §6.1（导入总管线与三分边界——输入全部为 io 产物、
 *     导入只产草稿不产修订）、§6.2（io/modeling/runtime 三分表）、§6.3
 *     （URDF 字段映射表——四清单逐项可观察、默认补全不静默）、§6.4
 *     （链型判定两维度——本提交仅含单可动主链直通与分支检测，选链/
 *     能力矩阵随第二提交）、§6.7（导入资源与对象身份——临时句柄、
 *     Recorded 外部引用记录可携带）、§9.4.3（IModelImportMapper 接口
 *     契约——mapUrdf/mapXacroExpanded/mapWorkCellXml 三方法）、§3.3
 *     （公共头表 Import.hpp 行——T05）
 *   - units/io.md §9.6（IResourceReader 四方法产物——字节/快照/依赖树
 *     值类型）、§10.5（与 modeling 边界——modeling 不自行读文件，io 不建
 *     RobotDesign）
 *   - 需求 MDL-03（URDF 导入字段映射/默认补全/忽略与不支持项报告，不静默
 *     猜测/降级/排除）、MDL-11（任意有限非零轴；缺 axis 取局部 +X 成待
 *     确认草稿；零/非法轴仅报告不提交）、MDL-12（四类关节识别＋链型判定）、
 *     MDL-18（WorkCell 反向导入 R2——mapWorkCellXml R1 边界响应）、
 *     MDL-22（未配置安装默认地面）、NFR-COR-02（同输入字节＋同 options→
 *     同输出；诊断按源文件行序稳定排序）、NFR-COR-03（非法值不静默转 0）、
 *     NFR-SEC-01/02（路径/预算防护全部经 io——SA-14）、CON-03（外部引用
 *     Recorded 记录）
 *   - 任务契约 tasks/foundation/WP-13-T05.json acceptance 1~5；
 *     knownPitfalls：O-40（pugixml 经 vcpkg 经典模式安装 1.16/x64-windows，
 *     modeling PRIVATE——DOM 有界性由 io BudgetGuard 前置保证：进入本单元
 *     的字节已经 io 预算入账，DOM 规模与输入字节同阶，不二次读文件）、
 *     P-MDL-3（自碰撞配置只产出导入报告"策略草稿候选输入"清单——不写
 *     policy 对象，交接 API 待 policy 卡冻结）、P-MDL-8（io/runtime 契约
 *     Draft——io 值类型按当周 ResourceIo.hpp 落位形态消费，漂移按卡
 *     R-MDL-1 增量同步）
 *
 * 背景说明（导入映射在产品里的位置——为什么它是"纯函数＋报告"）：
 * URDF 导入是七阶段工作流第一阶段的三条入口之一（模板/导入/编辑）。io
 * 是唯一文件读取者（字节＋快照＋依赖树已经过 SafePath/预算/环检测防护），
 * 本单元把已验证的字节映射为 RobotDesign **草稿**＋导入报告，全程不产生
 * 修订、不落盘——真正的对象身份在命令 prepare 阶段才分配（§6.7），用户
 * 在向导页依据报告确认/选链/决议待确认项后才可能走到应用。因此本头全部
 * 服务为无状态纯函数：同输入字节＋同 options→同输出（含诊断顺序——按源
 * 文件行序稳定排序，NFR-COR-02）；任何"源文件说了但本软件不映射"的内容
 * 都必须出现在报告对应清单里（NFR-COR-03 不静默：默认补全/忽略/不支持
 * 三类面缺一不可）。
 *
 * P-MDL-8 落位说明（ValidatedSource 为什么由 modeling 定义）：卡面 §9.4.3
 * 的 ValidatedSource 是"io open/snapshot/dependencyTree 产物"的复合概念，
 * io 侧落位的是三个独立值类型（ResourceSnapshot/ResourceDependencyTree
 * ＋句柄字节）；本头以 modeling 自有值类型将其组合为映射输入契约（成员
 * 全部为 io 公共值类型——零 io 语义复制），io 产物→ValidatedSource 的
 * 装配归调用方（向导域侧页）。依赖树节点允许携带缺失叶（exists=false，
 * io §6.5 缺失清单载体）——映射按 §6.7"缺失≠不可行"转 Recorded 记录。
 *
 * 线程安全：ModelImportMapper 无共享可变状态、可重入、多线程并发调用
 * 安全（卡 §3.4 总约定 1）。确定性：不读环境变量/时钟/locale/文件系统；
 * 数值解析经 std::from_chars（locale 无关）；诊断按（文件，行，列，产出
 * 序）稳定排序。全 SI（m/rad/kg/N·m）；角度制式 rad（注释显式标注）。
 */

#ifndef IRD_MODELING_IMPORT_HPP
#define IRD_MODELING_IMPORT_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>      // core::DiagnosticRecord（diags 输出参数元素类型）
#include <sdurws/ird/core/Digest.hpp>        // core::Digest256（来源摘要——XacroProvenance/资源记录）
#include <sdurws/ird/io/ResourceIo.hpp>      // io::ResourceSnapshot/ResourceDependencyTree（§9.6 产物值类型）
#include <sdurws/ird/modeling/RobotDesign.hpp>  // RobotDesign/JointEntry/LinkEntry 等（草稿值模型）

namespace sdurws::ird::modeling {

// =====================================================================
// 输入值类型：ValidatedSource（§9.4.3 @pre"字节＋快照＋依赖树，无环"）
// =====================================================================

/**
 * @brief 导入映射的已验证输入（P-MDL-8 处置——io 三产物值的 modeling 侧
 *        组合契约，见文件头注）。
 *
 * 字段语义（全部为 io 产物原样传递，本单元不解释其防护语义）：
 *   - bytes：入口文档的已验证字节（io open 流式读取/dependencyTree 预算
 *     内产物——模型绝不自行读文件，SA-14）。UTF-8 良构 XML 已由 io 的
 *     良构检查保证（IO-FORMAT-XML-SYNTAX 在 io 侧拦截）。
 *   - entrySnapshot：入口文档的读取快照（§8.2 三元组——contentDigest 为
 *     来源身份要素；finalPath 仅追溯提示不作身份，路径不入任何诊断明文
 *     ——敏感值经 diagnostics 脱敏前不外泄，本单元诊断上下文只用依赖树
 *     相对键）。
 *   - dependencyTree：io §6.5 依赖树（无环——有环在 io 侧以
 *     IO-FORMAT-XML-CYCLE 失败，不会到达本单元）。节点允许 exists=false
 *     缺失叶（缺失清单载体）——映射按 §6.7 转 Recorded 记录＋IO-RES-
 *     MISSING 事实诊断，草稿可携带（V-08"应用可过（Warning）"）。
 *
 * 生命周期/所有权：纯值类型，调用方所有；映射过程只读。线程安全：不可变
 * 共享安全。
 */
struct ValidatedSource {
    std::vector<std::uint8_t> bytes;                  ///< 入口文档字节（io 产物——预算内已验证）
    io::ResourceSnapshot entrySnapshot{};             ///< 入口文档快照（digest＝来源身份要素）
    io::ResourceDependencyTree dependencyTree;        ///< 依赖树（无环；允许缺失叶 exists=false）
};

// =====================================================================
// 导入选项（ImportOptions）
// =====================================================================

/**
 * @brief 导入映射选项（§9.4.3"同输入字节＋同 options→同输出"的 options）。
 *
 * selectedMainBranch＝用户显式选链（§6.4 维度一）：取**分支根连杆**的
 * 净化后 localName——向导页依据分支报告（report.branches）由用户点选后
 * 回填；多可动分支文件在未选择时拒绝产出草稿（未经用户显式选择不排除
 * 可动分支——DTB 禁止项）。语义细则：每次调用解析**一层**分裂——选中
 * 分支内部若再分裂，返回新一轮候选清单请求继续选链（向导循环）；单可
 * 动链文件本字段留空即可。
 *
 * 生命周期/所有权：纯值类型，调用方所有。
 */
struct ImportOptions {
    std::string selectedMainBranch;   ///< 用户显式选择的主链分支根连杆名（空＝未选择）
};

// =====================================================================
// 导入报告值类型（§9.4.3 @post"映射/默认补全/忽略/不支持/分支报告/
// 待确认清单/资源状态表"的报告面——四清单逐项可观察，MDL-03/AT-15）
// =====================================================================

/**
 * @brief 源文件位置（报告条目与诊断的定位面＋稳定排序键）。
 *
 * relFile＝依赖树相对键（正斜杠、折叠小写——io relPath 约定；不以绝对
 * 路径入报告——敏感值纪律）。line/column 自 1 起（0＝无定位——链级/文件
 * 级事实）。排序键＝（relFile，line，column）字典序。
 */
struct ImportSourceSpan {
    std::string relFile;        ///< 源文件相对键（依赖树 relPath；空＝入口文档）
    std::uint32_t line = 0;     ///< 行号（1 起；0＝无定位）
    std::uint32_t column = 0;   ///< 列号（1 起；0＝无定位）

    bool operator==(const ImportSourceSpan& o) const noexcept
    {
        return relFile == o.relFile && line == o.line && column == o.column;
    }
    bool operator!=(const ImportSourceSpan& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 字段映射清单条目（四清单之一——"源元素→RobotDesign 落点"的
 *        可观察登记；§6.3 表逐行的执行面）。
 *
 * valueText＝映射值的稳定文本（定点格式化、不经 locale——确定性；物理量
 * 附单位）。sourceValueText≠valueText 时（名称净化等）两者都入条目。
 */
struct ImportMappedItem {
    std::string sourcePath;       ///< 源元素路径（如 "joint[1]/axis"；树内定位）
    std::string targetField;      ///< RobotDesign 落点（如 "joints[0].axis"）
    std::string valueText;        ///< 映射值稳定文本（物理量带单位：m/rad/kg/N·m）
    std::string note;             ///< 备注（来源标记/降级提示——如"密度不可用：估算不可用"）
    ImportSourceSpan span;        ///< 源位置（行序排序键）

    bool operator==(const ImportMappedItem& o) const
    {
        return sourcePath == o.sourcePath && targetField == o.targetField
            && valueText == o.valueText && note == o.note && span == o.span;
    }
    bool operator!=(const ImportMappedItem& o) const { return !(*this == o); }
};

/**
 * @brief 默认补全清单条目（四清单之二——一切默认补全必入本清单，不静默；
 *        NFR-COR-03/MDL-03）。
 *
 * appliedValue＝补全后的值（如 "ground(preset)"/"not-provided"）；
 * reason＝补全依据（卡面出处语义，如"URDF 无安装语义——MDL-22 默认地面"）。
 */
struct ImportDefaultItem {
    std::string field;            ///< 被补全的字段（RobotDesign 落点或源缺席面）
    std::string appliedValue;     ///< 补全值稳定文本（not-provided＝显式降级标记）
    std::string reason;           ///< 补全依据（需求/卡面语义）
    ImportSourceSpan span;        ///< 源位置（缺席补全时指到最近的宿主元素）

    bool operator==(const ImportDefaultItem& o) const
    {
        return field == o.field && appliedValue == o.appliedValue
            && reason == o.reason && span == o.span;
    }
    bool operator!=(const ImportDefaultItem& o) const { return !(*this == o); }
};

/**
 * @brief 忽略项清单条目（四清单之三——外来扩展等"报告不解释"的内容；
 *        §6.3 "<gazebo> 等外来扩展→忽略项清单"）。
 */
struct ImportIgnoredItem {
    std::string element;          ///< 被忽略的元素名（局部名原文）
    std::string reason;           ///< 忽略依据（如"外来扩展——报告不解释"）
    ImportSourceSpan span;        ///< 源位置

    bool operator==(const ImportIgnoredItem& o) const
    {
        return element == o.element && reason == o.reason && span == o.span;
    }
    bool operator!=(const ImportIgnoredItem& o) const { return !(*this == o); }
};

/**
 * @brief 不支持项清单条目（四清单之四——已识别但 R1 无映射语义的内容；
 *        不静默丢弃、不静默降级）。
 *
 * kind 词表（本提交；表尾追加纪律）：
 *   "joint-type"（planar/floating 等不可表达类型）、"mimic-joint"（识别＋
 *   报告＋阻断——类型保留不转 FixedFrame）、"transmission"（线性耦合候选
 *   提示，R2）、"ros-uri"（package:// 等 ROS URI——引导改相对路径）、
 *   "mesh-scale"（非单位缩放——schema 无落点）、"geometry-primitive"
 *   （box/cylinder/sphere——§6.3 未登记映射语义）、"joint-velocity-limit"
 *   （<limit velocity>——§4.3-A 无 maxVelocity 字段，不发明 schema）、
 *   "multiple-visual"/"multiple-collision"（多几何引用——schema 单引用，
 *   仅首个映射其余逐条报告）、"workcell-channel-r2"（mapWorkCellXml 的
 *   R1 边界引导——MDL-18 R2/阶段 D）。
 */
struct ImportUnsupportedItem {
    std::string kind;             ///< 不支持类别（上行词表）
    std::string subject;          ///< 定位主体（关节名/元素路径/文件引用）
    std::string reason;           ///< 不支持依据（卡面语义）
    std::string guidance;         ///< 用户引导（如"改相对路径后重试"）
    ImportSourceSpan span;        ///< 源位置

    bool operator==(const ImportUnsupportedItem& o) const
    {
        return kind == o.kind && subject == o.subject && reason == o.reason
            && guidance == o.guidance && span == o.span;
    }
    bool operator!=(const ImportUnsupportedItem& o) const { return !(*this == o); }
};

/**
 * @brief 待确认草稿清单条目（§9.4.3 @post"待确认清单"——用户确认后可
 *        应用；确认动作经草稿编辑/确认流，导入期只登记不代决）。
 *
 * kind 词表："axis-default-plus-x"（缺 axis 默认局部 +X——MDL-11）、
 * "limit-missing"（revolute/prismatic 缺 lower/upper）、
 * "working-range-unconfirmed"（continuous 工程工作范围未确认——§6.4；
 * 未确认不得正式运行，确认值不回写权威 bounds）。
 */
struct ImportPendingItem {
    std::string kind;             ///< 待确认类别（上行词表）
    std::string subject;          ///< 定位主体（关节名等）
    std::string question;         ///< 待确认问题（含默认值/缺失面描述）
    ImportSourceSpan span;        ///< 源位置

    bool operator==(const ImportPendingItem& o) const
    {
        return kind == o.kind && subject == o.subject && question == o.question
            && span == o.span;
    }
    bool operator!=(const ImportPendingItem& o) const { return !(*this == o); }
};

/**
 * @brief 关节映射状态条目（MDL-11"该关节标记 Invalid"的承载——零轴/
 *        非有限轴关节在本清单标记 invalid，含该类关节的草稿不可提交）。
 *
 * status 词表："mapped"（正常映射）/"invalid"（不可提交——零轴/非有限轴）。
 */
struct ImportJointStatusItem {
    std::string jointName;        ///< 关节 localName（净化后）
    std::string status;           ///< "mapped"/"invalid"（上行词表）
    std::string reason;           ///< 状态依据（invalid 时含轴原文——可观察）
    ImportSourceSpan span;        ///< 源位置

    bool operator==(const ImportJointStatusItem& o) const
    {
        return jointName == o.jointName && status == o.status
            && reason == o.reason && span == o.span;
    }
    bool operator!=(const ImportJointStatusItem& o) const { return !(*this == o); }
};

/**
 * @brief 资源状态表条目（§9.4.3 @post"资源状态表"——resourceManifest 的
 *        报告镜像；缺失文件按 §6.7"缺失≠不可行"以 Recorded 携带）。
 *
 * state 词表："recorded"（已登记——digest 为空串表示缺失叶未取得内容）、
 * "missing"（依赖树缺失叶——IO-RES-MISSING 事实）。
 */
struct ImportResourceItem {
    std::string resourceId;       ///< resourceManifest 键（＝依赖树相对键）
    std::string relPath;          ///< 依赖树相对键（呈现/追溯）
    std::string state;            ///< "recorded"/"missing"（上行词表）
    std::string digestHex;        ///< 内容摘要十六进制（小写；缺失叶为空串）
    ImportSourceSpan span;        ///< 源位置（引用处）

    bool operator==(const ImportResourceItem& o) const
    {
        return resourceId == o.resourceId && relPath == o.relPath
            && state == o.state && digestHex == o.digestHex && span == o.span;
    }
    bool operator!=(const ImportResourceItem& o) const { return !(*this == o); }
};

/**
 * @brief 自碰撞排除候选条目（§6.3 "<disable_collisions>"行——P-MDL-3
 *        处置：仅作导入报告的策略草稿候选输入，**不写策略对象**、不写
 *        RobotDesign 编码权威语义；交接 API 待 policy 卡冻结后由后续
 *        任务经报告装配）。
 */
struct ImportSelfCollisionItem {
    std::string link1;            ///< 排除对第一连杆（localName）
    std::string link2;            ///< 排除对第二连杆（localName）
    ImportSourceSpan span;        ///< 源位置

    bool operator==(const ImportSelfCollisionItem& o) const noexcept
    {
        return link1 == o.link1 && link2 == o.link2 && span == o.span;
    }
    bool operator!=(const ImportSelfCollisionItem& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 传动回填候选条目（§6.3 "<limit effort>"行——effort→torqueLimit
 *        的 drivetrain 对象 Peak 候选；传动对象不在导入草稿内创建，仅
 *        报告登记供 apply-drivetrain-design 回填流消费）。
 */
struct ImportDrivetrainItem {
    std::string jointName;        ///< 关节 localName
    std::string valueText;        ///< effort 值稳定文本（单位 N·m——旋转关节口径）
    ImportSourceSpan span;        ///< 源位置

    bool operator==(const ImportDrivetrainItem& o) const
    {
        return jointName == o.jointName && valueText == o.valueText && span == o.span;
    }
    bool operator!=(const ImportDrivetrainItem& o) const { return !(*this == o); }
};

/**
 * @brief 错误项（§6.3"已提供但 m≤0/非 SPD→导入报告错误项（应用将被
 *        断言阻断）"的承载＋命名冲突面——草稿可产生但不可提交修订；
 *        prepare 断言（T08）为应用边界兜底，本清单是其导入期预提示面）。
 *
 * kind 词表："mass-nonpositive"（m≤0）、"inertia-not-spd"（非对称正定）、
 * "inertia-triangle"（三角不等式）、"value-illegal"（数值解析失败/NaN/
 * ±Inf——MDL-06/I-MDL-3，原文保留于 detail）、"limit-order"（qmin≥qmax）、
 * "name-conflict"（净化后 localName 重复——I-MDL-2 应用边界前拦截）。
 */
struct ImportErrorItem {
    std::string kind;             ///< 错误类别（上行词表）
    std::string subject;          ///< 定位主体（字段路径/关节名）
    std::string detail;           ///< 违例事实（值稳定文本；脱敏前不外泄）
    ImportSourceSpan span;        ///< 源位置

    bool operator==(const ImportErrorItem& o) const
    {
        return kind == o.kind && subject == o.subject && detail == o.detail
            && span == o.span;
    }
    bool operator!=(const ImportErrorItem& o) const { return !(*this == o); }
};

/**
 * @brief 分支报告条目（§6.4 维度一——"分支报告（对象与原因可观察）"；
 *        MDL-IMPORT-BRANCH-SELECTION 语义的报告载体）。
 *
 * 多可动分支文件的每个候选分支一行（含被选中与未选中两种状态）；辅助
 * 分支不构成拒绝——处置面为场景/环境候选或忽略（用户选择），本条目是
 * 该处置的报告承载。
 */
struct ImportBranchItem {
    std::string branchRoot;       ///< 分支根连杆（净化后 localName——选链回填键）
    std::string splitLink;        ///< 分裂点连杆（该分支自其分歧）
    std::string splitJoint;       ///< 分裂关节（分歧处的父关节 localName）
    std::string disposition;      ///< 处置面："selected"（主链）/"aux-candidate"（场景/环境候选或忽略——用户选择）
    std::string reason;           ///< 可观察原因（含分支可动关节描述）
    ImportSourceSpan span;        ///< 源位置（分歧关节）

    bool operator==(const ImportBranchItem& o) const
    {
        return branchRoot == o.branchRoot && splitLink == o.splitLink
               && splitJoint == o.splitJoint && disposition == o.disposition
               && reason == o.reason && span == o.span;
    }
    bool operator!=(const ImportBranchItem& o) const { return !(*this == o); }
};

/**
 * @brief 主链能力结论类别（§6.4 维度二能力矩阵的结论面——判定实现为
 *        导入映射的纯函数部分，同一判定被模板创建入口复用（卡 §6.4））。
 *
 * 词表两值（表尾追加纪律）：FullTemplateRange＝六/七轴全旋转（含经确认
 * 工程工作范围的 continuous，类型保留）——模板/识别/编辑/正式计算全通；
 * BeyondTemplateRange＝4/5 轴或目标链含 prismatic（及不在六/七轴表述内
 * 的其它轴数）——导入识别与草稿兼容编辑通过，模板创建与正式计算/报告
 * 阻断（诊断"超出首版产品模板范围"；R1；MDL-12-S1 启用后仅六/七轴含
 * prismatic 放开）。mimic/planar/floating 不产生能力结论（阻断型失败
 * ——UnsupportedJointType 无草稿／mimic 阻断提交）。
 */
enum class ChainCapabilityKind {
    FullTemplateRange,     ///< 六/七轴全旋转——全能力
    BeyondTemplateRange,   ///< 超出首版产品模板范围——草稿兼容编辑，模板/正式计算阻断
};

/**
 * @brief 主链能力结论（所选主链的维度二判定结果——报告承载；"不静默
 *        降级关节类型"的类型保留面：prismatic 保持 Prismatic，本结论
 *        只界定能力边界不改写任何关节类型——V12-01）。
 */
struct ChainCapability {
    ChainCapabilityKind kind = ChainCapabilityKind::BeyondTemplateRange;  ///< 结论类别
    std::uint32_t movableAxes = 0;  ///< 主链可动关节数（revolute+continuous+prismatic；fixed 不计）
    bool containsPrismatic = false;  ///< 主链含 prismatic（单位 m 限位）
    std::string reason;              ///< 结论依据（卡 §6.4 行语义；呈现归文案层）

    bool operator==(const ChainCapability& o) const noexcept
    {
        return kind == o.kind && movableAxes == o.movableAxes
               && containsPrismatic == o.containsPrismatic && reason == o.reason;
    }
    bool operator!=(const ChainCapability& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 维度二链型能力判定（§6.4 能力矩阵的**单一纯函数实现**——卡 §6.4
 *        尾段原文"判定实现为导入映射的纯函数部分（Import.hpp），同一判定
 *        被模板创建入口复用（§2.1 创建列）"的函数面；WP-13-T07 从
 *        mapUrdf 第九步内联逻辑提取，Import 与 Template 共用同一实现，
 *        不存在第二份判定）。
 *
 * 判定口径（与提取前 mapUrdf 第九步逐字一致——ImportTest 既有断言钉住）：
 * 可动关节数（revolute+continuous+prismatic；fixed 不计）∈{6,7} 且不含
 * prismatic→FullTemplateRange；其余（4/5 轴、含 prismatic、或不在六/七
 * 轴表述内的其它轴数——1~3 轴无模板语义同归范围外）→BeyondTemplateRange
 * （草稿兼容编辑通过，模板创建与正式计算/报告阻断；类型保留不降级
 * ——V12-01）。mimic/planar/floating 不进入本判定（阻断型失败在导入
 * 映射更早的步骤处理——它们根本产生不了合法 JointType 序列）。
 *
 * @param chainTypes [in] 所选主链的逐轴类型序列（链序——下标即链序）；
 *                    空序列＝无可动链（movableAxes=0→BeyondTemplateRange，
 *                    reason 携带轴数表述）
 * @return 能力结论（kind/movableAxes/containsPrismatic/reason 四字段全量；
 *         reason 为卡 §6.4 行语义的依据串，呈现归文案层）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同序列同结论同 reason）。
 */
ChainCapability judgeChainCapability(const std::vector<JointType>& chainTypes);

/**
 * @brief 导入报告（§9.4.3 @post 全集——四清单＋待确认＋关节状态＋资源
 *        状态表＋两类候选＋错误项＋分支报告＋主链能力结论）。
 *
 * 确定性（NFR-COR-02）：各清单条目按源文件行序追加（解析遍历即文档序）；
 * 同输入字节＋同 options→逐字段相等的报告。submittable＝草稿是否可提交
 * 修订（false＝存在零轴/物理违例/不支持阻断面——应用边界断言兜底前的
 * 导入期预判；缺失资源与待确认项**不**置 false——V-08/逐条确认流）。
 */
struct ImportReport {
    std::string sourceLabel;      ///< 源标识（依赖树 rootRel——呈现/追溯；不作身份）
    std::vector<ImportMappedItem> mapped;                   ///< 字段映射清单
    std::vector<ImportDefaultItem> defaults;                ///< 默认补全清单（不静默——NFR-COR-03）
    std::vector<ImportIgnoredItem> ignored;                 ///< 忽略项清单
    std::vector<ImportUnsupportedItem> unsupported;         ///< 不支持项清单
    std::vector<ImportPendingItem> pendingConfirms;         ///< 待确认草稿清单
    std::vector<ImportJointStatusItem> jointStatuses;       ///< 关节映射状态（invalid 标记面）
    std::vector<ImportResourceItem> resources;              ///< 资源状态表
    std::vector<ImportSelfCollisionItem> selfCollisionCandidates;  ///< 策略草稿候选输入（P-MDL-3）
    std::vector<ImportDrivetrainItem> drivetrainCandidates; ///< 传动回填候选（effort→Peak）
    std::vector<ImportErrorItem> errors;                    ///< 错误项（应用阻断面）
    std::vector<ImportBranchItem> branches;                 ///< 分支报告（维度一——对象与原因可观察）
    ChainCapability chainCapability{};                      ///< 主链能力结论（维度二——所选主链）
    bool submittable = true;      ///< 草稿可提交修订（false＝存在不可提交面）

    bool operator==(const ImportReport& o) const
    {
        return sourceLabel == o.sourceLabel && mapped == o.mapped
            && defaults == o.defaults && ignored == o.ignored
            && unsupported == o.unsupported && pendingConfirms == o.pendingConfirms
            && jointStatuses == o.jointStatuses && resources == o.resources
            && selfCollisionCandidates == o.selfCollisionCandidates
            && drivetrainCandidates == o.drivetrainCandidates
            && errors == o.errors && branches == o.branches
            && chainCapability == o.chainCapability
            && submittable == o.submittable;
    }
    bool operator!=(const ImportReport& o) const { return !(*this == o); }
};

// =====================================================================
// Xacro 来源记录（mapXacroExpanded 输入——§6.5；展开语义引擎归 WP-13-T06）
// =====================================================================

/**
 * @brief Xacro 展开来源记录（§6.5"来源记录：原始 .xacro 的
 *        ResourceSnapshot digest 进草稿 externalRefs；展开环境（逐条
 *        替换）随导入报告留痕"的输入载体）。
 *
 * T06（XacroExpand）产出本值→mapXacroExpanded 消费：来源摘要进报告与
 * 草稿 externalRefs（Recorded），substitutions 逐条入报告留痕。T06 落位
 * 前本类型即其产出契约（P-MDL-8：消费侧先行定义、冻结后增量同步）。
 */
struct XacroProvenance {
    core::Digest256 sourceDigest{};   ///< 原始 .xacro 内容摘要（SHA-256；来源身份）
    std::string sourceAbsPath;        ///< 原始 .xacro 路径文本（ExternalResourceRecord.absPath 装配面——路径不作身份）
    /// 展开环境逐条替换（参数名→代入值；构造序保序——确定性）
    std::vector<std::pair<std::string, std::string>> substitutions;
};

// =====================================================================
// 错误面：ImportErrorCode/ImportError（§9.4.3 @错误 行值域的局部枚举——
// Errors.hpp 头注"两族专用错误枚举"同款先例：接口局部载体不并入域级表）
// =====================================================================

/**
 * @brief 导入映射错误码（§9.4.3 @错误 行收编＋R1 边界值；枚举顺序＝
 *        卡面列举序，表尾追加纪律同 ModelingErrorCode）。
 *
 * 与 ImportOutcome 的关系（重要——错误值不等于"无草稿"）：error 是产出
 * 内最严重阻断条件的机器判别面——ZeroAxisReported/MimicBlocked/
 * NameConflict 时草稿仍产出（仅报告/类型保留，不可提交）；Only
 * UnsupportedJointType（不可表达类型）与 MultiBranchNeedsSelection/
 * SourceInconsistent/NotImplemented 时无草稿（无半成品）。ResourceMissing
 * 是软事实（§6.7 缺失≠不可行）——经资源状态表承载，error 不置位。
 */
enum class ImportErrorCode : std::uint8_t {
    /// "UnsupportedJointType"——关节类型不可表达（planar/floating 等；
    /// 不得转 FixedFrame——M-6）。
    UnsupportedJointType,
    /// "MimicBlocked"——mimic 关节（识别＋报告＋阻断；类型保留、经报告
    /// 呈现，不经选择绕过——MDL-12/V-10）。
    MimicBlocked,
    /// "ZeroAxisReported"——零轴/非有限轴（仅报告＋关节标记 Invalid；
    /// 含该类关节的草稿不得提交修订——MDL-11）。
    ZeroAxisReported,
    /// "MultiBranchNeedsSelection"——多可动分支未显式选链（分支报告随
    /// outcome 报告可观察；未经用户显式选择不排除可动分支——§6.4）。
    MultiBranchNeedsSelection,
    /// "ResourceMissing"——外部资源缺失（软事实：转 Recorded 记录草稿
    /// 可携带——§6.7/V-08；error 面仅在本值出现于 outcome.error 时表示
    /// 调用方需关注，映射本身不因它失败）。
    ResourceMissing,
    /// "NameConflict"——localName 重复（净化后仍冲突；应用边界前拦截
    /// ——I-MDL-2）。
    NameConflict,
    /// "SourceInconsistent"——源不一致（字节与依赖树不匹配/URDF 结构
    /// 违例（悬空引用/多根/断链）——io 良构检查之上 URDF 语义层的结构性
    /// 破损；无草稿产出）。
    SourceInconsistent,
    /// "NotImplemented"——mapWorkCellXml 的 R1 边界响应（MDL-18 R2/
    /// 阶段 D；不留桩实现——§6.6）。
    NotImplemented,
};

/**
 * @brief 取导入错误码的稳定 token（枚举成员名原文，如 "MimicBlocked"）。
 *
 * 实现侧唯一映射点（switch 全枚举、无 default——新增枚举值未登记表项时
 * 编译器告警暴露，io/modeling errorCodeToken 同款防线）。本 token 是导入
 * 接口错误面的判别串，不是 MDL-* 稳定诊断码。
 *
 * @param code [in] 导入错误码（全表 8 值均有 token）
 * @return 稳定 token（静态存储期）。纯函数；线程安全；确定性。
 */
std::string_view importErrorCodeToken(ImportErrorCode code) noexcept;

/**
 * @brief 导入映射错误值（ModelingError/EstimateError 同形——码＋有序
 *        参数表＋开发级细节；值面返回不穿越单元边界抛出）。
 *
 * params 键随码而异（如 UnsupportedJointType→joint-name/source-type；
 * MultiBranchNeedsSelection→branch-count/branch-roots）。detail 仅供内部
 * 诊断链，用户呈现前经 diagnostics 脱敏（敏感值纪律同 ModelingError）。
 */
struct ImportError {
    ImportErrorCode code = ImportErrorCode::SourceInconsistent;  ///< 稳定错误码
    std::vector<std::pair<std::string, std::string>> params;     ///< 上下文参数（构造序保序）
    std::string detail;                                          ///< 开发级细节（脱敏前不外泄）
};

// =====================================================================
// 产出值类型：ImportOutcome（§6.1 时序"ImportOutcome{draft, report}"）
// =====================================================================

/**
 * @brief 导入映射产出：草稿工作集＋导入报告＋阻断条件（§6.1/§9.4.3）。
 *
 * 三元关系（调用方判定表）：
 *   - draft 有值：映射成功到草稿粒度——报告仍可能携带待确认/不支持/
 *     缺失资源面；是否可提交看 report.submittable 与 error。
 *   - draft 无值：无法产出草稿（不可表达类型/未选链/源不一致/R1 边界）
 *     ——此时 error 必有值，报告仍完整产出（"识别＋报告"语义：报告是
 *     无条件产出面，AT-15 四清单逐项可观察不因失败丢失）。
 *   - error 有值且 draft 有值：阻断型软失败（零轴/mimic/名称冲突）——
 *     草稿用于呈现与编辑，不可提交修订。
 *
 * 所有权：值语义归调用方（卡 §9.4.3"ImportOutcome 内草稿与报告值语义归
 * 调用方"）；不产生修订、不落盘（§6.1 红线——落盘经 DraftService 归
 * 向导域侧）。
 */
struct ImportOutcome {
    std::optional<RobotDesign> draft;   ///< 草稿工作集（nullopt＝无半成品）
    ImportReport report;                ///< 导入报告（恒产出）
    std::optional<ImportError> error;   ///< 最严重阻断条件（无错误时为空）

    bool operator==(const ImportOutcome& o) const;
    bool operator!=(const ImportOutcome& o) const { return !(*this == o); }
};

// =====================================================================
// 接口：IModelImportMapper（§9.4.3 原文签名）＋无状态实现
// =====================================================================

/**
 * @brief 导入映射器接口（卡 §9.4.3 原文签名的本单元落位；纯函数服务）。
 *
 * 契约要点（§9.4.3 逐条）：
 *   - @pre source 为 io open/snapshot/dependencyTree 产物（字节＋快照＋
 *     依赖树，无环）；调用方契约违约（字节与依赖树不匹配、入口文档不在
 *     树中等）→ 值面 SourceInconsistent（无草稿产出），不抛异常。
 *   - @post 产出草稿工作集＋导入报告；不产生修订、不落盘；同输入字节＋
 *     同 options→同输出（含诊断顺序——按源文件行序稳定排序）。
 *   - @错误 ImportErrorCode（值面——ImportOutcome.error）。
 *   - 线程：可重入多线程；所有权：ImportOutcome 值语义归调用方。
 */
class IModelImportMapper {
public:
    virtual ~IModelImportMapper() = default;

    /**
     * @brief URDF 业务映射（§6.3/§6.4——字段映射/轴语义/资源映射＋链型
     *        判定两维度：分支报告＋显式选链、能力矩阵、continuous 工程
     *        工作范围待确认、mimic/planar/floating 阻断）。
     *
     * @param source  [in] io 已验证产物（ValidatedSource 契约见类型注）
     * @param options [in] 导入选项（确定性输入的一部分）
     * @param diags   [out] 诊断记录输出（追加不清空；按源文件行序稳定
     *                排序；码面＝已注册 MDL-IMPORT-族与 IO-族稳定码，唯一
     *                构造点见 DiagCodes.hpp 常量——禁字符串拼码）
     *
     * @return ImportOutcome{draft, report, error}（三元关系见类型注）
     */
    virtual ImportOutcome mapUrdf(const ValidatedSource& source,
                                  const ImportOptions& options,
                                  std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief Xacro 展开后映射（§9.4.3——展开产物为 URDF 形态，走 mapUrdf
     *        同一边界；来源记录随草稿/报告留痕）。
     *
     * 展开语义引擎归 WP-13-T06（XacroExpand）；本方法只消费展开产物：
     * 映射行为与 mapUrdf 完全一致（同一安全边界——§6.5），差异仅在来源
     * 记录：provenance.sourceDigest 以"原始 .xacro"名义入报告留痕，
     * substitutions 逐条入映射清单 note。
     *
     * @param expanded   [in] 展开产物的 io 形态输入（同 ValidatedSource 契约）
     * @param provenance [in] 展开来源记录（XacroProvenance）
     * @param options    [in] 导入选项
     * @param diags      [out] 诊断输出（同 mapUrdf）
     * @return ImportOutcome（同 mapUrdf）
     */
    virtual ImportOutcome
        mapXacroExpanded(const ValidatedSource& expanded,
                         const XacroProvenance& provenance,
                         const ImportOptions& options,
                         std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief WorkCell 反向导入（MDL-18——R2/阶段 D 通道；§6.6）。
     *
     * R1 边界响应：返回 NotImplemented 稳定错误＋报告说明（unsupported
     * 清单 "workcell-channel-r2" 引导条目），不产草稿、不留桩实现
     * （§6.6"R1 不实现、不留桩代码"原文）；阶段 D 启用时由 WP-13-T17
     * 领取实现（有损提取＋提取报告）。R1 无已注册诊断码面（§9.5 无
     * 对应行——分批纪律不私定码值），引导经报告条目承载，diags 不产出。
     *
     * @param source  [in] io 已验证产物（R2 消费；R1 不解释）
     * @param options [in] 导入选项（R2 消费；R1 不解释）
     * @param diags   [out] 诊断输出（R1 不追加——无注册码面）
     * @return ImportOutcome{无草稿, 报告（通道引导）, NotImplemented}
     */
    virtual ImportOutcome
        mapWorkCellXml(const ValidatedSource& source,
                       const ImportOptions& options,
                       std::vector<core::DiagnosticRecord>& diags) const = 0;
};

/**
 * @brief IModelImportMapper 无状态实现（卡 §3.4 总约定 1：无共享可变
 *        状态、可重入、多线程并发安全；Codec/PropertyEstimator 同款
 *        "接口＋final 实现"落位形态）。
 *
 * DOM 解析经 pugixml（O-40：vcpkg 经典模式 1.16/x64-windows，modeling
 * PRIVATE——有界性由 io BudgetGuard 前置保证：输入字节已过预算，DOM 规模
 * 与输入同阶；本单元不二次读文件、不解析外部实体）。数值解析经
 * std::from_chars（locale 无关——NFR-COR-01/02）。
 */
class ModelImportMapper final : public IModelImportMapper {
public:
    ModelImportMapper() = default;

    ImportOutcome mapUrdf(const ValidatedSource& source,
                          const ImportOptions& options,
                          std::vector<core::DiagnosticRecord>& diags) const override;

    ImportOutcome mapXacroExpanded(const ValidatedSource& expanded,
                                   const XacroProvenance& provenance,
                                   const ImportOptions& options,
                                   std::vector<core::DiagnosticRecord>& diags) const override;

    ImportOutcome mapWorkCellXml(const ValidatedSource& source,
                                 const ImportOptions& options,
                                 std::vector<core::DiagnosticRecord>& diags) const override;
};

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_IMPORT_HPP
