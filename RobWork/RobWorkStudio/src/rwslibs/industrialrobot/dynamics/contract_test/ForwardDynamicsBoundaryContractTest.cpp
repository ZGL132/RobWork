/**
 * @file   ForwardDynamicsBoundaryContractTest.cpp
 * @brief  WP-17-T05 契约测试——正动力学一致性面的单元边界契约：P-DYN-7
 *         落位选型（本域确定性 RK4 引擎）的产品面静态钉扎（零 rwsim 物理
 *         引擎消费符号）＋公共契约形态编译期核对（词表常量/四态枚举同源
 *         ——消费面契约不漂移）。
 *
 * 设计依据：
 *   - units/dynamics.md §6.3.6（仿真场景不触碰共享快照——rwsim 场景对象
 *     的接入形态受选型约束）、§10.2（R1/R2 行"rwsim 引擎类选型为 T05
 *     落位验证项——P-DYN-7"）、§11.4（替身边界——静态扫描精神）、§3.1
 *     （R-1/R-2——include 白名单已由 RneaBoundaryContractTest 全产品面
 *     用例覆盖，本文件不再重复）
 *   - 任务契约 tasks/foundation/WP-17-T05.json（acceptance 1 的对端静态
 *     防线——引擎选型落位后以词表扫描钉住，防后续批次静默引入引擎依赖）
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
#include <type_traits>
#include <vector>

using sdurws::ird::dynamics::DynamicsValidity;
using sdurws::ird::dynamics::ForwardCheckOutcome;
using sdurws::ird::dynamics::ForwardCheckSettings;
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
/// 提及"PhysicsEngine 选型依据"不是消费符号；RneaBoundaryContractTest
/// 同款实现）。
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
// 用例 1：P-DYN-7 落位选型的产品面静态钉扎——正动力学面零 rwsim 物理引擎
// 消费符号（引擎＝本域确定性 RK4；rwsim 引擎接入须先走单元卡增量修订）。
// =====================================================================

TEST(DynForwardBoundary, ForwardDynamicsFaceZeroRwsimEngineSymbols_WP17T05)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05"}, std::vector<std::string>{});
    // 本用例验证（§10.2 R1/R2 行——P-DYN-7 落位选型的执行面钉扎）：正动力
    // 学面（ForwardDynamics.hpp/.cpp）剥注释后的代码中不出现任何 rwsim
    // 物理引擎消费符号。选型登记（单元卡 §1.2 T05 注）：本域引擎＝同源
    // RNEA＋固定步长 RK4；rwsim 引擎（PhysicsEngine 工厂/BtSimulator/
    // DynamicWorkCell 场景）不落位的四条实测依据见 ForwardDynamics.hpp
    // 文件头——后续批次若引入引擎，须先走单元卡增量修订（P-DYN-7 词表
    // 变更）并同步本词表，静态防线保证"文档先行、代码后动"。
    //
    // 词表来源（rwsim 引擎接入的符号化指纹；任一命中即选型越位）：
    //   - 引擎工厂/实现类：PhysicsEngine（rwsim::simulator 工厂）、
    //     BtSimulator（Bullet 引擎实现类）、DynamicSimulator（老仿真器）；
    //   - 场景对象：DynamicWorkCell（DWC 场景消费——选型外路径）、
    //     RigidDevice（引擎侧设备句柄）、makeState（快照 State 工厂——
    //     §6.3.6 的消费面受选型约束，本域零 State 句柄）；
    //   - 命名空间指纹：namespace rwsim（rwsim 头包含即命中）。
    const char* forbidden[] = {
        "PhysicsEngine", "BtSimulator", "DynamicSimulator", "DynamicWorkCell",
        "RigidDevice", "makeState", "namespace rwsim",
    };

    const std::vector<fs::path> files = collectForwardDynamicsSources();
    ASSERT_FALSE(files.empty()) << "正动力学面为空——扫描失去对象";

    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(in)) << "无法读取正动力学面文件：" << file.string();
        const std::string code = stripComments(
            std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()});
        for (const char* word : forbidden) {
            EXPECT_EQ(code.find(word), std::string::npos)
                << "P-DYN-7 选型越位：正动力学面代码出现 rwsim 引擎消费符号 '"
                << word << "'（" << file.string()
                << "）——引擎选型＝本域确定性 RK4，rwsim 接入须先单元卡增量修订";
        }
    }
}

// =====================================================================
// 用例 2：公共契约形态编译期核对——词表常量锁定值/四态枚举同源/配置
// 默认保守（消费面契约不漂移的编译期钉扎）。
// =====================================================================

TEST(DynForwardBoundary, ForwardDynamicsPublicContractShape_WP17T05)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05", "NFR-MNT-03"},
                  std::vector<std::string>{});
    // 本用例验证（§4.2/§10.2 契约面的编译期不变量）：
    //   1. integratorToken 词表锁定值非空且与单元卡 §4.2 增量修订登记值
    //      一致（"rk4-fixed"——P-DYN-7 落位锁定；字面量在本用例独立书写，
    //      与常量的失同步即失败——防两处漂移）；
    //   2. ForwardCheckState 四态与 DynamicsValidity 同源（同一枚举类型
    //      ——outcome.state 不是本头新造的平行枚举）；
    //   3. ForwardCheckSettings 默认 mode＝Skip（"未配置＝不执行"的保守
    //      缺省——§6.3.4 显式不适用语义）。
    EXPECT_FALSE(kForwardIntegratorToken.empty());
    EXPECT_STREQ(std::string{kForwardIntegratorToken}.c_str(), "rk4-fixed")
        << "词表锁定值与单元卡 §4.2 登记值失同步（P-DYN-7 落位登记）";

    static_assert(std::is_same_v<decltype(ForwardCheckOutcome{}.state),
                                 DynamicsValidity::ForwardCheckState>,
                  "outcome.state 必须与 DynamicsValidity::ForwardCheckState 同型"
                  "（§4.4 四态唯一来源——禁平行枚举）");
    EXPECT_EQ(ForwardCheckOutcome{}.state, DynamicsValidity::ForwardCheckState::NotRun)
        << "outcome 默认态＝NotRun（未执行即未完成——不伪造 Passed）";
    EXPECT_EQ(ForwardCheckSettings{}.mode, ForwardCheckSettings::Mode::Skip)
        << "配置默认＝Skip（未配置不执行的保守缺省）";
}
