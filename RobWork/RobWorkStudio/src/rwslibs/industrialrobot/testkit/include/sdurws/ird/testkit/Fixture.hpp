/**
 * @file   Fixture.hpp
 * @brief  测试夹具与确定性环境（TempDir／ReproRecord／DeterministicEnv）。
 *
 * 设计依据：
 *   - units/testkit.md §6.1（夹具生命周期六步）、§6.2（临时目录与资源隔离）、
 *     §6.3（确定性环境与重放）；任务契约 tasks/foundation/TK-T08.json（≙WP-02-T08）
 *   - 需求 NFR-COR-02（可复现：等价集合＋稳定排序，不要求逐字节相同）
 *
 * 背景说明：本头文件交付 §6.2/§6.3 的三个可独立复用的夹具原语。
 * §6.1 的 GoldenFixture（gtest 集成形态）位于 gtest/GoldenFixture.hpp——
 * 库本体零 gtest（testkit.md §2.4 T-2/DTB §5.5 D-06：断言与 gtest 类型只在
 * 消费方 _test 目标翻译单元出现），与本单元 AssertMacros.hpp（TK-T05 交付）
 * 同一分层先例。
 *
 * 线程安全：TempDir 各实例独立（并行安全——目录按 pid＋tag＋随机后缀隔离，
 * §6.2）；ReproRecord/DeterministicEnv 为纯值类型（不可变共享安全）。
 */

#ifndef SDURWS_IRD_TESTKIT_FIXTURE_HPP
#define SDURWS_IRD_TESTKIT_FIXTURE_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <sdurws/ird/testkit/Dataset.hpp>     // DatasetRef（重放记录的数据集定位）
#include <sdurws/ird/testkit/TestPaths.hpp>   // TestKitError（错误类型，前置定义）

namespace sdurws::ird::testkit {

/**
 * @brief 测试专用临时目录（RAII；§6.2）。
 *
 * 生命周期：构造即在 <系统临时目录>/ird-test/<pid>-<tag>-<随机后缀> 建立目录
 * （父层 ird-test/ 一并按需创建）；析构递归删除（keepOnFailure 语义见下）。
 *
 * 资源隔离规则（§6.2 原文语义）：
 *   - 测试不得写共享位置（源码树/用户目录）——需要文件系统现面的被测代码
 *     一律指向本目录；
 *   - 并行测试（ctest -j）安全：目录按 pid＋tag＋随机后缀隔离，同名 tag 的
 *     并发实例互不串扰。
 *
 * 失败保留语义（§6.2 keepOnFailure）：默认 true——测试失败时保留现场（目录
 * 不删除，并向 stderr 打印保留路径；"报告 artifacts 登记"的字段化输出随
 * TK-T10 Report 落地，当前以 stderr 行为过渡形态）。库本体不依赖 gtest
 * （D-06），"当前测试失败"这一信号无法由库自行观测，因此提供
 * noteTestFailure(bool) 注入接缝——gtest 侧 GoldenFixture 在 TearDown 里以
 * HasFailure() 调用（增量接缝：设计原文仅列 keepOnFailure 一项；本接缝为
 * TK-T08 实现细节级登记，units/testkit.md 变更记录 v0.8，TK-T01
 * detail::resolveDataRoot 同款处理）。
 *
 * 异常语义：构造失败（无法建立目录）＝环境错误，fail-fast 抛
 * TestKitError(env-unavailable)；析构删除失败只记录告警不抛出（§6.2 原文：
 * "删除失败记录告警不抛出"——析构路径抛错会遮蔽测试本身的失败）。
 */
class TempDir {
public:
    /**
     * @brief 建立临时目录（构造即存在）。
     *
     * @param tag [in] 目录名语义标签（仅允许 [A-Za-z0-9._-]，用于人读辨识；
     *               非法字符抛 TestKitError(usage)——防标签注入路径分隔符）
     *
     * @throws TestKitError(usage)            tag 为空或含非法字符
     * @throws TestKitError(env-unavailable)  系统临时目录不可得/目录建立失败
     */
    explicit TempDir(std::string_view tag);

    /**
     * @brief RAII 清理：按 keepOnFailure＋失败信号决定删除或保留。
     *
     * 删除走递归删除（std::filesystem::remove_all＋error_code，不抛）；
     * 删除失败时向 stderr 打印告警行（含路径与 error_code.message()）后返回。
     * 保留时打印 "ird-tempdir-kept: <路径>"（TK-T10 Report 落地前的人工辨识形态）。
     */
    ~TempDir();

    TempDir(const TempDir&) = delete;             ///< 独占资源（目录）——禁拷贝
    TempDir& operator=(const TempDir&) = delete;  ///< 同上
    TempDir(TempDir&&) = delete;                  ///< 移动会转移"析构责任"而无收益——禁移动
    TempDir& operator=(TempDir&&) = delete;       ///< 同上

    /**
     * @brief 临时目录绝对路径（构造成功后恒有效；本对象存活期内不变）。
     *
     * @return 目录路径（<系统临时目录>/ird-test/<pid>-<tag>-<随机后缀>）
     */
    const std::filesystem::path& path() const noexcept;

