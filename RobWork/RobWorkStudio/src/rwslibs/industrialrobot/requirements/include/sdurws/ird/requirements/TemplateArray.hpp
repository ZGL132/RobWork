/**
 * @file   TemplateArray.hpp
 * @brief  ITemplateArrayService——工艺模板/工位镜像/四类批量阵列/一键重生成
 *         与解除关联（§7.1/§7.2 原文契约的落位）：全部为纯函数，把"生成
 *         一批新需求条目"这件事与编辑器/命令彻底解耦。
 *
 * 设计依据：
 *   - units/requirements.md §3.3（公共头表 TemplateArray.hpp 行——T07）、
 *     §7.1（工艺模板：TemplateKind 词表 6 值、applyTemplate 产出编辑批次
 *     {新条目数组＋生成溯源}、模板不绕过命令）、§7.2（镜像阵列与派生
 *     需求：镜像面＝refFrame＋法向、不可镜像规则处置 PendingManualResolution、
 *     派生无环＝溯源为一次性参数快照、删除保护＝允许＋诊断提示）、
 *     §9.5（ITemplateArrayService 行原文签名）、§4.7（I-REQ-10 派生不回写）、
 *     §4.3（GenerationProvenance 生成溯源值模型）、§3.4（纯函数服务：无
 *     共享可变状态、可重入、并发安全；物理量恒 SI）
 *   - 需求 REQ-07（工艺模板）、REQ-11（镜像/阵列/需求集撤销）、AT-24、
 *     NFR-COR-01/02（确定性）、NFR-COR-03（不静默改写）、D-REQ-4（模板
 *     产物为普通条目可继续手改——非 DerivedReadOnly）
 *   - 任务契约 tasks/foundation/WP-14-T07.json acceptance 1~5（本头即其
 *     服务面落位；编辑器批次入口/批量撤销随 Editor.hpp/.cpp T07 增列）
 *
 * 背景说明（为什么模板/镜像/阵列只是"草稿生成辅助"）：§7.1 原文——模板
 * **不得绕过命令**：本服务产出的 EditBatch 只是一组带溯源的候选条目，
 * 经编辑器 applyEdit 进入草稿后，仍须走 apply-requirement-set 命令才能
 * 成为项目修订。这样"生成"零副作用（纯函数、无修订、无对象写入），
 * 撤销/重做/审计语义完全复用命令通道，不产生第二条写路径（PA-1 权威
 * 唯一——修订写路径归 project）。
 *
 * ★ I-REQ-10（派生不回写）的服务面执行：产物条目为**普通独立条目**，
 * 溯源 GenerationProvenance 只携带"一次性参数快照"（generatorId/instanceId/
 * linked/parameters）——没有指向源条目的活性引用字段，源条目在本服务的
 * 全部出口中保持字节不变（零回写）。源条目 ObjectId 仅以字符串键值对
 * （参数键 "source-id"）进入参数快照，用于重生成时的确定性重解析与删除
 * 提示扫描——它是快照留痕，不是活性链接：源条目变更不会传播到派生条目
 * （§7.2"变更传播＝用户显式重生成"）。
 *
 * 线程安全：服务与自由函数无共享可变状态、可重入——多线程并发调用安全
 * （§3.4 总约定 1）。确定性：同输入→同输出（临时 ObjectId 除外——身份
 * 生成本质随机，NFR-COR-01 的确定性承诺不含 ObjectId 分配；instanceId
 * 同理每次调用新生成）。
 */

#ifndef IRD_REQUIREMENTS_TEMPLATEARRAY_HPP
#define IRD_REQUIREMENTS_TEMPLATEARRAY_HPP

#include <sdurws/ird/core/DiagData.hpp>                  // DiagnosticRecord——批次警告诊断载体
#include <sdurws/ird/requirements/Errors.hpp>            // RequirementError——参数非法值面
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // TaskPoint/词表/GenerationProvenance

#include <string>
#include <vector>

