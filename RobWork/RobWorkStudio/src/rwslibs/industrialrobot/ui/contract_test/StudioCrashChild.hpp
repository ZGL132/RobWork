/**
 * @file   StudioCrashChild.hpp
 * @brief  WP-24-T02 进程级崩溃契约用例的子进程模式分派契约（AT-11 真实
 *         失败能力——子进程装捕获→受控崩溃→父进程断言诊断文件落盘脱敏）。
 *
 * 设计依据：
 *   - units/diagnostics.md §7.6（崩溃诊断文件＝AT-11 观测点"写出且脱敏"；
 *     OS 级捕获与应用壳装配归 WP-24-T02）、§12.1 协作行；
 *   - 任务契约 tasks/foundation/WP-24-T02.json acceptance 4（"进程级契约
 *     用例：子进程故意崩溃→父进程断言诊断文件落盘且脱敏；测试放行通道
 *     必须有防误触设计"）；
 *   - 先例：project/test/Tx15Child.{hpp,cpp}（main 拦截 `--ird-…-child
 *     <mode>` 分派到本头声明的入口；父进程经 testkit TestProcessRunner
 *     启动——同单元 ProcessCrashContractTest 同款协议）。
 *
 * 参数传递纪律（与 Tx15Child 同源）：分派 token 走 argv（纯 ASCII）；实际
 * 参数走环境变量（宽字符安全——崩溃目录/日志目录可能含非 ASCII 路径）：
 *   IRD_T24_CRASH_DIR    崩溃诊断文件目录（必填——av/abort 模式）
 *   IRD_T24_LOG_DIR      开发日志目录（必填——av/abort 模式；dev 尾快照源）
 *   IRD_T24_WATCHDOG_MS  看门狗时限毫秒（可选——hang 模式注入短时限；
 *                        缺省 kCrashReportWatchdog）
 *
 * 模式语义（封闭白名单——未知模式返回 9 分派违约，显性失败）：
 *   av     装捕获（SEH 过滤器路径）→受控空指针写入崩溃——产品 av 通道同款
 *          形态；父进程断言诊断文件落盘＋脱敏（本组主用例）；
 *   abort  装捕获→raise(SIGABRT)——CRT 终止族路径（信号半区实证）；
 *   hang   构造本地上下文（看门狗时限取 IRD_T24_WATCHDOG_MS）→快照源在
 *          发射路径内永久挂起→看门狗自我了断（kCrashWatchdogSelfExitCode
 *          ——发射核心 emitCrashReportTo 与看门狗为产品全局路径的唯一
 *          实现，本地上下文与全局安装共用同一发射主体）。
 */

#ifndef SDURWS_IRD_UI_CONTRACT_TEST_STUDIOCRASHCHILD_HPP
#define SDURWS_IRD_UI_CONTRACT_TEST_STUDIOCRASHCHILD_HPP

#include <string>

namespace sdurws {
namespace ird {
namespace ui {
namespace studio {

/**
 * @brief 子进程模式分派入口（main 拦截 `--ird-wp24-t02-child` 后调用）。
 *
 * @param mode [in] 模式 token（av|abort|hang——白名单外返回 9）
 *
 * @return av/abort：不返回（进程在捕获路径内终结）；hang：不返回（看门狗
 *         终结）；装配违约（环境变量缺失等）返回 8；分派违约返回 9——
 *         非零码父进程显性失败（Tx15Child 同款错误语义）。
 */
int runCrashTestChild(const std::string& mode);

}  // namespace studio
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_CONTRACT_TEST_STUDIOCRASHCHILD_HPP
