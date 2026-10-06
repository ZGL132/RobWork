/**
 * @file   DiagCodesTest.cpp
 * @brief  drivetrain 稳定诊断码登记表用例组（DtDiagCodes）——DT-* 19 码
 *         全表清单纪律、码值句法（经 core 契约权威校验）与登记出处完整
 *         性（任务契约 WP-18-T02 acceptance 2"DT-* 域诊断码按
 *         units/drivetrain.md 登记表装配期注册"的实现侧自证面）。
 *
 * 设计依据：
 *   - units/drivetrain.md §6.3/§7.2/§9/§10/§12.1（DT-* 登记值散布各节
 *     ——全表 19 码的字面清单即其机器核对面）、§1.3/D-DT-15（DT-* 建议
 *     值；注册归 diagnostics；P-DT-7）、§5.3（c＝Δq_joint/Δθ_motor 口径
 *     ——P-DT-10）
 *   - units/core.md §4.8（DiagCode 句法 ^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64
 *     ——core 仅承载；CR-08 码值权威在 diagnostics）＋core/DiagData.hpp
 *     （DiagnosticRecord::make 的 C-3 句法校验＝core 侧句法权威执行点）
 *   - 收编确认锚点（如实声明，kinematics/DiagCodesTest.cpp 同款形态、
 *     不同执行面）：DT-* 码进入 diagnostics 全局装配（L5 装配清单注册）
 *     属 diagnostics 所有者的治理动作（P-DT-7——diagnostics.md §4.5
 *     前缀表补登 DT 前缀＋代码前缀表同步），两处文件均不在本任务
 *     allowedFiles；且 drivetrain 依赖白名单仅 core＋evidence（卡 §3.2
 *     点名 diagnostics＝表外边），本单元产品面与测试面均不可 include
 *     diagnostics 头、不可触真实 StableCodeRegistry——因此本组用例以
 *     core 句法权威（DiagnosticRecord::make C-3）验证码值合法性面，
 *     **不以桩伪造"已注册"结论**（登记表→StableCodeRegistry 的注册
 *     闭环随 P-DT-7 收编动作执行，届时由 L5 装配按本登记表逐行注册——
 *     诚实边界，未执行的验证不标注通过）。
 *   - 任务契约 tasks/foundation/WP-18-T02.json acceptance 2
 */

#include <sdurws/ird/drivetrain/DiagCodes.hpp>

#include <sdurws/ird/core/DiagData.hpp>  // core::DiagnosticRecord——句法权威（C-3 校验）
#include <sdurws/ird/core/Errors.hpp>    // core::CoreError——句法违约异常类型
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using sdurws::ird::core::DiagnosticRecord;
using sdurws::ird::drivetrain::DiagnosticEntry;
using sdurws::ird::drivetrain::drivetrainCodeEntries;

