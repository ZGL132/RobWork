/**
 * @file   ZipChannel.hpp
 * @brief  ZIP 通道——.rwpack 传输封装（§7.1）的容器层读取：条目枚举、
 *         逐字节还原读取（zip64/STORED/DEFLATE、加密拒绝、预算挂钩）与
 *         manifest 条目哈希校验（IO-PACK-HASH-MISMATCH 的前置能力）。
 *
 * 设计依据：
 *   - units/io.md §7.1（包格式契约：ZIP 容器、zip64 必开、条目名 UTF-8
 *     正斜杠、DEFLATE 允许、加密条目拒绝 IO-FORMAT-PACK-ENCRYPTED、
 *     压缩方法仅 STORED/DEFLATE、manifest 每条目 SHA-256 经 core
 *     ContentDigester、totalDigest＝对 manifest canonical 字节的摘要）、
 *     §12 IO-T04 行（"ZIP 通道：解包逐字节还原＋哈希（V14 前置）"）、
 *     §9.12（IO-FORMAT-PACK- 与 IO-PACK- 码族）、§4.5.2（包通道预算：
 *     条目声明大小与实际展开双重计数、以较大者入账）、§9.13（协作取消
 *     ——每块检查点）
 *   - 需求 PM-05（.rwpack 导入导出与完整性）、NFR-SEC-02（解包防护）、
 *     SA-12/NFR-MNT-03（SHA-256 唯一摘要算法）、NFR-DEP-03/NFR-SEC-05
 *     （P-IO-3 冻结：ZIP＝libzip 1.11.4，vcpkg x64-windows 经典模式——
 *     governance-log §1.9／io.md §15.3，2026-09-17 所有者裁决）
 *   - 任务契约 tasks/foundation/IO-T04.json（≙WP-11-T05）acceptance 4
 *     （解包逐字节还原＋manifest 条目哈希校验可用）＋acceptance 5
 *     （P-IO-3 处置——选型已冻结，本头对应实现的链接登记随本任务）
 *
 * 背景说明（ZIP 通道与包导入协议的边界——为什么本头不是导入器）：
 *   §7.3 的九步导入协议（临时区会话/威胁矩阵/逐步取消/发布语义）归
 *   IPackageImporter（IO-T06）；本头只承载**容器层能力**：打开归档、
 *   枚举条目、按名读取条目原始字节（逐字节还原——不解释内容）、按
 *   manifest 清单校验条目哈希。这样切分的原因：①容器层可独立验证（V14
 *   哈希校验是导入协议步骤⑤的构件）；②九步协议需要临时区/发布等尚属
 *   IO-T06 的设施，本头不预建其依赖。条目路径安全规则（SP-2/SP-4/
 *   SP-8/SP-9——穿越/保留名/绝对路径等）同样归导入器（io.md §7.1 条目
 *   约束行"条目约束：SP-*"）——本通道只把字节读进内存，不落盘，无
 *   路径逃逸面；导入器展开临时区时再行路径裁决。
 *
 * 第三方依赖（P-IO-3 冻结口径）：ZIP 编解码经 vcpkg libzip 1.11.4（zip64
 * 原生 64 位 API；ZIP_EM_* 加密方法枚举——§7.1 加密拒绝语义只需检测）。
 * 本头不暴露 libzip 类型（实现细节封装在 ZipChannel.cpp——公共面零第
 * 三方类型，消费方不传染链接依赖语义）。
 *
 * 线程约束：IZipChannel 会话型单线程（一个实例绑定一个包文件——§9.5
 * 会话型同款模型）；同一包文件可开多个会话并行读（libzip 句柄独立）。
 * ZipEntryInfo/ZipManifestEntry/ZipEntryVerifyReport 为纯值类型。
 *
 * 确定性（NFR-COR-01/02）：同包同条目同字节（libzip 解压确定性）；哈希
 * 校验结论同包同判（SHA-256 纯字节变换）；报告按调用方清单序排列。
 */

#ifndef SDURWS_IRD_IO_ZIPCHANNEL_HPP
#define SDURWS_IRD_IO_ZIPCHANNEL_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>   // core::Digest256——条目哈希（SHA-256 唯一摘要算法）
#include <sdurws/ird/io/Budget.hpp>     // IBudgetGuard/BudgetScopeId——包通道预算挂钩（§4.5.2）
#include <sdurws/ird/io/IoFwd.hpp>      // IoString/IoResult/IoCancelToken
#include <sdurws/ird/io/IoError.hpp>    // IoError/IoErrorCode——错误轨道

