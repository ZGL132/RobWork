/**
 * @file   IoFwd.hpp
 * @brief  io 单元公共前向头——IoString/IoResult<T> 公共别名、IoCancelToken
 *         协作取消令牌、IoProgress 进度结构与版本常量。
 *
 * 设计依据：
 *   - units/io.md §3.1（公共头表 IoFwd.hpp 行：前向声明与公共别名
 *     （IoResult<T>、IoCancelToken、版本常量））、§9.0（本头全部实体的
 *     定义原文与公共约定——签名均为实现建议，实现期允许等价调整、语义
 *     不变）、§8.6（kAccessVersion=1 读取契约版本——P-RT-6 裁决落点）、
 *     §4.4（取消是协作式检查点，非抢占）
 *   - 需求 NFR-MNT-01（计算内核零 Qt——本头纯标准库）、NFR-MNT-02
 *     （单元边界）
 *   - 任务契约 tasks/foundation/IO-T01.json（≙WP-11-T02）acceptance 1
 *     （IoFwd/IoError 公共头就位）
 *
 * 背景说明（为什么每个 io 接口都从这两个头起步——§9.0 公共约定的载体）：
 *   - io 对外接口一律非抛出（§1.4）：每个接口经 IoResult<T> 返回"值或
 *     错误"，错误轨道的类型（IoError/IoErrorCode）定义于 IoError.hpp，
 *     本头 include 之——IoResult 的 ok 判定需要 IoError 完整定义
 *     （error.code == Ok），前向声明不够；且 IoResult 与 IoError 同为
 *     全接口签名面，消费者经任一 io 公共头必然同时可见两者，此 include
 *     不增加额外暴露面。
 *   - 取消与进度是长操作（包导入导出/固化/资源复制）的公共协作面：
 *     io 自建线程纪律（§9.0"io 不创建线程"）之下，取消由调用方
 *     （execution/命令侧）驱动 IoCancelToken 实现，io 侧仅在检查点轮询；
 *     进度经 IoProgressCallback 回调上抛，io 不持有回调线程。
 *
 * IoCancelToken 所有权与生命周期：io 定义接口、**调用方实现**（§9.0 注释
 * 原文）；所有长操作以 `IoCancelToken*` 接收（null＝不可取消）——裸指针
 * 由调用方持有并保证在操作期间存活，io 不接管所有权、不释放、不复制。
 *
 * 线程安全：isCancelled() 实现方须满足幂等且一经真值不再复位（§9.0 注释
 * 原文）——典型实现为原子布尔或内存屏障保护的标志位；io 侧仅在操作线程
 * 轮询，不对其做并发写。IoProgress/IoError/IoResult 为纯值类型，并发
 * 只读安全。IoResult 的模板实例由使用处隐式实例化（头文件内联定义）。
 *
 * 确定性：kAccessVersion 为编译期常量（内容寻址读取契约的一部分——变更
 * 即格式版本升级，走 §8.6 与单元卡增量修订）；IoProgress::stage 指向
 * 稳定英文短语（字面量，进程生存期有效），不携带本地化文案（文案归
 * ui——UX-02 键值分离）。
 */

#ifndef SDURWS_IRD_IO_IOFWD_HPP
#define SDURWS_IRD_IO_IOFWD_HPP

#include <cstdint>
#include <functional>
#include <string>

#include <sdurws/ird/io/IoError.hpp>   // IoError/IoErrorCode——IoResult 错误轨道与 ok 判定所需完整类型

namespace sdurws::ird::io {

// =====================================================================
// 公共别名与版本常量（§9.0 原文）
// =====================================================================

/**
 * io 字符串公共别名：一律 UTF-8 编码（§9.0 注释原文"// UTF-8"）。
 * Windows 路径在 io 内部按 §4.2 规范化，接口签名面统一 UTF-8 窄字符串
 * （不经 wchar_t/QString——零 Qt 约束与跨单元一致口径），编码转换是
 * 边界处的显式步骤，不在签名面隐式发生。
 */
using IoString = std::string;

/**
 * 读取契约版本常量（§8.6 P-RT-6 裁决：accessVersion=1）。
 * 资源快照/固化引用携带该版本号——同一 accessVersion 保证 ResourceBytes
 * 读取语义（最低至下次调用、实际至 provider 析构）可比对、可复现。
 * 变更即读取契约升级，须走单元卡增量修订与消费方（project/runtime）
 * 同步，不得就地改值。
 */
inline constexpr std::uint32_t kAccessVersion = 1;

// =====================================================================
// 协作取消令牌（§9.0 原文签名；实现方＝调用方，io 侧仅轮询）
// =====================================================================

/**
 * @brief 协作取消令牌接口：io 定义、调用方（execution/命令侧）实现驱动。
 *
 * 取消是协作式检查点（§4.4）：io 在长操作的既定检查点（包导入九步逐步、
 * CSV 逐行、资源逐文件等）轮询 isCancelled()，命中即停止当前步骤、清理
 * 自身临时产物并返回 IoError{IO-CANCELLED}（§7.2/§7.3）——非抢占、非
 * 强杀，已提交内核的操作不可中断（§9.13 线程与取消总则）。
 *
 * UX-03 锚点：取消**不是错误诊断**——调用方收到 IO-CANCELLED 后把任务
 * 转入 Canceled 状态，不构造诊断条目。
 */
class IoCancelToken {
public:
    virtual ~IoCancelToken() = default;

