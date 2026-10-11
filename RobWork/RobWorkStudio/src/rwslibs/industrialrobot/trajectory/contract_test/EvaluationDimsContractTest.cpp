/**
 * @file   EvaluationDimsContractTest.cpp
 * @brief  评估维度扩展点的契约测试（WP-16-T11 铺设＋WP-16-T14 递进＋
 *         WP-16-T15 递进）——TRJ-08 接口约束的交付面契约：①载荷
 *         kindToken 与 trj Profile 证据项 ID 的**唯一书写点**（产品面源
 *         码扫描——词表值字面量在代码中各恰一次且均在 EvaluationDims.
 *         hpp，禁第二处字面量漂移）；②接口交付面词形契约（点分小写
 *         kebab／版本后缀——经接口消费逐项钉扎）。
 *
 * 设计依据：
 *   - units/trajectory.md §6.6（扩展机制——载荷版本化/Profile 版本化的
 *     版本锚必须单一，"静默维度"禁止）、§12.7（可扩展维度接口——S1 启
 *     用：payload v2＋trj.jerk-limit-record；S2 启用：段约束扩展版本＋
 *     trj.process-speed-record）、§13.3（trj Profile 行"实现落位冻结"）、
 *     §13.4（payload kindToken 基线）、§20 追踪矩阵 TRJ-08 行（评审项＋
 *     零占位断言）
 *   - 需求 TRJ-08（接口保持可扩展，不创建空字段、空页面或占位模块）、
 *     TRJ-08-S1（P1/R2——jerk 上限约束）、TRJ-08-S2（P1/R2——工艺速
 *     度约束：指定段 TCP 速度受限或恒定）
 *   - AGENTS §2.5/仓库域常量纪律（词表值唯一书写点——"禁字符串拼码/
 *     第二处字面量"，DiagCodes.hpp 码值纪律同源）
 *   - 任务契约 tasks/foundation/WP-16-T11.json（acceptance 1/2——铺设
 *     已验收）、tasks/foundation/WP-16-T14.json（acceptance 2——已验
 *     收）、tasks/foundation/WP-16-T15.json（acceptance 2）
 *
 * 本套件为**两模式皆跑**（非条件源）：被测面 EvaluationDims.hpp 纯值面
 * （零 rw 符号零上游依赖——P-TRJ-11 暴露面为零）；源码扫描用例与
 * BuildGraphContractTest 同款基础设施（IRD_TRAJECTORY_UNIT_ROOT 注入源
 * 码树根，两模式皆可定位源码树）。
 *
 * 扫描口径说明（为什么不锁运动律 token 词形）：本头十二词表值（payload
 * kindToken v4/v3/v2/v1＋八 itemId——v4 递进批新增 v4 token 与连续工艺
 * 边界项）的**判定/注册语义**落在本头单点，字面量在产品面代码中出现第二次
 * 即第二判定面（漂移通道）。运动律 token 的词表定义单点在 PlanConfig.
 * hpp（既有 T03/T08/T14 验收面），src/Playback.cpp 与 TimeParam.cpp 错
 * 误文案中的词形引用是文案呈现而非判定逻辑（不构成第二词表副本），故
 * 不作字面量计数对象——运动律词表纪律由 test/EvaluationDimsTest.cpp 的
 * 词表值断言与 TimeParamTest 的分派拒绝族承载。
 */

#include <sdurws/ird/trajectory/EvaluationDims.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace trj = sdurws::ird::trajectory;

namespace {

/// trajectory 单元树根（industrialrobot 目录——IRD_TRAJECTORY_UNIT_ROOT
/// 注入；BuildGraphContractTest 同款）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_TRAJECTORY_UNIT_ROOT};
    return dir;
}

/// 收集产品面（include/＋src/）全部 C++ 源文件；任一目录缺失以 ADD_
/// FAILURE 显性报出（辅助函数非 void，不能用 ASSERT_*）。
std::vector<fs::path> collectProductFaceFiles()
{
    std::vector<fs::path> files;
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot() / "trajectory" / sub;
        if (!fs::exists(base)) {
            ADD_FAILURE() << "trajectory 产品面目录缺失: " << sub;
            continue;
        }
        std::error_code iec;
        for (auto it = fs::recursive_directory_iterator(base, iec);
             it != fs::recursive_directory_iterator(); it.increment(iec)) {
            if (iec || !it->is_regular_file(iec)) { continue; }
            const auto ext = it->path().extension().string();
            if (ext == ".hpp" || ext == ".h" || ext == ".cpp") {
                files.push_back(it->path());
            }
        }
    }
    return files;
}

