/**
 * @file   EvaluationDimsTest.cpp
 * @brief  评估维度扩展点用例组（TrjEvaluationDims，WP-16-T11 铺设＋
 *         WP-16-T14 递进＋WP-16-T15 递进）——TRJ-08"接口约束"的运行期
 *         自证面：载荷 kindToken 词表封闭＋消费分派语义（§6.6.1）、trj
 *         Profile v1 注册集冻结（§13.3 实现落位冻结）＋v2 递进（TRJ-08-
 *         S1 启用——§12.7/§19.1 交接物）＋v3 递进（TRJ-08-S2 启用）、零
 *         占位守护（§6.5 反向约束——TRJ-08 禁止项的机器可断言表达）。
 *         §20 追踪矩阵 TRJ-08 行"评审项（T11 验收）＋零占位断言"与
 *         TRJ-08-S1/S2 行的执行承载。
 *
 * 设计依据：
 *   - units/trajectory.md §6.5（S1/S2 的承载面＝payload kindToken 递进
 *     ＋Profile 版本递进＋段约束扩展版本结构——不建 TimedSample jerk 成
 *     员/quality jerk 指标/jerk 诊断码；SegmentConstraint 无工艺速度槽
 *     位）、§6.6（扩展机制四条）、§12.7（可扩展维度接口——S1 启用：新
 *     运动律 token＋限值消费＋payload v2；S2 启用：段约束扩展版本＋
 *     Profile 递进；trj.jerk-limit-record/trj.process-speed-record 不在
 *     v1 Profile 注册）、§13.3（trj Profile v1 五行——itemId"实现落位
 *     冻结"）、§13.4（payload kindToken 基线）、§19.1（S1/S2 行交接
 *     物）、§18 T11/T14/T15 行
 *   - 需求 TRJ-08（P1/C/R1——接口保持可扩展，不创建空字段、空页面或占
 *     位模块）、TRJ-08-S1（P1/R2——jerk 上限约束）、TRJ-08-S2（P1/R2
 *     ——工艺速度约束：指定段 TCP 速度受限或恒定；分期交付 V13-02）
 *   - 任务契约 tasks/foundation/WP-16-T11.json（acceptance 1/2——已验
 *     收铺设）、tasks/foundation/WP-16-T14.json（acceptance 2——已验
 *     收）、tasks/foundation/WP-16-T15.json（acceptance 2——扩展点消费
 *     经词表，零空字段/零占位）
 *
 * 本套件为**两模式皆跑**（非条件源）：被测面 EvaluationDims.hpp 全部为
 * inline 值面（零 rw 符号、零上游单元依赖——P-TRJ-11 暴露面为零）；零
 * 占位守护用例对既有值的引用均为只读值断言——不构造任何 rw::math::Q
 * 运行期对象（成员封闭走 SFINAE 负向探测＋枚举 static_assert，均为编译
 * 期动作；码表描述符为 trajectory 库非条件源纯 std 值面），冒烟模式同
 * 等可编译可运行。
 */

#include <sdurws/ird/trajectory/EvaluationDims.hpp>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // CodeDescriptor（码表遍历断言的值类型——显式引入不依赖传递包含）
#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/PlanConfig.hpp>
#include <sdurws/ird/trajectory/TimeParam.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace trj = sdurws::ird::trajectory;

