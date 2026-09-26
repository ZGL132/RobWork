/**
 * @file   CollisionConsistencyContractTest.cpp
 * @brief  ④端口碰撞接入的契约面用例组（KinCollisionConsistency）——
 *         三入口一致的静态扫描（无本地碰撞副本/无 proximity 直链/唯一
 *         消费点，R-POL-2/V-25；acceptance 3）、无碰撞布尔开关的结构性
 *         检查（V13-01；acceptance 5）与 policy.resolved 依赖键声明
 *         （CON-06；acceptance 3）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/WP-15-T07.json acceptance 3（"本单元无
 *     本地碰撞副本、不做二次缓存判定（静态扫描＋运行断言）；不传阈值/
 *     模式参数（R-POL-5）、禁直链 sdurw_proximity（R-POL-2）；策略版本
 *     经 policy.resolved 依赖键＋快照 policyRef 入输入身份"）与
 *     acceptance 5（"不另存碰撞布尔开关……域值模型与配置 schema 均无
 *     开关字段（结构性检查）"）
 *   - units/kinematics.md §8.1（三入口一致由④端口保证）、§4.3
 *     （Conditional 依赖 collision-models——碰撞启用状态只读自 policy）、
 *     §9.6（碰撞两码行）
 *   - 先例：test/BuildRedLineTest.cpp（运行期源码扫描形态——unit root
 *     经编译定义注入；双模式可编译——纯文件系统＋core/policy 公共头）
 *
 * 双模式说明：本文件仅消费文件系统扫描＋产品头类型面（不触 rw 非模板
 * 类、不 include Collision.hpp——其消费面按登记口径集成模式专属），
 * 冒烟模式同批编译执行；真实④端口路径的行为用例见 test/
 * CollisionPortTest.cpp（集成模式专属——policy 侧同因 gating）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/kinematics/DiagCodes.hpp>
#include <sdurws/ird/kinematics/Evaluators.hpp>
#include <sdurws/ird/kinematics/Ik.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace evidence = sdurws::ird::evidence;
namespace kin = sdurws::ird::kinematics;

namespace {

/// kinematics 单元树根（industrialrobot 目录——IRD_KINEMATICS_UNIT_ROOT
/// 注入；BuildRedLineTest 同款）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_KINEMATICS_UNIT_ROOT};
    return dir;
}

/// 收集产品面（include/＋src/）全部 C++ 源文件路径（BuildRedLineTest
/// 同款收集域——测试面不在扫描域内）。
std::vector<fs::path> collectProductFaceFiles()
{
    std::vector<fs::path> files;
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot() / "kinematics" / sub;
        if (!fs::exists(base)) {
            ADD_FAILURE() << "kinematics 产品面目录缺失: " << sub;
            continue;
        }
        std::error_code iec;
        for (auto it = fs::recursive_directory_iterator(base, iec);
             it != fs::recursive_directory_iterator(); it.increment(iec)) {
            if (iec || !it->is_regular_file(iec)) { continue; }
            const auto ext = it->path().extension().string();
            if (ext == ".hpp" || ext == ".h" || ext == ".cpp" || ext == ".ipp") {
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

// ---- 结构性检查的检测元函数（C++17 无 requires——void_t 探测形态）----

template <typename T, typename = void>
struct HasCollisionEnabledMember : std::false_type {};

template <typename T>
struct HasCollisionEnabledMember<T, std::void_t<decltype(std::declval<T>().collisionEnabled)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasEnableCollisionMember : std::false_type {};

template <typename T>
struct HasEnableCollisionMember<T, std::void_t<decltype(std::declval<T>().enableCollision)>>
    : std::true_type {};

}  // namespace

// =====================================================================
// acceptance 3——静态扫描（无本地碰撞副本/无 proximity 直链/唯一消费点）
// =====================================================================

/**
 * R-POL-2/V-25 静态扫描（acceptance 3）：产品面（include/＋src/）零
 * proximity 痕迹（include 指令/策略/几何/检测器类型名——碰撞实现唯一
 * 归 policy 的机械证据）；碰撞会话的唯一构建点＝src/Collision.cpp
 * （policy/CollisionEvaluator.hpp 的 include 与 createSession 调用都
 * 只允许在该 TU——"无第二装配点"的结构保证）。运行断言（无二次缓存）
 * 见 test/CollisionPortTest.cpp 的计数后端用例——两防线分工登记于
 * 单元卡 §14.6 v0.7。
 */