namespace sdurws::ird::requirements {

// =====================================================================
// 前向声明（§9.5 regenerate 入参——RequirementWorkingSet 完整定义在
// Editor.hpp；本头不 include Editor.hpp，Editor.hpp 单向 include 本头，
// 消除模板服务↔编辑器的头文件环。实现 TU（TemplateArray.cpp）按需
// include Editor.hpp 取完整类型）
// =====================================================================

struct RequirementWorkingSet;  ///< 需求工作集（§9.3——编辑器演算视图）

// =====================================================================
// 生成溯源参数键（参数快照内约定键名——GenerationProvenance.parameters
// 的键值语义登记；字符串字面量常量＝唯一书写点，消费者共引禁二写）
// =====================================================================

/// 参数键：源条目 ObjectId 规范文本（一个键值对记录一个源；重生成重解析
/// 与删除提示扫描的快照键——非活性链接，见文件头 I-REQ-10 注）。
inline constexpr std::string_view kGenParamSourceId = "source-id";

/// 参数键：姿态规则不可镜像标记（值恒 "1"——AlignFrame/AlignGeometryNormal
/// 引用目标在镜像侧不存在，§7.2 处置为待人工处理；值面读取方据此呈现
/// "待人工处理"态，规则本体保留不降级）。
inline constexpr std::string_view kGenParamOrientationPending = "orientation-pending";

/// 参数键：生成时的类目 token（模板 kind token/阵列 kind token——溯源链
/// 的人类可读留痕面）。
inline constexpr std::string_view kGenParamKindToken = "kind";

// =====================================================================
// 模板参数（§7.1 applyTemplate 入参面；TemplateParams 为卡面未逐字段
// 规定的支撑值模型——字段集按"六类模板共用一个参数形状"登记，逐类
// 差异由 defaultTemplateParams 的设计默认承载）
// =====================================================================

/**
 * @brief 工艺模板生成参数（§7.1 TemplateParams——applyTemplate 入参）。
 *
 * 形状语义：在 refFrame 系内以 origin 为基准点生成 countX×countY 网格
 * 工位（任务点条目），相邻工位间距 spacingM（m，X 向）×spacingYM（m，
 * Y 向），每工位带 approach/retract 段（approachAxis 轴、approachDistanceM
 * m）、Fixed 姿态 fixedRpy（rad，Z-Y-X 欧拉）与容差 tolerance。
 *
 * 单位纪律（§3.4 总约定 3）：一切长度 m、角度 rad；本结构不做单位换算
 * （显示单位换算归 ui/导入通道）。
 *
 * 校验边界：countX/countY ≥1、spacingM/spacingYM >0 且有限、
 * approachDistanceM >0 且有限、fixedRpy 三分量有限、tolerance 合法
 * （I-REQ-5）、baseName 非空且不含集合内非法定位字符约束（非空即可——
 * 名称唯一性由编辑器集合级核对，I-REQ-3）。违约经 EditBatch 错误值面
 * 返回（applyTemplate 的产出为 err 态批次，见 EditBatch::ok）。
 */
struct TemplateParams {
    std::string baseName;  ///< 生成条目名前缀（非空；条目名＝前缀＋"-行号列号"序号）
    RequirementReference refFrame;  ///< 参考坐标系（场景引用——wellFormed 且场景词表内）
    rw::math::Vector3D<double> origin{0.0, 0.0, 0.0};  ///< 基准点（m；refFrame 系；格点 [1][1] 即 origin）
    double spacingM = 0.0;        ///< X 向相邻工位间距（m；>0 且有限）
    double spacingYM = 0.0;       ///< Y 向相邻工位间距（m；>0 且有限）
    int countX = 0;               ///< X 向工位数（≥1）
    int countY = 0;               ///< Y 向工位数（≥1）
    SegmentAxis approachAxis = SegmentAxis::ReferenceZ;  ///< 进退段轴（§5.1 词表——模板工位默认沿参考系 Z）
    double approachDistanceM = 0.0;  ///< 接近/撤离段距离（m；>0 且有限）
    rw::math::Vector3D<double> fixedRpy{0.0, 0.0, 0.0};  ///< Fixed 姿态角（rad；Z-Y-X 欧拉，refFrame 系）
    ToleranceSpec tolerance{};    ///< 容差（要求值；I-REQ-5）
};

/**
 * @brief 模板类别的设计默认参数（§7.1"模板参数数值为设计默认（黄金
 *        数据集锁定）"的落位——六类各一组锁定值，注释即黄金登记）。
 *
 * 黄金默认值表（2026-09-22 卡面锁定的"设计默认"在此逐类显式化；修改
 * 即语义变更，须走单元卡增量修订）：
 *   - BinPicking      1×2 格、间距 0.5 m（料箱拾取双工位）
 *   - MachineTending  1×3 格、间距 0.4 m（机床上下料三工位）
 *   - Palletizing     2×2 格、间距 0.3 m（码垛四角工位）
 *   - Inspection      1×1 格（单检测位）
 *   - ToolChange      1×1 格（单换刀位）
 *   - Handover        1×2 格、间距 0.6 m（交接双工位）
 * 共同默认：基准点原点、World 系、ReferenceZ 进退轴 0.1 m、Fixed 姿态
 * (0,0,0) rad、默认容差（1×10⁻³ m / 1°）、名前缀＝类别 token 小写。
 *
 * @param kind [in] 模板类别（全表 6 值）
 * @return 该类别的黄金默认参数（值语义——调用方可再改字段）
 *
 * 纯函数；线程安全；确定性（同类别同参数——黄金锁定面）。
 */
TemplateParams defaultTemplateParams(TemplateKind kind);

// =====================================================================
// 镜像面规格（§7.2——"过参考系镜像面（refFrame＋镜像轴法向）"）
// =====================================================================

/**
 * @brief 镜像面规格（§7.2 MirrorPlaneSpec）：过 refFrame 参考系原点、以
 *        axisNormal 为法向的平面。
 *
 * 几何语义（§7.2 原文"过参考系镜像面"）：平面过 refFrame 所指参考系的
 * **原点**；位置镜像在该参考系坐标下做反射 p' = p − 2(n·p)n（n 为法向
 * 单位向量）；姿态镜像为反射共轭 R' = M·R·M（M 为该平面的反射矩阵，
 * M·R·M 仍为纯旋转——行列式 +1）。这是**需求数据变换**：坐标全在
 * refFrame 系下表达，不做任何跨坐标系变换（卡 §7.2 隔离声明——机器人
 * 坐标变换语义归 runtime/下游）。
 *
 * 校验边界：axisNormal 非零且三分量有限（零法向无平面——非法参数）；
 * refFrame 须为场景词表引用（wellFormed 且 forScene）。违约经 EditBatch
 * 错误值面返回。
 */
struct MirrorPlaneSpec {
    RequirementReference refFrame;  ///< 镜像面所过参考系（World 或模型系/场景对象——场景词表）
    rw::math::Vector3D<double> axisNormal{0.0, 0.0, 0.0};  ///< 镜像面法向（refFrame 系；非零有限；内部单位化使用）
};

// =====================================================================
// 阵列参数（§7.2——四类阵列；ArrayParams 为卡面未逐字段规定的支撑值
// 模型，四构型共用一个参数形状，逐构型读取各自的字段子集）
// =====================================================================

/**
 * @brief 批量阵列生成参数（§7.2 ArrayParams——applyArray 入参）。
 *
 * ★ 与 §9.5 行原文签名的偏差登记（units/requirements.md §14.6 v0.7——
 * DTB §5.4 等价调整纪律）：ArrayParams 内嵌 `sources`（源条目值数组）。
 * 原因：applyArray(ArrayKind, ArrayParams) 无工作集入参——纯函数服务
 * 无状态，仅凭 ObjectId 无法解析出源条目值（值不在参数里就在入参里，
 * 二者必居其一）；把源条目**值**（含源 ObjectId）作为参数快照的输入
 * 载体是唯一自洽形态。applyMirror 同理以 `std::vector<TaskPoint>` 源值
 * 列表替代卡面的 ObjectId 列表（§9.5 代码块已随 v0.7 同步修订）。
 *
 * 四构型的字段读取表（其余字段忽略——逐构型校验各自子集，缺省值不
 * 参与语义）：
 *   - Linear     ：direction/spacingM/count；
 *   - Rectangular ：direction/spacingM/count＋direction2/spacing2M/count2；
 *   - Circular    ：center/radiusM/startAngleRad/angleStepRad/count；
 *   - Polyline    ：polyline/polylineSpacingM。
 *
 * 几何语义（全在源条目 refFrame 系下表达；派生条目继承源 refFrame）：
 *   - Linear      ：第 i 条（i=1..count）位置＝源位置＋direction·(spacingM·i)；
 *   - Rectangular ：第 (i,j) 条＝源位置＋direction·(spacingM·i)＋
 *                   direction2·(spacing2M·j)，i=1..count、j=1..count2；
 *   - Circular    ：第 i 条位于 refFrame 系 XY 平面圆（法向 Z——圆心/
 *                   半径在该平面表达）上，角度 θᵢ = startAngleRad＋
 *                   angleStepRad·i，位置＝center＋radiusM·(cosθᵢ, sinθᵢ, 0)；
 *   - Polyline    ：沿折线从起点量弧长 sₖ = polylineSpacingM·k（k=1 起），
 *                   sₖ ≤ 折线总弧长的每个 sₖ 处放一条（条数由间距与
 *                   总弧长派生——"数量"不由调用方直接指定）。
 *
 * 校验边界：sources 非空；Linear/Rectangular/Circular 的 count/count2 ≥1
 * 且间距 >0 有限、方向向量非零有限；Circular 半径 >0 有限；Polyline
 * 顶点 ≥2、间距 >0、折线总弧长 >0。违约经 EditBatch 错误值面返回。
 */
struct ArrayParams {
    /// 源条目值数组（非空——含源 ObjectId 与被派生的字段值；本服务不
    /// 改写源条目：I-REQ-10 零回写在出口面保证）。
    std::vector<TaskPoint> sources;