namespace {

/// ASCII 小写化（词根扫描辅助——域内词表词形全 ASCII，逐字符 tolower
/// 无 locale 依赖；NFR-COR-02 确定性）。
std::string toLowerAscii(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

/// 成员存在性负向探测（SFINAE——TRJ-08 零占位守护的编译期断言设施）：
/// HasMember<T, 名字>::value＝T 是否存在可访问成员函数/成员对象"名字"。
/// 为什么是负向探测而不是结构化绑定计数：TimedSample 含 rw::math::Q 成
/// 员，运行期构造（哪怕值初始化）会触发 Q 的库符号（构造/析构外联）——
/// 冒烟模式无框架库可链（TimeParam.hpp 头注"冒烟口径"同源）；SFINAE 只
/// 在编译期实例化类型表达式，零运行期对象、零符号依赖，且直接对应
/// §6.5 原文语义"TimedSample 无 jerk 成员"（负向存在性，而非成员总数）。
template <typename T, typename = void>
struct HasMemberJerk : std::false_type {};

template <typename T>
struct HasMemberJerk<T, std::void_t<decltype(std::declval<T&>().jerk)>>
    : std::true_type {};

/// 同款设施——工艺速度槽位候选名探测（S2 形态"指定段 TCP 速度受限/
/// 恒定"的可能槽位名族；启用名以本卡 §13.4/§6.2 修订登记为准，届时本
/// 断言随同一批移除——"修订四者同批"的机器提醒）。
template <typename T, typename = void>
struct HasMemberProcessSpeedTcp : std::false_type {};

template <typename T>
struct HasMemberProcessSpeedTcp<T,
    std::void_t<decltype(std::declval<T&>().processSpeedTcp)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasMemberProcessSpeed : std::false_type {};

template <typename T>
struct HasMemberProcessSpeed<T,
    std::void_t<decltype(std::declval<T&>().processSpeed)>>
    : std::true_type {};

}  // namespace

// =====================================================================
// 载荷 kindToken 词表（§6.6.1——消费分派点语义）
// =====================================================================

/**
 * 载荷 kindToken 唯一书写点值与消费分派语义（acceptance 1——载荷版本化
 * 承接点的可扩展性评审实质：词表封闭、判定面真实存在、未知值走拒绝而非
 * 静默按任一版本）。§13.4 冻结基线 "kindToken=\"trj.sequence-plan.v1\""；
 * §6.6.1 "v2 起（S1/S2/S3 任一启用）新 kindToken＋新 canonical schema，
 * 旧 payload 永久可读"。WP-16-T14 批（TRJ-08-S1 启用）执行 §6.6.1 铺设
 * 通道的首次真实递进；WP-16-T15 批（TRJ-08-S2 启用）执行第二次递进；
 * WP-16-T16 批（TRJ-08-S3 启用）执行第三次递进：当前产出值 v4、v3/v2/
 * v1 转历史已知值（词表四项——本用例断言随递进演进，递进纪律本身〔只
 * 增不删/精确串等值/未知拒绝〕不变）。
 */
TEST(TrjEvaluationDims, PayloadKindTokenV1VocabularyAndDispatch_WP16T11_ACC1)
{
    IRD_TEST_INFO("TRJ-08", {}, std::nullopt);

    // 唯一书写点值＝S3 启用批递进后的当前产出值（消费/产出两侧引用同一
    // 常量——第二处字面量漂移＝两个 schema 假装同一个；该纪律由契约测试
    // 源码扫描机器化，此处锁定常量值本身）。
    EXPECT_EQ(trj::kTrjPayloadKindToken, "trj.sequence-plan.v4");
    // 历史已知值常量＝v3/v2/v1 词形（旧 payload 永久可读的词表承载——
    // PA-2；词表只增不删：v1/v2/v3 各递进批的值各自冻结保留）。
    EXPECT_EQ(trj::kTrjPayloadKindTokenV1, "trj.sequence-plan.v1");
    EXPECT_EQ(trj::kTrjPayloadKindTokenV2, "trj.sequence-plan.v2");
    EXPECT_EQ(trj::kTrjPayloadKindTokenV3, "trj.sequence-plan.v3");

    // 已知值：当前产出 v4 与历史 v3/v2/v1 都判定 true（旧载荷永久可读
    // ——词表只增不删；四值各对应一个真实 schema 版本分支）。
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v4"));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v3"));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v2"));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v1"));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindToken));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV1));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV2));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV3));

    // 未知值族：全部判定 false——消费面必须拒绝而非静默按任一已知版本
    // 解读（NFR-COR-03 不静默；逐条代表一类真实拒收场景）：
    //   - 未来版本（S3 后续递进的 v5 回读旧程序——版本递进不回写旧判定
    //     面；v3 未启用时同类场景由 T11 原断言以 v3 覆盖、v4 未启用时由
    //     T15 断言以 v4 覆盖，递进后 v5 顶替其"未来值"位置）；
    //   - 大小写漂移（token 是身份面词形，不做规范化——宽容即身份漂移通道）；
    //   - 首尾空白（精确串等值——词形即身份；v3/v1 各验一侧）；
    //   - 域前缀缺失（他域 token 不属于本域词表）；
    //   - 空串与损坏数据（载荷缺 token 字段的现实形态）。
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v5"));
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken("TRJ.SEQUENCE-PLAN.V3"));
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken("TRJ.SEQUENCE-PLAN.V1"));
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken(" trj.sequence-plan.v3"));
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v1 "));
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken("sequence-plan.v3"));
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken(""));
}

// =====================================================================
// trj Profile v1 证据项注册集（§13.3 表"实现落位冻结"——逐行对照）
// =====================================================================

/**
 * v1 注册集封闭性与逐行冻结（acceptance 1——Profile 版本化承接点：清单
 * 恰 §13.3 表 trj 前缀五行、行序＝表行序、逐项三要素（itemId/itemClass/
 * 适用条件）与卡面表逐列一致；第六行"通用必需项"为 evidence 内建不在域
 * 内清单）。任何一侧漂移即失败（测试内自持字面清单——失同步防线，Diag-
 * CodesTest kSection144FullTable 同款形态）。
 */