namespace sdurws::ird::io {

// =====================================================================
// 条目信息与 manifest 清单（§7.1 manifest.json 的运行时形态）
// =====================================================================

/**
 * @brief 归档条目信息（listEntries 产出——容器层的客观事实，不含任何
 *        manifest 语义）。
 *
 * name 语义：条目原样名（libzip 层 UTF-8、正斜杠分隔——§7.1 包条目名
 * 约定；本通道不改写不归一化——名字合法性归导入器裁决，容器层只呈现）。
 * 目录条目（名尾 '/'）原样列出（§7.1"无目录条目（仅文件）"的**导入
 * 违例检测**归导入器——容器层不预设包内约定）。
 */
struct ZipEntryInfo {
    IoString name;                    ///< 条目名（UTF-8，正斜杠；容器层原样）
    std::uint64_t index = 0;          ///< 归档内条目序号（0 起——libzip 索引口径）
    std::uint64_t uncompressedSize = 0;   ///< 声明未压缩大小（字节——zip 头声明，可能说谎）
    std::uint64_t compressedSize = 0;     ///< 压缩后大小（字节）
    std::uint32_t crc32 = 0;          ///< zip CRC-32（容器层完整性；与 SHA-256 内容身份互不替代）
    bool encrypted = false;           ///< 加密标记（中央目录 encryption_method ≠ NONE）
    std::uint16_t compressionMethod = 0;  ///< 压缩方法码（0=STORED、8=DEFLATE；其余＝本通道拒绝）
};

/**
 * @brief manifest 条目校验清单项（§7.1 manifest.json entries 行的调用
 *        方形态：{path, size, sha256}；sha256 以原始 32 字节传入——调用
 *        方自 manifest 文本解析 hex 后装配）。
 */
struct ZipManifestEntry {
    IoString path;                    ///< 条目名（与归档内 name 精确比对——字节级）
    std::uint64_t size = 0;           ///< 声明未压缩大小（字节；与实际不符即校验失败）
    core::Digest256 sha256{};         ///< 声明内容摘要（对**未压缩原文**计算——§7.1 哈希行）
};

/**
 * @brief 单条 manifest 条目校验结论（verifyManifestEntries 产出；顺序＝
 *        调用方清单序——确定性报告面）。
 *
 * error.code == Ok 表示该条目通过（存在＋大小一致＋哈希一致）；其余码
 * 面语义见 verifyManifestEntries 注。params 携带定位与比较要素（entry/
 * expected/actual——与 IO-PACK-HASH-MISMATCH 的 paramSchema 对齐）。
 */
struct ZipEntryVerifyReport {
    IoString path;      ///< 校验的条目名（＝ZipManifestEntry::path 回显）
    IoError error;      ///< Ok＝通过；失败＝定位＋比较要素（码面见接口注）
};

// =====================================================================
// ZIP 通道会话（容器层读取——§7.1 传输封装）
// =====================================================================

/**
 * @brief ZIP 通道会话接口（一个实例绑定一个包文件；会话型单线程）。
 *
 * 行为契约（§12 IO-T04 行"解包逐字节还原＋哈希（V14 前置）"）：
 *   - 前置：包文件存在可读（P-1/P-7 角色由调用方经 SafePath 保证——本
 *     接口只做容器层检查）；归档可被 libzip 打开（否则 IO-FORMAT-PACK-ZIP）。
 *   - 后置：readEntryBytes 成功＝返回条目**未压缩原文**逐字节（zip64/
 *     STORED/DEFLATE）；verifyManifestEntries 成功＝每条目一报（条目级
 *     失败不中断整表——V14"定位条目"的报告形态）。
 *   - 错误类型：IO-FORMAT-PACK-ZIP（容器层损坏/不支持的压缩方法）、
 *     IO-FORMAT-PACK-ENCRYPTED（加密条目拒绝——§7.1）、IO-PACK-HASH-
 *     MISMATCH（条目哈希/大小不符）、IO-PACK-REF-INCOMPLETE（清单引用
 *     归档中不存在的条目——V29 语义）、IO-SEC-BUDGET-*（预算）、
 *     IO-CANCELLED（状态）、IO-RES-*（文件系统四分类）。
 *   - 线程：会话单线程；多会话并行（各自 libzip 句柄）。
 *   - 取消：读取每块检查点（§9.13 总则——命中即返回 IO-CANCELLED）。
 *   - 副作用：只读文件系统；无临时区（展开归 IO-T06 导入会话）。
 */
class IZipChannel {
public:
    virtual ~IZipChannel() = default;

    /**
     * @brief 枚举全部条目（容器层客观事实——含目录条目/加密标记/压缩
     *        方法，供调用方（导入器）做条目约束裁决）。
     *
     * @return 成功＝条目清单（归档序＝libzip 索引序——确定性）；失败＝
     *         IO-FORMAT-PACK-ZIP（中央目录不可读）
     */
    virtual IoResult<std::vector<ZipEntryInfo>> listEntries() = 0;

