/**
 * @file   RecoveryChild.hpp
 * @brief  EX-RCV-1 真进程用例的子进程半边——"主进程"模式宿主（建库→归档
 *         预留→写标记→停靠待杀）与命令行分派接口。
 *
 * 设计依据：
 *   - units/execution.md §11 EX-RCV-1 行（运行中 kill 主进程〔TestProcessRunner〕
 *     →重启＋恢复扫描→无 manifest 预留重建为 Interrupted）、§7.5 应用崩溃行
 *     （Job 限杀联动；重启恢复扫描——P-EX-6：Queued 纯内存任务不呈现，预留
 *     期起可观测）、§5.1 T14（恢复期指派）
 *   - units/testkit.md §6.5（TestProcessRunner/EventWatch 消费纪律——子进程
 *     "被杀"必须是父进程的注入动作，永不自杀；标记文件＝跨进程唯一判据）
 *   - 先例：project/test/Tx15Child.hpp/.cpp（PRJ-T15 同款停靠协议——标记
 *     文件 flush 后永久睡眠，父进程 kill/超时兜底回收）
 *
 * 与 PRJ-T15 子进程的差异：本宿主模拟的是 **execution 的主进程**（持有
 * 存储写锁＋归档预留），被杀后磁盘留下"无 manifest 的归档预留"现场——
 * 父进程（RecoveryProcessContractTest）重启开库并做执行侧恢复重建。
 *
 * 错误语义：本 TU 运行于 gtest 之外（main 拦截先于 InitGoogleTest）——
 * 失败以非零退出码表达（父进程 awaitFile 超时路径显性失败，无假阳性通道）。
 */

#ifndef SDURWS_IRD_EXECUTION_CONTRACT_TEST_RECOVERY_CHILD_HPP
#define SDURWS_IRD_EXECUTION_CONTRACT_TEST_RECOVERY_CHILD_HPP

#include <filesystem>
#include <string>

namespace exrcv {

/// 子进程命令行参数（父进程经 ProcessSpec.args 传入）。
struct ChildArgs {
    std::filesystem::path storePath;   ///< 项目库路径（不存在——由子进程创建）
    std::filesystem::path markerPath;  ///< 停靠标记文件（现场信息载体）
};

/// 识别并解析子进程模式参数（--ird-ex-rcv-child --store=… --marker=…）；
/// 非子进程模式返回 false（argv 不动）。
bool tryParseChildArgs(int argc, char** argv, ChildArgs& out);

/// 子进程主体：建库→归档预留（begin＋一批次字节，**不 finalize**）→写
/// 标记（run/branch/revision 规范文本）→永久停靠待杀。返回进程退出码
/// （正常路径永不返回——被杀；失败路径非零）。
int runRecoveryParkChild(const ChildArgs& args);

}  // namespace exrcv

#endif  // SDURWS_IRD_EXECUTION_CONTRACT_TEST_RECOVERY_CHILD_HPP