TEST(TrjEvaluationDims, ProfileV1ItemsFrozenFiveRows_WP16T11_ACC1)
{
    IRD_TEST_INFO("TRJ-08", {}, std::nullopt);

    // Profile 标识（§13.3 "profileId=\"trj\""——注册面与 descriptor 引用
    // 共用常量）。
    EXPECT_EQ(trj::kTrjProfileId, "trj");

    const std::array<trj::TrjEvidenceItemSpec, 5> items = trj::trjProfileV1Items();

    // 封闭性：恰 5 项（§13.3 表 trj 前缀行数；多一行＝未走版本递进的
    // "静默维度"违约，少一行＝收窄卡面承诺）。
    ASSERT_EQ(items.size(), 5U);

    // 行 1：路径与时间记录——Required，总是适用。
    EXPECT_EQ(items[0].itemId, "trj.path-and-time-record");
    EXPECT_EQ(items[0].itemClass, trj::TrjEvidenceItemClass::Required);
    EXPECT_EQ(items[0].applicability,
              trj::TrjEvidenceItemApplicability::Always);

    // 行 2：平滑复检证据——Required，总是适用（纯关节路径同样复检）。
    EXPECT_EQ(items[1].itemId, "trj.smooth-recheck-evidence");
    EXPECT_EQ(items[1].itemClass, trj::TrjEvidenceItemClass::Required);
    EXPECT_EQ(items[1].applicability,
              trj::TrjEvidenceItemApplicability::Always);

    // 行 3：笛卡尔段内 IK 连续性——Required，仅路径含笛卡尔段（纯关节
    // →NotApplicable 不计缺失——C2 例）。
    EXPECT_EQ(items[2].itemId, "trj.cartesian-ik-continuity");
    EXPECT_EQ(items[2].itemClass, trj::TrjEvidenceItemClass::Required);
    EXPECT_EQ(items[2].applicability,
              trj::TrjEvidenceItemApplicability::HasCartesianSegment);

    // 行 4：节拍分阶段分解——Suggested，仅时间参数化成功时产出。
    EXPECT_EQ(items[3].itemId, "trj.cycle-phase-breakdown");
    EXPECT_EQ(items[3].itemClass, trj::TrjEvidenceItemClass::Suggested);
    EXPECT_EQ(items[3].applicability,
              trj::TrjEvidenceItemApplicability::TimeParamSucceeded);

    // 行 5：轨迹动画数据——Suggested，仅成功轨迹。
    EXPECT_EQ(items[4].itemId, "trj.animation-data");
    EXPECT_EQ(items[4].itemClass, trj::TrjEvidenceItemClass::Suggested);
    EXPECT_EQ(items[4].applicability,
              trj::TrjEvidenceItemApplicability::SuccessfulTrajectory);

    // 常量与清单同源：五个 itemId 常量逐一与清单对应项相等（消费面引用
    // 常量、清单承载注册集——两读法必须同值）。
    EXPECT_EQ(trj::kTrjItemIdPathAndTimeRecord, items[0].itemId);
    EXPECT_EQ(trj::kTrjItemIdSmoothRecheckEvidence, items[1].itemId);
    EXPECT_EQ(trj::kTrjItemIdCartesianIkContinuity, items[2].itemId);
    EXPECT_EQ(trj::kTrjItemIdCyclePhaseBreakdown, items[3].itemId);
    EXPECT_EQ(trj::kTrjItemIdAnimationData, items[4].itemId);

    // 无重复 itemId（注册集身份唯一——重复项使 isTrjProfileV1ItemId 的
    // "属集"语义与 Profile 注册面歧义）。
    for (std::size_t i = 0; i < items.size(); ++i) {
        for (std::size_t j = i + 1; j < items.size(); ++j) {
            ASSERT_NE(items[i].itemId, items[j].itemId)
                << "v1 清单重复 itemId：index " << i << " 与 " << j;
        }
    }
}

/**
 * itemId 注册资格判定（acceptance 1——§12.7 "trj.jerk-limit-record 等
 * 不在 v1 Profile 注册"的机器断言：S1～S3 维度项、缺域前缀词形、空串全
 * 部 false；经 trjProfileV1Items() 全员遍历则全部 true——两接口组合消
 * 费的一致性钉扎）。
 */
TEST(TrjEvaluationDims, ProfileV1ItemIdLookup_WP16T11_ACC1)
{
    IRD_TEST_INFO("TRJ-08", {}, std::nullopt);

    // v1 注册集全员判定 true（判定面与清单单一数据源——清单扩面自动一致）。
    for (const trj::TrjEvidenceItemSpec& item : trj::trjProfileV1Items()) {
        EXPECT_TRUE(trj::isTrjProfileV1ItemId(item.itemId))
            << "v1 清单内项判定失败: " << item.itemId;
    }

    // §12.7 点名的未来维度项：不在 v1 注册集（零占位——这些项只能随
    // S1～S3 的 Profile 版本递进落位，v1 清单永不收编）。
    EXPECT_FALSE(trj::isTrjProfileV1ItemId("trj.jerk-limit-record"));
    EXPECT_FALSE(trj::isTrjProfileV1ItemId("trj.process-speed-record"));
    EXPECT_FALSE(trj::isTrjProfileV1ItemId("trj.continuous-process-boundary"));

    // 词形拒绝族：缺 "trj." 域前缀／未知项／空串（精确串等值——身份面
    // 词形不做规范化）。
    EXPECT_FALSE(trj::isTrjProfileV1ItemId("path-and-time-record"));
    EXPECT_FALSE(trj::isTrjProfileV1ItemId("trj.unknown-item"));
    EXPECT_FALSE(trj::isTrjProfileV1ItemId(""));
}

// =====================================================================
// 零占位守护（§6.5 R1 范围守卫——TRJ-08 禁止项的机器可断言表达）
// =====================================================================

