/**
 * @file   GoldenFixture.hpp
 * @brief  黄金数据集夹具的 gtest 集成形态（§6.1 夹具生命周期六步）。
 *
 * 设计依据：
 *   - units/testkit.md §6.1（建立→执行→断言→清理四阶段；SetUp 六步/TearDown
 *     三步的逐步语义）；任务契约 tasks/foundation/TK-T08.json（≙WP-02-T08）
 *   - 需求 NFR-COR-02（可复现）；§2.2（夹具不实现项目事务/写锁/调度——
 *     排除项边界）
 *
 * 分层说明（D-06 边界）：本头 include 了 <gtest/gtest.h>，仅供消费方
 * `_test`/`_contract_test` 目标包含（这些目标已链 GTest::gtest_main）——
 * 与同目录 AssertMacros.hpp（TK-T05 交付）同一分层先例；库本体
 * （Fixture.hpp）零 gtest。testkit 库不链接 gtest，本头也不进库编译。
 *
 * 用法（§6.1 建议形态）：业务测试类继承本夹具，按需覆写配置点
 * （datasetRef/seed/threadCount），测试体直接使用 dataset/profile/env/workDir
 * 五个产物。派生类的"业务阶段"（建立项目、产生修订等）自行编写——本夹具
 * 只提供通用六步（§2.2 排除项）。
 */

#ifndef SDURWS_IRD_TESTKIT_GTEST_GOLDENFIXTURE_HPP
#define SDURWS_IRD_TESTKIT_GTEST_GOLDENFIXTURE_HPP

#include <gtest/gtest.h>  // ::testing::Test（消费方 _test 目标专用——见文件头分层说明）

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <sdurws/ird/testkit/Dataset.hpp>           // GoldenDataset（②数据集装载）
#include <sdurws/ird/testkit/Fixture.hpp>           // TempDir/DeterministicEnv/ReproRecord
#include <sdurws/ird/testkit/TestPaths.hpp>         // goldenDataRoot（①数据根解析）
#include <sdurws/ird/testkit/ToleranceProfile.hpp>  // ToleranceProfile（③档案就绪）

namespace sdurws::ird::testkit {

/**
 * @brief 通用黄金数据集夹具（gtest::Test 派生；§6.1 六步生命周期的载体）。
 *
 * SetUp 六步（§6.1 原文）：
 *   ① TestPaths 解析数据根（env）→ ② GoldenDataset::load（含完整性）→
 *   ③ ToleranceProfile 就绪 → ④ DeterministicEnv 建立（种子/线程）→
 *   ⑤ TempDir 建立 → ⑥ TestRecord 初始化（测试 ID/需求/AT/数据集引用登记）。
 *
 * TearDown 三步（§6.1 原文）：
 *   ⑦ TestRecord 定稿写出 → ⑧ TempDir 清理（失败保留，§6.2）→ ⑨ 环境复位。
 *
 * 范围边界（本实现的两处预留，登记于 units/testkit.md 变更记录 v0.8）：
 *   - ⑥⑦ 两步依赖 TestRecord/Report（§7，TK-T10 交付物），本任务以注释占位
 *     接缝预留——TK-T10 落地后在此接线，夹具使用方接口不变；
 *   - ②③ 失败（数据集非法/环境不可用）"不进入测试体，结果分类见 §7.2"
 *     （§6.1 原文）——结果分类机制归 §7.2/TK-T10；当前以 GTEST_SKIP 占位
 *     （被跳过用例在报告中可见且不计入被测失败，语义与 §7.2"不是 Passed"
 *     一致），TK-T10 落地后替换为正式 outcome 分类。
 */
class GoldenFixture : public ::testing::Test {
protected:
    // ---- 配置点（派生类按需覆写； SetUp 前由基类调用）----

    /**
     * @brief 数据集引用（步骤②的装载目标）。
     *
     * @return 默认 {空,空}＝跳过步骤②③——纯算法用例（无需数据资产）不填；
     *         需要数据资产的用例覆写返回实际 {datasetId, version}
     */
    virtual DatasetRef datasetRef() const { return {}; }

    /// 随机种子（步骤④；默认 20260909——§6.3 固定默认值，无随机性测试也要可复现）。
    virtual std::uint64_t seed() const { return 20260909; }

    /// 线程数（步骤④；默认 1——多线程归约测试显式覆写，附录 D 第 8 项）。
    virtual int threadCount() const { return 1; }

    // ---- 六步产物（测试体直接使用；跳过对应步骤时保持空态）----

