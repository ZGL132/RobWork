/**
 * @file   PolicySummaryCard.hpp
 * @brief  工程策略摘要只读卡的呈现契约（UI-T07 阶段 A）——§6.7 右栏
 *         "诊断与设置区"内嵌策略摘要卡的数据行模型、分组词表与文案契约。
 *
 * 设计依据：
 *   - 需求 UX-08（统一工程策略入口；显示开关与计算开关分组异名）、ARC-05
 *     （策略单一权威——ui 不持有判定权与计算开关权威，权威归 policy/
 *     EngineeringPolicySet）；POL-ID-3（显示设置不存在于策略字段）；
 *   - units/ui.md §6.7（策略摘要只读卡：数据＝EngineeringPolicySet 公开
 *     字段投影，显示值经单位换算投影不改变策略内容身份；"修改策略…"跳转
 *     编辑适配器→①命令端口；编辑表单归阶段 B）、§4.2 右栏行（策略"修改"
 *     跳转编辑适配器→①命令端口；不得就地写权威对象）、§4.6（会话显示
 *     开关与策略严格分离；显示单位偏好默认 m/rad——KIN-12 显示投影）、
 *     §10.6（policy.md §10.6 交接行：工程策略摘要数据＝只读投影，显示
 *     单位经 core 换算投影不改身份）、§16.7 v0.9（本头登记行）；
 *   - 任务契约 tasks/foundation/UI-T07.json acceptance 1~3（摘要只读＋
 *     分组异名＋影响范围说明；编辑经①命令端口、表单归阶段 B；O-31 处置
 *     ——C-10 经 ui::IPolicySummarySource 自有端口承载、产品面零对端
 *     include，NoCrossUnitInclude_O31_UI_BUILD 守卫常驻）；
 *   - 同构先例：View3DContract.hpp（UI-T05——契约文案单点登记于公共头，
 *     模型层测试与 GUI 呈现面同源互证）。
 *
 * 背景说明（为什么卡的文案与分组要在公共头钉死）：策略摘要卡是 ui 对
 * UX-08/POL-ID-3 的承接点，三条语义必须可被验收与 review 直接对照——
 *   1. **分组异名**：碰撞"计算开关"（策略字段，随内容身份参与计算）与
 *      "显示碰撞几何/高亮"（会话显示设置，不存在于策略字段）必须呈现为
 *      两个不同名分组，并各自说明影响范围（POL-ID-3）。分组标题在本头
 *      单点登记，模型测试钉住"两名不混淆"，GUI 面板按同一函数渲染；
 *   2. **显式不适用**：对端 optional 空字段是语义内容（P-POL-2/O-10：
 *      不发明数值）——呈现层必须显示「显式不适用」而非补默认值；文案
 *      在本头单点登记；
 *   3. **编辑路径**：ui 不持有判定权与计算开关权威，修改策略一律经编辑
 *      适配器→①命令端口提交生成新修订（ARC-05/SA-03）；编辑表单归阶段
 *      B（单元卡 §13 UI-T07 行明示）——阶段 A 的跳转入口以"路径说明＋
 *      延期提示"呈现，不在 ui 侧虚构编辑能力（§11.4 不虚构业务能力）。
 *
 * O-31 处置（acceptance 3）：本头及卡的消费面只见 ui 自有类型
 * （PolicySummaryProjection/IPolicySummarySource）与 core 表内边类型
 * （UnitToken——显示单位换算经 core Units，ui→core 为 ARCH §3.5 表内
 * 登记边）；对 policy 零 include 零链接，C-10 经 L5 装配期适配
 * （IPolicySummarySource 适配 policy::IPolicyProvider）。
 *
 * 线程模型：纯函数＋编译期固定数据，无共享可变状态——任意线程可调用
 * （实际只在 UI 线程的装配/刷新路径消费——§3.4 M-1）。
 */

#ifndef SDURWS_IRD_UI_POLICYSUMMARYCARD_HPP
#define SDURWS_IRD_UI_POLICYSUMMARYCARD_HPP

