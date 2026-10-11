/**
 * @file   Export.hpp
 * @brief  标准轨迹导出（units/trajectory.md §15.13 Export——格式字段字典
 *         归本域，D-TRJ-8；执行通道＝io ICsvWriter 通用 CSV 写出；WP-16-T10
 *         批，TRJ-07"标准轨迹导出"的领域实现）。
 *
 * 设计依据：
 *   - units/trajectory.md §15.13（接口基线全行——定位/字段表/取舍/前置/
 *     后置/副作用/归属；R1 字段表＝头部〔方言标识行＋#ird-trajectory-
 *     export v1＋身份块六要素〕＋数据列 t_s,joint_index,q,qd,qdd,joint_type
 *     ＋可选段映射列 segment_index,source_task_point；驻留行显式 qd=qdd=0；
 *     前置"数据源＝已归档 Completed 结果的 payload（不从会话态导出）；目
 *     标可写"；后置"文件原子就位；失败/取消→先前输出完整保留（io 语义）"；
 *     副作用"文件系统写（导出副本）；**不产生修订**（AT-04）"；取舍"TCP
 *     位姿列不进 R1 导出"）
 *   - units/trajectory.md §14.5.1（trajectory.export 命令行——"ExportTarget
 *     ＋IAtomicFileWriter；失败保留先前输出"；"worker 或 IO 线程（数据已在
 *     归档结果中，无需重算）"）、§14.4（TRJ-EXPORT-FAILED 码——"标准轨迹
 *     导出失败（io 通道错误转译；先前输出完整保留语义随 io）"）
 *   - units/io.md C-6（字段字典由格式所有者注册——本域即轨迹格式所有者；
 *     io 卡无轨迹格式登记→本头定义，P-TRJ-5 维持登记）；io Csv.hpp §9.4
 *     （ICsvWriter——唯一转义点 NFR-SEC-03；canonical 写出：固定方言行/
 *     声明行尾/UTF-8 无 BOM/to_chars 最短——同数据两次导出字节相同）；
 *     AtomicFile.hpp §4.6（同目录暂存＋同卷原子替换——失败/取消目标不变）
 *   - 需求 TRJ-07、AT-04（导出不产生修订）、NFR-COR-04（导出身份块＝复现
 *     要素自明——§15.13"导出副本带身份块自明"）、NFR-SEC-03（转义唯一经
 *     io）、KIN-12（SI 真值落盘——显示单位换算归呈现层，不进导出）
 *   - 先例：optimization Export.hpp（WP-20-T09——io canonical 写出编排＋
 *     注入式写出端口形态；本头同构：通道经注入工厂缝，产品装配缺省
 *     makeCsvWriter，测试可注入替身验证失败语义）
 *
 * 文件布局（§15.13 头部行序——物理行号自 1 起，读回定位锚）：
 *   行 1        方言标识行（io renderDialectMarker——ICsvWriter emitDialect-
 *               Marker=true 自写）
 *   行 2        格式魔数行 "#ird-trajectory-export v1"（kTrajectoryExport-
 *               FormatMagic）
 *   行 3..8     身份块六行（snapshotId/sliceId/policyContentIdentity/
 *               algorithmVersion/planningSeed/robworkBaselineVersion——
 *               "# <key>=<value>" 单元格行）
 *   行 9        表头（6 列或 8 列——段映射列由请求段归投影开关）
 *   行 10..     数据长表（逐时刻×逐关节；驻留行显式 qd=qdd=0）
 *   读回：ICsvReader 以 headerRow=9（表头物理行号）消费（带标识文件的
 *   转义还原由 reader 承担——前 8 行为单字段行，按 §5.4 无表头前行容忍
 *   由读方 options 承载；identityLineCount 随结果交付，防行序演进后读方
 *   硬编码）。
 *
 * 头文件依赖纪律（冒烟模式安全——TrjTypes.cpp"零 Q 构造/析构点"同款）：
 *   本头对 rw 只经 TimeParam.hpp 的值类型头（header-only 数学）；实现 TU
 *   对 payload 只做 const 读取（分量级访问），零 Q 构造/析构——双模式皆
 *   可编译链接（集成 gating 差异见 CMakeLists 注释；io Csv.hpp 两模式皆
 *   可达——sdurws_ird_io 为七条登记边之一，双模式树内均存在）。
 *
 * 线程安全：导出为会话型单线程操作（ICsvWriter 会话单线程——io §9.4）；
 * 纯计算零修订（文件写仅为导出副本——AT-04）。确定性：同请求同 payload
 * →字节级同输出（io canonical 写出——NFR-COR-02）。
 */

