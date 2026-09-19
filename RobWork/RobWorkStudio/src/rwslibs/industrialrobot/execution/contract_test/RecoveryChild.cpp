/**
 * @file   RecoveryChild.cpp
 * @brief  EX-RCV-1 子进程半边实现（见 RecoveryChild.hpp 文件头）。
 *
 * 停靠（park）语义（Tx15Child 同款协议）：到达"预留已建、未 finalize"
 * 边界后先写标记文件（flush＋close——父进程 EventWatch.awaitFile 的可
 * 观察事件），再永久睡眠等待被杀（父进程 TestProcessRunner 的 Job Object
 * 兜底终止；ProcessSpec.timeout 到点同样回收——子进程永不自杀）。
 */

#include "RecoveryChild.hpp"

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>

#include <windows.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace exrcv {

bool tryParseChildArgs(int argc, char** argv, ChildArgs& out)
{
    bool seen = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] != nullptr ? argv[i] : "";
        const std::string childFlag = "--ird-ex-rcv-child";
        const std::string storeFlag = "--store=";
        const std::string markerFlag = "--marker=";
        if (arg == childFlag) {
            seen = true;
        } else if (arg.rfind(storeFlag, 0) == 0) {
            out.storePath = std::filesystem::u8path(arg.substr(storeFlag.size()));
        } else if (arg.rfind(markerFlag, 0) == 0) {
            out.markerPath = std::filesystem::u8path(arg.substr(markerFlag.size()));
        }
    }
    return seen && !out.storePath.empty() && !out.markerPath.empty();
}

int runRecoveryParkChild(const ChildArgs& args)
{
    namespace fs = std::filesystem;
    namespace core = sdurws::ird::core;
    namespace pd = sdurws::ird::project;

    // ---- 步 1：创建项目库（父进程传入的路径不存在——本进程扮演"主进程"
    // 首次打开项目的场景；失败＝环境错误，非零退出由父进程超时路径暴露）。
    std::unique_ptr<pd::ProjectStore> store =
        pd::ProjectStoreFactory::createNew(args.storePath, "EX-T09-恢复", nullptr, nullptr).store;
    if (!store) {
        return 3;
    }

    // ---- 步 2：生成运行身份（五元组——project 用存储真实 projectId，
    // branch/revision/run 现生成；规范文本写进标记文件供父进程断言）。
    core::TaskIdentity id;
    id.project = store->projectId();
    id.branch = core::BranchId::generate();
    id.revision = core::RevisionId::generate();
    id.run = core::RunId::generate();
    id.attempt = core::AttemptId{1};

    // ---- 步 3：归档预留（begin——results/<run>/ 目录建立＝D-12"派发期
    // 即预留"的崩溃判据面）＋写入一批次字节（未 finalize——无 manifest）。
    // 这就是"运行中主进程被杀"的磁盘现场：预留目录在、manifest 不在。
    pd::ArchiveRequest request;
    request.task = id;
    request.runDir = store->canonicalPath() / "results" / id.run.toCanonical();
    request.runKind = "kin-batch-eval";
    request.evaluationKey = "kin-batch-ik";
    pd::ArchiveSessionRef session = store->archive().begin(request);

    const std::vector<std::uint8_t> partialBytes{0x5A, 0xA5, 0x3C, 0x21, 0x00, 0x7E};
    pd::ArchiveBatch batch;
    pd::ArchiveItem item;
    item.relPath = "partial-batch.bin";
    item.bytes = partialBytes;
    batch.items.push_back(item);
    const pd::ArchiveStatus written = store->archive().writeBatch(session, batch);
    if (!written.ok) {
        return 4;  // 预留写入失败＝现场构造失败——显性退出（无假阳性）
    }

    // ---- 步 4：写停靠标记（先 flush 关闭——父进程读它即"现场已就绪"的
    // 可观察事件；内容＝身份规范文本＋批次字节数，供父进程逐一断言）。
    {
        std::ofstream marker(args.markerPath, std::ios::binary | std::ios::trunc);
        if (!marker) {
            return 5;
        }
        marker << "run=" << id.run.toCanonical() << "\n";
        marker << "branch=" << id.branch.toCanonical() << "\n";
        marker << "revision=" << id.revision.toCanonical() << "\n";
        marker << "partial-bytes=" << partialBytes.size() << "\n";
        marker.flush();
        if (!marker) {
            return 6;
        }
    }  // ofstream 析构＝关闭（awaitFile 的可见性保证）

    // ---- 步 5：永久停靠（"主进程运行中"——被杀是父进程的注入动作；
    // Job Object/超时兜底保证不留孤儿）。
    for (;;) {
        ::Sleep(250);
    }
    return 0;  // 不可达（语义完备性占位——编译器告警抑制）
}

}  // namespace exrcv
