/**
 * @file   IoError.hpp
 * @brief  io 单元错误契约——IoErrorCode 全表（稳定 token）＋IoError 值类型。
 *
 * 设计依据：
 *   - units/io.md §3.1（公共头表 IoError.hpp 行：IoErrorCode 稳定 token 枚举、
 *     IoError＝码＋上下文参数＋原始细节）、§9.0（"错误一律
 *     IoError{IoErrorCode, params, detail}"公共约定）、§9.12（IoErrorCode
 *     码汇总——建议值）、§1.4（对外接口一律非抛出：IoResult/输出参数＋
 *     错误码；异常仅允许在内部实现被捕获转换）
 *   - 需求 NFR-MNT-01（计算内核零 Qt——本头为纯标准库头）、NFR-MNT-02
 *     （单元边界——io 公共头仅暴露本头，私有实现细节不外泄）
 *   - 任务契约 tasks/foundation/IO-T01.json（≙WP-11-T02）acceptance 1
 *     （IoFwd/IoError 公共头就位）
 *
 * 背景说明（io 为什么用"错误码返回"而不是异常——§1.4 收窄约束）：io 的
 * 失败大头是环境错误（文件不存在/锁竞争/介质只读/编码不符），属于正常
 * 业务路径而非程序缺陷；对外接口一律非抛出（IoResult<T>/输出参数＋
 * 错误码），调用方（ui/workflow/execution）据此把失败转成用户可确认的
 * 诊断与状态，而不是让异常穿越跨单元边界。因此本头**没有异常类型**——
 * 与 diagnostics::DiagnosticsError/evidence::EvidenceError 的"设施异常轨"
 * 不同，io 的失败语义全部经值类型承载。
 *
 * 与稳定诊断码的分工（参照 diagnostics::Errors.hpp 同款口径，两个层面）：
 *   - 本表 IoErrorCode＝io 设施**错误面**：接口失败时经 IoError 返回给
 *     调用方的机器可判码，面向程序判定（isCancelled 分支、重试策略等）；
 *   - IO-\* 稳定诊断码＝**诊断内容**的码值，其注册权威＝diagnostics 的
 *     StableCodeRegistry（P-IO-6：§9.12 建议值随 IO-T02 向注册表收编时
 *     与 diagnostics 确认冻结）。跨单元上报时由调用方把 IoError 映射为
 *     DiagnosticRecord（diagnostics 工厂构造，io 不越权自建注册表——PA-1：
 *     单位/诊断文案权威归 core/diagnostics）。
 *
 * P-IO-6/冻结状态锚点（任务契约 knownPitfalls＝P-IO-3 关联面）：本头枚举
 * 成员与 token 为 units/io.md §9.12 **建议值**（卡面原文："随 IO-T02 注册
 * 冻结"）——本头按卡面原样承载建议值全集，使 IoResult 错误轨道自 IO-T01
 * 起即可编译；冻结裁决（WP-11 评审）如变更码值，走单元卡增量修订后同步
 * 本头，不在实现侧私改。
 *
 * 敏感性约束（§10.3/V30 两级脱敏的源头防线）：IoError::detail 与 params
 * 可携带**原始细节**（完整路径、单元格原文等）——它们只允许进入内部
 * 诊断构造链，经 diagnostics 脱敏设施后才能成为用户可见文案；导出/呈现
 * 路径必须使用脱敏后的摘要（exportSafeSummary 形态，§9.0 副作用注记）。
 *
 * 线程安全：本头全部实体为纯值类型（无共享可变状态），并发只读安全。
 * 确定性：枚举顺序＝§9.12 表行序（持久化契约面纪律：数值进入二进制
 * 契约，只允许表尾追加并走单元卡增量修订——diagnostics::DiagnosticsErrorCode
 * 同款）；同码同 token，跨进程一致（NFR-COR-02 的码面子集）。
 */