TEST(KinCollisionConsistency, StaticScanNoCollisionImplementationCopy_WP15T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-05", "NFR-COR-05"},
                  std::vector<std::string>{"AT-19"});

    // R-POL-2：零 proximity 直链痕迹（类型名/token 任一出现即旁路嫌疑
    // ——本单元消费碰撞只经 policy 会话的 ObjectId 级判定值）。
    static const char* kForbiddenTokens[] = {
        "rw/proximity",        ///< proximity 头 include
        "sdurw_proximity",     ///< proximity 库链接（唯一许可方＝policy）
        "ProximityStrategy",   ///< 检测器策略类型（实现载体）
        "ProximitySetup",      ///< 过滤规则集类型（policy 会话内部）
        "CollisionStrategy",   ///< 碰撞策略基类
        "ProximityModel",      ///< 检测器模型类型
        "doInCollision",       ///< 逐对查询入口（实现面）
    };
    const std::vector<fs::path> files = collectProductFaceFiles();
    ASSERT_FALSE(files.empty()) << "产品面为空——扫描域缺失";

    // 碰撞消费单点＝Collision.hpp（④端口类型面——唯一允许 include policy
    // 碰撞公共头的头）＋Collision.cpp（唯一实现 TU）。
    const auto isCollisionConsumptionPoint = [](const fs::path& file) {
        const std::string name = file.filename().string();
        return name == "Collision.cpp" || name == "Collision.hpp";
    };

    std::size_t evaluatorHeaderIncludes = 0;
    std::size_t createSessionCallSites = 0;
    for (const fs::path& file : files) {
        const std::string text = readFile(file);
        for (const char* token : kForbiddenTokens) {
            EXPECT_EQ(text.find(token), std::string::npos)
                << "产品面出现 proximity 痕迹（R-POL-2——碰撞实现唯一归 "
                   "policy）: " << token << " @ " << file.string();
        }
        // 唯一消费点：policy 碰撞公共头的 include 与 createSession 调用
        // 只允许在 Collision.hpp/Collision.cpp（无第二装配点——三入口
        // 一致的装配单点；文件名以路径尾段判别，跨平台分隔符无关）。
        if (text.find("policy/CollisionEvaluator.hpp") != std::string::npos) {
            ++evaluatorHeaderIncludes;
            EXPECT_TRUE(isCollisionConsumptionPoint(file))
                << "policy/CollisionEvaluator.hpp 只允许被 Collision.hpp/"
                   "Collision.cpp 消费（碰撞消费单点——V-25）: "
                << file.string();
        }
        if (text.find("createSession") != std::string::npos) {
            ++createSessionCallSites;
            EXPECT_TRUE(isCollisionConsumptionPoint(file))
                << "createSession 只允许在 Collision.hpp/Collision.cpp 出现"
                   "（会话装配单点——三入口一致）： "
                << file.string();
        }
    }
    // 单点存在性：消费单点头与实现 TU 各恰 1（源文件存在性不随构建
    // 模式变化）。
    EXPECT_EQ(evaluatorHeaderIncludes, 2U)
        << "policy/CollisionEvaluator.hpp 的产品面 include 恰 2 处"
           "（Collision.hpp 类型面＋Collision.cpp 实现）";
    EXPECT_GE(createSessionCallSites, 2U)
        << "消费单点应存在 createSession 面（文档＋接线入口）";
}

// =====================================================================
// acceptance 5——无碰撞布尔开关的结构性检查（V13-01）
// =====================================================================

/**
 * V13-01 结构性检查（acceptance 5）：域值模型与查询/请求 schema 均无
 * 碰撞启用开关成员——碰撞启用状态唯一读取源是 policy（接线入口
 * makeCollisionSession 读 policy.collision.enabled；登记随卡 §14.6
 * v0.7）。语义澄清（登记口径）：BatchDemands.collisionFreeRequired 是
 * requirements 投影的**要求值**（REQ-04 语义——"要不要查"），不是启用
 * 开关（"能不能查"）——后者在本单元 schema 面不存在。
 */
TEST(KinCollisionConsistency, StructuralNoCollisionEnableSwitch_WP15T07_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-05"},
                  std::vector<std::string>{"V13-01"});

    // 请求/查询 schema 面四型（碰撞会话指针的承载类型——开关若存在
    // 必然落在这些类型上）。
    EXPECT_FALSE(HasCollisionEnabledMember<kin::IkRequest>::value)
        << "IkRequest 不得有 collisionEnabled 成员（V13-01——启用状态只读"
           "自 policy）";
    EXPECT_FALSE(HasCollisionEnabledMember<kin::TaskPointIkQuery>::value)
        << "TaskPointIkQuery 不得有 collisionEnabled 成员（V13-01）";
    EXPECT_FALSE(HasCollisionEnabledMember<kin::BatchQuery>::value)
        << "BatchQuery 不得有 collisionEnabled 成员（V13-01）";
    EXPECT_FALSE(HasCollisionEnabledMember<kin::RegionCoverageQuery>::value)
        << "RegionCoverageQuery 不得有 collisionEnabled 成员（V13-01）";
    EXPECT_FALSE(HasEnableCollisionMember<kin::IkRequest>::value)
        << "IkRequest 不得有 enableCollision 成员（V13-01）";
    EXPECT_FALSE(HasEnableCollisionMember<kin::BatchQuery>::value)
        << "BatchQuery 不得有 enableCollision 成员（V13-01）";
    EXPECT_FALSE(HasEnableCollisionMember<kin::RegionCoverageQuery>::value)
        << "RegionCoverageQuery 不得有 enableCollision 成员（V13-01）";
    EXPECT_FALSE(HasEnableCollisionMember<kin::TaskPointIkQuery>::value)
        << "TaskPointIkQuery 不得有 enableCollision 成员（V13-01）";
}