/// 读取文件全文；不可读显性失败。
std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << path.string();
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 线性单遍注释剥离器（BuildRedLineTest 同款状态机——注释内容替换为空
/// 格、行结构保留；字符串/字符字面量内容原样保留。不用 regex：块注释的
/// 正则形态在 MSVC std::regex 回溯实现上对长注释会爆栈——既有实测结论）。
/// 本用例语义：剥注释后扫描＝只统计**代码中**的字面量（头文件 Doxygen
/// 注释里对卡面原文的引用不算字面量出现——文档引用不是判定面）。
std::string stripComments(const std::string& src)
{
    enum class State { Code, LineComment, BlockComment, StrLiteral, CharLiteral };
    std::string out;
    out.reserve(src.size());
    State state = State::Code;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const char ch = src[i];
        const char next = (i + 1 < src.size()) ? src[i + 1] : '\0';
        switch (state) {
        case State::Code:
            if (ch == '/' && next == '/') {           // 进入行注释
                state = State::LineComment;
                out += "  ";
                ++i;
            } else if (ch == '/' && next == '*') {    // 进入块注释
                state = State::BlockComment;
                out += "  ";
                ++i;
            } else if (ch == '"') {                   // 进入字符串字面量
                state = State::StrLiteral;
                out += ch;
            } else if (ch == '\'') {                  // 进入字符字面量
                state = State::CharLiteral;
                out += ch;
            } else {
                out += ch;
            }
            break;
        case State::LineComment:
            if (ch == '\n') {                         // 行注释止于换行
                state = State::Code;
            }
            out += (ch == '\n') ? '\n' : ' ';
            break;
        case State::BlockComment:
            if (ch == '*' && next == '/') {           // 块注释止于 */
                state = State::Code;
                out += "  ";
                ++i;
            } else {
                out += (ch == '\n') ? '\n' : ' ';
            }
            break;
        case State::StrLiteral:
            if (ch == '\\') {                         // 转义对整体保留跳读
                out += ch;
                if (i + 1 < src.size()) { out += src[i + 1]; }
                ++i;
            } else {
                out += ch;
                if (ch == '"') { state = State::Code; }
            }
            break;
        case State::CharLiteral:
            if (ch == '\\') {
                out += ch;
                if (i + 1 < src.size()) { out += src[i + 1]; }
                ++i;
            } else {
                out += ch;
                if (ch == '\'') { state = State::Code; }
            }
            break;
        }
    }
    return out;
}

/// 唯一书写点扫描的对象集（词表值字面量——各应在产品面代码中恰出现 1
/// 次，且所在文件均为 EvaluationDims.hpp；行序无语义，仅清单。WP-16-T16
/// 批递进：v4 token 为当前产出值、v3/v2/v1 为历史已知值常量——四词形各
/// 恰一处；连续工艺边界项为 v4 Profile 新增行）。
constexpr const char* kSingleWriteSiteLiterals[] = {
    "trj.sequence-plan.v4",           // 载荷 kindToken 当前产出（§6.6.1 三次递进）
    "trj.sequence-plan.v3",           // 载荷 kindToken 历史已知值（WP-16-T16 批表尾追加）
    "trj.sequence-plan.v2",           // 载荷 kindToken 历史已知值（WP-16-T15 批表尾追加）
    "trj.sequence-plan.v1",           // 载荷 kindToken 历史已知值（PA-2）
    "trj.path-and-time-record",       // Profile v1 行 1（§13.3）
    "trj.smooth-recheck-evidence",    // Profile v1 行 2
    "trj.cartesian-ik-continuity",    // Profile v1 行 3
    "trj.cycle-phase-breakdown",      // Profile v1 行 4
    "trj.animation-data",             // Profile v1 行 5
    "trj.jerk-limit-record",          // Profile v2 行 6（§12.7——TRJ-08-S1）
    "trj.process-speed-record",       // Profile v3 行 7（§12.7 点名词形族——TRJ-08-S2）
    "trj.continuous-process-boundary",// Profile v4 行 8（§12.7 点名词形族——TRJ-08-S3）
};

/// 唯一书写点常量所在的头文件名（出现处断言的目标文件——文件名级断言
/// 而非全路径，规避构建树/源码树路径差异）。
constexpr const char* kSingleWriteSiteFile = "EvaluationDims.hpp";

}  // namespace

