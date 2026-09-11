/**
 * @file   main.cpp
 * @brief  进程测试辅助子进程（sdurws_ird_testkit_prochelper）——TK-T11 用例的
 *         可控行为载体。
 *
 * 设计依据：
 *   - units/testkit.md §6.5（四态 outcome 需要"行为完全受测试控制"的子进程）、
 *     §9 TK-T11 行、§3.6（tools/ 不进产品安装面）
 *   - 任务契约 tasks/foundation/TK-T11.json acceptance 2
 *
 * 背景说明（为什么不用 cmd.exe 拼用例）：
 *   cmd 的转义/引号规则复杂且随 Windows 版本漂移，用 `cmd /c exit N` 之类
 *   拼接无法可靠构造"孙进程延迟写标记文件"的进程树场景（树终止验证的关键
 *   载体）。本辅助进程行为完全由参数表驱动、输出确定性，且与测试可执行文件
 *   同目录分发（CMake 对齐输出目录），用例零 PATH/工作目录依赖。
 *
 * 子命令一览（argv[1] 分发；参数均为 ASCII/路径）：
 *   exit0                          立即以 0 退出（正常退出载体）
 *   exit <code>                    以给定码退出（§6.5"非零退出＝崩溃"注入载体）
 *   sleep <ms>                     睡眠后以 0 退出（长跑载体）
 *   touch-after <ms> <file>        睡眠后创建标记文件（EventWatch 命中/树终止
 *                                  反向验证载体——孙进程若幸存必会写出）
 *   log-after <ms> <file> <text>   先写首行，睡眠后追加 text 行（增量到达载体）
 *   echo-env <name> <file>         将环境变量写为 "<name>=<value>" 行（env 覆盖载体）
 *   spawn-touch-after <ms> <file>  启动另一个自身进程执行 touch-after，并等待
 *                                  其退出（进程树载体：杀树时孙进程必须死）
 *
 * 线程/并发：单线程顺序执行，无共享状态。退出码恒为 0（正常完成）或参数给
 * 定码——任何其他失败路径以非结构化方式崩溃退出（用例不会走到）。
 */

// 集成模式的全局编译定义可能已注入同名宏（ird 构建面统一口径）——带守卫
// 定义避免 C4005 重定义警告；独立冒烟模式无注入时则由这里补齐。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // 裁剪 windows.h（仅用进程/等待 API）
#endif
#ifndef NOMINMAX
#define NOMINMAX             // 阻止 min/max 宏污染
#endif
#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

/**
 * @brief 宽命令行参数 → UTF-8 窄串（与 ProcessSpec.args 的 UTF-8 契约对称）。
 *
 * 测试传入的标记路径经 ProcessRunner 以 UTF-8→UTF-16 转换到达本进程命令行，
 * 此处转回 UTF-8 写盘/再传递，保证中文路径往返无损。
 *
 * @param wide [in] UTF-16 参数
 * @return UTF-8 参数
 */
std::string toUtf8(const std::wstring& wide)
{
    if (wide.empty()) {
        return {};
    }
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                           static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(need > 0 ? need : 0), '\0');
    if (need > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                              out.data(), need, nullptr, nullptr);
    }
    return out;
}

/**
 * @brief 以挂起→恢复的完整序列启动另一个本进程实例（进程树场景用）。
 *
 * 与 ProcessRunner 同款"先挂起后 Resume"序列在此并无必要（辅助进程不绑定
 * Job），直接 CreateProcessW＋等待即可；保持简单——复杂度只放在被测对象上。
 *
 * @param cmdLine [in] 子进程完整命令行（首段为自身模块全路径）
 * @return 子进程退出码
 */
DWORD runChildAndWait(const std::wstring& cmdLine)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmdLine;  // CreateProcessW 要求可写缓冲
    if (!::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, 0,
                          nullptr, nullptr, &si, &pi)) {
        return 1;  // 启动失败：以非 0 退出让上层用例以四态语义观测到异常
    }
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return code;
}