#ifndef SDURWS_IRD_IO_IOERROR_HPP
#define SDURWS_IRD_IO_IOERROR_HPP

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sdurws::ird::io {

// =====================================================================
// IoErrorCode——io 单元稳定错误码全表（units/io.md §9.12 建议值 57 值
// ＋IO-T04 表尾追加 1 值＝58 值；枚举顺序＝§9.12 表行序，表尾追加纪律
// 见 FormatJsonSyntax 成员注）。每个枚举成员注释末尾给出其稳定
// token（§9.12 原文连字符串面，如 "IO-FORMAT-CSV-DIALECT"）——token 是
// 跨进程/持久化产物中的码面标识，枚举成员名是 C++ 侧拼写（C++ 标识符
// 不允许连字符），两者一一对应，映射关系一经交付不得改义。
// =====================================================================

/**
 * @brief io 设施稳定错误码全表（§9.12 建议值全集，随 IO-T02 注册冻结）。
 *
 * 底型 std::uint16_t：§9.12 码表面向后续阶段扩展（阶段 B/C 的 Xacro、
 * 目录包等会追加码族），16 位预留充足增长空间且保持枚举体积恒定
 * （表尾追加纪律下不重排既有值——持久化契约面）。
 *
 * 失败归类（AGENTS.md 错误语义在 io 的落点，§4.2.5 四分类）：格式族
 * （IO-FORMAT-\*）与安全族（IO-SEC-\*）＝数据事实（文件内容不可信/越界），
 * 不归咎调用方也不归咎环境；资源族（IO-RES-\*）＝环境错误（不存在/
 * 权限/只读/锁竞争）；IO-PACK-\*＝包过程错误（导入导出事务中段失败，
 * 附清理语义——§7.6 状态图）；Cancelled＝状态而非错误（UX-03：调用方
 * 据此转 Canceled，**不落诊断**）；Ok＝无错误。
 */
enum class IoErrorCode : std::uint16_t {
    // ---- Ok/取消族（§9.12 第 1 行）----
    Ok,                    ///< "Ok"——无错误（IoResult::operator bool 为 true 的唯一码）
    Cancelled,             ///< "IO-CANCELLED"——协作式取消命中（§4.4 检查点；状态非错误，不落诊断——UX-03）

    // ---- 格式-CSV 族（§9.12 第 2 行）----
    FormatCsvDialect,      ///< "IO-FORMAT-CSV-DIALECT"——方言标识行缺失/语法非法（§5.1 封闭键集）
    FormatCsvEncoding,     ///< "IO-FORMAT-CSV-ENCODING"——编码不符（无 BOM 非 UTF-8 稳定拒绝，不猜转码——IO-D09）
    FormatCsvQuote,        ///< "IO-FORMAT-CSV-QUOTE"——引号/转义序列非法（§5.3 可逆转义编码还原失败）
    FormatCsvArity,        ///< "IO-FORMAT-CSV-ARITY"——行列数与表头不一致（§5.4 结构问题）
    FormatCsvDupCol,       ///< "IO-FORMAT-CSV-DUPCOL"——表头重复列名（§5.4）
    FormatCsvChar,         ///< "IO-FORMAT-CSV-CHAR"——非法字符（控制字符/孤立引号等，§5.7 错误矩阵）

    // ---- 格式-JSON 族（§9.12 第 3 行）----
    FormatJsonEncoding,    ///< "IO-FORMAT-JSON-ENCODING"——JSON 文档编码不符（§5.9.1 安全限制）
    FormatJsonDupKey,      ///< "IO-FORMAT-JSON-DUPKEY"——对象内重复键（受限 DOM 拒绝——§5.9.1）
    FormatJsonNumber,      ///< "IO-FORMAT-JSON-NUMBER"——数值字面量超出受限范围/语法非法（§5.9.1）
    FormatJsonVersionMissing, ///< "IO-FORMAT-JSON-VERSION-MISSING"——版本字段缺失（§5.9.2 schema 校验）
    FormatJsonVersionType,    ///< "IO-FORMAT-JSON-VERSION-TYPE"——版本字段类型非法（§5.9.2）
    FormatJsonVersionFuture,  ///< "IO-FORMAT-JSON-VERSION-FUTURE"——未来版本（写入方版本高于本软件支持——§5.9.2）
    FormatJsonVersionLegacy,  ///< "IO-FORMAT-JSON-VERSION-LEGACY"——废弃旧版本（§5.9.2）
    FormatJsonUnknown,     ///< "IO-FORMAT-JSON-UNKNOWN"——未知字段/结构（按 JsonProfile 判定——§5.9.2）
    FormatJsonRequired,    ///< "IO-FORMAT-JSON-REQUIRED"——必填字段缺失（§5.9.4 格式校验层）
    FormatJsonType,        ///< "IO-FORMAT-JSON-TYPE"——字段类型不符（§5.9.4）
    FormatJsonRange,       ///< "IO-FORMAT-JSON-RANGE"——字段取值超界（§5.9.4）

    // ---- 格式-包族（§9.12 第 4 行）----
    FormatPackZip,         ///< "IO-FORMAT-PACK-ZIP"——.rwpack 容器层损坏（zip 结构非法——§7.1）
    FormatPackEncrypted,   ///< "IO-FORMAT-PACK-ENCRYPTED"——加密包拒绝（§7.1：不承载加密语义）
    FormatPackEntry,       ///< "IO-FORMAT-PACK-ENTRY"——条目路径/结构不符 manifest（§7.4 导入威胁处置）
    FormatPackManifest,    ///< "IO-FORMAT-PACK-MANIFEST"——manifest 契约非法（rwpack.json 解析/摘要不符——§7.1）

    // ---- 格式-XML/网格族（§9.12 第 5 行）----
    FormatXmlCycle,        ///< "IO-FORMAT-XML-CYCLE"——URDF/Xacro include 循环（§6.2 展开边界）
    FormatMeshUnknown,     ///< "IO-FORMAT-MESH-UNKNOWN"——网格格式无法识别（§6.3 STL/OBJ/DAE 之外）

    // ---- 安全-路径族（§9.12 第 6 行）----
    SecPathEscape,         ///< "IO-SEC-PATH-ESCAPE"——路径穿越（解析结果逃逸角色根——SP-2，§4.3）
    SecPathSymlink,        ///< "IO-SEC-PATH-SYMLINK"——符号链接/junction/reparse point 越界（SP-6，§4.2.3）
    SecPathReserved,       ///< "IO-SEC-PATH-RESERVED"——Windows 保留名/保留字符（§4.2.2）
    SecPathTooLong,        ///< "IO-SEC-PATH-TOO-LONG"——路径超长（§4.3.1 规则总表）

    // ---- 安全-预算族（§9.12 第 7 行，比较型预算三要素诊断的码面——§5.8）----
    SecBudgetFile,         ///< "IO-SEC-BUDGET-FILE"——单文件大小超限（§4.5.1 预算维度）
    SecBudgetTotal,        ///< "IO-SEC-BUDGET-TOTAL"——解压总量超限（§4.5.1）
    SecBudgetCount,        ///< "IO-SEC-BUDGET-COUNT"——文件条数超限（§4.5.1）
    SecBudgetDepth,        ///< "IO-SEC-BUDGET-DEPTH"——目录/嵌套深度超限（§4.5.1）
    SecBudgetExpand,       ///< "IO-SEC-BUDGET-EXPAND"——解包累计展开量超限（§4.5.2 语义细则）
    SecBudgetRows,         ///< "IO-SEC-BUDGET-ROWS"——CSV 行数超限（§4.5.1）
    SecBudgetField,        ///< "IO-SEC-BUDGET-FIELD"——单元格长度/字段数超限（§4.5.1）
    SecBudgetJson,         ///< "IO-SEC-BUDGET-JSON"——JSON 文档量超限（§4.5.1）
    SecBudgetJsonDepth,    ///< "IO-SEC-BUDGET-JSON-DEPTH"——JSON 嵌套深度超限（§4.5.1）
    SecBudgetJsonString,   ///< "IO-SEC-BUDGET-JSON-STRING"——JSON 字符串长度超限（§4.5.1）
    SecBudgetMesh,         ///< "IO-SEC-BUDGET-MESH"——网格面数/顶点数超限（§4.5.1）
    SecBudgetInclude,      ///< "IO-SEC-BUDGET-INCLUDE"——URDF/Xacro include 数量超限（§4.5.1）
    SecBudgetRefDepth,     ///< "IO-SEC-BUDGET-REFDEPTH"——资源引用深度超限（§4.5.1）
    SecBudgetTemp,         ///< "IO-SEC-BUDGET-TEMP"——临时区占用超限（§4.5.1/§7.5）

    // ---- 安全-压缩炸弹族（§9.12 第 8 行）----
    SecBombRatio,          ///< "IO-SEC-BOMB-RATIO"——压缩比异常（zip 炸弹判据——§7.4 威胁处置矩阵）

    // ---- 资源族（§9.12 第 9 行，环境错误四分类——§4.2.5）----
    ResNotFound,           ///< "IO-RES-NOT-FOUND"——路径不存在（§4.2.5 分类一）
    ResAccessDenied,       ///< "IO-RES-ACCESS-DENIED"——权限不足（§4.2.5 分类二）
    ResReadonly,           ///< "IO-RES-READONLY"——介质只读（§4.2.5 分类三）
    ResLockConflict,       ///< "IO-RES-LOCK-CONFLICT"——文件锁竞争/sharing violation（§4.2.5 分类四）
    ResMissing,            ///< "IO-RES-MISSING"——固化引用的外部源缺失（Missing≠不可行——§6.6/§10.8）
    ResChanged,            ///< "IO-RES-CHANGED"——外部源内容与引用摘要不符（digest 判据——IO-D10）

    // ---- 包过程族（§9.12 第 10 行，导入导出事务中段失败——§7.6 状态图）----
    PackDuplicateEntry,    ///< "IO-PACK-DUPLICATE-ENTRY"——包内条目重复（§7.3 导入协议步失败）
    PackHashMismatch,      ///< "IO-PACK-HASH-MISMATCH"——解包后逐字节哈希校验不符（PM-05）
    PackRefIncomplete,     ///< "IO-PACK-REF-INCOMPLETE"——资源依赖树不完整（§6.5/§7.3）
    PackTargetExists,      ///< "IO-PACK-TARGET-EXISTS"——发布目标已存在（§7.3⑧ rename 语义）
    PackDiskFull,          ///< "IO-PACK-DISK-FULL"——磁盘空间不足（§7.3）
    PackCleanupFailed,     ///< "IO-PACK-CLEANUP-FAILED"——失败路径清理未完成（残留不可误识别——§7.6）

    // ---- 内部族（§9.12 第 11 行）----
    FormatInternal,        ///< "IO-FORMAT-INTERNAL"——防御性内部错误（开发级：触及即报缺陷，不进入用户文案）

    // ---- 表尾追加（IO-T04 落位——DTB §5.4 单元卡增量修订）----
    // 卡面缺口补登：§5.9.1 安全限制表与 §5.9.4 只给"语法层拒绝"语义，
    // 未给非数值类语法违例（截断文档/缺失分隔符/非法记号/悬空代理对等）
    // 的码面——JSON 族其余码各有所辖（NUMBER 仅数值字面量）。按本头
    // "只允许表尾追加并走单元卡增量修订"纪律补登（§9.12 JSON 族行同步、
    // §15.5 变更记录 v0.7、ioCodeDescriptors 同步注册）。
    FormatJsonSyntax       ///< "IO-FORMAT-JSON-SYNTAX"——JSON 语法层违例（非数值字面量的
};                         ///< 结构/记号非法；数值字面量语法非法仍归 IO-FORMAT-JSON-NUMBER）

/**
 * @brief io 错误值类型：稳定码＋上下文参数＋原始细节（§9.0 公共约定
 *        "错误一律 IoError{IoErrorCode, params, detail}"的类型化承载）。
 *
 * 生命周期/所有权：纯值类型，随 IoResult 或输出参数按值返回，调用方所有。
 * 空构造＝Ok（便于聚合容器默认初始化——"失败清单"中的占位项语义为无错），
 * 但业务代码应总是经接口返回的 IoResult 携带错误，不手工构造 Ok 值。
 *
 * 确定性（NFR-COR-01/02）：同输入同错误同 params 序——params 用保序
 * vector 而非关联容器，遍历序＝构造序，不经哈希/树序重排。
 */
struct IoError {
    /// 稳定错误码（Ok＝无错误）。
    IoErrorCode code = IoErrorCode::Ok;

    /**
     * 上下文参数（键值对，构造序保序——确定性要求）。
     * 典型键：行号/列号（逐行定位，§5.6）、预算三要素（limit/actual/
     * unit——§5.8 比较型诊断）、资源键、方言键。值为字符串：数值在构造处
     * 按稳定格式化规则（定点、不经 locale）转为文本，避免同一错误在不同
     * 环境产出不同参数文本。敏感值（完整路径/单元格原文）入此表前由实现
     * 自评——最终用户可见文案必须经 diagnostics 脱敏（§10.3）。
     */
    std::vector<std::pair<std::string, std::string>> params;

    /// 原始细节（底层错误文本/原文片段）。仅供内部诊断链与日志；不得未经
    /// 脱敏直接呈现（IoError.hpp 文件头"敏感性约束"）。
    std::string detail;
};

} // namespace sdurws::ird::io

#endif // SDURWS_IRD_IO_IOERROR_HPP