    // ---- Linear / Rectangular 共用主方向组 ----
    rw::math::Vector3D<double> direction{1.0, 0.0, 0.0};  ///< 主方向（refFrame 系；非零有限；内部单位化使用）
    double spacingM = 0.0;   ///< 主向间距（m；>0 且有限）
    int count = 0;           ///< 主向条数（Linear 生成总数；Rectangular 列数；≥1）

    // ---- Rectangular 第二方向组 ----
    rw::math::Vector3D<double> direction2{0.0, 1.0, 0.0};  ///< 第二方向（refFrame 系；非零有限；内部单位化使用）
    double spacing2M = 0.0;  ///< 第二向间距（m；>0 且有限）
    int count2 = 0;          ///< 第二向条数（仅 Rectangular；≥1）

    // ---- Circular ----
    rw::math::Vector3D<double> center{0.0, 0.0, 0.0};  ///< 圆心（m；refFrame 系 XY 平面内）
    double radiusM = 0.0;        ///< 圆半径（m；>0 且有限）
    double startAngleRad = 0.0;  ///< 起始角（rad；自 +X 轴逆时针）
    double angleStepRad = 0.0;   ///< 角步距（rad；非零——0 步距生成全重名同位点，非法）

    // ---- Polyline ----
    std::vector<rw::math::Vector3D<double>> polyline;  ///< 折线顶点（m；refFrame 系；≥2 点，相邻点不重合）
    double polylineSpacingM = 0.0;  ///< 沿折线弧长间距（m；>0 且有限）
};

// =====================================================================
// 编辑批次（§7.1/§7.2 产出面——"新条目数组＋生成溯源"的值承载）
// =====================================================================

/**
 * @brief 模板/镜像/阵列/重生成的编辑批次（§7.1 原文"编辑批次（新条目
 *        数组＋生成溯源）"的值承载；纯值——调用方所有）。
 *
 * 两态（与 CreateOutcome 的 ok/err 同构）：
 *   - ok=true：newPoints 为带完整溯源的候选条目（临时 ObjectId 已分配——
 *     编辑期句柄，正式分配归命令 prepare，O-36），provenance 为批次溯源
 *     （同批同 instanceId），diagnostics 为批次警告（如不可镜像规则的
 *     REQ-DERIVE-MIRROR-PENDING——warning 不阻断应用），summary 为人读
 *     中文摘要（编辑器并入变更摘要）；replaceNames 仅供重生成批次携带
 *     （被替换条目名——编辑器先按名移除再写入 newPoints，模板/镜像/
 *     阵列批次恒空）；
 *   - ok=false：error 为参数非法的首个违例（调用方错误——修正参数后
 *     重试；此时其余字段无效）。
 *
 * 确定性（NFR-COR-01/02）：同输入（含同源条目值与同参数）→同 newPoints
 * （除 ObjectId/instanceId 的随机性外逐字段一致）＋同 diagnostics 序＋
 * 同 summary——条目生成序＝参数派生序（确定性），批次内不做重排（编辑
 * 器写入时按 I-REQ-1 统一排序）。
 */
struct EditBatch {
    bool ok = false;  ///< true＝newPoints/provenance 有效；false＝error 有效
    std::vector<TaskPoint> newPoints;  ///< ok 态：新条目（临时句柄＋溯源已填；生成入口产物恒 ≥1）
    GenerationProvenance provenance{};  ///< ok 态：批次溯源（generatorId/instanceId/linked=true/parameters 快照）
    /// ok 态：被替换条目名（仅重生成批次非空——编辑器应用时先移除同名
    /// 条目再写入 newPoints；移除同样受 §5.1 引用保护约束——被引用条目
    /// 的批次整体拒绝）。升序去重（确定性）。
    std::vector<std::string> replaceNames;
    std::vector<core::DiagnosticRecord> diagnostics;  ///< ok 态：批次警告诊断（warning 级——不阻断应用）
    std::string summary;             ///< ok 态：人读中文摘要（"镜像生成 N 条"等）
    RequirementError error{};        ///< err 态：首个参数违例（调用方错误值面）

