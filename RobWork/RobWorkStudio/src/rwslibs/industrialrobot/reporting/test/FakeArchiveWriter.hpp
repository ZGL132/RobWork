/**
 * @file   FakeArchiveWriter.hpp
 * @brief  FakeArchiveWriter——IArchiveWriter 的最小确定性替身（§10 可控替身
 *         四具名之一；RPT-T10 随任务交付、RPT-T11 起为具名正本——局部夹具
 *         收口的唯一登记形态，卡行 T11 产物列原文）。
 *
 * 设计依据：
 *   - units/reporting.md §9.6（IArchiveWriter 三方法契约原文——open 同卷
 *     临时区/addEntry 纯相对正斜杠〔SP-2〕/finish 原子替换到目标、异常/
 *     失败→清理临时、目标不变）、§7.6（"封装：IArchiveWriter（注入——
 *     io ZIP 适配，P-RPT-7）逐条目写入 → finish"）、§10（可控替身清单——
 *     FakeArchiveWriter）
 *   - 任务契约 tasks/foundation/RPT-T10.json acceptance 3~5（取消/失败
 *     清理的注入观测面；P-RPT-7——测试经本替身注入，不私建第二套 ZIP
 *     实现）、tasks/foundation/RPT-T11.json acceptance 2（四具名替身
 *     收口——本头即具名正本，无需再收敛）
 *
 * 替身边界声明（RP-STATE-4／§10 替身边界声明原文——四具名替身共用，全文
 *   亦登记于 test/README.md，此处为具名正本之一）：
 *   本替身仅验证 reporting 侧契约（会话纪律/条目名 SP-2 拒绝/只增/失败与
 *   放弃路径的临时清理/目标不变语义），**不构成 ZIP 容器格式的正确性证明**
 *   ——本替身不产出 ZIP 字节（临时文件＝条目字节的顺序拼接，仅承载"临时区
 *   存在→发布→清理"的生命周期事实）；容器格式/压缩/原子替换的真实性归
 *   io 单元验证矩阵（io ZipChannel 之上的薄适配——P-RPT-7/P-IO-1 合并
 *   裁决后的实现面）。条目字节来自被测链路的真实产出（canonical JSON），
 *   替身不伪造任何包内容。
 *
 * 与真实 io 实现的语义对齐点（替身按契约实现——reporting 侧可观测语义）：
 *   - open 建同卷临时文件（<target>.ird-fake-partial——同卷保证 rename 可行）；
 *   - addEntry 校验 SP-2（绝对/反斜杠/".." 段/空名/重复名→std::invalid_argument
 *     ——调用方违约 fail-fast，§9.6 非法调用行）并追加字节；
 *   - finish 原子替换（Windows rename 不覆盖——先 remove 再 rename 的替身
 *     近似，真实原子性归 io）并返回条目字节面的确定性摘要；失败注入→
 *     清理临时、目标不变；
 *   - 析构：存在未 finish 会话→清理临时（RAII——Bundle.hpp 文件头"放弃
 *     路径"节的实现义务承载）。
 *
 * 可注入故障（RP-BUN-1 观测需要）：
 *   - failOpen：open 抛 ReportError(ExportFailed)（目标不可达形态）；
 *   - failAtAddEntry：第 n 次 addEntry 抛 ReportError(ExportFailed)（写出
 *     环境失败——磁盘满/介质错误的等价观测面）；
 *   - failAtFinish：finish 抛 ReportError(ExportFailed)（发布失败——临时
 *     已清理、目标不变）。
 *
 * 线程约束：单线程使用（被测装配器会话单线程——非线程安全替身）。
 */

#ifndef SDURWS_IRD_REPORTING_TEST_FAKEARCHIVEWRITER_HPP
#define SDURWS_IRD_REPORTING_TEST_FAKEARCHIVEWRITER_HPP

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>     // core::Digest256/ContentDigester
#include <sdurws/ird/reporting/Bundle.hpp>
#include <sdurws/ird/reporting/Errors.hpp>

namespace sdurws::ird::reporting::test_fakes {

/**
 * @brief 归档写出器替身（真实临时文件承载生命周期——见文件头替身边界声明）。
 */
class FakeArchiveWriter final : public IArchiveWriter {
public:
    // ---- 注入面（RP-BUN-1 三类故障观测） ----

