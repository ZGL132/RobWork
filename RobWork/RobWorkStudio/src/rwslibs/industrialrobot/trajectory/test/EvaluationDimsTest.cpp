/**
 * @file   EvaluationDimsTest.cpp
 * @brief  评估维度扩展点用例组（TrjEvaluationDims，WP-16-T11 铺设＋
 *         WP-16-T14 递进）——TRJ-08"接口约束"的运行期自证面：载荷
 *         kindToken 词表封闭＋消费分派语义（§6.6.1）、trj Profile v1 注
 *         册集冻结（§13.3 实现落位冻结）＋v2 递进（TRJ-08-S1 启用——
 *         §12.7/§19.1 交接物）、零占位守护（§6.5 反向约束——TRJ-08 禁
 *         止项的机器可断言表达）。§20 追踪矩阵 TRJ-08 行"评审项（T11 验
 *         收）＋零占位断言"与 TRJ-08-S1 行的执行承载。
 *
 * 设计依据：
 *   - units/trajectory.md §6.5（S1 的承载面＝payload kindToken v2＋
 *     Profile 版本递进——不建 TimedSample jerk 成员/quality jerk 指标/
 *     jerk 诊断码）、§6.6（扩展机制四条）、§12.7（可扩展维度接口——
 *     S1 启用：新运动律 token＋限值消费＋payload v2；trj.jerk-limit-
 *     record 不在 v1 Profile 注册）、§13.3（trj Profile v1 五行——
 *     itemId"实现落位冻结"）、§13.4（payload kindToken 基线）、§19.1
 *     （S1 行交接物）、§18 T11/T14 行
 *   - 需求 TRJ-08（P1/C/R1——接口保持可扩展，不创建空字段、空页面或占
 *     位模块）、TRJ-08-S1（P1/R2——jerk 上限约束，分期交付 V13-02）
 *   - 任务契约 tasks/foundation/WP-16-T11.json（acceptance 1/2——已验
 *     收铺设）、tasks/foundation/WP-16-T14.json（acceptance 2——扩展点
 *     消费经词表，零空字段/零占位）
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
 * 通道的首次真实递进：当前产出值 v2、v1 转历史已知值（词表两项——本用
 * 例断言随递进演进，递进纪律本身〔只增不删/精确串等值/未知拒绝〕不变）。
 */