/**
 * @brief 睡眠指定毫秒（测试时间尺度；steady 语义由 Sleep 的系统时钟近似即可
 *        ——用例断言的是"事件是否到达"，不是睡眠精度）。
 *
 * @param msArg [in] 毫秒数字符串（非法解析按 0 处理＝立即执行）
 */
void sleepMs(const std::wstring& msArg)
{
    const int ms = static_cast<int>(std::wcstol(msArg.c_str(), nullptr, 10));
    std::this_thread::sleep_for(std::chrono::milliseconds{ms < 0 ? 0 : ms});
}

int wmain(int argc, wchar_t** argv)
{
    // 参数不足：直接以 1 退出——用例侧会以 Crashed/Exited 之外的失败形态暴露
    // （装配错误的兜底，不在正常路径上）。
    if (argc < 2) {
        return 1;
    }
    const std::wstring cmd{argv[1]};

    if (cmd == L"exit0" && argc == 2) {
        return 0;  // 正常完成事件：退出码 0（§6.5 Exited 判定载体）
    }
    if (cmd == L"exit" && argc == 3) {
        // 崩溃注入载体：§6.5"崩溃＝子进程非零退出"——由参数控制退出码
        return static_cast<int>(std::wcstol(argv[2], nullptr, 10));
    }
    if (cmd == L"sleep" && argc == 3) {
        sleepMs(argv[2]);
        return 0;
    }
    if (cmd == L"touch-after" && argc == 4) {
        sleepMs(argv[2]);
        // 标记文件内容为单行标识：EventWatch.awaitFile 只探测存在性，
        // 内容仅供人工排查失败现场时辨认来源。
        std::ofstream marker{fs::path{argv[3]}, std::ios::binary};
        marker << "prochelper touch-after\n";
        return marker.good() ? 0 : 1;
    }
    if (cmd == L"log-after" && argc == 5) {
        const fs::path logPath{argv[3]};
        {
            // 先写第一行并落盘：保证 awaitLineInFile 超时用例/增量用例看到
            // "文件已有内容但目标行未到"的中间态。
            std::ofstream log{logPath, std::ios::binary};
            log << "prochelper: first line\n";
        }
        sleepMs(argv[2]);
        std::ofstream append{logPath, std::ios::binary | std::ios::app};
        append << toUtf8(argv[4]) << "\n";
        return append.good() ? 0 : 1;
    }
    if (cmd == L"echo-env" && argc == 4) {
        // 两段式读取（先取长度再取内容）——GetEnvironmentVariableW 标准姿势。
        const DWORD need = ::GetEnvironmentVariableW(argv[2], nullptr, 0);
        std::wstring wideValue;
        if (need > 0) {
            wideValue.resize(need);
            ::GetEnvironmentVariableW(argv[2], wideValue.data(), need);
            wideValue.resize(need - 1);  // 去掉返回长度计入的终止 NUL
        }
        // 变量缺失时写 "name="（空值）而非缺行：让"未覆盖"与"覆盖为空"在
        // 文件上可区分，用例断言更精确。
        std::ofstream out{fs::path{argv[3]}, std::ios::binary};
        out << toUtf8(argv[2]) << "=" << toUtf8(wideValue) << "\n";
        return out.good() ? 0 : 1;
    }
    if (cmd == L"spawn-touch-after" && argc == 4) {
        // 进程树载体：启动一个孙进程执行 touch-after（延迟写标记），然后
        // 等待其退出——本进程充当"父"，孙进程是树中深处节点。杀树语义的
        // 验证判据：若孙进程被漏杀，它会在延迟后写出标记文件（用例断言
        // 标记不出现＝整棵树确实被终止）。
        wchar_t self[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, self, MAX_PATH);
        // 孙进程命令行：自身模块路径整体加引号（路径含空格场景），其余参数
        // 为纯数字毫秒与路径——路径可能含空格，同样加引号。
        std::wstring childCmd = std::wstring{L'"'} + self + L"\" touch-after " +
                                argv[2] + L" \"" + argv[3] + L'"';
        return static_cast<int>(runChildAndWait(childCmd));
    }

    return 1;  // 未知子命令/参数个数不匹配：装配错误兜底（用例不会走到）
}
