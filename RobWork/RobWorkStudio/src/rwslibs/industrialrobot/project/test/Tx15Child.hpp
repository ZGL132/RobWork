/**
 * @file   Tx15Child.hpp
 * @brief  PRJ-T15 真进程契约用例的子进程模式分派声明（PRJ-TX-4/F8、
 *         PRJ-TX-7、PRJ-TX-8 的子进程半边）。
 *
 * 设计依据：
 *   - units/project.md §12 PRJ-T15 行（产物：PRJ-TX-1～14 用例体，含
 *     TestProcessRunner 接入的 F8/PRJ-TX-7/8）、§7.6 F8 行（TestProcessRunner
 *     kill 于 T1～T7 每步后）、§11 PRJ-TX-7/8 行；
 *   - units/testkit.md §6.5（TestProcessRunner/EventWatch——AT-11/13 自动化
 *     载体：进程崩溃/强杀/事件等待）、§10.1（触发登记：消费者 PRJ-T15）。
 *
 * 背景说明（为什么子进程与本测试可执行文件同体）：真进程崩溃注入的"被杀
 * 进程"必须真实持有被测资源（存储写锁句柄/在途事务/在途归档会话），独立
 * 小工具无法链接 product 库的私有实现头（TxEngine/ObjectStore 等 src/ 头
 * ——R-2 只允许同单元测试目标消费）。沿用 LockContractTest（PRJ-T03）的
 * "可执行文件自再执行"先例：contract_test 目标的 main 在 gtest 初始化前
 * 拦截 `--ird-wp04-t15-child <mode>` 分派到本头声明的入口；父进程经
 * testkit TestProcessRunner 启动/等待/强杀。
 *
 * 参数传递纪律（宽字符无损）：分派 token 走窄 argv（纯 ASCII）；路径类
 * 参数走环境变量（父进程 SetEnvironmentVariableW 写入、子进程
 * GetEnvironmentVariableW 读取——窄 argv 经 ANSI 代码页会破坏非 ASCII
 * 临时路径，LockContractTest 同款教训）。环境变量名（唯一权威登记处）：
 *   - IRD_TX15_DIR     项目 .rwdesign 目录（宽字符路径）
 *   - IRD_TX15_MARKER  停靠标记文件路径（子进程到达目标边界后写出）
 *   - IRD_TX15_PARK    停靠边界 token（crash-commit 模式专用，见 .cpp）
 *
 * 线程与生命周期：子进程单线程执行分派函数后退出（或停靠等待被杀）；
 * 不运行 gtest（main 拦截发生在 InitGoogleTest 之前——子进程退出码即
 * 通信面，不产生 gtest 报告文件）。
 */

#ifndef SDURWS_IRD_PROJECT_TEST_TX15CHILD_HPP
#define SDURWS_IRD_PROJECT_TEST_TX15CHILD_HPP

namespace sdurws::ird::project::tx15 {

/**
 * @brief 子进程模式分派入口（main 拦截 `--ird-wp04-t15-child` 后调用）。
 *
 * @param mode [in] 模式 token（窄 ASCII；分派表见 Tx15Child.cpp 文件头）。
 *            未知模式返回 9（分派表违约——显性失败，不静默成功）。
 * @return 子进程退出码：0＝正常完成；非 0＝模式内失败（父进程据此断言
 *         Exited 形态的退出码语义——testkit §6.5"崩溃＝非零退出"的反例面）。
 *
 * 停靠（park）模式的进程由父进程 TestProcessRunner::kill() 终止——本函数
 * 不返回（进程被 Job Object 终止）。
 */
int runChild(const char* mode);

}  // namespace sdurws::ird::project::tx15

#endif  // SDURWS_IRD_PROJECT_TEST_TX15CHILD_HPP