#ifndef IRD_TRAJECTORY_EXPORT_HPP
#define IRD_TRAJECTORY_EXPORT_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/io/Csv.hpp>           // io::ICsvWriter/makeCsvWriter（唯一转义点——NFR-SEC-03）
#include <sdurws/ird/trajectory/Errors.hpp>    // TrajectoryError（前置违约 fail-fast）
#include <sdurws/ird/trajectory/TimeParam.hpp> // TimeParameterization（数据源——已归档 payload）
#include <sdurws/ird/trajectory/TrjTypes.hpp>  // CancelSignal

namespace sdurws::ird::trajectory {

// =====================================================================
// 格式常量（§15.13 字段字典——唯一书写点；改动即格式版本演进，须单元卡
// 增量修订＋魔数版本行递进）
// =====================================================================

/// 格式魔数行（§15.13 头部——`#ird-trajectory-export v1`；版本行随格式
/// 演进递进，读方按行首 "#ird-trajectory-export " 前缀识别）。
inline constexpr char kTrajectoryExportFormatMagic[] = "#ird-trajectory-export v1";

// =====================================================================
// joint_type 列词表（§15.13 数据列——模型关节类型投影的落盘词表）
// =====================================================================

/**
 * @brief 导出关节类型（joint_type 列的两值封闭词表——与 Recheck.hpp
 *        JointTypeKind 同源同语义，独立枚举承载：本头不消费 Recheck 面，
 *        调用方按权威模型投影时两枚举机械对应〔词表锁定由契约测试钉住
 *        一致性〕）。
 */
enum class TrajectoryExportJointType : std::uint8_t {
    /// 旋转关节（q/qd/qdd 单位 rad、rad/s、rad/s²——SI 真值落盘）。
    Revolute,
    /// 移动关节（q/qd/qdd 单位 m、m/s、m/s²——SI 真值落盘）。
    Prismatic,
};

/// joint_type 列的落盘 token（小写——"revolute"/"prismatic"；唯一书写点）。
std::string_view exportJointTypeToken(TrajectoryExportJointType kind);

// =====================================================================
// 身份块（§15.13 头部六要素——复现要素 NFR-COR-04；导出副本带身份自明）
// =====================================================================

/**
 * @brief 导出身份块（§15.13 R1 字段表头部六要素——复现该轨迹的全部身份
 *        绑定；值语义纯结构；线程安全）。
 *
 * 字段来源（装配层从已归档 Completed 结果的 payload 身份块投影；本域只
 * 透传落盘，不解析）：snapshotId/sliceId/policyContentIdentity 为规范文
 * 本（core::ContentIdentity::toCanonical——"cid-<64 hex>"形）；algorithm-
 * Version 为 kTrjAlgorithmVersion 词表值；planningSeed 为规划种子十进制；
 * robworkBaselineVersion 为 runtime 基线 token（evidence Slice 环境身份
 * 同源——§5.6 运行身份要素）。
 */
struct TrajectoryExportIdentity {
    /// 绑定快照内容身份（CON-01——规范文本，非空）。
    std::string snapshotId;
    /// 绑定切片内容身份（CON-05——缓存键/当前性判据；非空）。
    std::string sliceId;
    /// 已解析策略内容身份（CON-06 透传；非空）。
    std::string policyContentIdentity;
    /// 轨迹算法版本 token（kTrjAlgorithmVersion——非空）。
    std::string algorithmVersion;
    /// 规划种子（十进制落盘；§5.5 seed 纪律——>0，由 payload 身份块保证）。
    std::uint64_t planningSeed = 0;
    /// runtime 基线版本 token（§5.6 运行身份要素——非空）。
    std::string robworkBaselineVersion;
};

// =====================================================================
// 请求/通道/结果
// =====================================================================

/**
 * @brief 段映射列的段归条目（可选段映射列 segment_index/source_task_point
 *        的数据面——TimeParamResult.segmentDurationsS 逐段对齐）。
 *
 * 值语义纯结构；线程安全。
 */
struct TrajectoryExportSegmentAttribution {
    /// 段序号（0 基——§6.3 定位键；与段时长投影下标一一对应）。
    std::uint32_t segmentIndex = 0;
    /// 来源任务点规范文本（core::ObjectId::toCanonical——"id-<hex>"形；
    /// 空串＝无来源〔序列级工具段/驻留无站语义〕——ARC-04 可追溯列）。
    std::string sourceTaskPoint;
};

/**
 * @brief 标准轨迹导出请求（§15.13 前置的请求侧承载；前置违约一律
 *        fail-fast，token "trajectory/export/..."）。
 *
 * 前置（§15.13 与 §15.0 纪律逐项落点，校验序见 exportStandardTrajectory）：
 *   - payload 非空且样本非空、时刻非递减、端点样本在位（首 0 末 total，
 *     位级——§6.2 采样不变量）、全值有限（非有限静默落盘被 io 拒绝，提
 *     前拦截给可定位错误）；**数据源＝已归档 Completed 结果的 payload**，
 *     从会话态构造导出＝调用方违约（本域结构性不触会话——§15.13 前置）；
 *   - jointTypes 与样本关节维等长（joint_type 列逐轴落盘）；
 *   - 目标路径非空；父目录须已存在（io 原子写不代建目录——AtomicFile
 *     契约）；目标已存在且 overwriteConfirmed==false → TargetNotConfirmed
 *     （§15.13"不覆盖未经确认文件"——用户确认语义，素材轨非异常）；
 *   - 段映射列开关：segmentAttribution 与 segmentDurationsS **同空同非
 *     空**（两列同开同关——§15.13 可选段映射列成对出现）；非空时逐段时
 *     长 >0 且 Σ==payload.totalDurationS（位级——§12.4 自洽，防段归与
 *     payload 不同源）、attribution[i].segmentIndex==i（下标对齐契约）；
 *   - identity 五个文本字段非空（复现要素完整性——NFR-COR-04）；
 *   - cancel 可空（空＝不可取消；样本批边界轮询——§15.1）。
 *
 * 单位/坐标系：全部数据列 SI 真值（q：rad|m；qd：rad/s|m/s；qdd：rad/s²|
 * m/s²；t：s——KIN-12 显示单位不进导出）。值语义纯结构；线程安全。
 */
struct TrajectoryExportRequest {
    /// 发布目标路径（io 原子替换就位；父目录须已存在——见前置）。
    std::filesystem::path targetPath;
    /// 目标已存在时的覆盖确认（false＋目标存在→TargetNotConfirmed——
    /// §15.13"不覆盖未经确认文件"；true→io OverwriteAtomic 同义原子替换，
    /// 失败仍保留先前输出）。
    bool overwriteConfirmed = false;
    /// 数据源（已归档 Completed 结果的时间参数化 payload——非空；只读借
    /// 用，调用期存活即可）。
    const TimeParameterization* payload = nullptr;
    /// 逐轴关节类型（joint_type 列——与样本关节维等长）。
    std::vector<TrajectoryExportJointType> jointTypes;
    /// 段时长投影（s；TimeParamResult.segmentDurationsS 原样透传——段映
    /// 射列开关半区；与 segmentAttribution 同空同非空）。
    std::vector<double> segmentDurationsS;
    /// 段归条目（段映射列开关半区——见前置；空＝不携带段映射两列）。
    std::vector<TrajectoryExportSegmentAttribution> segmentAttribution;
    /// 身份块六要素（头部行 3..8——复现要素 NFR-COR-04）。
    TrajectoryExportIdentity identity;
    /// 取消观测（可空＝不可取消；样本批边界轮询——命中即中止，目标不
    /// 变〔io RAII 放弃语义〕，UX-03）。
    CancelSignal cancel;
};

/**
 * @brief 导出写出通道缝（§15.13 执行通道的注入式承载——optimization
 *        ExportIoPorts 先例同构：产品装配走缺省工厂，测试注入替身验证
 *        失败/取消语义，不经本工厂）。
 *
 * 线程：工厂调用发生在导出会话起点（单线程）；实例语义随 io §9.4。
 */
struct TrajectoryExportIoPorts {
    /// CSV 写出器工厂（缺省＝io::makeCsvWriter——io 唯一转义点；替身注入
    /// 点＝故障注入测试的 prepare/写入/finish 各失败位）。
    std::function<std::unique_ptr<io::ICsvWriter>()> csvWriterFactory{
        [] { return io::makeCsvWriter(); }};
};

/**
 * @brief 导出结局（§15.13 后置的落地面——值语义纯结构；线程安全）。
 *
 * 状态—字段联动（实现保证）：
 *   - Ok：dataRowCount＝写出的数据行数（逐时刻×逐关节长表）；identity-
 *     LineCount＝头部块行数（魔数 1＋身份块 6＝7——读回以 headerRow=
 *     本值+2〔方言标识行 1 行＋表头前头部块〕定位，见文件布局注）；
 *   - TargetNotConfirmed：目标存在且未确认覆盖——errorToken=
 *     "IO-PACK-TARGET-EXISTS"（io 词表单点复用）、errorMessage 中文；目
 *     标未触碰（预检在写会话建立之前）；
 *   - Canceled：样本批边界取消命中——零错误素材、目标不变（io RAII 放
 *     弃——暂存清理，替换未发生）；dataRowCount 不携带有效值；
 *   - Failed：io 通道错误——errorToken＝io 稳定码（IO-*；诊断组装面据
 *     此出 TRJ-EXPORT-FAILED 素材，§14.4"io 通道错误转译"）；errorMessage
 *     中文；**先前输出完整保留**（io 结构性保证——finish 前失败＝目标
 *     不变）。
 */
struct TrajectoryExportOutcome {
    /// 导出结局（四态封闭词表）。
    enum class Status : std::uint8_t {
        /// 成功——目标原子就位（先前输出被确认替换或首次落盘）。
        Ok,
        /// 目标已存在且未确认覆盖（§15.13——用户确认语义；目标未触碰）。
        TargetNotConfirmed,
        /// 取消观测命中（UX-03——目标不变；非错误）。
        Canceled,
        /// io 通道失败（先前输出完整保留——io 结构性保证）。
        Failed,
    };