namespace {

/// units/drivetrain.md 登记表的**应登记码面**（测试内自持字面清单——与
/// 实现清单机械比对，任一侧漂移即失败：失同步防线）。行序＝卡面章节序
/// （§6.3 表行 1~8 → §7.2 新增 4 码 → §9 两码 → §10 四码 → §12.1 一码
/// ——实现清单序＝确定性序的依据面）；卡面增码（随消费任务）时在实现
/// 清单与本清单表尾同步追加并走单元卡增量修订（WP-18-T03/T05 产码路径
/// 如需新码照此办理——既有行不重排）。
const char* kUnitCardFullTable[] = {
    "DT-COUPLING-STAGE-LOCKED",           // §6.3 行 1（R1 能力门控）
    "DT-MATRIX-NONDIAGONAL-LOCKED",       // §6.3 行 2（非对角阻断）
    "DT-AXIS-TYPE-OUT-OF-SCOPE",          // §6.3 行 3（链型/关节类型范围外）
    "DT-RATIO-ZERO",                      // §6.3 行 4（c=0 非法）
    "DT-MATRIX-NONFINITE",                // §6.3 行 5＝§7.2（非有限）
    "DT-INPUT-DIMENSION-MISMATCH",        // §6.3 行 6＝§7.2（维度不匹配）
    "DT-INPUT-AXIS-ORDER-MISMATCH",       // §6.3 行 7（轴序不一致）
    "DT-INPUT-EMPTY",                     // §6.3 行 8（空输入 fail-fast）
    "DT-MATRIX-NONSQUARE",                // §7.2（非方矩阵）
    "DT-MATRIX-SINGULAR",                 // §7.2（奇异——不伪逆放行）
    "DT-MATRIX-ILL-CONDITIONED",          // §7.2（病态——P-RT-7 阈值）
    "DT-MATRIX-TIME-VARYING-UNSUPPORTED", // §7.2（时变矩阵）
    "DT-INERTIA-INVALID",                 // §9.2（转子惯量非法）
    "DT-INERTIA-NOT-POSITIVE-DEFINITE",   // §9.3（反射惯量非正定）
    "DT-EFFICIENCY-INVALID",              // §10.1（效率越界）
    "DT-ROTOR-MISSING",                   // §6.2/§10.7（转子缺失降级）
    "DT-INPUT-SAMPLE-MISSING",            // §10.4（缺样本降级）
    "DT-INPUT-TIME-NONMONOTONIC",         // §10.4/§12.1（时间非单调）
    "DT-SERIES-LENGTH-MISMATCH",          // §12.1（序列长度不一致）
};

/// 登记表码数（卡面全集 19——实现清单与本清单同长断言的期望值）。
constexpr std::size_t kExpectedCodeCount
    = sizeof(kUnitCardFullTable) / sizeof(kUnitCardFullTable[0]);

}  // namespace

/**
 * 全表清单纪律（acceptance 2——"按 units/drivetrain.md 登记表"）：实现
 * 清单与卡面字面清单逐行机械比对（同长＋同序＋同值）；无重复码（键唯一
 * ——diagnostics 注册协议 §4.5 的前置面）；全表 DT- 前缀（D-DT-15——
 * 前缀即所有权声明，drivetrain 域码不跨前缀）。
 */
TEST(DtDiagCodes, FullTableMatchesUnitCard_WP18T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{"AT-38"});

    const std::vector<DiagnosticEntry> entries = drivetrainCodeEntries();

    // 同长：实现清单不许多登（私造码值——NFR-MNT-03）也不许漏登
    //（登记表不完整——装配期注册的数据源缺口）。
    ASSERT_EQ(entries.size(), kExpectedCodeCount)
        << "DT-* 登记清单与 units/drivetrain.md 登记表行数不一致（实现 "
        << entries.size() << " vs 卡面 " << kExpectedCodeCount << "）";

    // 同序＋同值：清单序＝卡面章节序（确定性序——NFR-COR-02），逐行
    // 机械比对（任一侧漂移即失败）。
    for (std::size_t i = 0; i < kExpectedCodeCount; ++i) {
        EXPECT_EQ(entries[i].code, kUnitCardFullTable[i])
            << "行 " << i << " 码值漂移（实现清单序须与卡面章节序一致）";
    }

    // 无重复：键唯一是 diagnostics 注册协议（§4.5）的前置——重复码在
    // 登记表层面即暴露，不等注册期才失败。
    for (std::size_t i = 0; i < entries.size(); ++i) {
        for (std::size_t j = i + 1; j < entries.size(); ++j) {
            EXPECT_NE(entries[i].code, entries[j].code)
                << "重复码值（键唯一前置——§4.5）: " << entries[i].code;
        }
    }

    // 全表 DT- 前缀：D-DT-15 前缀即所有权声明——drivetrain 登记表不得
    // 混入他单元前缀（P-DT-7 收编时前缀表按 DT→drivetrain 校验）。
    for (const auto& entry : entries) {
        EXPECT_EQ(entry.code.substr(0, 3), std::string_view{"DT-"})
            << "非 DT- 前缀码混入登记表（D-DT-15）: " << entry.code;
    }
}

