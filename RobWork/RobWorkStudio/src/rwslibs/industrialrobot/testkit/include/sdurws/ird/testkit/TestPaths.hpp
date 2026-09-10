/**
 * @file   TestPaths.hpp
 * @brief  黄金数据根定位——环境变量/编译期默认两级解析（testkit 首个公共头）。
 *
 * 设计依据：
 *   - units/testkit.md §4.6（数据根定位：SDURWS_IRD_TESTDATA_DIR 环境变量
 *     优先，回落编译期 CMake 默认；未设环境变量且编译默认不存在→
 *     TestKitError(env-unavailable)）、§3.6（编译期默认＝
 *     industrialrobot/testdata，经 CMake 缓存变量 SDURWS_IRD_TESTDATA_DIR）
 *   - 任务契约 tasks/foundation/TK-T01.json（≙WP-02-T01，units/testkit.md §9
 *     TK-T01 行）：testdata 根变量落位＋TestPaths.hpp＋三态解析用例
 *   - 需求 NFR-COR-01（可复现对照的数据载体——黄金数据集的定位入口）
 *
 * 背景说明：各业务单元的黄金数据集（解析算例/独立参考实现输出等）集中存放
 * 于 industrialrobot/testdata/（源内默认，CI 开箱可用）；数据集外置（大数据
 * 集或制品通道）时仅需设置一次环境变量 SDURWS_IRD_TESTDATA_DIR 覆写
 * （决策 D-05）。本头是全部数据消费（TK-T03 Dataset 装载、TK-T04 容差档案）
 * 的路径解析唯一入口——任何单元不得自行拼装数据根路径。
 *
 * 落位说明（TK-T01 与单元卡的偏差登记，见 testkit.md 变更记录 v0.3）：
 *   1. TestKitError 原设计位于 Dataset.hpp（testkit.md §5），但 Dataset 随
 *      TK-T03 落地而 TestPaths（TK-T01）先需要 env-unavailable 错误，故
 *      前置定义于本头；Dataset.hpp 落地时将包含本头而非重复定义——错误
 *      类型的语义与 testkit.md §5 完全一致（五枚举值原样冻结）。
 *   2. detail::resolveDataRoot 是为"env/默认/缺失三态"可测性增设的纯函数
 *      接缝（编译默认目录的存在性无法在测试运行期改写，公共 API 只能覆盖
 *      其中两态）；它是内部实现细节，不构成稳定契约，签名可随实现调整。
 */

#ifndef SDURWS_IRD_TESTKIT_TESTPATHS_HPP
#define SDURWS_IRD_TESTKIT_TESTPATHS_HPP

#include <filesystem>  // C++17：路径类型（testkit.md §1.4 允许 std::filesystem）
#include <stdexcept>   // std::runtime_error 基类
#include <string>
#include <string_view>

namespace sdurws::ird::testkit {

/**
 * @brief testkit 全单元共用的错误分类（语义冻结自 testkit.md §5/§7.2）。
 *
 * 五类错误对应测试结果四类状态中的"数据集非法"（DatasetInvalid/
 * ToleranceUndefined/UnitMismatch）与"环境不可用"（EnvUnavailable）以及
 * 调用方误用（Usage）。分类的用途：夹具据此把失败分流为"数据资产缺陷"
 * 或"环境不可用"，而不是笼统计为测试失败——数据损坏不得伪装成算法回归。
 */
enum class TestKitErrorKind {
    DatasetInvalid,      ///< 清单/档案/完整性校验失败——数据资产缺陷
    ToleranceUndefined,  ///< fieldPath 无对应容差条目（附录 D C4：报错不默认）
    UnitMismatch,        ///< 单位 token 未注册或量纲不符——数据集非法级
    EnvUnavailable,      ///< 数据根缺失/环境不可用（本头的 goldenDataRoot 使用）
    Usage                ///< 调用方契约违约（如集合断言超规模护栏）——fail-fast
};

/**
 * @brief testkit 统一错误类型（唯一允许从 testkit 接口抛出的异常）。
 *
 * 生命周期与值语义：按值抛出/捕获，无可变共享状态；拷贝移动均安全。
 * what() 携带"<kind 助记码>: <detail>"前缀，助记码与 testkit.md §7.2
 * 结果分类使用的机器前缀一致（dataset-invalid / tolerance-undefined /
 * unit-mismatch / env-unavailable / usage），便于日志与报告机器判读。
 *
 * 线程安全：构造与只读访问并发安全；无全局状态。
 */
class TestKitError : public std::runtime_error {
public:
    /**
     * @brief 构造带分类的错误。
     *
     * @param kind   [in] 错误分类（五枚举值之一）
     * @param detail [in] 人读说明（建议含字段路径/目录等定位信息，中文描述）
     */
    TestKitError(TestKitErrorKind kind, std::string detail);

    /**
     * @brief 取错误分类。
     *
     * @return 构造时传入的分类，永不抛出（noexcept）
     */
    TestKitErrorKind kind() const noexcept;