/**
 * §6.5 反向约束全族（acceptance 2——"零空字段/空页面/占位模块"的编译
 * 期＋运行期守护）：
 *   a. v1 证据项清单零 S1～S3 维度词根（jerk/工艺速度/连续工艺）——
 *      "不新增静默维度"（§6.6.2）；
 *   b. SegmentSpaceType 词表值序锁定——JointLinear=0/CartesianLine=1/
 *      ContinuousProcess=2（§6.5 第三条的启用批演进：S3 启用经需求变更
 *      ＋本卡修订扩展词表〔WP-16-T16 批〕，值序锁定断言即扩值报警面保
 *      持——再次扩值强制走需求变更＋本卡增量修订流程）；
 *   c. TimedSample 无 jerk 成员（§6.5 第一条——SFINAE 负向探测，纯编译
 *      期零运行期对象；§12.1 "jerk 连续属 R2 本卡不承诺、不预留字段"）；
 *   d. SegmentConstraint 无工艺速度槽位（§6.5 第二条——候选名族负向探
 *      测；S2 启用名以本卡修订登记为准，届时断言随同一批移除）；
 *   e. TRJ-* 码表零 jerk 码（§6.5 第一条"诊断无 jerk 码"——12 码逐一遍
 *      历；S1 启用时增码＝§14.4 增量修订＋表尾追加，不是今天的占位）；
 *   f. 运动律词表当前唯一值（§6.6.3——config 默认值＝唯一实现 token；
 *      分派行为的词表外拒绝已由 TimeParamTest.FailFastFamily 钉扎，本
 *      用例锁定词表值纪律本身）。
 *
 * 断言形态说明（为什么 (c)/(d) 用 SFINAE 而不是构造对象断字段值）：
 * TimedSample 含 rw::math::Q 成员，运行期构造（哪怕值初始化）需要 Q 的
 * 库符号（构造/析构外联非 header-only）——冒烟模式无框架库可链（TimePa-
 * ram.hpp 头注"冒烟口径"同源）。SFINAE 探测只在编译期实例化类型表达式
 * （decltype/declval 不构造对象），零符号依赖，两模式皆编译；且语义直
 * 指 §6.5 原文"无 jerk 成员"（负向存在性）。`static_assert(!Has…::value)`
 * 在成员被占位添加的瞬间使本文件编译失败——占位无法合入。
 */
