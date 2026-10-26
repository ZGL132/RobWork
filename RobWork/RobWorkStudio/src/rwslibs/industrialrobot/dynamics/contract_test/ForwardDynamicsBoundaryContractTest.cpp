/**
 * @file   ForwardDynamicsBoundaryContractTest.cpp
 * @brief  WP-09/REUSE-T53 契约测试——正动力学一致性面的单元边界契约：
 *         ODE 引擎会话消费的真实契约钉扎（GOV-T21 治理取代 P-DYN-7 自持
 *         约束后按其约束改写——"本批改成真实上游调用、输入和行为契约测试，
 *         不能把旧断言当新实现许可"）＋公共契约形态编译期核对（词表常量/
 *         四态枚举同源——消费面契约不漂移）。
 *
 * 设计依据：
 *   - units/dynamics.md §15.2 GOV-T21 行（P-DYN-7"自持 RK4、禁止 rwsim
 *     引擎"约束自该治理被取代——DYN-01/05 必须落到 RecursiveNewtonEuler /
 *     PhysicsEngine 真实路径；本文件改写为迁移后契约的执行面钉扎）、
 *     §6.3.6（仿真场景不触碰共享快照——会话私有物化副本口径）、§11.4
 *     （替身边界——静态扫描精神）、§3.1（R-1/R-2——include 白名单已由
 *     RneaBoundaryContractTest 全产品面用例覆盖，本文件不重复）
 *   - 任务契约 tasks/REUSE-T53.json（acceptance 4——词表同步放行＋断开
 *     钉扎；真实上游调用/行为 oracle 归 ForwardDynamicsTest 用例组——
 *     本文件为静态边界防线，两层互补）
 *   - 先例：RneaBoundaryContractTest.cpp（同款扫描形态——collectProduct
 *     Sources/stripComments 自持实现，T-1 扫描域为本单元源码树）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/dynamics/DynTypes.hpp>       // DynamicsValidity::ForwardCheckState（四态同源）
#include <sdurws/ird/dynamics/ForwardDynamics.hpp>  // kForwardIntegratorToken/ForwardCheckSettings
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using sdurws::ird::dynamics::DynamicsValidity;
using sdurws::ird::dynamics::ForwardCheckOutcome;
using sdurws::ird::dynamics::ForwardCheckSettings;
using sdurws::ird::dynamics::kForwardCheckAlgorithmVersion;
using sdurws::ird::dynamics::kForwardIntegratorToken;

namespace fs = std::filesystem;

namespace {

/// dynamics 单元树根（industrialrobot 目录——IRD_DYNAMICS_UNIT_ROOT 注入，
/// 与 RneaBoundaryContractTest 同源）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_DYNAMICS_UNIT_ROOT};
    return dir;
}

/// 收集正动力学面的产品源码文件（include/ForwardDynamics.hpp＋src/Forward
/// Dynamics.cpp——本契约的扫描对象；R-2 私有头禁令下 FD 面无其他文件）。
std::vector<fs::path> collectForwardDynamicsSources()
{
    std::vector<fs::path> files;
    const fs::path hpp = unitRoot() / "dynamics" / "include" / "sdurws" / "ird"
                         / "dynamics" / "ForwardDynamics.hpp";
    const fs::path cpp = unitRoot() / "dynamics" / "src" / "ForwardDynamics.cpp";
    for (const fs::path& p : {hpp, cpp}) {
        if (!fs::exists(p)) {
            // 文件缺失＝扫描失效面——显性失败（不静默跳过）。
            ADD_FAILURE() << "正动力学面文件不存在：" << p.string();
            continue;
        }
        files.push_back(p);
    }
    return files;
}

/// 剥离 // 行注释与 /* */ 块注释（词表扫描只针对**代码**——设计注释里
/// 提及被删内核的历史登记不是消费符号；RneaBoundaryContractTest 同款实现）。
std::string stripComments(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    enum class State { Code, LineComment, BlockComment } state = State::Code;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char next = (i + 1 < text.size()) ? text[i + 1] : '\0';
        switch (state) {
            case State::Code:
                if (c == '/' && next == '/') {
                    state = State::LineComment;
                    ++i;
                } else if (c == '/' && next == '*') {
                    state = State::BlockComment;
                    ++i;
                } else {
                    out.push_back(c);
                }
                break;
            case State::LineComment:
                if (c == '\n') {
                    out.push_back('\n');
                    state = State::Code;
                }
                break;
            case State::BlockComment:
                if (c == '*' && next == '/') {
                    ++i;
                    state = State::Code;
                }
                break;
        }
    }
    return out;
}

}  // namespace

// =====================================================================
// 用例 1：WP-09 迁移后的边界契约钉扎（双向）——
//   (a) 引擎会话消费在位：正动力学面必须消费 runtime 受控引擎会话工厂
//       （buildFdEngineSession/FdEngineSession 符号在代码中出现——迁移
//       缺席即回退到自持核，本断言红）；
//   (b) 自持旧核断开：已删除内核的符号（质量阵求解/列提取/状态缓冲）不
//       得在正动力学面代码中重现——平行核回流即红（断开实验的静态面，
//       与 ForwardDynamicsTest 的行为断开实验互补）。
// =====================================================================