    /**
     * @brief 分类→机器可读助记码（dataset-invalid 等，testkit.md §7.2 前缀）。
     *
     * 报告写出与失败消息复用同一映射，保证助记码单点维护不漂移。
     *
     * @param kind [in] 错误分类
     * @return 对应的小写连字符助记码（静态串，无所有权转移）
     */
    static const char* toToken(TestKitErrorKind kind) noexcept;

private:
    TestKitErrorKind kind_;  ///< 分类留存（what() 已含助记码，此字段供程序判读）
};

namespace detail {

/**
 * @brief 数据根两级解析的纯函数核心（公共 API goldenDataRoot 的可测接缝）。
 *
 * 规则（testkit.md §4.6 原文语义）：
 *   1. envValue 非空 → 直接返回该值（信任环境变量；env 指向的目录不存在
 *      属于后续数据装载阶段要暴露的问题，本层不做二次校验——CI 设错变量
 *      将在 Dataset::load 时以 EnvUnavailable 失败，不会静默通过）；
 *      envValue 为空串视为未设置（防御 CI 空值配置，也是 Windows 测试
 *      清除环境变量的等价手段——MSVC CRT 无 unsetenv）。
 *   2. envValue 空（未设）→ 回落 compiledDefault；该目录必须真实存在，
 *      不存在即抛 TestKitError(env-unavailable)——"源内默认开箱可用"
 *      的前提被破坏（仓库不完整/路径配错），属环境错误而非测试失败。
 *
 * @param envValue        [in] std::getenv("SDURWS_IRD_TESTDATA_DIR") 的返回值，
 *                        可为 nullptr（未设）；空串按未设处理
 * @param compiledDefault [in] 编译期默认数据根（CMake 变量
 *                        SDURWS_IRD_TESTDATA_DIR 经编译定义注入）
 *
 * @return 解析出的数据根（不保证存在性——env 态返回值未校验，见规则 1）
 *
 * @throws TestKitError(env-unavailable) 当 envValue 为空且 compiledDefault
 *         目录不存在（含 compiledDefault 为空路径的情形）
 *
 * 确定性：纯函数（无环境读写、无时间戳），同输入同输出（NFR-COR-02 精神）。
 */
std::filesystem::path resolveDataRoot(
    const char* envValue, const std::filesystem::path& compiledDefault);

}  // namespace detail

/**
 * @brief 解析黄金数据集根目录（环境变量优先，回落编译期默认）。
 *
 * 环境变量名：SDURWS_IRD_TESTDATA_DIR（与 CMake 缓存变量同名，见
 * testkit/CMakeLists.txt）。实现委托 detail::resolveDataRoot（规则见其
 * 注释）。CI 使用方式：源内默认即开箱可用；外置数据时设一次变量
 * （testkit.md §7.5）。
 *
 * @return 数据根绝对/规范路径（编译默认来自 CMake 注入的正斜杠绝对路径；
 *         env 态返回环境变量原值，未做词法归一——保证与调用方设置的路径
 *         逐字符一致，便于断言与日志比对）
 *
 * @throws TestKitError(env-unavailable) 未设环境变量且编译默认目录不存在
 *         （如仓库 testdata/ 实体尚未随首个数据集任务建立时）
 *
 * 线程安全：并发只读安全（std::getenv 只读）；测试修改环境变量与本调用
 * 之间存在数据竞争，因此 env 相关用例须串行执行（本测试文件内天然串行）。
 */
std::filesystem::path goldenDataRoot();

/**
 * @brief 数据集目录＝<数据根>/golden/<datasetId>（testkit.md §3.4 布局）。
 *
 * 仅做路径拼装，不校验目录存在与 datasetId 合法性——datasetId 句法
 * （[a-z0-9-]{3,64}）与目录一致性校验归 TK-T03 的清单装载（§4.2.2）；
 * 空串 id 会返回 golden/ 目录本身，调用方（装载层）负责拒绝。
 *
 * @param datasetId [in] 数据集标识（合法句法见 testkit.md §4.2.2；前置：
 *                  非空且合法，本函数不做句法校验）
 *
 * @return 数据集版本目录的父目录（manifest.json 位于其下）
 *
 * @throws TestKitError(env-unavailable) 数据根不可解析时（透传 goldenDataRoot）
 */
std::filesystem::path datasetDir(std::string_view datasetId);

/**
 * @brief 容差档案目录＝<数据根>/tolerance/<profileId>（testkit.md §3.4）。
 *
 * 与 datasetDir 同款：纯拼装，profileId 句法校验归 TK-T04 档案装载。
 *
 * @param profileId [in] 档案标识（前置：非空且合法，本函数不做句法校验）
 *
 * @return 档案版本目录的父目录（v<version>.json 位于其下）
 *
 * @throws TestKitError(env-unavailable) 数据根不可解析时（透传 goldenDataRoot）
 */
std::filesystem::path toleranceProfileDir(std::string_view profileId);

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_TESTPATHS_HPP