TEST(TrjEvaluationDims, ZeroPlaceholderGuard_WP16T11_ACC2)
{
    IRD_TEST_INFO("TRJ-08", {}, std::nullopt);

    // (a) v1 清单零 S1～S3 维度词根（大小写不敏感子串扫描——防占位项以
    //     任何大小写变体混入；词根取 §6.5 点名的三个维度的核心词形）。
    for (const trj::TrjEvidenceItemSpec& item : trj::trjProfileV1Items()) {
        const std::string lowered = toLowerAscii(item.itemId);
        EXPECT_EQ(lowered.find("jerk"), std::string::npos)
            << "v1 清单混入 jerk 维度占位: " << item.itemId;
        EXPECT_EQ(lowered.find("process-speed"), std::string::npos)
            << "v1 清单混入工艺速度维度占位: " << item.itemId;
        EXPECT_EQ(lowered.find("continuous"), std::string::npos)
            << "v1 清单混入连续工艺维度占位: " << item.itemId;
    }

    // (b) SegmentSpaceType 词表值序锁定（JointLinear=0、CartesianLine=1、
    //     ContinuousProcess=2）。WP-16-T16 批（TRJ-08-S3 启用）演进注：
    //     S3 的承载选择＝**SegmentSpaceType 词表扩展**（§19.1 S3 行交接
    //     物明文——与 S2"扩展版本结构"的承载选择不同：空间类型词表是
    //     段分类的既有承载点，扩展即其启用条款"需求变更＋本卡修订"的
    //     落值；需求侧依据＝REQUIREMENTS §17.1 TRJ-08-S3 行 R2 解冻）。
    //     本断言由"恰两值"演进为"值序锁定"——既有两值序不变（消费面
    //     值稳定），第三值表尾追加（封闭词表只增不删；再次扩值须走需求
    //     变更＋本卡增量修订，任何"顺手插值"会使值序断言爆——报警面保
    //     持）。
    static_assert(static_cast<std::uint8_t>(trj::SegmentSpaceType::JointLinear)
                      == 0,
        "SegmentSpaceType 词表第一值必须为 JointLinear（§6.2 封闭词表——"
        "既有值序零变化）");
    static_assert(static_cast<std::uint8_t>(trj::SegmentSpaceType::CartesianLine)
                      == 1,
        "SegmentSpaceType 词表第二值必须为 CartesianLine（§6.2 封闭词表——"
        "既有值序零变化）");
    static_assert(
        static_cast<std::uint8_t>(trj::SegmentSpaceType::ContinuousProcess)
            == 2,
        "SegmentSpaceType 词表第三值必须为 ContinuousProcess（§19.1 S3 行"
        "交接物——WP-16-T16 批表尾追加；再次扩值须走需求变更＋本卡增量修"
        "订，不得静默插值）");

    // (c) TimedSample 无 jerk 成员（§6.5 第一条/§12.1 不预留字段——编译
    //     期负向存在性断言；占位添加 jerk 成员即编译失败）。
    static_assert(!HasMemberJerk<trj::TimedSample>::value,
        "TimedSample 出现 jerk 成员——TRJ-08-S1 启用必须走单元卡修订＋实现"
        "＋黄金算例同批（§6.5 不预留字段/§6.6 词表纪律），不得静默占位");

    // (d) SegmentConstraint 无工艺速度槽位（§6.5 第二条——候选名族负向
    //     探测）。WP-16-T15 批（TRJ-08-S2 启用）演进注：S2 的承载选择为
    //     **段约束扩展版本结构 ProcessSpeedConstraint**（§19.1 S2 行交
    //     接物"SegmentConstraint 扩展版本"——独立值类型经请求面逐段指
    //     定），SegmentConstraint 本体三成员保持不变，故本断言**不移
    //     除**、守护继续成立（原注释"届时断言随同一修订批移除"针对的
    //     是"S2 槽位落 SegmentConstraint 本体"的备选路径——本批以扩展
    //     版本结构落地，槽位路径未被采用，断言转为 S2 启用后的长期守护）。
    static_assert(!HasMemberProcessSpeedTcp<trj::SegmentConstraint>::value,
        "SegmentConstraint 出现 processSpeedTcp 槽位——工艺速度约束的承"
        "载是段约束扩展版本结构 ProcessSpeedConstraint（§6.5/§19.1 S2 "
        "行），SegmentConstraint 本体不得扩槽位");
    static_assert(!HasMemberProcessSpeed<trj::SegmentConstraint>::value,
        "SegmentConstraint 出现 processSpeed 槽位——工艺速度约束的承载是"
        "段约束扩展版本结构 ProcessSpeedConstraint（§6.5/§19.1 S2 行），"
        "SegmentConstraint 本体不得扩槽位");

    // (e) TRJ-* 码表零 jerk 码（§6.5 "诊断无 jerk 码"——全表遍历；码值
    //     唯一书写点纪律下词根扫描等价于"无 jerk 码常量"。描述符工厂为
    //     trajectory 库非条件源符号——冒烟模式可链，两模式皆跑）。
    const std::vector<sdurws::ird::diagnostics::CodeDescriptor> descriptors =
        trj::trajectoryCodeDescriptors();
    ASSERT_EQ(descriptors.size(), 12U);  // §14.4 全表 12 码（T03 冻结面）
    for (const auto& descriptor : descriptors) {
        EXPECT_EQ(toLowerAscii(descriptor.code).find("jerk"),
                  std::string::npos)
            << "码表混入 jerk 占位码: " << descriptor.code;
    }

    // (f) 运动律词表两值（§6.6.3 封闭词表；S1 新 token＝本卡增量修订＋
    //     实现＋黄金算例四者同批——WP-16-T14 批递进落地：quintic 语义零
    //     变化、septic 为 S1 真实交付；无默认槽位空置）。注意：常量是
    //     constexpr char 数组，EXPECT_EQ 直接比较会退化为指针地址比较
    //     ——经 std::string_view 取内容比较（词形契约）。
    EXPECT_EQ(std::string_view(trj::kTimeParamMethodQuinticSplineC2),
              "quintic-spline-c2");
    EXPECT_EQ(std::string_view(trj::kTimeParamMethodSepticSplineC3),
              "septic-spline-c3");
    // 配置默认值＝既有唯一值 quintic（默认构造面零漂移——S1 是显式选
    // 择项而非静默替换，既有配置/缓存身份零影响；消费链无需特判"默认
    // 值不在词表"的空分支）。
    const trj::TrajectoryPlanConfiguration defaultConfig{};
    EXPECT_EQ(defaultConfig.timeParamMethod,
              std::string(trj::kTimeParamMethodQuinticSplineC2));
}

/**
 * 接口交付面经消费路径的组合钉扎（acceptance 1——"公共接口交付面至少
 * 一条经接口消费的用例"：模拟一个真实消费面的完整分派流程——先验载荷
 * token 可分派性，再验证据项注册资格，两判定面协同决定"接受/拒绝"）。
 * 消费语义即 §6.6.1/§12.7 的分派点协议：已知 token＋对应版本已注册
 * itemId→接受；任一未知→拒绝（消费面 fail 语义由调用方决定，本域契约
 * 是判定面本身）。WP-16-T16 批递进后：当前产出组合为 v4 载荷＋v4 注册
 * 集（TRJ-08-S1/S2/S3 现形），v3/v2/v1 载荷＋各自集合为历史可读组合。
 */
