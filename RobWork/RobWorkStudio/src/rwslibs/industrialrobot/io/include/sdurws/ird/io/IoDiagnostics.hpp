/**
 * @file   IoDiagnostics.hpp
 * @brief  io 诊断面——IoErrorCode 的稳定码面 token、IO-\* 稳定诊断码表
 *         （§9.12 建议值）向 diagnostics StableCodeRegistry 的注册，与
 *         比较型预算三要素的 IoError 构造助手。
 *
 * 设计依据：
 *   - units/io.md §3.1（公共头表 IoDiagnostics.hpp 行："IO-* 码表（建议
 *     值）、诊断构造助手（比较型预算三要素、逐行列定位、脱敏接入）"——
 *     本头落位前两项；逐行列定位助手随首个消费者（CSV 通道 IO-T03）落
 *     位，不预建无消费者能力）、§9.12（IoErrorCode 码汇总——建议值，随
 *     IO-T02 注册冻结）、§10.3（与 diagnostics：io→diagnostics 产出
 *     IO-\* CodeDescriptor；"码未注册→IO-FORMAT-INTERNAL 拒绝构造"）
 *   - units/diagnostics.md §8.6（io 路径与预算错误→诊断：SafePath 违规
 *     →安全/Error；BudgetGuard 超限→安全/Error、比较型：实际字节数/
 *     上限/字节单位；码值与 paramSchema 由 io 卡产出）、§4.5（注册协议：
 *     CodeDescriptor 字段约束）
 *   - 任务契约 tasks/foundation/IO-T02.json acceptance 3（比较型三要素
 *     诊断）＋acceptance 5（P-IO-6：IO-\* 码值以 §9.12 建议值向
 *     diagnostics StableCodeRegistry 注册并与 diagnostics 所有者收编
 *     确认——不私定码值）
 *
 * 背景说明（码值权威链——为什么 io 只"注册"不"收编"）：稳定码的注册
 * 表唯一权威＝diagnostics::StableCodeRegistry（PA-1/NFR-MNT-03）；IO-\*
 * 码值文本以 units/io.md §9.12 建议值**原样**产出 CodeDescriptor——码
 * 值、分类、严重级别均可追溯到卡面；"与 diagnostics 所有者收编确认"
 * （内置码表 §4.6 的 87 码清单位于 diagnostics 单元，扩编属其所有者）
 * 是治理侧动作，随本任务的 traceability 留痕提请——本实现不改动
 * diagnostics 单元任何文件（allowedFiles 红线），不私定任何码值。
 *
 * 注册形态：io 产出描述符清单（ioCodeDescriptors）＋注册函数
 * （registerIoCodeTable）；装配期（IoRuntime::create——§9.11"码表注册
 * 在此完成"）调用。Ok/Cancelled **不在注册表**（§9.12 首行："取消＝状
 * 态而非错误，不落诊断"UX-03——状态码不是诊断码，无诊断语义可注册）。
 *
 * 线程安全：ioCodeDescriptors 纯函数；registerIoCodeTable 只转发
 * registry->registerCode（注册期单线程约定随 diagnostics 装配语义）；
 * makeComparativeError 纯函数。确定性：清单序＝§9.12 表行序（确定性
 * 序，manifest 由注册表侧排序——同注册集同摘要）。
 */

#ifndef SDURWS_IRD_IO_IODIAGNOSTICS_HPP
#define SDURWS_IRD_IO_IODIAGNOSTICS_HPP

#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>   // CodeDescriptor/IDiagnosticRegistry——注册协议
#include <sdurws/ird/io/IoError.hpp>              // IoError/IoErrorCode——io 错误面