/**
 * 唯一书写点契约（acceptance 2——零占位/零漂移的源码面守护）：六个词表
 * 值字面量在产品面（include/**＋src/**）**剥注释后**各恰出现一次，且出
 * 现文件均为 EvaluationDims.hpp。第二次出现＝第二判定面（词表值漂移通
 * 道——两个 schema 假装同一个）；出现在他处＝词表管理权旁落（§6.6 版
 * 本锚必须单点）。test/**、contract_test/** 不在扫描域（测试面自持字
 * 面清单是失同步防线的合法形态——DiagCodesTest kSection144FullTable
 * 同款纪律）。
 */
TEST(TrjEvaluationDimsContract, KindTokenAndItemIdsSingleWriteSite_WP16T11_ACC2)
{
    IRD_TEST_INFO("TRJ-08", {}, std::nullopt);

    // 逐字面量在产品面代码（剥注释后）计数出现次数与所在文件。
    for (const std::string_view literal : kSingleWriteSiteLiterals) {
        std::size_t hits = 0;
        std::string hitFile;
        for (const fs::path& path : collectProductFaceFiles()) {
            const std::string stripped = stripComments(readFile(path));
            // 子串计数（非重叠——词表值互不为彼此前缀的子串：六值之间
            // 无包含关系，计数语义无歧义）。
            for (std::size_t pos = stripped.find(literal);
                 pos != std::string::npos;
                 pos = stripped.find(literal, pos + literal.size())) {
                ++hits;
                hitFile = path.filename().string();
            }
        }
        ASSERT_EQ(hits, 1U)
            << "词表值字面量在产品面代码中出现 " << hits
            << " 次（应为恰 1 次——唯一书写点）: " << literal;
        EXPECT_EQ(hitFile, kSingleWriteSiteFile)
            << "词表值字面量出现于 " << hitFile
            << "（应仅在 EvaluationDims.hpp——版本锚单点）: " << literal;
    }
}

/**
 * 接口交付面词形契约（acceptance 1——经接口消费的交付面钉扎）：Profile
 * v1/v2/v3 清单经 trjProfileV1Items()/trjProfileV2Items()/trjProfileV3-
 * Items() 遍历消费，逐项校验词形与注册资格——
 *   - itemId 词形＝"trj." 前缀＋点分小写 kebab（全小写字母/数字/连字
 *     符/点，无空白无下划线——证据项 ID 是身份面词形，跨域引用与黄金
 *     数据集按词形精确匹配）；
 *   - 每项过对应版本 isTrjProfileV{1,2,3}ItemId（判定面与清单单一数据
 *     源的一致性——在真实消费序上验证，非仅常量值对照）；
 *   - payload kindToken 词形＝itemId 词形＋".v<数字>"版本后缀（§13.4
 *     形态；词表锚 v3——WP-16-T15 批递进，v2/v1 为历史锚）。
 * 词形漂移（大写/下划线/空格）即破坏跨域消费契约——优化域 ③端口组合
 * （§19.2"评估键/Profile/payload 契约稳定"）与黄金数据集均按词形引用。
 */