TEST(TrjEvaluationDims, ConsumerDispatchProtocol_WP16T11_ACC1)
{
    IRD_TEST_INFO("TRJ-08", {}, std::nullopt);

    // 接受路径（当前产出面）：v4 载荷＋v4 注册项（TRJ-08-S3 启用批的
    // 合法组合——含 S3 新增连续工艺边界项、S2/S1 维度项与 v1 继承项）。
    const bool acceptV4 =
        trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindToken)
        && trj::isTrjProfileV4ItemId(trj::kTrjItemIdPathAndTimeRecord)
        && trj::isTrjProfileV4ItemId(trj::kTrjItemIdJerkLimitRecord)
        && trj::isTrjProfileV4ItemId(trj::kTrjItemIdProcessSpeedRecord)
        && trj::isTrjProfileV4ItemId(
            trj::kTrjItemIdContinuousProcessBoundary);
    EXPECT_TRUE(acceptV4);

    // 接受路径（历史可读面）：v3/v2/v1 载荷＋各自集合注册项（旧 payload
    // 永久可读——递进不回写旧判定面，PA-2）。
    const bool acceptV3Legacy =
        trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV3)
        && trj::isTrjProfileV3ItemId(trj::kTrjItemIdProcessSpeedRecord);
    EXPECT_TRUE(acceptV3Legacy);
    const bool acceptV2Legacy =
        trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV2)
        && trj::isTrjProfileV2ItemId(trj::kTrjItemIdJerkLimitRecord);
    EXPECT_TRUE(acceptV2Legacy);
    const bool acceptV1Legacy =
        trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV1)
        && trj::isTrjProfileV1ItemId(trj::kTrjItemIdPathAndTimeRecord);
    EXPECT_TRUE(acceptV1Legacy);

    // 拒绝路径一：载荷未知（未来 v5 token 回读——§6.6.1 消费面必须拒绝，
    // 即使 itemId 已注册；schema 版本不一致＝不可解读）。
    const bool rejectUnknownToken =
        trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v5")
        && trj::isTrjProfileV1ItemId(trj::kTrjItemIdAnimationData);
    EXPECT_FALSE(rejectUnknownToken);

    // 拒绝路径二：证据项未注册（S1/S2/S3 维度项冒入 v1/v2/v3——§12.7
    // 不在已交付 Profile 注册；即使载荷 token 合法也拒绝——Profile 集
    // 合封闭性独立于载荷版本；v2 载荷配 v1 判定面同理只认 v1 五项）。
    const bool rejectUnregisteredItem =
        trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV1)
        && trj::isTrjProfileV1ItemId("trj.jerk-limit-record");
    EXPECT_FALSE(rejectUnregisteredItem);
    const bool rejectV2PredicateOnS2Shape =
        trj::isTrjProfileV2ItemId("trj.process-speed-record");
    EXPECT_FALSE(rejectV2PredicateOnS2Shape);
    const bool rejectV3PredicateOnS3Shape =
        trj::isTrjProfileV3ItemId("trj.continuous-process-boundary");
    EXPECT_FALSE(rejectV3PredicateOnS3Shape);
}

// =====================================================================
// Profile v2 递进（WP-16-T14 批——TRJ-08-S1 启用的证据项集合显式变化）
// =====================================================================

/**
 * v2 注册集封闭性与 v1 继承一致性（WP-16-T14 acceptance 2——扩展点消费
 * 经 EvaluationDims 词表的机器断言面）：
 *   a. v2 恰 6 项＝v1 五行原样（逐项三要素与 v1 清单位级一致——PA-2：
 *      已交付行零改写）＋表尾 jerk 行（§12.7 点名词形）；
 *   b. jerk 行三要素＝{trj.jerk-limit-record, Required,
 *      JerkLimitedMotionLaw}（§13.3 v2 递进行）；
 *   c. 判定面分派：v1 五项过 isTrjProfileV2ItemId（继承）、jerk 项过
 *      isTrjProfileV2ItemId 但不过 isTrjProfileV1ItemId（v1 永不收编——
 *      §12.7 零占位守护的递进后形态）、S2 词形两侧全 false；
 *   d. v2 清单无重复 itemId（注册集身份唯一）。
 */
TEST(TrjEvaluationDims, ProfileV2Progression_WP16T14_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S1", {}, std::nullopt);

    // (a) 封闭性：恰 6 项（v1 五行＋S1 jerk 行）。
    const std::array<trj::TrjEvidenceItemSpec, 6> v2 = trj::trjProfileV2Items();
    const std::array<trj::TrjEvidenceItemSpec, 5> v1 = trj::trjProfileV1Items();
    ASSERT_EQ(v2.size(), 6U);

    // v1 五行原样继承（逐项三要素位级一致——递进是"表尾新增"不是
    // "改写重排"，PA-2）。
    for (std::size_t i = 0; i < v1.size(); ++i) {
        EXPECT_EQ(v2[i].itemId, v1[i].itemId)
            << "v2 行 " << i << " 与 v1 清单不一致（继承零改写违约）";
        EXPECT_EQ(v2[i].itemClass, v1[i].itemClass);
        EXPECT_EQ(v2[i].applicability, v1[i].applicability);
    }

    // (b) jerk 行三要素（§13.3 v2 递进行；词形＝§12.7 点名原文）。
    EXPECT_EQ(v2[5].itemId, "trj.jerk-limit-record");
    EXPECT_EQ(v2[5].itemClass, trj::TrjEvidenceItemClass::Required);
    EXPECT_EQ(v2[5].applicability,
              trj::TrjEvidenceItemApplicability::JerkLimitedMotionLaw);

    // (c) 判定面分派（两集合判定面与清单单一数据源的一致性）。
    for (const trj::TrjEvidenceItemSpec& item : v1) {
        EXPECT_TRUE(trj::isTrjProfileV2ItemId(item.itemId))
            << "v1 继承项未过 v2 判定: " << item.itemId;
    }
    EXPECT_TRUE(trj::isTrjProfileV2ItemId(trj::kTrjItemIdJerkLimitRecord));
    EXPECT_FALSE(trj::isTrjProfileV1ItemId(trj::kTrjItemIdJerkLimitRecord))
        << "v1 判定面收编了 S1 维度项（§12.7 零占位守护违约）";
    EXPECT_FALSE(trj::isTrjProfileV2ItemId("trj.process-speed-record"))
        << "v2 集合混入 S2 维度占位（TRJ-08-S2 未启用）";
    EXPECT_FALSE(trj::isTrjProfileV2ItemId(""));

    // (d) v2 清单无重复 itemId（注册集身份唯一）。
    for (std::size_t i = 0; i < v2.size(); ++i) {
        for (std::size_t j = i + 1; j < v2.size(); ++j) {
            ASSERT_NE(v2[i].itemId, v2[j].itemId)
                << "v2 清单重复 itemId：index " << i << " 与 " << j;
        }
    }
}