    bool operator==(const EditBatch& o) const;
    bool operator!=(const EditBatch& o) const { return !(*this == o); }
};

// =====================================================================
// 重生成产出（§9.5 RegenerateOutcome——"替换 linked∧未手改 条目；
// 手改条目→冲突诊断清单（不静默覆盖）"）
// =====================================================================

/**
 * @brief 一键重生成产出（§9.5 行原文契约的值承载）。
 *
 * 两态（与 EditBatch 同构约定）：
 *   - found=true：generatorInstanceId 命中工作集内某生成批次——
 *     batch.newPoints 为**未手改 linked 条目**的替换条目（同批溯源、linked
 *     不变、新临时句柄；"替换"语义＝编辑器应用时先移除被替换条目再写入
 *     替换条目），conflictNames 为手改条目名清单（确定性升序），diagnostics
 *     为每条手改条目的 REQ-DERIVE-REGENERATE-CONFLICT（warning——不阻断，
 *     手改条目原样保留）；found 且批内全部条目未手改时 conflictNames/diagnostics
 *     为空；
 *   - found=false：generatorInstanceId 未命中（批次不存在或已无 linked
 *     条目——解除关联后重生成即空转）；error 为定位错误。
 *
 * "手改"判定（确定性、无隐藏状态）：把该批次的生成参数快照按生成器
 * 确定性重算，得到候选条目集；工作集内同批次 linked 条目按名称与候选
 * 配对——除 ObjectId/溯源参数快照外任一字段不一致即"已手改"（生成是
 * 确定性的，值有差异只能来自手改；名称被改的条目无法配对，同样计入手改）。
 */
struct RegenerateOutcome {
    bool found = false;  ///< true＝批次命中且 batch/conflictNames 有效；false＝error 有效
    EditBatch batch{};   ///< found 态：替换条目批次（ok 恒 true；溯源沿用原 instanceId/parameters）
    std::vector<std::string> conflictNames;  ///< found 态：手改条目名（升序去重——确定性）
    std::vector<core::DiagnosticRecord> diagnostics;  ///< found 态：REQ-DERIVE-REGENERATE-CONFLICT 逐条警告
    RequirementError error{};  ///< !found 态：定位错误（批次不存在）