#include <string>
#include <vector>

#include <sdurws/ird/core/Units.hpp>      // core::UnitToken（显示单位换算——表内登记边）
#include <sdurws/ird/ui/UiProjections.hpp>  // PolicySummaryProjection/PolicyThresholdProjection（C-10 值投影）

namespace sdurws::ird {
namespace ui {

// =====================================================================
// 分组词表（POL-ID-3 分组异名的"名"——两个分组标题单点登记）
// =====================================================================

/**
 * @brief 策略摘要卡的分组标识（§6.7/POL-ID-3：显示开关与计算开关分组异名）。
 *
 * 两分组的存在意义不同：PolicyAuthority 组呈现**策略字段**（随内容身份
 * 参与计算——只读投影，修改经命令端口）；SessionDisplay 组呈现**会话显示
 * 设置**的边界说明（不存在于策略字段——POL-ID-3，切换显示不触发重算、
 * 不产生修订，KIN-12 同案）。枚举一经交付只允许表尾追加并升单元卡修订
 * （分组标题文案与行序被模型测试钉住，重排/改名会破坏验收对照——禁止）。
 */
enum class PolicySummaryGroup : std::uint8_t {
    /// 工程策略（计算权威）组——策略字段只读投影（计算开关/阈值/计数）。
    PolicyAuthority,
    /// 会话显示设置组——不属策略字段的显示面说明（POL-ID-3 分组异名的
    /// "另一名"；阶段 A 为边界说明呈现，显示控件随三维视图交互交付）。
    SessionDisplay,
};

/**
 * @brief 分组标题（§6.7 摘要卡分组的冻结呈现文案——单点登记）。
 *
 * 标题即"异名"的字面：两名都点明各自的影响范围归属（"计算权威"对
 * "不属策略字段"），模型测试钉住两标题互异且非空。
 *
 * @param group [in] 分组标识（全两值皆有登记标题——未知值为调用方错误，
 *              契约约定只传合法枚举值）
 * @return 分组标题（UTF-8 中文；静态存储期——调用方只读引用）
 */
const char* policySummaryGroupTitle(PolicySummaryGroup group) noexcept;

// =====================================================================
// 显示单位选项（KIN-12 显示投影的卡面参数）
// =====================================================================

/**
 * @brief 摘要卡的显示单位选项（安全间距/行程上限的显示换算目标）。
 *
 * 语义锚点：§4.6 用户级设置"显示单位偏好（KIN-12 显示投影），默认 m／rad
 * （独立于求解配置 KIN-13）"。偏好值的用户级设置承载随用户设置任务落地；
 * 阶段 A 的卡面消费 §4.6 出厂默认（siDefaults()）。判定阈值（近限位比/
 * 条件数）为无量纲——恒按无量纲呈现，不经本选项（无换算语义）。
 *
 * 约束：length 须为 Length 量纲 token、angle 须为 Angle 量纲 token
 * （违约＝调用方错误——core::convert 量纲核对 fail-fast 抛 CoreError，
 * 不静默出错误数值）。
 */
struct PolicySummaryDisplayUnits {
    /// 安全间距显示单位（Length 量纲——§4.6 默认 m）。
    core::UnitToken length;
    /// 行程上限显示单位（Angle 量纲——§4.6 默认 rad）。
    core::UnitToken angle;