// =====================================================================
// acceptance 3/5——policy.resolved 依赖键（CON-06）＋collision-models
// 条件依赖（V13-01 的切片面）
// =====================================================================

/**
 * CON-06/V13-01 依赖声明面（acceptance 3/5）：四个评估器的 descriptor
 * 全部声明 policy.resolved（策略变更→切片失效——策略版本入输入身份的
 * 依赖键半区；快照 policyRef 半区归快照/evidence 切片面）；region-
 * coverage 另声明 collision-models 条件依赖（策略未启用不进切片——
 * T06 已落位，本用例防回归钉住）。
 */
TEST(KinCollisionConsistency, PolicyResolvedDependencyInAllDescriptors_WP15T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-05", "NFR-COR-05"},
                  std::vector<std::string>{"CON-06", "V13-01"});

    // 逐 descriptor 核对 policy.resolved（Required——任一缺失＝策略版本
    // 脱离失效面，CON-06 违约）。
    const std::vector<evidence::EvaluatorDescriptor> descriptors = {
        kin::makePoseMetricsDescriptor(),
        kin::makeTaskPointIkDescriptor(),
        kin::makeTaskPointsBatchDescriptor(),
        kin::makeRegionCoverageDescriptor(),
    };
    for (const evidence::EvaluatorDescriptor& d : descriptors) {
        bool hasPolicyResolved = false;
        for (const evidence::DependencyDeclaration& decl : d.inputs) {
            if (decl.key == "policy.resolved") {
                hasPolicyResolved = true;
                EXPECT_EQ(decl.kind, evidence::DependencyKind::Policy);
                EXPECT_EQ(decl.requiredness, evidence::DependencyRequiredness::Required)
                    << d.key << "：policy.resolved 应为 Required（CON-06）";
            }
        }
        EXPECT_TRUE(hasPolicyResolved)
            << d.key << " 缺 policy.resolved 依赖声明（CON-06——策略变更"
                         "→切片失效的依赖键）";
    }

    // region-coverage 的 collision-models 条件依赖（§4.3 行——策略未启用
    // 碰撞时该键不进切片；V13-01 的切片面载体，T06 落位回归钉住）。
    const evidence::EvaluatorDescriptor coverage = descriptors[3];
    bool hasConditionalCollisionModels = false;
    for (const evidence::DependencyDeclaration& decl : coverage.inputs) {
        if (decl.key == "collision-models") {
            hasConditionalCollisionModels = true;
            EXPECT_EQ(decl.requiredness, evidence::DependencyRequiredness::Conditional)
                << "collision-models 应为 Conditional（V13-01——策略未启用"
                   "不进切片）";
        }
    }
    EXPECT_TRUE(hasConditionalCollisionModels)
        << "kin.region-coverage 缺 collision-models 条件依赖（§4.3 行）";
}

// =====================================================================
// acceptance 2——碰撞两码的在册性（产码面素材核对锚）
// =====================================================================

/**
 * 碰撞两码常量在册（§9.6 行 8/9——产码面随 T07 落位的码值核对锚；行为
 * 面用例见 TaskPointIkEvaluatorContractTest/TaskPointsBatchEvaluator-
 * ContractTest/CollisionPortTest 的具名用例）。断言两常量非空且前缀
 * 正确——码值文本的注册权威在 DiagCodes.cpp 工厂（DiagCodesTest 全表
 * 断言），此处防头常量意外漂移。
 */
TEST(KinCollisionConsistency, CollisionDiagCodeConstantsRegistered_WP15T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-05"},
                  std::vector<std::string>{});

    EXPECT_GT(std::string(kin::kKinCollisionFiltered).size(), 0U);
    EXPECT_EQ(std::string(kin::kKinCollisionFiltered).substr(0, 4), "KIN-");
    EXPECT_EQ(std::string(kin::kKinCollisionFiltered), "KIN-COLLISION-FILTERED");
    EXPECT_GT(std::string(kin::kKinCollisionUnavailable).size(), 0U);
    EXPECT_EQ(std::string(kin::kKinCollisionUnavailable).substr(0, 4), "KIN-");
    EXPECT_EQ(std::string(kin::kKinCollisionUnavailable), "KIN-COLLISION-UNAVAILABLE");
}