// =====================================================================
// Profile v3 递进（WP-16-T15 批——TRJ-08-S2 启用的证据项集合显式变化）
// =====================================================================

/**
 * v3 注册集封闭性与 v2 继承一致性（WP-16-T15 acceptance 2——扩展点消费
 * 经 EvaluationDims 词表的机器断言面）：
 *   a. v3 恰 7 项＝v2 六行原样（逐项三要素与 v2 清单位级一致——PA-2：
 *      已交付行零改写）＋表尾工艺速度行（§12.7 点名词形族的 S2 词形）；
 *   b. 工艺速度行三要素＝{trj.process-speed-record, Required,
 *      ProcessSpeedConstrainedSegment}（§13.3 v3 递进行）；
 *   c. 判定面分派：v2 六项过 isTrjProfileV3ItemId（继承）、工艺速度项
 *      过 isTrjProfileV3ItemId 但不过 isTrjProfileV1ItemId/isTrjProfile-
 *      V2ItemId（已交付集合永不收编——§12.7 零占位守护的递进后形态）、
 *      S3 词形两侧全 false；
 *   d. v3 清单无重复 itemId（注册集身份唯一）。
 */
TEST(TrjEvaluationDims, ProfileV3Progression_WP16T15_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    // (a) 封闭性：恰 7 项（v2 六行＋S2 工艺速度行）。
    const std::array<trj::TrjEvidenceItemSpec, 7> v3 = trj::trjProfileV3Items();
    const std::array<trj::TrjEvidenceItemSpec, 6> v2 = trj::trjProfileV2Items();
    const std::array<trj::TrjEvidenceItemSpec, 5> v1 = trj::trjProfileV1Items();
    ASSERT_EQ(v3.size(), 7U);

    // v1/v2 行原样继承（逐项三要素位级一致——递进是"表尾新增"不是
    // "改写重排"，PA-2）。
    for (std::size_t i = 0; i < v2.size(); ++i) {
        EXPECT_EQ(v3[i].itemId, v2[i].itemId)
            << "v3 行 " << i << " 与 v2 清单不一致（继承零改写违约）";
        EXPECT_EQ(v3[i].itemClass, v2[i].itemClass);
        EXPECT_EQ(v3[i].applicability, v2[i].applicability);
    }
    for (std::size_t i = 0; i < v1.size(); ++i) {
        EXPECT_EQ(v3[i].itemId, v1[i].itemId)
            << "v3 行 " << i << " 与 v1 清单不一致（继承零改写违约）";
    }

    // (b) 工艺速度行三要素（§13.3 v3 递进行；词形＝§12.7 点名词形族的
    // S2 词形——T11 铺设期"未来维度项"断言所用词形，启用后转为真实行）。
    EXPECT_EQ(v3[6].itemId, "trj.process-speed-record");
    EXPECT_EQ(v3[6].itemClass, trj::TrjEvidenceItemClass::Required);
    EXPECT_EQ(v3[6].applicability,
              trj::TrjEvidenceItemApplicability::ProcessSpeedConstrainedSegment);

    // (c) 判定面分派（三集合判定面与清单单一数据源的一致性）。
    for (const trj::TrjEvidenceItemSpec& item : v2) {
        EXPECT_TRUE(trj::isTrjProfileV3ItemId(item.itemId))
            << "v2 继承项未过 v3 判定: " << item.itemId;
    }
    EXPECT_TRUE(
        trj::isTrjProfileV3ItemId(trj::kTrjItemIdProcessSpeedRecord));
    EXPECT_FALSE(trj::isTrjProfileV1ItemId(
        trj::kTrjItemIdProcessSpeedRecord))
        << "v1 判定面收编了 S2 维度项（§12.7 零占位守护违约）";
    EXPECT_FALSE(trj::isTrjProfileV2ItemId(
        trj::kTrjItemIdProcessSpeedRecord))
        << "v2 判定面收编了 S2 维度项（历史集合永不收编违约）";
    EXPECT_FALSE(trj::isTrjProfileV3ItemId("trj.continuous-process-boundary"))
        << "v3 集合混入 S3 维度占位（TRJ-08-S3 未启用）";
    EXPECT_FALSE(trj::isTrjProfileV3ItemId(""));

    // (d) v3 清单无重复 itemId（注册集身份唯一）。
    for (std::size_t i = 0; i < v3.size(); ++i) {
        for (std::size_t j = i + 1; j < v3.size(); ++j) {
            ASSERT_NE(v3[i].itemId, v3[j].itemId)
                << "v3 清单重复 itemId：index " << i << " 与 " << j;
        }
    }
}