    bool operator==(const RegenerateOutcome& o) const;
    bool operator!=(const RegenerateOutcome& o) const { return !(*this == o); }
};

// =====================================================================
// ITemplateArrayService——接口（§9.5 行原文契约；纯函数）
// =====================================================================

/**
 * @brief 模板/镜像/阵列服务（§9.5 行原文契约——支撑接口；§7.1/§7.2 的
 *        实现面）。纯函数：无共享可变状态、可重入、并发安全（§3.4）。
 *
 * 生成器标识（generatorId——GenerationProvenance 的生成器登记串）：
 *   - 模板："template:<kind-token 小写连字符>"（如 "template:bin-picking"）；
 *   - 镜像："mirror"；
 *   - 阵列："array:<kind-token 小写连字符>"（如 "array:circular"）。
 * instanceId＝新 core::ObjectId 规范文本（每次生成调用新生成——批次
 * 唯一；重生成沿用被重生成批次的 instanceId，保持"同批次"语义）。
 */
class ITemplateArrayService {
public:
    virtual ~ITemplateArrayService() = default;

    /**
     * @brief 应用工艺模板（§7.1 行原文——纯函数产出编辑批次）。
     *
     * 生成 countX×countY 个任务点条目（黄金默认参数见
     * defaultTemplateParams）：独立临时 ObjectId、名称＝前缀-行号列号、
     * Fixed 姿态/进退段/容差按参数、generation 溯源完整（linked=true）。
     * 产物为普通条目（source=UserProvided——D-REQ-4 可继续手改，非
     * DerivedReadOnly）。
     *
     * @param kind   [in] 模板类别（词表 6 值）
     * @param params [in] 生成参数（推荐 defaultTemplateParams(kind) 起步）
     * @return ok＝编辑批次（可为 applyEdit 入参）；err＝参数非法
     *
     * 纯函数；线程安全；确定性（同输入同产出——ObjectId/instanceId 除外）。
     */
    virtual EditBatch applyTemplate(TemplateKind kind, const TemplateParams& params) const = 0;