TEST(DynForwardBoundary, ForwardDynamicsConsumesEngineSessionAndOldKernelDisconnected_REUSE_T53)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05"}, std::vector<std::string>{});
    // 本用例验证（GOV-T21 约束的执行面——迁移后契约钉扎）：
    //   (a) 正动力学面（ForwardDynamics.hpp/.cpp）剥注释后的代码中**必须**
    //       出现 runtime 引擎会话消费符号（WP-09 迁移在位；会话工厂构造权
    //       归 runtime——REUSE-T52 构造面裁决同墙复利，dynamics 经登记边
    //       消费）；
    //   (b) 已删除的旧自持内核符号**不得**重现（质量阵逐列提取/部分主元
    //       消元/状态缓冲——旧核三段的符号化指纹；任一命中＝平行核回流，
    //       H2 禁止平行实现的红线执行面）。
    // 真实引擎行为 oracle（解析基准/收敛/确定性/负载事件）由
    // ForwardDynamicsTest 用例组真实运行承载——本用例为静态边界防线。
    const char* required[] = {
        "buildFdEngineSession",   // runtime 工厂消费（会话物化唯一入口）
        "FdEngineSession",        // 会话类型消费（推进/读回面）
    };
    const char* forbidden[] = {
        "solveLinearSystem",      // 旧核：部分主元高斯消元（已删除）
        "kSingularPivotRelative", // 旧核：消元主元下界常量（已删除）
        "callRneaSample",         // 旧核：M/h 单样本提取消费面（已删除）
        "massFlat",               // 旧核：质量阵行主序缓冲（已删除）
    };

    const std::vector<fs::path> files = collectForwardDynamicsSources();
    ASSERT_FALSE(files.empty()) << "正动力学面为空——扫描失去对象";

    // (a) 会话消费在位——(hpp, cpp) 合并扫描至少出现一次（hpp 承载登记、
    //     cpp 承载消费；两者同失即迁移缺席）。
    {
        std::string merged;
        for (const fs::path& file : files) {
            std::ifstream in(file, std::ios::binary);
            ASSERT_TRUE(static_cast<bool>(in)) << "无法读取正动力学面文件：" << file.string();
            merged += stripComments(
                std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()});
        }
        for (std::string_view word : required) {
            EXPECT_NE(merged.find(word), std::string::npos)
                << "WP-09 迁移缺席：正动力学面代码未消费引擎会话符号 '" << word
                << "'——正动力学必须经 runtime 受控会话消费 ODE 引擎（GAP-FD-"
                   "BUILD-01 提案执行面），自持核不得回流";
        }
    }

    // (b) 旧核断开——逐文件独立扫描（任一文件重现旧核符号即红）。
    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(in)) << "无法读取正动力学面文件：" << file.string();
        const std::string code = stripComments(
            std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()});
        for (std::string_view word : forbidden) {
            EXPECT_EQ(code.find(word), std::string::npos)
                << "旧自持核回流：正动力学面代码重现已删除内核符号 '" << word
                << "'（" << file.string()
                << "）——质量阵/消元/RK4 内核已随 WP-09 删除，平行实现违反 H2";
        }
    }
}

// =====================================================================
// 用例 2：公共契约形态编译期核对——词表常量锁定值/算法版本/四态枚举同源/
// 配置默认保守（消费面契约不漂移的编译期钉扎）。
// =====================================================================

TEST(DynForwardBoundary, ForwardDynamicsPublicContractShape_REUSE_T53)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05", "NFR-MNT-03"},
                  std::vector<std::string>{});
    // 本用例验证（§4.2/§10.2 契约面的编译期不变量）：
    //   1. integratorToken 词表锁定值非空且与单元卡 §4.2 增量修订登记值
    //      一致（"ode-fixed"——WP-09 换核登记；字面量在本用例独立书写，
    //      与常量的失同步即失败——防两处漂移）；
    //   2. 算法实现版本随换核升版（"dyn-forward-check/2"——旧结果不与新
    //      结果混比，REUSE-T52 kRneaAlgorithmVersion 先例同款）；
    //   3. ForwardCheckState 四态与 DynamicsValidity 同源（同一枚举类型
    //      ——outcome.state 不是本头新造的平行枚举）；
    //   4. ForwardCheckSettings 默认 mode＝Skip（"未配置＝不执行"的保守
    //      缺省——§6.3.4 显式不适用语义）。
    EXPECT_FALSE(kForwardIntegratorToken.empty());
    EXPECT_STREQ(std::string{kForwardIntegratorToken}.c_str(), "ode-fixed")
        << "词表锁定值与单元卡 §4.2 登记值失同步（WP-09 换核登记）";
    EXPECT_STREQ(std::string{kForwardCheckAlgorithmVersion}.c_str(), "dyn-forward-check/2")
        << "算法版本 token 与换核登记失同步（旧结果不与新结果混比）";

    static_assert(std::is_same_v<decltype(ForwardCheckOutcome{}.state),
                                 DynamicsValidity::ForwardCheckState>,
                  "outcome.state 必须与 DynamicsValidity::ForwardCheckState 同型"
                  "（§4.4 四态唯一来源——禁平行枚举）");
    EXPECT_EQ(ForwardCheckOutcome{}.state, DynamicsValidity::ForwardCheckState::NotRun)
        << "outcome 默认态＝NotRun（未执行即未完成——不伪造 Passed）";
    EXPECT_EQ(ForwardCheckSettings{}.mode, ForwardCheckSettings::Mode::Skip)
        << "配置默认＝Skip（未配置不执行的保守缺省）";
}