    bool failOpen = false;          ///< true＝open 抛 ExportFailed（目标不可达）
    int failAtAddEntry = 0;         ///< n>0＝第 n 次 addEntry 抛 ExportFailed
    bool failAtFinish = false;      ///< true＝finish 抛 ExportFailed（发布失败）

    // ---- 观测面（测试断言用） ----

    /// 已写入条目（保序——addEntry 调用序即容器条目序的替身投影）。
    std::vector<std::string> entryNames;
    /// 条目字节（名称→字节——供包内容断言）。
    std::map<std::string, std::vector<std::uint8_t>> entryBytes;
    /// open 是否成功发生过（会话开启事实）。
    bool opened = false;
    /// finish 是否成功发生过（发布完成事实——目标文件随之存在）。
    bool finished = false;

    /// 当前临时文件路径（未 finish 且 open 过＝存在；发布/清理后为空语义——
    /// 文件系统不存在该路径）。
    std::filesystem::path tempPath() const { return m_temp; }

    // ---- IArchiveWriter（§9.6 契约语义的替身承载） ----

    void open(const std::filesystem::path& target) override
    {
        // 会话纪律：重复 open＝调用方违约（fail-fast——Bundle.hpp 契约注释）。
        if (opened && !finished) {
            throw std::logic_error("fake: 重复 open（会话已开启）");
        }
        if (failOpen) {
            // 注入目标不可达（父目录不存在/权限缺失等环境失败的等价观测面）
            // ——失败路径零临时残留（open 尚未建临时）。
            throw ReportError(ReportErrorCode::ExportFailed,
                              "fake: open 失败（注入——目标不可达）");
        }
        m_target = target;
        // 同卷临时区：<target>.ird-fake-partial（同目录＝同卷——§9.6 open
        // 注释"同卷临时区"的替身承载；真实 io 的临时命名/恢复扫描归 io）。
        m_temp = m_target.parent_path()
                 / (m_target.filename().string() + ".ird-fake-partial");
        // 预清理（前次异常残留的防御——真实 io 有恢复扫描，替身直接重置）。
        std::error_code ec;
        std::filesystem::remove(m_temp, ec);
        // 建空临时文件（写探测——父目录不可达在此暴露为环境失败）。
        {
            std::ofstream file(m_temp, std::ios::binary | std::ios::trunc);
            if (!file) {
                m_temp.clear();
                throw ReportError(ReportErrorCode::ExportFailed,
                                  "fake: 临时区创建失败（父目录不可达）");
            }
        }
        opened = true;
        finished = false;
        entryNames.clear();
        entryBytes.clear();
    }

    void addEntry(const std::string& entryName, const std::vector<std::uint8_t>& bytes) override
    {
        // 条目名 SP-2 校验（§9.6 非法调用行"条目名绝对/上溯（io SP-2 拒绝）"
        // 的替身承载——调用方违约 fail-fast）：
        //   - 空名；- 绝对路径（首位分隔符/盘符）；- 反斜杠（非纯正斜杠）；
        //   - ".." 上溯段；- 同名重复（只增纪律——§9.6 合法调用行对偶）。
        const bool sp2Violation = entryName.empty() || entryName.front() == '/'
                                  || entryName.find('\\') != std::string::npos
                                  || entryName.find(':') != std::string::npos
                                  || hasDotDotSegment(entryName);
        if (sp2Violation) {
            throw std::invalid_argument("fake: 条目名违反 SP-2（绝对/反斜杠/上溯/空名）——"
                                        + entryName);
        }
        if (!opened || finished) {
            throw std::logic_error("fake: 会话未开启或已终结（addEntry 前须 open）");
        }
        if (std::find(entryNames.begin(), entryNames.end(), entryName) != entryNames.end()) {
            throw std::invalid_argument("fake: 条目名重复（只增纪律）——" + entryName);
        }
        // 写出故障注入（第 n 次 addEntry——计数按调用序）：失败即"会话终结"
        // 处置——清理临时、目标不变（§9.6 finish 失败注释的会话侧对偶：
        // 失败＝责任终结，下次 open 从干净状态重开——§7.4 可重试语义的
        // 替身承载），随后抛出。
        if (failAtAddEntry > 0 && ++m_addCounter == failAtAddEntry) {
            cleanupTemp();
            opened = false;
            finished = false;
            throw ReportError(ReportErrorCode::ExportFailed,
                              "fake: addEntry 写出失败（注入——第 " + std::to_string(m_addCounter)
                                  + " 次；临时已清理、目标不变）");
        }
        // 追加到临时文件（append——字节序＝调用序）并登记观测面。
        std::ofstream file(m_temp, std::ios::binary | std::ios::app);
        if (!file) {
            cleanupTemp();
            opened = false;
            throw ReportError(ReportErrorCode::ExportFailed,
                              "fake: 临时文件写失败（会话终结）——" + entryName);
        }
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            cleanupTemp();
            opened = false;
            throw ReportError(ReportErrorCode::ExportFailed,
                              "fake: 临时文件写失败（写后校验；会话终结）——" + entryName);
        }
        entryNames.push_back(entryName);
        entryBytes[entryName] = bytes;
    }