    /**
     * @brief 工位镜像（§7.2——过参考系镜像面反射派生新条目）。
     *
     * 每个源条目派生恰一条镜像条目：位置反射 p−2(n·p)n；姿态规则按
     * 种类处置（§7.2 原文）——Fixed/PointAtTarget/ToolRollFree 正常镜像
     * （姿态反射共轭/目标点反射/滚转区间翻转），AlignFrame/
     * AlignGeometryNormal 引用目标在镜像侧不存在→**保留规则原样**＋
     * 参数快照打 orientation-pending 标记＋REQ-DERIVE-MIRROR-PENDING
     * warning（不静默猜、不静默降级为 Fixed）；sequenceKey 不继承（顺序
     * 是用户语义，批量派生后由用户显式编排——见实现注）。
     *
     * @param sources [in] 源条目值（含 ObjectId；本服务零回写——出口面
     *                保证源条目字节不变）
     * @param plane   [in] 镜像面（refFrame＋法向；源条目须与面同参考系
     *                表达——异系镜像属跨坐标变换语义，本单元不做）
     * @return ok＝编辑批次；err＝面参数非法或 sources 空
     *
     * 纯函数；线程安全；确定性。
     */
    virtual EditBatch applyMirror(const std::vector<TaskPoint>& sources,
                                  const MirrorPlaneSpec& plane) const = 0;