    /// 导出结局（四态）。
    Status status = Status::Canceled;
    /// 稳定错误 token（TargetNotConfirmed 恒 "IO-PACK-TARGET-EXISTS"；
    /// Failed 恒 IO-* 族 io 稳定码；Ok/Canceled 恒空）。
    std::string errorToken;
    /// 中文错误文案（TargetNotConfirmed/Failed 非空——说明失败点与实际
    /// 状态；Ok/Canceled 恒空）。
    std::string errorMessage;
    /// 头部块行数（魔数＋身份块——Ok 时有效；读回定位锚，防行序演进后
    /// 读方硬编码）。
    std::size_t identityLineCount = 0;
    /// 数据行数（逐时刻×逐关节长表——Ok 时有效；＝Σ样本数×关节数）。
    std::size_t dataRowCount = 0;
};

/**
 * @brief 执行标准轨迹导出（§15.13 签名——格式字段字典本域定义＋io 通用
 *        通道执行；TRJ-07"标准轨迹导出"的唯一领域执行点）。
 *
 * 执行序（每步语义见行内注释）：
 *   1. 前置校验（TrajectoryExportRequest 前置全表——违约抛 TrajectoryError
 *      fail-fast，token "trajectory/export/..."）；
 *   2. 可写性预检（目标存在∧未确认覆盖→TargetNotConfirmed——§15.13"不
 *      覆盖未经确认文件"；目标不存在→放行）；
 *   3. 取消轮询（命中→Canceled，零触碰）；
 *   4. 建立写会话（工厂创建 writer→open 文件目标——io 于 open 即建同目
 *      录暂存文件，尽早暴露权限/占用问题；方言＝io 缺省＋标识行自写）；
 *   5. 头部块（魔数行＋身份块六行——单 Text 单元格行；首字符 '#'，io 转
 *      义不触及，原样落盘）；
 *   6. 表头行（6 列或 8 列——writeHeader 经 io 转义）；
 *   7. 数据长表（样本序×关节序双层循环——行序＝样本优先、关节次之的稳
 *      定序；t/q/qd/qdd 经 io to_chars 最短往返；驻留行显式 qd=qdd=0〔随
 *      样本表原值落盘，§15.13"驻留行显式"〕；段映射两列按开关追加；取消
 *      在样本批边界轮询——命中即返回，writer 析构放弃＝目标不变）；
 *   8. finish（flush＋同卷原子替换——目标就位；失败→清理暂存目标不变）
 *      →Ok。
 *
 * 本函数零修订零项目触碰（导出副本写盘是唯一副作用——AT-04；§15.13"副
 * 作用"行）；不消费会话态/评估器/规划设施（数据源仅 payload——§15.13 前
 * 置）；文件系统写全部经 io 通道（零自有文件写路径——N13/SA-12：CSV 通
 * 用转义/解析归 io，本域只登记格式字段字典）。
 *
 * @param request [in] 导出请求（前置见 TrajectoryExportRequest 注；违约
 *                抛 TrajectoryError——fail-fast）
 * @param ports   [in] 写出通道缝（缺省＝产品装配 makeCsvWriter；测试替
 *                身注入点）
 *
 * @return 导出结局（四态——失败/取消先前输出完整保留，§15.13 后置）
 *
 * @throws TrajectoryError 调用方契约违约（token "trajectory/export/..."——
 *         payload 完整性/维度/身份块/段归投影/工厂缺席等；见各校验点）
 *
 * 单线程使用；确定性（同请求同 payload→字节级同输出——io canonical）。
 */
TrajectoryExportOutcome exportStandardTrajectory(
    const TrajectoryExportRequest& request,
    const TrajectoryExportIoPorts& ports = {});

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_EXPORT_HPP
