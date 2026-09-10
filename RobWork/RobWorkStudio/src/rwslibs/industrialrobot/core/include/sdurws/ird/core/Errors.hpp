/**
 * @file   Errors.hpp
 * @brief  CoreError——core 单元唯一异常类型（错误表达契约的异常轨载体）。
 *
 * 设计依据：
 *   - units/core.md §4.10（错误类型：`CoreError : std::runtime_error`，消息前缀稳定）
 *   - 需求 ERR-01（诊断记录字段）、NFR-REL-05（用户可见文案不在 core 生成）
 *   - 任务契约 tasks/foundation/CORE-T02.json（≙WP-03-T02）：fromCanonical/
 *     ContentDigester::finalize 的抛错路径需要本类型——§4.10 无独立任务行，
 *     按"首个消费者落位最小契约"惯例随本任务建立（签名与 §4.10 原文逐字一致，
 *     不新增成员；后续任务如需扩展走 core.md 增量修订）
 *
 * 背景说明：core 的错误表达是"异常＋try* 非抛出双轨"（§4.10/D-07）：异常仅在
 * 调用方契约违约或需要快速失败时出现；可恢复查询一律另有 try* 变体。
 * what() 携带稳定消息前缀（如 "core/identity/parse:"），面向开发诊断；
 * 用户可见文案与诊断码登记归 diagnostics 单元（NFR-REL-05），core 不越权。
 */

#ifndef SDURWS_IRD_CORE_ERRORS_HPP
#define SDURWS_IRD_CORE_ERRORS_HPP

#include <stdexcept>
#include <string>

namespace sdurws::ird::core {

/**
 * @brief core 唯一异常类型（units/core.md §4.10 原文契约，不增删成员）。
 *
 * 值语义：按值抛出/捕获；无成员状态（消息全在 runtime_error 基类）。
 * 线程安全：不可变，仅构造与 what() 只读访问。
 *
 * 前缀约定（§4.10 稳定前缀清单，抛出点各自维护）：
 *   - core/identity/parse:  身份规范文本解析失败（tag/长度/字符集）
 *   - core/digest/finalized: ContentDigester finalize 后再使用
 *   - 其余前缀随对应 §5 接口在各自任务落地时登记
 */
class CoreError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;   ///< 透传构造（string 与 const char* 两形）
};

}  // namespace sdurws::ird::core

#endif  // SDURWS_IRD_CORE_ERRORS_HPP