/**
 * 码值句法经 core 契约权威校验（acceptance 2——句法合法性面）：逐码以
 * core::DiagnosticRecord::make 构造完整诊断记录——C-3 校验（句法
 * ^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64，违约抛 CoreError("core/diag/code:")）
 * 即 core 侧句法权威执行点；本单元依赖白名单无 diagnostics 边（卡
 * §3.2），真实 StableCodeRegistry 的前缀-所有权校验随 P-DT-7 收编后在
 * L5 装配期执行（文件头注"收编确认锚点"——不以桩伪造注册结论）。
 *
 * 同用例并断言"唯一书写点"纪律：登记清单内码值必须逐字等于 DiagCodes.hpp
 * 常量（禁第二处字面量——常量与清单失同步即失败）。
 */
TEST(DtDiagCodes, CodeSyntaxViaCoreContract_WP18T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-COR-03"},
                  std::vector<std::string>{});

    const std::vector<DiagnosticEntry> entries = drivetrainCodeEntries();
    ASSERT_FALSE(entries.empty()) << "登记表为空（扫描失效防线）";

    for (const auto& entry : entries) {
        // C-3 句法权威：make() 内部校验 code 句法/长度＋必填串非空——
        // 句法非法即抛 CoreError，用例失败（core.md §4.8 CR-08：core 仅
        // 承载句法，本断言即"句法承载"的机器面）。context/cause/action
        // 以非空占位语义串满足必填校验（本用例只验证码值面）。
        EXPECT_NO_THROW({
            DiagnosticRecord::make(
                std::string{entry.code},
                {},                              // subject 可空（瞬时开发诊断）
                {},                              // localName 可空
                {},                              // runtimeName 可空
                "drivetrain 登记表句法校验",      // context（非空）
                "DT-* 登记表用例校验桩",          // cause（非空）
                "无——句法验证用例");              // recommendedAction（非空）
        }) << "码值句法违约（core §4.8 ^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64）: "
           << entry.code;
    }

    // 唯一书写点：常量与登记清单逐字一致（禁字符串拼码/第二处字面量
    // ——T03+ 产码路径引用常量，登记表引用同一常量，两处失同步即实现
    // 缺陷）。
    EXPECT_EQ(entries[0].code, sdurws::ird::drivetrain::kDtCouplingStageLocked);
    EXPECT_EQ(entries[8].code, sdurws::ird::drivetrain::kDtMatrixNonsquare);
    EXPECT_EQ(entries[18].code, sdurws::ird::drivetrain::kDtSeriesLengthMismatch);
}

/**
 * 登记出处完整性（acceptance 2——登记表可追溯面）：每条登记行必须携带
 * 卡面出处（units/drivetrain.md §x.y 锚点——review 时可对照文档）与
 * 非空语义登记（NFR-MNT-03：码的语义权威在卡面，物化不丢锚点）。
 */
TEST(DtDiagCodes, EntriesCarryCardProvenance_WP18T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-03"},
                  std::vector<std::string>{});

    const std::vector<DiagnosticEntry> entries = drivetrainCodeEntries();
    ASSERT_FALSE(entries.empty()) << "登记表为空（扫描失效防线）";

    for (const auto& entry : entries) {
        // 出处锚点：统一以卡面路径开头（可追溯——AGENTS §2.4"与设计
        // 文档建立追溯"的登记表面）。
        EXPECT_EQ(entry.sourceClause.substr(0, std::string_view{"units/drivetrain.md "}.size()),
                  std::string_view{"units/drivetrain.md "})
            << "登记行缺卡面出处锚点: " << entry.code;
        // 出处含章节号（§ 引导——防"有路径无章节"的退化登记）。
        EXPECT_NE(entry.sourceClause.find("§"), std::string_view::npos)
            << "出处锚点缺章节号: " << entry.code;
        // 语义登记非空：空语义＝登记表退化为码表（丢失卡面处置口径）。
        EXPECT_FALSE(entry.semantics.empty())
            << "登记行语义为空: " << entry.code;
    }
}
