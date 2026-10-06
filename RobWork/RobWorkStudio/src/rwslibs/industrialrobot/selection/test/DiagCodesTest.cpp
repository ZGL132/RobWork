/**
 * @file   DiagCodesTest.cpp
 * @brief  selection 稳定诊断码登记表用例组（SelDiagCodes）——SEL-* 17 码
 *         全表清单纪律、码值句法（经 core 契约权威校验）与登记出处完整
 *         性（任务契约 WP-19-T02 acceptance 2"SEL-* 域诊断码按
 *         units/selection.md 登记表装配期注册"的实现侧自证面）。
 *
 * 设计依据：
 *   - units/selection.md §2.2/§5.3/§6.2/§6.3/§9.3（SEL-* 登记值散布各节
 *     ——全表 17 码的字面清单即其机器核对面）、§1.3（SEL-* 建议值；前缀
 *     已在 diagnostics.md §4.5 业务域命名空间清单在册——无补登事项）、
 *     §10.3（ReasonToken 词表的逐 token 稳定码映射随 WP-19-T06——本组
 *     用例不覆盖、不预建）
 *   - units/core.md §4.8（DiagCode 句法 ^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64
 *     ——core 仅承载；CR-08 码值权威在 diagnostics）＋core/DiagData.hpp
 *     （DiagnosticRecord::make 的 C-3 句法校验＝core 侧句法权威执行点）
 *   - 收编确认锚点（如实声明，drivetrain/DiagCodesTest.cpp 同款形态、
 *     不同前缀面）：SEL-* 码进入 diagnostics 全局装配（L5 装配清单注册）
 *     的执行面属装配侧动作——selection 依赖白名单仅 core＋evidence 编译
 *     边（卡 §3.2 把 diagnostics 列入"运行时注入/端口"列、明文不落编译
 *     链接边），本单元产品面与测试面均不可 include diagnostics 头、不可
 *     触真实 StableCodeRegistry——因此本组用例以 core 句法权威
 *     （DiagnosticRecord::make C-3）验证码值合法性面，**不以桩伪造"已
 *     注册"结论**（登记表→StableCodeRegistry 的注册闭环随 L5 装配按本
 *     登记表逐行注册——诚实边界，未执行的验证不标注通过；与 DT 前缀
 *     不同，SEL 前缀已在注册表前缀-所有权表在册，注册无前缀阻塞）。
 *   - 任务契约 tasks/foundation/WP-19-T02.json acceptance 2
 */

#include <sdurws/ird/selection/DiagCodes.hpp>

#include <sdurws/ird/core/DiagData.hpp>  // core::DiagnosticRecord——句法权威（C-3 校验）
#include <sdurws/ird/core/Errors.hpp>    // core::CoreError——句法违约异常类型
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using sdurws::ird::core::DiagnosticRecord;
using sdurws::ird::selection::DiagnosticEntry;
using sdurws::ird::selection::selectionCodeEntries;

namespace {

/// units/selection.md 登记表的**应登记码面**（测试内自持字面清单——与
/// 实现清单机械比对，任一侧漂移即失败：失同步防线）。行序＝卡面章节序
/// （§2.2 一码 → §5.3 表行序八码 → §6.2 一码 → §6.3 表行序四码 → §9.3
/// 表行序三码——实现清单序＝确定性序的依据面）；卡面增码（随消费任务
/// T03+）时在实现清单与本清单表尾同步追加并走单元卡增量修订——既有行
/// 不重排。
const char* kUnitCardFullTable[] = {
    "SEL-INPUT-AXIS-OUT-OF-SCOPE",      // §2.2（移动关节范围外）
    "SEL-CATALOG-SCHEMA-MISMATCH",      // §5.3 行 1（字段字典不完整）
    "SEL-CATALOG-UNIT-INVALID",         // §5.3 行 2（单位非法）
    "SEL-CATALOG-FIELD-MISSING",        // §5.3 行 3（必填缺失）
    "SEL-CATALOG-DUPLICATE-MODEL",      // §5.3 行 4a（重复型号）
    "SEL-CATALOG-DUPLICATE-ID",         // §5.3 行 4b（稳定 ID 重复）
    "SEL-CATALOG-RANGE-INVALID",        // §5.3 行 5（数值范围/非有限）
    "SEL-CATALOG-REF-DANGLING",         // §5.3 行 6＝§6.3（引用悬空）
    "SEL-CATALOG-COMPAT-CONFLICT",      // §5.3 行 7（兼容冲突）
    "SEL-CURVE-EXTRAPOLATION-DENIED",   // §6.2（默认禁止外推）
    "SEL-CURVE-UNORDERED",              // §6.3 行 1（采样点无序）
    "SEL-CURVE-DUP-X",                  // §6.3 行 2（重复横坐标）
    "SEL-CURVE-NONFINITE",              // §6.3 行 3（非有限点）
    "SEL-CURVE-INTERVAL-INVALID",       // §6.3 行 4（区间不合法）
    "SEL-COMBO-INCOMPATIBLE",           // §9.3 行 1（组合不兼容）
    "SEL-COMBO-AXIS-MAPPING-INCOMPLETE",// §9.3 行 2（轴映射不完整）
    "SEL-IDENTITY-MISMATCH",            // §9.3 行 11/12（身份不一致）
};

/// 登记表码数（卡面已具名全集 17——实现清单与本清单同长断言的期望值；
/// §10.3 词表的逐 token 稳定码映射随 WP-19-T06 增列，不在本表）。
constexpr std::size_t kExpectedCodeCount
    = sizeof(kUnitCardFullTable) / sizeof(kUnitCardFullTable[0]);

}  // namespace