TEST(TrjEvaluationDims, PayloadKindTokenV1VocabularyAndDispatch_WP16T11_ACC1)
{
    IRD_TEST_INFO("TRJ-08", {}, std::nullopt);

    // 唯一书写点值＝S1 启用批递进后的当前产出值（消费/产出两侧引用同一
    // 常量——第二处字面量漂移＝两个 schema 假装同一个；该纪律由契约测试
    // 源码扫描机器化，此处锁定常量值本身）。
    EXPECT_EQ(trj::kTrjPayloadKindToken, "trj.sequence-plan.v2");
    // 历史已知值常量＝v1 词形（旧 payload 永久可读的词表承载——PA-2）。
    EXPECT_EQ(trj::kTrjPayloadKindTokenV1, "trj.sequence-plan.v1");

    // 已知值：当前产出 v2 与历史 v1 都判定 true（v1 载荷永久可读——
    // 词表只增不删；两值各对应一个真实 schema 版本分支）。
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v2"));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v1"));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindToken));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV1));

    // 未知值族：全部判定 false——消费面必须拒绝而非静默按任一已知版本
    // 解读（NFR-COR-03 不静默；逐条代表一类真实拒收场景）：
    //   - 未来版本（S2/S3 递进后的 v3 回读旧程序——版本递进不回写旧判
    //     定面；v2 尚未启用时同类场景由 T11 原断言覆盖，递进后 v3 顶替
    //     其"未来值"位置）；
    //   - 大小写漂移（token 是身份面词形，不做规范化——宽容即身份漂移通道）；
    //   - 首尾空白（精确串等值——词形即身份；v2/v1 各验一侧）；
    //   - 域前缀缺失（他域 token 不属于本域词表）；
    //   - 空串与损坏数据（载荷缺 token 字段的现实形态）。
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v3"));
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken("TRJ.SEQUENCE-PLAN.V2"));
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken("TRJ.SEQUENCE-PLAN.V1"));
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken(" trj.sequence-plan.v2"));
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v1 "));
    EXPECT_FALSE(trj::isKnownTrjPayloadKindToken("sequence-plan.v2"));
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
 *   b. SegmentSpaceType 词表恰两值——无 ContinuousProcess 值（§6.5 第
 *      三条；插入/追加新值都会使两值断言之一偏移、static_assert 爆，
 *      强制走需求变更＋本卡修订流程）；
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

    // (b) SegmentSpaceType 词表恰两值（JointLinear=0、CartesianLine=1）。
    //     新增 S3 的 ContinuousProcess＝需求变更＋本卡修订＋实现＋黄金算
    //     例同批（§6.2/§6.5）——任何"顺手加一个枚举值"都会使两值断言之
    //     一偏移， 此断言即报警。
    static_assert(static_cast<std::uint8_t>(trj::SegmentSpaceType::JointLinear)
                      == 0,
        "SegmentSpaceType 词表第一值必须为 JointLinear（§6.2 封闭两值词表）");
    static_assert(static_cast<std::uint8_t>(trj::SegmentSpaceType::CartesianLine)
                      == 1,
        "SegmentSpaceType 词表出现第三值（如 ContinuousProcess）——TRJ-08-S3 "
        "必须走需求变更＋单元卡修订，不得静默扩词表（§6.5/§6.6）");

    // (c) TimedSample 无 jerk 成员（§6.5 第一条/§12.1 不预留字段——编译
    //     期负向存在性断言；占位添加 jerk 成员即编译失败）。
    static_assert(!HasMemberJerk<trj::TimedSample>::value,
        "TimedSample 出现 jerk 成员——TRJ-08-S1 启用必须走单元卡修订＋实现"
        "＋黄金算例同批（§6.5 不预留字段/§6.6 词表纪律），不得静默占位");

    // (d) SegmentConstraint 无工艺速度槽位（§6.5 第二条——候选名族负向
    //     探测；S2 启用时的正式槽位名以本卡修订登记为准，届时本断言随
    //     同一修订批移除——修订与断言移除不可分离）。
    static_assert(!HasMemberProcessSpeedTcp<trj::SegmentConstraint>::value,
        "SegmentConstraint 出现 processSpeedTcp 槽位——TRJ-08-S2 启用必须"
        "走单元卡修订（§6.5 不建工艺速度字段），不得静默占位");
    static_assert(!HasMemberProcessSpeed<trj::SegmentConstraint>::value,
        "SegmentConstraint 出现 processSpeed 槽位——TRJ-08-S2 启用必须"
        "走单元卡修订（§6.5 不建工艺速度字段），不得静默占位");

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
 * 是判定面本身）。WP-16-T14 批递进后：当前产出组合为 v2 载荷＋v2 注册
 * 集（TRJ-08-S1 现形），v1 载荷＋v1 集合为历史可读组合。
 */
TEST(TrjEvaluationDims, ConsumerDispatchProtocol_WP16T11_ACC1)
{
    IRD_TEST_INFO("TRJ-08", {}, std::nullopt);

    // 接受路径（当前产出面）：v2 载荷＋v2 注册项（TRJ-08-S1 启用批的
    // 合法组合——含 S1 新增 jerk 项与 v1 继承项两侧）。
    const bool acceptV2 =
        trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindToken)
        && trj::isTrjProfileV2ItemId(trj::kTrjItemIdPathAndTimeRecord)
        && trj::isTrjProfileV2ItemId(trj::kTrjItemIdJerkLimitRecord);
    EXPECT_TRUE(acceptV2);

    // 接受路径（历史可读面）：v1 载荷＋v1 注册项（旧 payload 永久可读
    // ——递进不回写旧判定面，PA-2）。
    const bool acceptV1Legacy =
        trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV1)
        && trj::isTrjProfileV1ItemId(trj::kTrjItemIdPathAndTimeRecord);
    EXPECT_TRUE(acceptV1Legacy);

    // 拒绝路径一：载荷未知（未来 v3 token 回读——§6.6.1 消费面必须拒绝，
    // 即使 itemId 已注册；schema 版本不一致＝不可解读）。
    const bool rejectUnknownToken =
        trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v3")
        && trj::isTrjProfileV1ItemId(trj::kTrjItemIdAnimationData);
    EXPECT_FALSE(rejectUnknownToken);

    // 拒绝路径二：证据项未注册（S1 维度项冒入 v1——§12.7 不在 v1 Profile
    // 注册；即使载荷 token 合法也拒绝——Profile 集合封闭性独立于载荷版本；
    // v2 载荷配 v1 判定面同理只认 v1 五项，S2/S3 未注册词形同样拒绝）。
    const bool rejectUnregisteredItem =
        trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV1)
        && trj::isTrjProfileV1ItemId("trj.jerk-limit-record");
    EXPECT_FALSE(rejectUnregisteredItem);
    const bool rejectV2PredicateOnS2Shape =
        trj::isTrjProfileV2ItemId("trj.process-speed-record");
    EXPECT_FALSE(rejectV2PredicateOnS2Shape);
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