    /**
     * @brief 设置"失败保留"开关（§6.2；默认 true）。
     *
     * @param keep [in] true＝测试失败时保留现场；false＝析构无条件删除
     *                  （例如对失败现场无排查价值的纯临时用例显式关闭）
     */
    void keepOnFailure(bool keep) noexcept;

    /**
     * @brief 注入"当前测试失败"信号（增量接缝；见类注释）。
     *
     * @param failed [in] true＝当前测试已失败（gtest 侧传 HasFailure()）；
     *                    false＝未失败。该信号只影响下一次析构的保留决策。
     */
    void noteTestFailure(bool failed) noexcept;

private:
    std::filesystem::path path_;   ///< 临时目录绝对路径（构造后不变）
    bool keepOnFailure_ = true;    ///< 失败保留开关（§6.2 默认 true）
    bool testFailed_ = false;      ///< 最近一次注入的失败信号（初值 false＝未失败）
};

/**
 * @brief 复现记录（§6.3/§7.1）：测试确定性上下文的唯一来源，亦随 TestRecord
 *        持久化（持久化接线随 TK-T10）。
 *
 * 默认值口径（§6.3 原文）：seed 默认 20260909（固定值——无随机性测试也要
 * 可复现）；threadCount 默认 1（多线程归约测试显式设值，附录 D 第 8 项）。
 * softwareVersion/gitCommit 由 CI 注入或编译期宏提供（IRD_GIT_COMMIT——宏注入
 * 随 CI 通道接线登记，当前留空串为合法值）。
 *
 * JSON 形态（toJson/fromJson）：跨进程复现载体（附录 A.4——ReproRecord JSON
 * 可独立传给另一次运行）。序列化经 JsonLite（TK-T02，受限 JSON 唯一入口）。
 */
struct ReproRecord {
    std::uint64_t seed = 20260909;   ///< 随机种子（无单位整数；默认 20260909 固定值）
    int threadCount = 1;             ///< 线程数（≥1；多线程归约测试显式设值）
    std::string softwareVersion;     ///< 被测目标版本（CI 注入或编译期宏；可空）
    std::string gitCommit;           ///< 编译期宏 IRD_GIT_COMMIT（可空）
    DatasetRef dataset;              ///< 数据集定位 {datasetId, version}（可空＝无数据集）
    std::string toleranceProfile;    ///< 容差档案 "<id>@<version>"（可空）
    std::string notes;               ///< 人工备注（可空）

    /// 值相等（逐字段；确定性口径——不引入容差语义）。
    bool operator==(const ReproRecord& o) const noexcept;
    bool operator!=(const ReproRecord& o) const noexcept { return !(*this == o); }

    /**
     * @brief 序列化为 JSON 对象文本（键序固定：seed→threadCount→softwareVersion
     *        →gitCommit→dataset→toleranceProfile→notes——确定性输出，NFR-COR-02）。
     *
     * @return 单行 JSON 对象（键序＝上列固定序；dataset 空时序列化为
     *         {"datasetId":"","version":""}——键恒在，避免消费端双态解析）
     */
    std::string toJson() const;

    /**
     * @brief 从 JSON 文本反序列化（跨进程复现入口，附录 A.4）。
     *
     * @param text [in] toJson() 形态的 JSON 对象文本（允许任意键序——解析按
     *                  键名取值，与写出端固定键序无关）
     *
     * @throws TestKitError(dataset-invalid) 文本非法 JSON，或任一字段缺失/
     *         类型不符/越界（threadCount < 1）——消息含 "repro-json:" 前缀
     *         与字段路径（§5 错误契约：拒绝必须可定位）
     */
    static ReproRecord fromJson(std::string_view text);
};

/**
 * @brief 确定性环境（§6.3）：持有 ReproRecord 作为本次测试确定性上下文的
 *        唯一来源。
 *
 * 约束（§6.3 原文）：种子经被测代码的注入点传入（各域 solver 接口显式收
 * seed），testkit 不改写全局 rand；线程数经被测执行接口显式设置——本类只是
 * "记录的持有者与传递者"，不做任何全局环境改写。
 *
 * 重放语义（NFR-COR-02/附录 D 第 8 项，消费端义务）：同一 ReproRecord 下两次
 * 运行，结果集合等价＋稳定排序一致＋逐元素在档案容差内——不要求浮点文件
 * 逐字节相同。集合/顺序断言（§5.4）即该语义的判定工具。
 */
class DeterministicEnv {
public:
    /**
     * @brief 以给定记录建立确定性环境。
     *
     * @param r [in] 复现记录（按值持有——记录＝唯一来源，外部后续修改不生效）
     */
    explicit DeterministicEnv(ReproRecord r) noexcept;

    /// 只读访问本次测试的确定性上下文（本对象存活期内引用恒有效）。
    const ReproRecord& record() const noexcept;

private:
    ReproRecord record_;   ///< 确定性上下文（构造后不可变——"唯一来源"语义）
};

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_FIXTURE_HPP