    /**
     * @brief 查询取消标志（幂等；一经真值不再复位——§9.0 注释原文）。
     *
     * 实现约束：并发调用安全（io 在操作线程轮询、调用方在 UI/命令线程
     * 置位）；返回 false 不保证后续仍为 false（轮询语义），返回 true 后
     * 必须恒为 true（复位语义由实现方保证，io 依赖该契约跳过清理后步骤）。
     */
    virtual bool isCancelled() const = 0;
};

// =====================================================================
// 进度结构与回调（§9.0 原文签名）
// =====================================================================

/**
 * @brief 进度快照：已完成单位数/总量＋阶段标签。
 *
 * 单位语义：done/total 的计数单位随阶段而定（文件数、字节量、步骤号），
 * 由 stage 短语与各接口文档共同注明——本结构不规定统一单位；total=0
 * 表示总量未知（流式/探测阶段），此时 done 仅表示已完成的绝对量。
 * 单调性：同一阶段内 done 不回退（io 侧保证），跨阶段重置经 stage 切换
 * 表达。
 */
struct IoProgress {
    std::uint64_t done;    ///< 已完成单位数（单位随 stage 语义；total=0 时仍有效）
    std::uint64_t total;   ///< 总量（0＝未知；已知时 done ≤ total）
    const char* stage;     ///< 阶段标签（稳定英文短语，字面量生存期；不携带本地化文案——UX-02）
};

/// 进度回调类型：长操作在检查点回调上抛；io 不持有回调、不在回调内执行
/// 重入调用（调用方在回调内只做 UI 更新/取消置位——§9.13 总则）。
using IoProgressCallback = std::function<void(const IoProgress&)>;

// =====================================================================
// 非抛出返回容器（§9.0 原文签名；错误轨道见 IoError.hpp）
// =====================================================================

/**
 * @brief 非抛出返回容器：成功携带 value、失败携带 IoError（§1.4）。
 *
 * 约定（§9.0 注释原文）：ok 时 error.code == Ok 且 value 有效；!ok 时
 * value 处于默认构造状态（"仅 ok 时有效"的机器可判形式——调用方以
 * operator bool 判定，不得在 !ok 时读取 value）。T 须可默认构造（io
 * 接口的返回类型均为值语义类型——§9.0 生命周期注记）。
 *
 * 用法形态（§9.0 契约片段的判定路径）：调用方以 @c if (!r) 判定——失败
 * 分支读 r.error（code/params/detail 驱动诊断构造），成功分支读 r.value。
 */
template <typename T>
struct IoResult {
    T value{};          ///< 结果值；仅 ok 时有效（!ok 时为默认构造状态）
    IoError error;      ///< 错误信息；ok 时 error.code == Ok（此时 params/detail 为空约定）

    /// ok 判定：error.code == Ok 为 true。显式限定（explicit）——禁止在
    /// 算术/比较上下文中被隐式误用为数值，只允许 if (!r) 形式的布尔语境。
    explicit operator bool() const noexcept
    {
        // ok 判定的唯一依据是错误码（而非 value 状态）——错误轨道单一权威。
        return error.code == IoErrorCode::Ok;
    }
};

/**
 * void 特化：无值载荷的成功/失败容器（§9.1 normalizePackEntries、§9.2
 * charge/closeScope 等接口的返回形态——动作型接口只需要 ok/错误轨道，
 * 无值可携）。字段与判定语义同主模板（仅去 value 成员）。
 */
template <>
struct IoResult<void> {
    IoError error;      ///< 错误信息；ok 时 error.code == Ok

    /// ok 判定：同主模板（错误轨道单一权威；explicit 限定同款理由）。
    explicit operator bool() const noexcept
    {
        return error.code == IoErrorCode::Ok;
    }
};

} // namespace sdurws::ird::io

#endif // SDURWS_IRD_IO_IOFWD_HPP