    /**
     * @brief §4.6 出厂默认（m／rad——KIN-12 显示投影的缺省显示制式）。
     *
     * @return 默认选项（core 单位注册表的 m/rad token；单位表为进程级
     *         静态注册——find 失败不可达，属构建期事实）
     */
    static PolicySummaryDisplayUnits siDefaults();
};

// =====================================================================
// 行模型（卡的呈现数据——纯值，模型层可测）
// =====================================================================

/**
 * @brief 摘要卡一行（稳定键＋组标识＋行标签＋行值文本）。
 *
 * 行是卡的最小呈现单元：GUI 面板把每行渲染为一个只读文本标签
 * （"label：value"，控件 objectName＝"ird_policy_row_"＋key），模型层
 * 测试直接断言行内容。valueText 已含单位换算结果与「显式不适用」文案
 * ——模型层产出即最终呈现文本（GUI 不二次加工，UX-02 呈现值单一出口）。
 *
 * key 是行级稳定标识（小写连字符词法——与 §6.3 token 同风格；可用集：
 * policy/collision/safety-clearance/pairs/travel-limit/near-limit-ratio/
 * condition-number/edit-path/display-separation/display-controls）。
 * 装配函数对同一行在不同投影形态下恒给出同一 key（未装载占位形态的
 * 行集不同，但已装载形态行集恒定），GUI 定位与测试断言都以 key 为锚，
 * 不依赖行序/文本漂移。一经交付只允许表尾追加并升单元卡修订。
 */
struct PolicySummaryRow {
    /// 行稳定键（见结构体注释——GUI objectName 与测试断言的锚点）。
    std::string key;
    /// 所属分组（POL-ID-3 分组异名的行级归属）。
    PolicySummaryGroup group = PolicySummaryGroup::PolicyAuthority;
    /// 行标签（如"碰撞检查""安全间距"）。
    std::string label;
    /// 行值文本（已换算显示值/「显式不适用」/说明文案——见各装配规则）。
    std::string value;
};

// =====================================================================
// 冻结文案（卡的契约文案单点——模型测试与 GUI 同源消费）
// =====================================================================

/// 「显式不适用」呈现文案（对端 optional 空字段的唯一合法呈现——P-POL-2
/// "不发明数值"；任何数值补默认都属违例）。
inline constexpr const char* kPolicyNotApplicableText = "显式不适用";

/// 未装载占位行值（available==false——不虚构数值，§6.7 摘要只读面语义）。
inline constexpr const char* kPolicyNotLoadedText = "未装载";

/// 编辑路径说明（acc2：修改策略经①命令端口提交生成新修订——ARC-05/
/// SA-03 单一权威与不可变历史的界面表达；阶段 A 的入口以本说明＋延期
/// 提示呈现，编辑表单归阶段 B——单元卡 §13 UI-T07 行明示）。
inline constexpr const char* kPolicyEditPathNote =
    "修改策略经命令端口提交生成新修订（权威归策略，ui 只读）";

/// 编辑入口的阶段延期提示（点击"修改策略…"的反馈文案——§4.1 占位说明
/// 口径"该入口将在后续版本提供"，不虚构业务能力；表单随阶段 B 交付）。
inline constexpr const char* kPolicyEditDeferredNotice =
    "该入口将在后续版本提供（编辑表单随阶段 B 交付）";

/// 显示设置边界说明（POL-ID-3 的卡面文案：显示开关是会话显示设置，不
/// 存在于策略字段——对计算与策略内容身份零影响；KIN-12"显示单位只是
/// 投影"同案）。
inline constexpr const char* kPolicyDisplaySeparationNote =
    "显示碰撞几何/高亮等是会话显示设置，不存在于工程策略字段（POL-ID-3）："
    "仅影响显示呈现，不改变计算与策略内容身份";

/// 显示控件阶段说明（阶段 A 无可交互显示开关——三维视图交互归阶段 B，
/// 不虚构业务能力；与 View3DContract 的占位口径同源）。
inline constexpr const char* kPolicyDisplayControlsDeferredNote =
    "显示控件随三维视图交互在后续版本提供";

// =====================================================================
// 阈值显示投影（单位换算经 core Units——不改策略内容身份）
// =====================================================================

/**
 * @brief 单个阈值字段的显示投影结果（换算值＋显示单位符号）。
 *
 * present==false 时三字段均为空态（present=false、值 0、单位空串）——
 * 呈现层据此取 kPolicyNotApplicableText，**不得**读 displayValue。
 */
struct PolicyThresholdDisplay {
    /// 阈值是否显式提供（直承 PolicyThresholdProjection.present）。
    bool present = false;
    /// 显示换算值（present==true 时有效；呈现精度 6 位有效数字——呈现层
    /// 投影非工程比较值，SI 真值仍是唯一权威）。
    double displayValue = 0.0;
    /// 显示单位符号（core 单位注册表 token 原文：mm/deg/1……；无量纲域
    /// 为 "1"——呈现层对 "1" 只显数值不显单位符号）。
    std::string displayUnitSymbol;
};

/**
 * @brief 把阈值投影换算为显示值（显示值经单位换算投影——§6.7/policy.md
 *        §10.6"显示单位经 core 换算投影，不改身份"）。
 *
 * 换算经 core::convert（SI→显示单位一次乘除，注册表因子精确）——与
 * KIN-12"显示单位只是投影，不改变 SI 真值"同案：本函数不产生任何策略
 * 身份输入（内容身份在 policy 侧按 SI 语义闭包计算，显示制式不参与）。
 *
 * @param threshold   [in] 阈值投影（present==false 直接返回空态——不换算
 *                    不发明数值）
 * @param siUnit      [in] 该字段域的 SI 单位 token（安全间距 m／行程上限
 *                    rad／判定阈值 "1"——policy.md §4.4"单位由字段位置
 *                    固定"；须与 displayUnit 同量纲）
 * @param displayUnit [in] 显示目标单位 token（须与 siUnit 同量纲——量纲
 *                    不匹配属调用方错误，core::convert fail-fast）
 * @return 显示投影（present==true 时 displayValue/displayUnitSymbol 有效）
 *
 * @throws core::CoreError siUnit/displayUnit 未注册或量纲不匹配（调用方
 *         错误——fail-fast，不用错误显示值继续渲染）
 */
PolicyThresholdDisplay formatPolicyThreshold(const PolicyThresholdProjection& threshold,
                                             core::UnitToken siUnit,
                                             core::UnitToken displayUnit);

// =====================================================================
// 行装配（§6.7 摘要卡呈现数据的唯一权威组装——模型与 GUI 同源）
// =====================================================================

/**
 * @brief 组装摘要卡全部行（§6.7 策略摘要只读卡的呈现数据权威）。
 *
 * 装配规则（冻结——测试逐行断言的依据）：
 *   - 工程策略（计算权威）组：
 *       · available==false：仅一行占位（"工程策略"→kPolicyNotLoadedText，
 *         不虚构数值），随后的会话显示设置组照常呈现（显示设置边界与
 *         策略装载无关）；
 *       · available==true：碰撞检查（启用含启用域 token 列表／停用）、
 *         安全间距（换算显示值或「显式不适用」）、碰撞对规则（"必检 n
 *         对·过滤 m 对"）、行程上限（换算显示值或「显式不适用」）、
 *         近限位比、条件数警告（判定阈值族——换算显示值或「显式不适用」）、
 *         修改策略路径说明（kPolicyEditPathNote——acc2 的卡面承载）。
 *   - 会话显示设置组（恒呈现）：POL-ID-3 边界说明＋显示控件阶段说明
 *     （两行——分组异名的"另一名"内容面）。
 *   - 行序＝上列装配序（稳定呈现——NFR-COR-02；测试钉序）。
 *
 * 为什么是纯函数而不是卡控件私有逻辑：分组异名/不适用文案/单位换算是
 * UX-08/POL-ID-3 的验收点，纯函数使模型层（无 GUI）即可逐行断言，GUI
 * 面板只做渲染不持语义（View3DContract/manifest 同案——单一权威）。
 *
 * @param summary [in] 策略摘要投影（IPolicySummarySource 返回值——
 *                C-10 端口承载，L5 适配 policy::IPolicyProvider）
 * @param units   [in] 显示单位选项（安全间距/行程上限的显示换算目标；
 *                量纲违约经 formatPolicyThreshold fail-fast）
 * @return 全部行（组标识随行——GUI 按分组归并渲染；纯函数，同输入同输出）
 */
std::vector<PolicySummaryRow>
policySummaryRows(const PolicySummaryProjection& summary,
                  const PolicySummaryDisplayUnits& units);

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_POLICYSUMMARYCARD_HPP
