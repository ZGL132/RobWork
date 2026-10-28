/**
 * @file   ContractTestMain.cpp
 * @brief  `sdurws_ird_execution_contract_test` 的自有测试入口——子进程模式
 *         拦截（EX-RCV-1 真进程场景）＋安装 testkit TestRecordListener
 *         （ird-test-report.json 聚合落盘）。
 *
 * 设计依据：
 *   - units/execution.md §12 EX-T09 行（契约测试套件落地——TestProcessRunner
 *     按 TK-T11 触发接入 EX-RCV-1 真进程场景）、§11（报告留痕：gtest XML＋
 *     ird-test-report.json 并存）
 *   - units/testkit.md §7.3（installTestRecordListener 原文签名——测试 main
 *     自持两种形态之一）、§6.5（TestProcessRunner 消费——子进程半边经同一
 *     exe 双形态协议承载）
 *   - 先例：project/test/LinkageContractTest 的 main 拦截（PRJ-T15 同款——
 *     子进程分派必须先于 InitGoogleTest）＋project/test/TestMainReport.cpp
 *     （RecordListener 安装形态）
 *
 * 运行形态：
 *   1. 子进程模式：`--ird-ex-rcv-child --store=<path> --marker=<path>`——
 *      运行 RecoveryChild 停靠协议（gtest 之外，永不进入 RUN_ALL_TESTS）；
 *   2. 测试模式：初始化 gtest→安装报告监听器→执行全部用例。可用
 *      --ird_report=<path> 指定报告路径（缺省＝工作目录下
 *      ird-test-report.json）；该参数在 gtest 解析前被摘除。
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）。
 */

#include "RecoveryChild.hpp"
#include <sdurws/ird/execution/ChannelProtocol.hpp>
#include <sstream>

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdlib>
#include <cstring>

/** @brief 本测试 exe 的故障 worker 模式：仅操作父测试提供的继承探针，随后发坏帧。
 * @param argc/argv [in] ProcessLauncher 的私有值参数；不访问正式项目。
 * @return 0 正常自退；10 缺少数据句柄。父 Supervisor 拥有该子进程和 Job。
 */
static int runFaultWorkerChild(int argc, char** argv)
{
    HANDLE data = nullptr;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--data-h") == 0)
            data = reinterpret_cast<HANDLE>(std::strtoull(argv[i + 1], nullptr, 0));
    }
    if (data == nullptr) return 10;
    if (const char* value = std::getenv("IRD_EXECUTION_TEST_EXTRA_HANDLE"))
        ::SetEvent(reinterpret_cast<HANDLE>(std::strtoull(value, nullptr, 0)));
    const char* mode = std::getenv("IRD_EXECUTION_TEST_FAULT_MODE");
    DWORD written = 0;
    if (mode != nullptr) {
        namespace ex = sdurws::ird::execution;
        namespace core = sdurws::ird::core;
        core::TaskIdentity identity;
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::strcmp(argv[i], "--identity") != 0) continue;
            std::istringstream input(argv[i + 1]);
            std::string part[5];
            for (auto& value : part) std::getline(input, value, '|');
            identity.project = core::ProjectId::fromCanonical(part[0]);
            identity.branch = core::BranchId::fromCanonical(part[1]);
            identity.revision = core::RevisionId::fromCanonical(part[2]);
            identity.run = core::RunId::fromCanonical(part[3]);
            identity.attempt = core::AttemptId::fromCanonical(part[4]);
        }
        ex::ChannelFrame frame;
        frame.identity = identity;
        frame.seq = 1;
        if (std::strcmp(mode, "unsolicited-ack") == 0
            || std::strcmp(mode, "before-accept") == 0) {
            frame.type = ex::ChannelMsgType::Hello;
            frame.payload = ex::encodeHello({ex::kChannelProtocolVersion,
                ex::manifestDigestHex("stage-a-double|1|7")});
            auto bytes = ex::encodeFrame(frame);
            ::WriteFile(data, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
            if (std::strcmp(mode, "before-accept") == 0) {
                // 与父测试的冻结输入一致，确保反例只违反派发阶段，不能靠解码失败通过。
                const std::string script = "stage-a-script:v1\n";
                const std::vector<std::uint8_t> snapshot(script.begin(), script.end());
                frame.type = ex::ChannelMsgType::FinalOutput;
                frame.payload = ex::encodeFinalOutput({ex::computeContentIdentity(snapshot),
                    ex::computeContentIdentity({0xC0, 0xDE}), {0x01}});
            } else {
                frame.type = ex::ChannelMsgType::CancelAck;
                frame.payload.clear();
            }
            frame.seq = 2;
        } else {
            frame.type = ex::ChannelMsgType::DispatchAccept; // 握手前的有效帧，不能推进宿主。
        }
        std::vector<ex::ChannelFrame> frames;
        if (frame.type == ex::ChannelMsgType::FinalOutput) {
            // FinalOutput 即使只有一片也必须带分片头；反例不能误测成畸形载荷。
            if (!ex::fragmentMessage(frame.type, identity, frame.seq, frame.payload, 4096, frames))
                return 11;
        } else {
            frames.push_back(std::move(frame));
        }
        for (const auto& out : frames) {
            const auto bytes = ex::encodeFrame(out);
            ::WriteFile(data, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        }
    } else {
        const unsigned char malformed[512] = {};
        ::WriteFile(data, malformed, sizeof(malformed), &written, nullptr);
    }
    ::Sleep(4000); // 有界停靠，只为观察父进程错误回收，不作时序通过判据。
    return 0;
}
#endif

/// 测试入口：子进程分派→gtest 初始化→报告监听器→执行全部用例。
int main(int argc, char** argv)
{
#ifdef _WIN32
    if (argc > 1 && std::strcmp(argv[1], "--ird-worker") == 0)
        return runFaultWorkerChild(argc, argv);
#endif
    // 子进程模式拦截（先于 InitGoogleTest——gtest 不认识自有参数；PRJ-T15
    // 同款时序）。命中即以子进程身份运行并退出（退出码＝子进程语义）。
    {
        exrcv::ChildArgs childArgs;
        if (exrcv::tryParseChildArgs(argc, argv, childArgs)) {
            return exrcv::runRecoveryParkChild(childArgs);
        }
    }

    ::testing::InitGoogleTest(&argc, argv);
    // 监听器所有权按 gtest 公开契约移交：Append 接管并在测试程序结束时释放
    // （gtest/gtest.h TestEventListeners::Append 注释；F-958 修复后为堆对象，
    // 静态/栈对象地址交 Append 属非法 delete）。聚合报告在
    // OnTestProgramEnd 写盘；写盘失败只向 stderr 告警，不掩盖测试结果
    // （RecordListener 契约）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