    core::Digest256 finish() override
    {
        if (!opened || finished) {
            throw std::logic_error("fake: 会话未开启或已终结（finish 前须 open）");
        }
        if (failAtFinish) {
            // 发布失败注入：清理临时、目标不变（§9.6 finish 签名注释原文
            // "异常/失败→清理临时、目标不变"——失败路径清理义务的实现侧
            // 承载；可重试——下次 open 重新开会话）。
            cleanupTemp();
            throw ReportError(ReportErrorCode::ExportFailed,
                              "fake: finish 发布失败（注入——临时已清理、目标不变）");
        }
        // 原子替换（替身近似：Windows rename 不覆盖既有目标——先 remove；
        // 真实原子协议归 io 验证矩阵，见文件头替身边界声明）。
        std::error_code ec;
        if (std::filesystem::exists(m_target, ec)) {
            std::filesystem::remove(m_target, ec);
            if (ec) {
                cleanupTemp();
                throw ReportError(ReportErrorCode::ExportFailed,
                                  "fake: 旧目标移除失败（替换不可行）");
            }
        }
        std::filesystem::rename(m_temp, m_target, ec);
        if (ec) {
            cleanupTemp();
            throw ReportError(ReportErrorCode::ExportFailed,
                              "fake: rename 失败（发布不可行）");
        }
        finished = true;
        // 容器级字节面摘要（确定性：条目字节顺序拼接后 SHA-256——core
        // 唯一算法；规范化域是替身自有口径，装配器不消费该值）。
        std::vector<std::uint8_t> all;
        for (const std::string& name : entryNames) {
            const auto& bytes = entryBytes[name];
            all.insert(all.end(), bytes.begin(), bytes.end());
        }
        core::ContentDigester digester;
        digester.update(all.data(), all.size());
        return digester.finalize();
    }

    /// RAII 放弃路径（Bundle.hpp 文件头"放弃路径"节——析构时存在未 finish
    /// 会话→清理临时、目标不变）。已 finish（或从未 open）→零动作。
    ~FakeArchiveWriter() override { cleanupTemp(); }

private:
    /// ".." 上溯段判定（路径分段扫描——SP-2 口径的上溯拒绝面）。
    static bool hasDotDotSegment(const std::string& name)
    {
        std::size_t begin = 0;
        while (begin <= name.size()) {
            const std::size_t slash = name.find('/', begin);
            const std::size_t end = (slash == std::string::npos) ? name.size() : slash;
            if (end - begin == 2 && name[begin] == '.' && name[begin + 1] == '.') {
                return true;
            }
            if (slash == std::string::npos) {
                break;
            }
            begin = slash + 1;
        }
        return false;
    }

    /// 临时清理（幂等——finish 成功后临时已不存在，删除失败被吞【std::error_code
    /// 形态】——替身清理失败仅影响观测面，不抛出：放弃路径不抛义务）。
    void cleanupTemp() noexcept
    {
        if (m_temp.empty()) {
            return;
        }
        std::error_code ec;
        std::filesystem::remove(m_temp, ec);
        m_temp.clear();
    }

    std::filesystem::path m_target;   ///< 归档目标（open 参数）
    std::filesystem::path m_temp;     ///< 同卷临时文件（空串＝无活跃临时）
    int m_addCounter = 0;             ///< addEntry 计数（故障注入定位）
};

}  // namespace sdurws::ird::reporting::test_fakes

#endif  // SDURWS_IRD_REPORTING_TEST_FAKEARCHIVEWRITER_HPP