    std::optional<GoldenDataset> dataset;        ///< 步骤②产物（未要求数据集时为空）
    std::optional<ToleranceProfile> profile;     ///< 步骤③产物（跟随数据集档案引用）
    std::optional<DeterministicEnv> env;         ///< 步骤④产物（确定性上下文）
    std::unique_ptr<TempDir> workDir;            ///< 步骤⑤产物（测试写盘唯一合法位置）
    ReproRecord repro;                           ///< 本次测试的复现记录（env 的来源；
                                                 ///  ⑥⑦ 接线前即对测试体可见）

    /**
     * @brief SetUp：执行步骤①～⑤（⑥ 预留，见类注释范围边界）。
     *
     * 任一环境/数据步骤失败：不进入测试体（GTEST_SKIP 占位——正式分类随
     * TK-T10），跳过消息含步骤号与原因，保证报告可定位。
     */
    void SetUp() override
    {
        // 步骤①：数据根两级解析（env→编译默认）。失败＝环境不可用（§4.6），
        // 数据资产类用例无从继续——跳过而非计失败（§6.1"②③失败不进入测试体"）。
        std::filesystem::path root;
        try {
            root = goldenDataRoot();
        } catch (const TestKitError& e) {
            GTEST_SKIP() << "[golden-fixture 步骤①] 数据根不可解析: " << e.what();
        }

        // 步骤②③：数据集装载（含完整性）＋容差档案就绪。
        // datasetRef() 返回空 datasetId＝纯算法用例，两步整体跳过（产物保持空态）。
        const DatasetRef ref = datasetRef();
        if (!ref.datasetId.empty()) {
            // 步骤②：GoldenDataset::load——解析→schema→完整性→交叉校验（§5.1）。
            try {
                dataset = GoldenDataset::load(ref);
            } catch (const TestKitError& e) {
                GTEST_SKIP() << "[golden-fixture 步骤②] 数据集装载失败: " << e.what();
            }
            // 步骤③：容差档案——默认跟随数据集 manifest 的档案引用
            // （toleranceProfileRef()＝{id, version}）；目录布局＝
            // tolerance/<id>/v<version>.json（TK-T04 落位取舍）。
            const auto profileRef = dataset->toleranceProfileRef();
            const auto profilePath = root / "tolerance" / profileRef.first
                / ("v" + profileRef.second + ".json");
            try {
                profile = ToleranceProfile::load(profilePath);
            } catch (const TestKitError& e) {
                GTEST_SKIP() << "[golden-fixture 步骤③] 容差档案装载失败: " << e.what();
            }
            // 复现记录登记数据资产定位（§7.1：数据集引用进复现上下文）。
            repro.dataset = ref;
            repro.toleranceProfile
                = profileRef.first + "@" + profileRef.second;  // "<id>@<version>" 口径
        }

        // 步骤④：确定性环境（记录＝唯一来源；种子/线程来自配置点）。
        repro.seed = seed();
        repro.threadCount = threadCount();
        env = DeterministicEnv{repro};

        // 步骤⑤：TempDir（tag 固定 "golden"——隔离由 pid＋随机后缀保证，§6.2）。
        workDir = std::make_unique<TempDir>("golden");

        // 步骤⑥：TestRecord 初始化——预留接缝（TestRecord 归 §7/TK-T10；
        // 落地后在此以 repro＋需求/AT 登记初始化，接口不变）。
    }

    /**
     * @brief TearDown：执行步骤⑦～⑨（⑦ 预留，见类注释范围边界）。
     *
     * ⑧ 的失败保留语义：先向 TempDir 注入 gtest 失败信号（库本体零 gtest
     * ——D-06，信号必须由本 gtest 侧注入），再触发析构：失败→保留现场并
     * 打印保留路径；通过→递归删除。删除失败只告警不抛（§6.2）。
     */
    void TearDown() override
    {
        // 步骤⑦：TestRecord 定稿写出——预留接缝（TK-T10；落地后按 outcome
        // 分类写出，接缝位置不变）。

        // 步骤⑧：TempDir 清理（失败保留）。HasFailure() 含测试体与 SetUp 的
        // 非致命失败（gtest 语义）——与"测试失败保留现场"口径一致。
        if (workDir) {
            workDir->noteTestFailure(::testing::Test::HasFailure());
            workDir.reset();  // 析构内完成删除/保留决策（见 TempDir 契约）
        }

        // 步骤⑨：环境复位——显式逆序析构（档案→数据集→环境），保证依赖
        // 方向上后建的先拆（数据集/档案无运行期登记，此处为顺序确定性）。
        profile.reset();
        dataset.reset();
        env.reset();
    }
};

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_GTEST_GOLDENFIXTURE_HPP