/**
 * 全表清单纪律（acceptance 2——"按 units/selection.md 登记表"）：实现
 * 清单与卡面字面清单逐行机械比对（同长＋同序＋同值）；无重复码（键唯一
 * ——diagnostics 注册协议 §4.5 的前置面）；全表 SEL- 前缀（卡 §1.3——
 * 前缀即所有权声明，selection 域码不跨前缀）。
 */
TEST(SelDiagCodes, FullTableMatchesUnitCard_WP19T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{"SEL-01", "SEL-02", "SEL-05", "SEL-09"});

    const std::vector<DiagnosticEntry> entries = selectionCodeEntries();

    // 同长：实现清单不许多登（私造码值——NFR-MNT-03）也不许漏登
    //（登记表不完整——装配期注册的数据源缺口）。
    ASSERT_EQ(entries.size(), kExpectedCodeCount)
        << "SEL-* 登记清单与 units/selection.md 登记表行数不一致（实现 "
        << entries.size() << " vs 卡面 " << kExpectedCodeCount << "）";

    // 同序＋同值：清单序＝卡面章节序（确定性序——NFR-COR-02），逐行
    // 机械比对（任一侧漂移即失败）。
    for (std::size_t i = 0; i < kExpectedCodeCount; ++i) {
        EXPECT_EQ(entries[i].code, kUnitCardFullTable[i])
            << "行 " << i << " 码值漂移（实现清单序须与卡面章节序一致）";
    }

    // 无重复：键唯一是 diagnostics 注册协议（§4.5）的前置——重复码在
    // 登记表层面即暴露，不等注册期才失败。§5.3 行 6 与 §6.3 曲线缺失行
    // 同码（REF-DANGLING）、§9.3 行 11/12 同码（IDENTITY-MISMATCH）——
    // 同码在表中只登记一次（行内出处合并登记）。
    for (std::size_t i = 0; i < entries.size(); ++i) {
        for (std::size_t j = i + 1; j < entries.size(); ++j) {
            EXPECT_NE(entries[i].code, entries[j].code)
                << "重复码值（键唯一前置——§4.5）: " << entries[i].code;
        }
    }

    // 全表 SEL- 前缀：卡 §1.3 前缀即所有权声明——selection 登记表不得
    // 混入他单元前缀（前缀-所有权表按 SEL→selection 校验）。
    for (const auto& entry : entries) {
        EXPECT_EQ(entry.code.substr(0, 4), std::string_view{"SEL-"})
            << "非 SEL- 前缀码混入登记表（卡 §1.3）: " << entry.code;
    }
}

/**
 * 码值句法经 core 契约权威校验（acceptance 2——句法合法性面）：逐码以
 * core::DiagnosticRecord::make 构造完整诊断记录——C-3 校验（句法
 * ^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64，违约抛 CoreError("core/diag/code:")）
 * 即 core 侧句法权威执行点；本单元依赖白名单无 diagnostics 编译边（卡
 * §3.2 注入列），真实 StableCodeRegistry 的前缀-所有权校验随 L5 装配
 * 执行（文件头注"收编确认锚点"——不以桩伪造注册结论）。
 *
 * 同用例并断言"唯一书写点"纪律：登记清单内码值必须逐字等于 DiagCodes.hpp
 * 常量（禁第二处字面量——常量与清单失同步即失败）。
 */
TEST(SelDiagCodes, CodeSyntaxViaCoreContract_WP19T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-COR-03"},
                  std::vector<std::string>{});

    const std::vector<DiagnosticEntry> entries = selectionCodeEntries();
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
                "selection 登记表句法校验",       // context（非空）
                "SEL-* 登记表用例校验桩",         // cause（非空）
                "无——句法验证用例");              // recommendedAction（非空）
        }) << "码值句法违约（core §4.8 ^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64）: "
           << entry.code;
    }

    // 唯一书写点：常量与登记清单逐字一致（禁字符串拼码/第二处字面量
    // ——T03+ 产码路径引用常量，登记表引用同一常量，两处失同步即实现
    // 缺陷）。抽首/中/尾三行锚定。
    EXPECT_EQ(entries[0].code, sdurws::ird::selection::kSelInputAxisOutOfScope);
    EXPECT_EQ(entries[9].code, sdurws::ird::selection::kSelCurveExtrapolationDenied);
    EXPECT_EQ(entries[16].code, sdurws::ird::selection::kSelIdentityMismatch);
}

/**
 * 登记出处完整性（acceptance 2——登记表可追溯面）：每条登记行必须携带
 * 卡面出处（units/selection.md §x.y 锚点——review 时可对照文档）与
 * 非空语义登记（NFR-MNT-03：码的语义权威在卡面，物化不丢锚点）。
 */
TEST(SelDiagCodes, EntriesCarryCardProvenance_WP19T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-03"},
                  std::vector<std::string>{});

    const std::vector<DiagnosticEntry> entries = selectionCodeEntries();
    ASSERT_FALSE(entries.empty()) << "登记表为空（扫描失效防线）";

    for (const auto& entry : entries) {
        // 出处锚点：统一以卡面路径开头（可追溯——AGENTS §2.4"与设计
        // 文档建立追溯"的登记表面）。
        EXPECT_EQ(entry.sourceClause.substr(0, std::string_view{"units/selection.md "}.size()),
                  std::string_view{"units/selection.md "})
            << "登记行缺卡面出处锚点: " << entry.code;
        // 出处含章节号（§ 引导——防"有路径无章节"的退化登记）。
        EXPECT_NE(entry.sourceClause.find("§"), std::string_view::npos)
            << "出处锚点缺章节号: " << entry.code;
        // 语义登记非空：空语义＝登记表退化为码表（丢失卡面处置口径）。
        EXPECT_FALSE(entry.semantics.empty())
            << "登记行语义为空: " << entry.code;
    }
}
