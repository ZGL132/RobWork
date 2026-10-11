/**
 * @file   TrajPanelTypes.hpp
 * @brief  trajectory 插件界面的零 Qt 值类型半区——服务缝聚合、会话态与
 *         呈现私有 DTO（units/trajectory.md §14.5.2 投影与会话态的面板
 *         侧承载；WP-16-T12 落位）。
 *
 * 设计依据：
 *   - units/trajectory.md §14.5.2（投影与会话态——DomainReadinessItem
 *     同构投影供七态呈现（domainKey="trajectory"）；动画数据＝归档
 *     payload 的查表面；播放＝按时刻插值查表驱动会话姿态；局部重规划
 *     产品语义"新增一份归档结果、两份结果都是历史运行"）、§14.5.1
 *     （七命令行"UI 线程"列——曲线/动画只读归档 payload、纯插值查表
 *     零规划/碰撞/时间化计算）、§16.2（零修订与零计算红线机器可断言）；
 *   - units/trajectory.md §10.7 对位面（命令受理记录的零修订观测——
 *     Commands.hpp kTrajectorySessionContract 四常量的逐条呈现面）；
 *   - 先例：dynamics/plugin/DynPanelTypes.hpp（WP-17-T09 服务缝聚合＋
 *     会话态的零 Qt 模型半区形态——"缝由装配层注入、面板零环境依赖"
 *     同款纪律）；
 *   - 任务契约 tasks/foundation/WP-16-T12.json（acceptance 1/2）。
 *
 * ★ 落地面口径（诚实登记，DTB §5.4 精神——单元卡 §1.2 T12 注同步）：
 *   1. **ui 公共类型的消费边界**：本头为插件私有值半区，零 ui 头包含
 *      （ui 类型翻译只在 assembly 公共头与 TrajUiModule 完成——私有值
 *      面与 ui 类型解耦，便于模型层测试零 Qt 装载）；就绪投影行以字段
 *      同构的会话态承载，翻译为 ui 域行归 TrajUiModule::readonly-
 *      Projections 单点。
 *   2. **缝纪律（dynamics 先例同款）**：动画查表/曲线投影/命令提交/
 *      可用性/文案解析/会话姿态驱动全部为 std::function 缝，由装配层
 *      （宿主或开发 harness）注入；缝为空＝该面未装配——呈现侧如实
 *      降级（"数据未装配"），绝不虚构数值（NFR-COR-03）。数据缝的返回
 *      类型为计算库数据面值（Playback.hpp 的只读投影产物——T10 落位
 *      的公共契约），本头经包含消费之。
 *   3. **零计算红线（卡 §16.1/§16.2）**：本头与整个插件面不消费任何
 *      规划/碰撞/时间化/复检设施符号；面板侧只做查表、读数与呈现——
 *      契约测试 TrajectoryPluginAssemblyContractTest 全文词表扫描钉住。
 *
 * 线程约束：全部值类型无同步原语——仅 UI 线程访问（卡 §3.4——面板/
 * 会话态生命周期随装配层；数据缝的线程安全语义由缝的提供方声明，本侧
 * 约定全部缝在 UI 线程调用）。
 */

#ifndef IRD_TRAJECTORY_PLUGIN_TRAJPANELTYPES_HPP
#define IRD_TRAJECTORY_PLUGIN_TRAJPANELTYPES_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>        // core::EngineeringStatus（最近正式
                                                  //   判定词表——ui.md §6.5 投影行
                                                  //   verdict 字段同构；core 词表直用）
#include <sdurws/ird/trajectory/Commands.hpp>    // 命令 token 词表/零修订契约/
                                                  //   TrajectoryCommandOutcome（§15.11
                                                  //   ——同单元公共契约，T10 落位）
#include <sdurws/ird/trajectory/Playback.hpp>    // TrajectoryCurveSeries（曲线缝返回
                                                  //   类型——T10 数据面公共契约；纯
                                                  //   std 面〔double 向量〕值类型）