namespace sdurws::ird::io {

// =====================================================================
// 码面 token（IoErrorCode 枚举成员 ↔ §9.12 稳定 token）
// =====================================================================

/**
 * @brief 取 IoErrorCode 的稳定 token（§9.12 原文连字符串，如
 *        "IO-FORMAT-CSV-DIALECT"；Ok → "Ok"、Cancelled →
 *        "IO-CANCELLED"——§9.12 首行两值）。
 *
 * token 与枚举成员的一一对应关系由 IoError.hpp 枚举注释逐成员登记、
 * 本函数为实现侧唯一映射点（IoError.hpp 与本函数若失同步，注册表
 * 前缀校验与测试的 §9.12 全集比对会即刻暴露——两处清单受同一张卡面
 * 表约束）。返回值指向静态存储期字面量。
 *
 * 用途：批量违规明细的码面呈现（SafePath 批量预检 detail）、注册表
 * 描述符生成、诊断记录的 code 字段（工厂消费——后续任务）。
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同码同串）。
 */
std::string_view errorCodeToken(IoErrorCode code);

// =====================================================================
// IO-\* 稳定诊断码表（§9.12 建议值——P-IO-6 注册冻结）
// =====================================================================

/**
 * @brief 产出 IO-\* 稳定诊断码的 CodeDescriptor 全集（除 Ok/Cancelled
 *        外的全部 §9.12 码——状态码不注册，见文件头注）。
 *
 * 逐族登记口径（全部可追溯到卡面/diagnostics 卡）：
 *   - 码值文本＝§9.12 表连字符串原样（P-IO-6"不私定码值"；注意 §9.12
 *     安全族按连字符展开为 IO-SEC-PATH-SYMLINK 等全拼——正文行文的
 *     IO-SEC-SYMLINK 简写同指该码）。
 *   - ownerUnit＝"io"（diagnostics §4.5 前缀-所有权表 IO→io）。
 *   - 分类/严重＝diagnostics §8.6 映射义务：SafePath 违规→
 *     SecurityOrRedaction/Error；BudgetGuard 超限→SecurityOrRedaction/
 *     Error＋requiresComparison=true（比较型三要素）；CSV 逐行→
 *     InputInvalid/Error；格式族→FormatOrVersion；资源族按四分类
 *     （NotFound/Missing/Changed→ResourceMissing；AccessDenied/
 *     Readonly/LockConflict→PermissionOrLock）；包过程内容完整性码→
 *     FormatOrVersion、过程执行码→ExecutionFailed；Internal→
 *     Internal/Dev（userVisible/reportable/historical 强制 false——
 *     §4.5 注册期验证）。
 *   - paramSchema：预算族 ["actual","limit","unit"]（§8.6 三要素）；
 *     路径安全族 ["role","path"]（脱敏 display——IoError 错误轨道的
 *     同键约定）；CSV 族 ["row","column","snippet"]（snippet 仅开发
 *     级保留、呈现经脱敏——AT-02 口径）；其余按最小参数面声明；
 *     FormatInternal 声明 "[]"（无参数）。
 *   - retryable＝分类动作族机械映射（diagnostics DiagCodes 同款：安全
 *     族 Never——inspect-resource；格式/输入/资源/权限/执行族 UserRetry）。
 *
 * @return 描述符清单（顺序＝§9.12 表行序——确定性；每次调用返回新值）
 */
std::vector<diagnostics::CodeDescriptor> ioCodeDescriptors();

/**
 * @brief 将 IO-\* 码表全量注册进稳定码注册表（P-IO-6 的执行面）。
 *
 * 前置：registry 未 seal、不含同码冲突登记（重复注册→注册表抛
 * DiagnosticsError(DuplicateCode)——不捕获不吞，装配期 fail-fast）。
 * 后置：全部 IO-\* 码可被 registry->find 命中；manifest 反映全表。
 *
 * @param registry [in,out] 目标注册表（调用方持有——L5 装配的
 *                 diagnostics::StableCodeRegistry 实例；本函数不接管）
 * @throws diagnostics::DiagnosticsError 注册期验证失败（码值冲突/字段
 *         非法——描述符由本单元产出，出现即实现缺陷，fail-fast）
 */
void registerIoCodeTable(diagnostics::IDiagnosticRegistry& registry);

// =====================================================================
// 比较型三要素构造助手（§3.1"诊断构造助手（比较型预算三要素）"）
// =====================================================================

/**
 * @brief 以比较型三要素构造 IoError（预算超限诊断的标准承载——§4.5.2
 *        "比较型三要素：实际值/上限/单位"；diagnostics §8.6"实际字节
 *        数/上限/字节单位"）。
 *
 * params 固定三键（序即构造序）：actual（潜在累计值，十进制定点文
 * 本）、limit（生效限额）、unit（维度单位 token——bytes/chars/levels/
 * count/ratio）。数值文本化不经 locale（NFR-COR-02 确定性）；后续把
 * IoError 映射为 DiagnosticRecord 时，此三键与预算码的 paramSchema
 * ["actual","limit","unit"] 对齐（工厂占位一致性校验可过）。
 *
 * @param code   [in] 超限稳定码（IO-SEC-BUDGET- 全族或 IO-SEC-BOMB-RATIO）
 * @param actual [in] 触发时的实际值（饱和投影值——BudgetGuard 口径）
 * @param limit  [in] 生效限额（tighten/relax 后的 spec 值）
 * @param unit   [in] 单位 token（见 ioCodeDescriptors 预算族说明）
 * @param detail [in] 开发级细节（规则出处/定位上下文；不进用户文案）
 * @return IoError（code＋三要素 params＋detail）
 */
IoError makeComparativeError(IoErrorCode code, std::uint64_t actual, std::uint64_t limit,
                             std::string_view unit, std::string detail = {});

} // namespace sdurws::ird::io

#endif // SDURWS_IRD_IO_IODIAGNOSTICS_HPP