    /**
     * @brief 按名读取条目未压缩原文（逐字节还原——不解释不加工）。
     *
     * 条目检查序（先cheap后重——失败快路径）：
     *   ① 按名定位（字节级精确——ZIP_FL_ENC_UTF_8；同名列取首个命中，
     *     重复条目的裁决归导入器——V13 前置语义，容器层不代行）；
     *     未命中＝IO-PACK-REF-INCOMPLETE（清单引用完整性语义——V29）；
     *   ② 加密检测（声明加密方法 ≠ NONE→IO-FORMAT-PACK-ENCRYPTED——
     *     §7.1 加密拒绝；在解压前拒绝，不给任何解密尝试面）；
     *   ③ 压缩方法检测（非 STORED/DEFLATE→IO-FORMAT-PACK-ZIP——§7.1
     *     "压缩方法仅 STORED/DEFLATE"；在解压前拒绝）；
     *   ④ 预算预记账（chargeArchive：压缩侧＝声明压缩量、展开侧＝声明
     *     未压缩量——§4.5.2"声明大小先入账"的预检笔；zip 炸弹比例在
     *     此拦截）；
     *   ⑤ 解压读取（64 KiB 块循环——每块取消检查点＋SingleFileBytes/
     *     TotalBytes 记账；实际展开量超出声明部分按 §4.5.2"以较大者
     *     入账"补差笔）。
     *
     * @param name   [in] 条目名（UTF-8 原样——字节级比对）
     * @param budget [in] 预算守卫（null＝不记账但保留容器层检查——测试/
     *               轻量场景口径；生产装配恒传——§9.5 前置同款约定）
     * @param budgetScope [in] 调用方 scope 句柄（0＝自开内部 scope 用毕
     *               回收——BudgetScopeSession 等价形态，§9.2）
     * @param cancel [in] 取消令牌（null＝不可取消；检查点＝每读取块）
     * @return 成功＝条目未压缩原文（IoString 承载字节——二进制安全）；
     *         失败＝见类注错误类型
     */
    virtual IoResult<IoString> readEntryBytes(const IoString& name, IBudgetGuard* budget,
                                              BudgetScopeId budgetScope, IoCancelToken* cancel) = 0;

    /**
     * @brief manifest 条目哈希校验（V14 前置能力——导入协议步骤⑤的构件）。
     *
     * 对清单逐条目执行：定位→读原文→core ContentDigester SHA-256 复算
     * →与声明比对（SA-12：摘要算法唯一——不引入第二哈希实现）。条目级
     * 失败（缺失/大小不符/哈希不符）**不中断整表**——逐条目报告返回
     * （V14"定位条目"观测点；"整体拒绝"的裁决归导入器，容器层只给事实）。
     * 全部条目级失败时本接口仍返回 ok（报告集即结果）——接口级失败仅
     * 预算/取消/容器损坏。
     *
     * 码面语义：缺失→IO-PACK-REF-INCOMPLETE（params entry）；大小不符→
     * IO-PACK-HASH-MISMATCH（params entry/expected/actual＝字节数）；
     * 哈希不符→IO-PACK-HASH-MISMATCH（expected/actual＝小写 hex 摘要）。
     *
     * @param entries [in] manifest 清单（调用方持有——按清单序出报告）
     * @param budget  [in] 预算守卫（null＝不记账——同 readEntryBytes 口径）
     * @param budgetScope [in] 调用方 scope（0＝自开内部 scope）
     * @param cancel  [in] 取消令牌（null＝不可取消；检查点＝每条目读取块）
     * @return 成功＝逐条目报告（清单序）；接口级失败＝IO-SEC-BUDGET- 族、
     *         IO-CANCELLED/IO-FORMAT-PACK-ZIP
     */
    virtual IoResult<std::vector<ZipEntryVerifyReport>>
        verifyManifestEntries(const std::vector<ZipManifestEntry>& entries, IBudgetGuard* budget,
                              BudgetScopeId budgetScope, IoCancelToken* cancel) = 0;
};

/**
 * @brief 打开 ZIP 通道会话（一个包文件一个会话——§7.1 传输封装；文件
 *        本体不是项目权威格式——§7.1"不是第二权威格式"）。
 *
 * @param packFile [in] 包文件路径（存在性/权限错误→IO-RES-*；容器层
 *                 无法打开→IO-FORMAT-PACK-ZIP）
 * @return 成功＝非空会话（unique_ptr——会话型单线程；RAII 关闭 libzip
 *         句柄）；失败＝IO-RES-NOT-FOUND/IO-RES-ACCESS-DENIED（文件打
 *         不开——环境错误四分类）、IO-FORMAT-PACK-ZIP（容器层损坏/非
 *         ZIP 内容）
 */
IoResult<std::unique_ptr<IZipChannel>> openZipChannel(const std::filesystem::path& packFile);

} // namespace sdurws::ird::io

#endif // SDURWS_IRD_IO_ZIPCHANNEL_HPP