TEST(TrjEvaluationDimsContract, InterfaceDeliveryFaceVocabularyShape_WP16T11_ACC1)
{
    IRD_TEST_INFO("TRJ-08", {}, std::nullopt);

    // itemId 词形：^trj\.[a-z0-9-]+$（点仅限 "trj." 前缀处；段内小写
    // kebab——§13.3 表第一列全部词形归纳）。
    const std::regex itemIdShape{R"(^trj\.[a-z0-9-]+$)"};
    // kindToken 词形：^trj\.[a-z0-9-]+\.v[0-9]+$（§13.4 "trj.sequence-
    // plan.v1"——域前缀＋名段＋版本段三段式）。
    const std::regex kindTokenShape{R"(^trj\.[a-z0-9-]+\.v[0-9]+$)"};

    // 经接口消费：遍历 v1 注册集（非自持字面清单——直接消费交付面函数）。
    const std::array<trj::TrjEvidenceItemSpec, 5> items = trj::trjProfileV1Items();
    ASSERT_EQ(items.size(), 5U);
    for (const trj::TrjEvidenceItemSpec& item : items) {
        EXPECT_TRUE(std::regex_match(std::string(item.itemId), itemIdShape))
            << "itemId 词形违约（应为 trj. 前缀点分小写 kebab）: "
            << item.itemId;
        // 判定面与清单同源一致（真实消费序：先清单后判定）。
        EXPECT_TRUE(trj::isTrjProfileV1ItemId(item.itemId))
            << "v1 清单内项未过注册判定: " << item.itemId;
    }

    // v2 递进集合同面钉扎（WP-16-T14 批——词形/判定面/集合封闭性）。
    const std::array<trj::TrjEvidenceItemSpec, 6> itemsV2 =
        trj::trjProfileV2Items();
    ASSERT_EQ(itemsV2.size(), 6U);
    for (const trj::TrjEvidenceItemSpec& item : itemsV2) {
        EXPECT_TRUE(std::regex_match(std::string(item.itemId), itemIdShape))
            << "v2 itemId 词形违约: " << item.itemId;
        EXPECT_TRUE(trj::isTrjProfileV2ItemId(item.itemId))
            << "v2 清单内项未过注册判定: " << item.itemId;
    }

    // v3 递进集合同面钉扎（WP-16-T15 批——TRJ-08-S2 启用的词形/判定面/
    // 集合封闭性；工艺速度项词形消费是本批 acceptance 2 的交付面）。
    const std::array<trj::TrjEvidenceItemSpec, 7> itemsV3 =
        trj::trjProfileV3Items();
    ASSERT_EQ(itemsV3.size(), 7U);
    for (const trj::TrjEvidenceItemSpec& item : itemsV3) {
        EXPECT_TRUE(std::regex_match(std::string(item.itemId), itemIdShape))
            << "v3 itemId 词形违约: " << item.itemId;
        EXPECT_TRUE(trj::isTrjProfileV3ItemId(item.itemId))
            << "v3 清单内项未过注册判定: " << item.itemId;
    }

    // v4 递进集合同面钉扎（WP-16-T16 批——TRJ-08-S3 启用的词形/判定面/
    // 集合封闭性；连续工艺边界项词形消费是本批 acceptance 2 的交付面）。
    const std::array<trj::TrjEvidenceItemSpec, 8> itemsV4 =
        trj::trjProfileV4Items();
    ASSERT_EQ(itemsV4.size(), 8U);
    for (const trj::TrjEvidenceItemSpec& item : itemsV4) {
        EXPECT_TRUE(std::regex_match(std::string(item.itemId), itemIdShape))
            << "v4 itemId 词形违约: " << item.itemId;
        EXPECT_TRUE(trj::isTrjProfileV4ItemId(item.itemId))
            << "v4 清单内项未过注册判定: " << item.itemId;
    }

    // payload kindToken 词形与词表锚（当前产出值 v4——历史 v3/v2/v1 词形经
    // kTrjPayloadKindTokenV2/V1 常量同面可读；词表只增不删）。
    EXPECT_TRUE(std::regex_match(std::string(trj::kTrjPayloadKindToken),
                                 kindTokenShape))
        << "payload kindToken 词形违约: " << trj::kTrjPayloadKindToken;
    EXPECT_TRUE(std::regex_match(std::string(trj::kTrjPayloadKindTokenV1),
                                 kindTokenShape))
        << "历史 kindToken 词形违约: " << trj::kTrjPayloadKindTokenV1;
    EXPECT_TRUE(std::regex_match(std::string(trj::kTrjPayloadKindTokenV2),
                                 kindTokenShape))
        << "历史 kindToken 词形违约: " << trj::kTrjPayloadKindTokenV2;
    EXPECT_TRUE(std::regex_match(std::string(trj::kTrjPayloadKindTokenV3),
                                 kindTokenShape))
        << "历史 kindToken 词形违约: " << trj::kTrjPayloadKindTokenV3;
    EXPECT_EQ(trj::kTrjPayloadKindToken, "trj.sequence-plan.v4");
    EXPECT_EQ(trj::kTrjPayloadKindTokenV1, "trj.sequence-plan.v1");
    EXPECT_EQ(trj::kTrjPayloadKindTokenV2, "trj.sequence-plan.v2");
    EXPECT_EQ(trj::kTrjPayloadKindTokenV3, "trj.sequence-plan.v3");
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindToken));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV1));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV2));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindTokenV3));

    // Profile 标识词形（§13.3 profileId="trj"——单段小写）。
    EXPECT_EQ(trj::kTrjProfileId, "trj");

    // 词表枚举封闭性（运行期"全值出现"断言——§13.3 表逐行对应：类别
    // 两值在 v1 清单、前四个适用条件值在 v1 清单各至少出现一次；WP-16-
    // T14 批新增的 JerkLimitedMotionLaw 由 v2 递进行承载、WP-16-T15 批
    // 新增的 ProcessSpeedConstrainedSegment 由 v3 递进行承载（下方 v2/v3
    // 遍历覆盖）。新增枚举值时本断言不变仍绿（新值可不在既有清单中），
    // 但下方 switch 的全值覆盖意图显性化——消费面处理新值的义务由评估
    // 器批按 Profile 版本递进承载，本头词表扩值＝本卡增量修订纪律，见
    // EvaluationDims.hpp 词表注释）。
    bool sawRequired = false;
    bool sawSuggested = false;
    bool sawAlways = false;
    bool sawHasCartesianSegment = false;
    bool sawTimeParamSucceeded = false;
    bool sawSuccessfulTrajectory = false;
    for (const trj::TrjEvidenceItemSpec& item : items) {
        switch (item.itemClass) {  // 全值覆盖无 default——枚举扩值时此处
                                   // 的覆盖意图即被破坏（review 可见点；
                                   // 扩值须随本卡 §13.3 增量修订）
        case trj::TrjEvidenceItemClass::Required:
            sawRequired = true;
            break;
        case trj::TrjEvidenceItemClass::Suggested:
            sawSuggested = true;
            break;
        }
        switch (item.applicability) {
        case trj::TrjEvidenceItemApplicability::Always:
            sawAlways = true;
            break;
        case trj::TrjEvidenceItemApplicability::HasCartesianSegment:
            sawHasCartesianSegment = true;
            break;
        case trj::TrjEvidenceItemApplicability::TimeParamSucceeded:
            sawTimeParamSucceeded = true;
            break;
        case trj::TrjEvidenceItemApplicability::SuccessfulTrajectory:
            sawSuccessfulTrajectory = true;
            break;
        case trj::TrjEvidenceItemApplicability::JerkLimitedMotionLaw:
            // v1 清单不含该值（WP-16-T14 批表尾追加，v2 递进行承载）——
            // 此分支仅为全值覆盖完备性（无 default 的编译期提醒）。
            break;
        case trj::TrjEvidenceItemApplicability::ProcessSpeedConstrainedSegment:
            // v1 清单不含该值（WP-16-T15 批表尾追加，v3 递进行承载）——
            // 同上全值覆盖完备性。
            break;
        case trj::TrjEvidenceItemApplicability::ContinuousProcessSegment:
            // v1 清单不含该值（WP-16-T16 批表尾追加，v4 递进行承载）——
            // 同上全值覆盖完备性。
            break;
        }
    }
    EXPECT_TRUE(sawRequired);
    EXPECT_TRUE(sawSuggested);
    EXPECT_TRUE(sawAlways);
    EXPECT_TRUE(sawHasCartesianSegment);
    EXPECT_TRUE(sawTimeParamSucceeded);
    EXPECT_TRUE(sawSuccessfulTrajectory);
    // 新增条件值经 v2/v3 清单真实承载（递进同批的机器断言——不只是枚举
    // 值存在，而是有清单行消费它）。
    bool sawJerkLimitedMotionLaw = false;
    for (const trj::TrjEvidenceItemSpec& item : itemsV2) {
        if (item.applicability
            == trj::TrjEvidenceItemApplicability::JerkLimitedMotionLaw) {
            EXPECT_EQ(item.itemId, trj::kTrjItemIdJerkLimitRecord);
            sawJerkLimitedMotionLaw = true;
        }
    }
    EXPECT_TRUE(sawJerkLimitedMotionLaw);
    bool sawProcessSpeedConstrainedSegment = false;
    for (const trj::TrjEvidenceItemSpec& item : itemsV3) {
        if (item.applicability
            == trj::TrjEvidenceItemApplicability::ProcessSpeedConstrainedSegment) {
            EXPECT_EQ(item.itemId, trj::kTrjItemIdProcessSpeedRecord);
            sawProcessSpeedConstrainedSegment = true;
        }
    }
    EXPECT_TRUE(sawProcessSpeedConstrainedSegment);
    // 新增条件值经 v4 清单真实承载（递进同批的机器断言——不只是枚举值
    // 存在，而是有清单行消费它）。
    bool sawContinuousProcessSegment = false;
    for (const trj::TrjEvidenceItemSpec& item : itemsV4) {
        if (item.applicability
            == trj::TrjEvidenceItemApplicability::ContinuousProcessSegment) {
            EXPECT_EQ(item.itemId,
                      trj::kTrjItemIdContinuousProcessBoundary);
            sawContinuousProcessSegment = true;
        }
    }
    EXPECT_TRUE(sawContinuousProcessSegment);
}