namespace sdurws::ird::trajectory {

// =====================================================================
// 动画帧呈现值（面板私有纯值 DTO——查表缝的出参面；WP-16-T12）。
// =====================================================================

/**
 * @brief 单帧动画呈现值（面板私有纯 std 面 DTO——std::vector<double>
 *        载体，零 rw 对象成员）。
 *
 * 为什么不用 PlaybackFrame（Playback.hpp 的帧值类型）：其 q/qd/qdd 成员
 * 是 rw::math::Q（虚析构＝框架库符号）——插件面板 TU 消费它会把框架库
 * 符号依赖带进冒烟口径（冒烟树无框架库可链——T04 gating 纪律"产品代
 * 码只消费 rw 的 header-only 面"的插件面承载）。本 DTO 由查表缝的组装
 * 侧（宿主装配层/开发 harness）从 TrajectoryPlaybackTable::sampleAt 帧
 * 值逐轴拷贝而来（翻译单点），面板只读数值呈现——单位/坐标系语义与
 * PlaybackFrame 完全一致（q/qd/qdd：转动 rad|移动 m 逐轴；t 自轨迹起
 * 点起算，s）。值语义纯结构；线程安全。
 */
struct TrajPlaybackSample {
    /// 帧时刻（s；＝查表入参原值——非样本重整时刻）。
    double tS = 0.0;
    /// 关节位置（插值查表产物；SI——rad|m 逐轴按关节类型；轴序＝链序）。
    std::vector<double> q;
    /// 关节速度（插值查表产物；SI——rad/s 或 m/s 逐轴）。
    std::vector<double> qd;
    /// 关节加速度（插值查表产物；SI——rad/s² 或 m/s² 逐轴）。
    std::vector<double> qdd;
    /// 是否命中精确样本（true＝位级样本值零插值漂移；false＝区间线性插值）。
    bool atSample = false;
    /// 帧所属段序号（0 基；nullopt＝查表未提供段归投影——不伪造段归）。
    std::optional<std::uint32_t> segmentIndex;
};

// =====================================================================
// 会话命令受理记录（工作流页呈现缓冲——零修订契约的逐条观测面）。
// =====================================================================

/**
 * @brief 单条会话命令受理记录（§15.11 handle 结果的会话留痕——工作流页
 *        "最近命令"只读清单的行素材）。
 *
 * producesRevision 逐条取自受理结果（零修订契约——kTrajectorySession-
 * Contract 的 producesRevision 恒 false；AT-04"动画/预览不产生项目修订"
 * 的机器可断言面：模型层测试对每条记录断言本位为 false）。
 * 值语义纯结构；线程安全。
 */
struct TrajCommandRecord {
    std::string commandToken;      ///< 命令 token（Commands.hpp 词表值）
    bool accepted = false;         ///< 是否受理（false＝用户可见的不受理
                                   ///<   ——拒绝原因见 rejectionToken）
    std::string rejectionToken;    ///< 拒绝 token（受理时空串——Commands.hpp
                                   ///<   TrajectoryCommandOutcome 语义透传）
    bool producesRevision = false; ///< 是否产生修订（受理时恒 false——零
                                   ///<   修订契约；未受理亦 false）
};

// =====================================================================
// 插件会话态（面板呈现事实的唯一载体——装配层注入与刷新）。
// =====================================================================

/**
 * @brief trajectory 插件会话态（dynamics 先例 DynModuleSessionState 同款
 *        收缩形态——只承载呈现所需事实，零权威语义）。
 *
 * 权威边界（PA-1 纪律）：本结构的全部字段都是**装配层注入的呈现事实
 * 投影**——就绪真值归域就绪校验、任务事实归 execution、当前性归
 * evidence；本插件不判定、不缓存权威结论（防第二真值）。插件唯一
 * "写"的字段是命令受理记录缓冲（会话级呈现史——零修订，不入任何
 * 持久层）。
 *
 * 线程约束：仅 UI 线程访问（装配层注入与面板读取同线程——卡 §3.4）。
 */
struct TrajModuleSessionState {
    /// 会话纪元（ui.md §6.2——装配层刷新时递增；面板据此丢弃迟到刷新）。
    std::uint64_t epoch = 0;
    /// 会话是否可写（只读模式——写类呈现入口降级；本域七命令全部零修订，
    /// 只读模式仅影响"发起规划"的提交出口可用性，由命令可用性缝统一
    /// 门控——插件本地零二次判定）。
    bool writable = true;
    /// 就绪校验结论透传（true＝输入完整；结论权威在域就绪校验——
    /// UX-10"未完成附缺项列表"的素材面）。
    bool inputComplete = false;
    /// 缺项文案键清单（UX-02/UX-10——键经宿主文案解析呈现；本插件
    /// 零文案值、零哈希/内部标识进用户文本）。
    std::vector<std::string> missingItemKeys;
    /// 是否存在在途任务（true＝"计算中"呈现素材之一——ui.md §6.3 触发
    /// 面；事实归 execution，装配层注入）。
    bool hasActiveTask = false;
    /// 最近正式判定（core 词表直用——默认 NotApplicable＝无判定，不伪造
    /// 可行性；UX-10"数据不足"等七态素材之一）。
    core::EngineeringStatus verdict = core::EngineeringStatus::NotApplicable;
    /// 最近命令受理记录（呈现缓冲——新记录追加于尾；超过容量上限时
    /// 从头丢弃最旧记录。容量 8 条是呈现缓冲截断，非业务阈值——
    /// 完整受理史归 execution 任务清单，本缓冲只服务工作流页清单）。
    std::vector<TrajCommandRecord> recentCommands;
};

// =====================================================================
// 服务缝聚合（面板数据源——装配层注入；空缝＝未装配，呈现如实降级）。
// =====================================================================

/**
 * @brief trajectory 插件服务缝聚合（dynamics 先例 DynPanelServices 同款
 *        形态——全部 std::function 缝，装配层在面板创建前注入）。
 *
 * 缝清单与空缝语义（逐缝注明降级呈现——绝不虚构数值，NFR-COR-03）：
 *   - playbackSource：动画播放查表缝（§14.5.1 play/playback-seek 行"UI
 *     线程（纯插值查表）"——查表面消费与帧值翻译在缝组装侧〔宿主装配
 *     层/开发 harness〕完成：入参时刻（s）→组装侧查表→逐轴拷贝为
 *     TrajPlaybackSample 纯值出参；面板只读呈现，零查表器符号消费——
 *     冒烟口径的框架库符号隔离，见 TrajPlaybackSample 注）。空缝→动
 *     画区呈现"轨迹数据未装配"。
 *   - curvesSource：曲线投影读取（§14.5.1 show-curve 行"读归档 payload
 *     投影"；投影在缝组装侧经曲线投影器现产，本缝只取现成序列值——
 *     TrajectoryCurveSeries 纯 std 面值类型）。空缝→曲线视图呈现"曲
 *     线数据未装配"空态。
 *   - sessionPoseSink：会话姿态驱动缝（§14.5.2"播放＝按时刻插值查表
 *     驱动会话姿态"的域外半区——纯值帧交宿主 View3D 桥组装应用〔Q
 *     对象构造归宿主侧〕；KIN-06 零修订语义经 Commands.hpp 四常量契
 *     约钉住，本插件不触碰姿态应用侧）。空缝→帧值只更新面板读数区
 *     （开发 harness 形态——真实 View3D 桥接线归宿主装配批，如实降级
 *     不虚构姿态效果）。
 *   - commandSubmit：命令提交出口（§14.5.1 七命令 token——真实注册
 *     权威＝宿主命令注册表（§15.11），本缝是插件侧唯一出口）。空缝→
 *     按钮点击呈现"命令出口未装配"（不静默丢弃——诚实反馈）。
 *   - commandAvailability：命令可用性查询（统一按钮门控——只读模式/
 *     未装配态的可用性判定权威在宿主；空缝→全部命令按可用呈现，
 *     点击时经提交缝/空缝语义如实反馈）。
 *   - textResolver：文案解析（titleKey→工程用语——宿主文案资源唯一
 *     出口 UX-02；空缝→按钮/标题呈现键名原文——dynamics 同纪律，
 *     开发 harness 的可见缺口，不是产品装配形态）。
 *
 * 值语义：可拷贝（缝闭包共享装配层捕获物——shared 语义由闭包自然
 * 承载）；仅 UI 线程调用。
 */
struct TrajPanelServices {
    /// 动画总时长缝（s——进度条映射域的宿主承接面；空＝未装配）。
    std::function<double()> durationSource;
    /// 动画播放查表缝（入参时刻 s→纯值帧出；空＝未装配——空态呈现）。
    std::function<TrajPlaybackSample(double tS)> playbackSource;
    /// 曲线投影读取缝（入参通道词表三值；空＝未装配——空态呈现）。
    std::function<std::vector<TrajectoryCurveSeries>(TrajectoryCurveChannel)>
        curvesSource;
    /// 会话姿态驱动缝（纯值帧出——宿主 View3D 桥组装应用；空＝帧读数
    ///   降级）。
    std::function<void(const TrajPlaybackSample&)> sessionPoseSink;
    /// 命令提交出口缝（空＝未装配——点击反馈"出口未装配"）。
    std::function<void(const std::string& commandToken)> commandSubmit;
    /// 命令可用性查询缝（空＝全部按可用呈现）。
    std::function<bool(const std::string& commandToken)> commandAvailability;
    /// 文案解析缝（空＝键名原文呈现——开发态可见缺口）。
    std::function<std::string(const std::string& titleKey)> textResolver;
};

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_PLUGIN_TRAJPANELTYPES_HPP