// =====================================================================
// Profile v4 递进（WP-16-T16 批——TRJ-08-S3 启用的证据项集合显式变化）
// =====================================================================

/**
 * v4 注册集封闭性与 v3 继承一致性（WP-16-T16 acceptance 2——扩展点消费
 * 经 EvaluationDims 词表的机器断言面）：
 *   a. v4 恰 8 项＝v3 七行原样（逐项三要素与 v3 清单位级一致——PA-2：
 *      已交付行零改写）＋表尾连续工艺边界行（§12.7 点名词形族的 S3 词
 *      形——T15 批负向断言所用词形，启用后转为真实行）；
 *   b. 连续工艺边界行三要素＝{trj.continuous-process-boundary, Required,
 *      ContinuousProcessSegment}（§13.3 v4 递进行）；
 *   c. 判定面分派：v3 七项过 isTrjProfileV4ItemId（继承）、连续工艺边
 *      界项过 isTrjProfileV4ItemId 但不过 isTrjProfileV1ItemId/V2/V3
 *      （已交付集合永不收编——§12.7 零占位守护的递进后形态）；
 *   d. v4 清单无重复 itemId（注册集身份唯一）。
 */
TEST(TrjEvaluationDims, ProfileV4Progression_WP16T16_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    // (a) 封闭性：恰 8 项（v3 七行＋S3 连续工艺边界行）。
    const std::array<trj::TrjEvidenceItemSpec, 8> v4 = trj::trjProfileV4Items();
    const std::array<trj::TrjEvidenceItemSpec, 7> v3 = trj::trjProfileV3Items();
    const std::array<trj::TrjEvidenceItemSpec, 5> v1 = trj::trjProfileV1Items();
    ASSERT_EQ(v4.size(), 8U);

    // v1/v3 行原样继承（逐项三要素位级一致——递进是"表尾新增"不是
    // "改写重排"，PA-2）。
    for (std::size_t i = 0; i < v3.size(); ++i) {
        EXPECT_EQ(v4[i].itemId, v3[i].itemId)
            << "v4 行 " << i << " 与 v3 清单不一致（继承零改写违约）";
        EXPECT_EQ(v4[i].itemClass, v3[i].itemClass);
        EXPECT_EQ(v4[i].applicability, v3[i].applicability);
    }
    for (std::size_t i = 0; i < v1.size(); ++i) {
        EXPECT_EQ(v4[i].itemId, v1[i].itemId)
            << "v4 行 " << i << " 与 v1 清单不一致（继承零改写违约）";
    }

    // (b) 连续工艺边界行三要素（§13.3 v4 递进行；词形＝§12.7 点名词形
    // 族的 S3 词形——T15 批"未来维度项"负向断言所用词形，启用后转为
    // 真实行）。
    EXPECT_EQ(v4[7].itemId, "trj.continuous-process-boundary");
    EXPECT_EQ(v4[7].itemClass, trj::TrjEvidenceItemClass::Required);
    EXPECT_EQ(v4[7].applicability,
              trj::TrjEvidenceItemApplicability::ContinuousProcessSegment);

    // (c) 判定面分派（多集合判定面与清单单一数据源的一致性）。
    for (const trj::TrjEvidenceItemSpec& item : v3) {
        EXPECT_TRUE(trj::isTrjProfileV4ItemId(item.itemId))
            << "v3 继承项未过 v4 判定: " << item.itemId;
    }
    EXPECT_TRUE(trj::isTrjProfileV4ItemId(
        trj::kTrjItemIdContinuousProcessBoundary));
    EXPECT_FALSE(trj::isTrjProfileV1ItemId(
        trj::kTrjItemIdContinuousProcessBoundary))
        << "v1 判定面收编了 S3 维度项（§12.7 零占位守护违约）";
    EXPECT_FALSE(trj::isTrjProfileV2ItemId(
        trj::kTrjItemIdContinuousProcessBoundary))
        << "v2 判定面收编了 S3 维度项（历史集合永不收编违约）";
    EXPECT_FALSE(trj::isTrjProfileV3ItemId(
        trj::kTrjItemIdContinuousProcessBoundary))
        << "v3 判定面收编了 S3 维度项（历史集合永不收编违约）";
    EXPECT_FALSE(trj::isTrjProfileV4ItemId(""));

    // (d) v4 清单无重复 itemId（注册集身份唯一）。
    for (std::size_t i = 0; i < v4.size(); ++i) {
        for (std::size_t j = i + 1; j < v4.size(); ++j) {
            ASSERT_NE(v4[i].itemId, v4[j].itemId)
                << "v4 清单重复 itemId：index " << i << " 与 " << j;
        }
    }
}