    /**
     * @brief 批量阵列（§7.2——四构型 Linear/Rectangular/Circular/Polyline）。
     *
     * 按构型字段读取表（ArrayParams 类注）从源条目派生 N 条：位置按构型
     * 几何平移/极坐标定位/弧长定位；姿态规则原样继承（含 PointAtTarget
     * 目标点——多工位指向同一目标是有意义的工艺语义；需要逐点目标时
     * 逐源再次派生）；sequenceKey 不继承。
     *
     * @param kind   [in] 阵列构型（词表 4 值）
     * @param params [in] 阵列参数（含源条目值——类注偏差登记）
     * @return ok＝编辑批次（条目数＝构型几何派生数）；err＝参数非法
     *
     * 纯函数；线程安全；确定性。
     */
    virtual EditBatch applyArray(ArrayKind kind, const ArrayParams& params) const = 0;

    /**
     * @brief 一键重生成（§9.5 行原文——替换 linked∧未手改 条目；手改
     *        条目→冲突诊断清单，不静默覆盖）。
     *
     * 以工作集中 instanceId 命中的批次溯源参数快照确定性重算候选条目，
     * 与该批次 linked 条目逐名称配对：一致→替换条目（新临时句柄）；不
     * 一致/无法配对→手改（冲突清单＋REQ-DERIVE-REGENERATE-CONFLICT
     * warning，原条目保留）。linked=false 的条目不参与重生成。
     *
     * @param ws                 [in] 需求工作集（只读——本函数零回写，
     *                           替换经返回的 EditBatch 由编辑器应用）
     * @param generatorInstanceId [in] 生成批次实例标识（GenerationProvenance.
     *                           instanceId——同批条目同值）
     * @return found＝重生成产出（含冲突面）；!found＝批次未命中＋定位错误
     *
     * 纯函数；线程安全；确定性（同工作集同入参→同产出）。
     */
    virtual RegenerateOutcome regenerate(const RequirementWorkingSet& ws,
                                         const std::string& generatorInstanceId) const = 0;
};

/// ITemplateArrayService 的产品实现（无状态——可默认构造，拷贝/移动平凡）。
class TemplateArrayService final : public ITemplateArrayService {
public:
    EditBatch applyTemplate(TemplateKind kind, const TemplateParams& params) const override;
    EditBatch applyMirror(const std::vector<TaskPoint>& sources,
                          const MirrorPlaneSpec& plane) const override;
    EditBatch applyArray(ArrayKind kind, const ArrayParams& params) const override;
    RegenerateOutcome regenerate(const RequirementWorkingSet& ws,
                                 const std::string& generatorInstanceId) const override;
};

// =====================================================================
// 解除关联（§7.1——"linked 条目支持一键按参数重生成/解除关联"的后半；
// §9.5 卡面未单列方法，登记为同头自由函数——§14.6 v0.7 增量登记）
// =====================================================================

/**
 * @brief 解除指定生成批次的关联（§7.1"解除关联＝linked=false"）：把
 *        entries 中 generation.instanceId==generatorInstanceId 且 linked
 *        的条目改为 linked=false（其余字段——含 ObjectId、参数快照——
 *        逐字节保持）。
 *
 * 解除后该批次条目不再被 regenerate 替换（"重生成不再替换该条目"，
 * §4.3 linked=false 行语义）；参数快照保留（溯源链留痕不因解除而失真）。
 *
 * @param entries            [in] 工作集点集条目（只读）
 * @param generatorInstanceId [in] 目标批次实例标识
 * @return 更新后的条目数组（序保持输入序；无命中时与输入逐字段相等）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<TaskPoint> unlinkGenerator(const std::vector<TaskPoint>& entries,
                                       const std::string& generatorInstanceId);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_TEMPLATEARRAY_HPP
