/**
 * @file   BuildRedLineTest.cpp
 * @brief  execution 构建红线用例组（EX-BLD-1 前两项＋§1.4 直接包含禁令
 *         ＋P-EX-3/O-24 具名自证）——零 Qt/头路径布局（无私有头出
 *         include）自检。
 *
 * 设计依据：
 *   - units/execution.md §11 EX-BLD-1 行（构建红线：零 Qt〔含 worker
 *     目标〕、无私有头出 include/、依赖图仅四条登记边、产品目标零
 *     testkit——EX-T01 行验证方式取"EX-BLD-1 前两项"）、§3.2（依赖图：
 *     三条登记单元边＋标准库＋Win32——零 Qt 含 Core〔D-01〕、零
 *     runtime/policy/业务单元编译边、零 testkit）、§3.4/§3.5（头包含
 *     形式与 R-2 纪律、源码目录布局）、§2.2（O-24 四词表归 core——
 *     不可越界列）、§12 EX-T01 行（src/ 空起步——锚点翻译单元形态）；
 *   - 需求 NFR-MNT-01（计算内核零 Qt）、NFR-MNT-02（单元边界可维护）；
 *   - 任务契约 tasks/foundation/EX-T01.json acceptance 1（落位形态）/
 *     acceptance 2（红线扫描零命中；P-EX-3 处置：runtime/policy 经
 *     §3.3 最小接口注入消费零编译依赖）/acceptance 4（O-24 处置：四词表
 *     按 core 词表现状承接，execution 侧零重定义）；
 *   - 同构先例：project/test/BuildRedLineTest.cpp（PRJ-T01 同款落位形态，
 *     扫描面按 execution 卡红线裁剪）。
 *
 * 运行期源码扫描说明：扫描对象＝源码树（经 IRD_EXECUTION_UNIT_ROOT 注入
 * 的 industrialrobot 根下的 execution/），因此本用例对工作目录零依赖、在
 * 集成与独立冒烟两种模式下行为一致。CMake 配置期的链接面守卫（单元边/
 * 零 Qt/零 gtest）在 execution/CMakeLists.txt 文件末尾，与本用例互为两道
 * 防线（源码 include 面＋目标链接面）；构建图边（runtime/policy/
 * diagnostics/testkit）的 CMakeLists 文本扫描在 _contract_test 目标
 * （LinkageContractTest.cpp——§3.4 测试目标分工：跨单元契约面）。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// execution 单元树根（industrialrobot 目录——IRD_EXECUTION_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_EXECUTION_UNIT_ROOT};
    return dir;
}

/// 递归收集 C++ 源/头文件（相对路径、已排序——失败信息确定性，NFR-COR-02 精神）。
std::vector<fs::path> collectCppFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::exists(dir)) { return files; }
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        const auto ext = it->path().extension().string();
        if (ext == ".hpp" || ext == ".h" || ext == ".cpp") {
            files.push_back(fs::relative(it->path(), dir, ec));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// 全文读取；读失败显性失败（不留"读不到＝零命中"的假阳性通道）。
std::string readFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// 判断一行是否为 #include 预处理指令（跳过行首空白）。红线扫描只认指令行：
/// 注释散文中出现"sdurws/ird/core"等路径字样（如锚点翻译单元文件头的处置
/// 说明）不是 include 行为，按任意文本匹配会把注释误报为越界 include
/// （本项目注释密集且要求建立文档追溯，必须把"文档提及"与"编译期包含"
/// 区分开——PRJ-T01 实施期曾真实检出该误报并以此门控修正，即"测试真实
/// 失败能力"的执行证据）。
bool isIncludeDirective(const std::string& line)
{
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos) { return false; }
    return line.compare(first, 8, "#include") == 0;
}

}  // namespace

/**
 * 锚定：源码树可达（公共头保留位 README＋src/ 锚点翻译单元——落位期形态）。
 *
 * "src/ 空起步"的既定先例形态＝仅含注释的锚点翻译单元（CORE-T01/POL-T01/
 * DIAG-T02/PRJ-T01 同款）：CMake 对零源码 STATIC 库在生成期即报
 * "No SOURCES given to target"（实测），锚点文件是"库可生成、无接口预建"
 * （NFR-MNT-04）的最小载体。
 */
TEST(ExecutionBuild, SourceTreeReachable_DT_BUILD)
{
    ASSERT_TRUE(fs::exists(unitRoot())) << "IRD_EXECUTION_UNIT_ROOT 不存在";
    // 公共头保留位：单元卡 §3.1 组成表约定 12 个契约头随 EX-T02+ 落地，
    // 当前仅有 README.md（任务卡指向 §12——EX-T01 复核，见留痕）——保留位
    // 存在性是头路径布局检查的前提。
    ASSERT_TRUE(fs::exists(unitRoot() / "execution" / "include" / "sdurws" / "ird"
                           / "execution" / "README.md"))
        << "公共头保留位缺失（README.md 应存在）";
    // src/ 空起步：EX-T01 产物形态要求锚点翻译单元存在（STATIC 库可生成的
    // 最小条件），实现文件随 EX-T02+ 填充——扫描非空即锚点在位。
    ASSERT_FALSE(collectCppFiles(unitRoot() / "execution" / "src").empty())
        << "src 扫描为空（Execution.cpp 锚点翻译单元应存在）";
}

/**
 * O-24 处置自证（常驻守卫；EX-T02 起转正形态）：execution 侧零词表
 * 重定义。
 *
 * TaskState/TaskOutcome/EvaluationMode/EngineeringStatus 四词表按 core
 * 词表现状承接（sdurws/ird/core/Evaluation.hpp，九态词表归 core——本卡
 * §2.2 不可越界列）。EX-T01 落位期本检查钉住"公共头面仅 README 保留位"
 * （头未产出的过渡形态）；EX-T02 按 §3.1 组成表开始落契约头后，本检查
 * 转正为词表重定义扫描：四词表的任何定义形态（enum class ＋词表名）在
 * execution include 面零命中——消费一律经 core 公共头 include 行。
 * 五元组身份类型（RunId/AttemptId/TaskIdentity）的同类扫描在
 * _contract_test（IdentityVocabularyContractTest——跨单元契约面分工）。
 * O-24 本体在 DTB §4 仍为登记未决态，本用例只钉住"core 现状承接"的
 * 代码面口径，不构成消账（消账权在 DTB §4 登记流程/所有者）。
 */
TEST(ExecutionBuild, NoVocabularyRedefinition_O24_DT_BUILD)
{
    const auto incDir = unitRoot() / "execution" / "include" / "sdurws" / "ird"
                        / "execution";
    // 词表定义形态模式（enum class ＋词表名）——注释散文中的词表名提及
    // （设计追溯要求）不带 "enum class " 前缀，不误报。
    const std::vector<std::string> kVocabularyEnums = {
        "enum class TaskState", "enum class TaskOutcome",
        "enum class EvaluationMode", "enum class EngineeringStatus",
    };
    bool sawTaskTypesHeader = false;   // 消费面锚点：词表消费头应在位
    for (const auto& item : fs::directory_iterator(incDir)) {
        if (!item.is_regular_file()) { continue; }
        const auto rel = item.path().filename().string();
        if (rel == "TaskTypes.hpp") { sawTaskTypesHeader = true; }
        std::ifstream in(item.path(), std::ios::binary);
        ASSERT_TRUE(in.is_open()) << "无法读取公共头: " << rel;
        const std::string text{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
        for (const auto& pattern : kVocabularyEnums) {
            EXPECT_EQ(text.find(pattern), std::string::npos)
                << "execution 禁止重定义 core 词表（O-24/§2.2 不可越界列）: "
                << rel << " → " << pattern;
        }
        // 消费形态核对：TaskTypes.hpp 必须以 include 行承接 core 词表头
        // （EX-T01 落位期"零跨单元头消费"随 EX-T02 消费任务按计划解禁
        // ——P-EX-1 处置：按冻结后签名消费，diff 后增量同步）。
        if (rel == "TaskTypes.hpp") {
            EXPECT_NE(text.find("#include <sdurws/ird/core/Evaluation.hpp>"),
                      std::string::npos)
                << "词表承接应以 core 公共头 include 行表达";
        }
    }
    EXPECT_TRUE(sawTaskTypesHeader)
        << "TaskTypes.hpp 应在位（EX-T02 §3.1 组成表产物——本扫描的锚点）";
}

/** 零 Qt（含 Core——单元卡 D-01 比 L3 上限更严）：产品面零 Q 头（NFR-MNT-01；
 *  EX-BLD-1 第 1 项；worker 目标同口径——其构建登记随 EX-T06，届时本扫描
 *  面自动覆盖 worker/ 目录新增源文件）。 */
TEST(ExecutionBuild, NoQtInclude_DT_BUILD_NFR_MNT_01)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "execution" / sub)) {
            const auto text = readFile(unitRoot() / "execution" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (!isIncludeDirective(line)) { continue; }
                // Qt 头只有 <QXxx>/"QXxx" 两种包含形态；`#include <Q` 与
                // `#include "Q` 前缀即可全盖（QtCore/QObject 同以前缀命中）。
                // execution 的线程/进程/同步原语用 std＋Win32（§1.4 D-01），
                // UI 线程投递由 ui 侧总线扩展承担（§10.4）——零 Qt 是设计
                // 决策而非仅层规则底线。
                if (line.find("#include <Q") != std::string::npos
                    || line.find("#include \"Q") != std::string::npos) {
                    ADD_FAILURE() << "execution 禁含 Qt 头（含 Core，D-01）: "
                                  << rel.string() << ":" << lineno;
                }
            }
        }
    }
}

/** 零 Eigen/rw::math 直接包含（§3.2 依赖图：几何数值类型仅经 core/evidence
 *  公共头传递；execution 的登记边不含任何 rw 基线库——区别于 policy 的
 *  R-5 例外与 runtime 的 L1 基线库链接）。 */
TEST(ExecutionBuild, NoEigenOrRwMathDirectInclude_DT_BUILD)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "execution" / sub)) {
            const auto text = readFile(unitRoot() / "execution" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (!isIncludeDirective(line)) { continue; }
                // rw::math/Eigen 只允许经 core/evidence 公共头"传递"到达
                // （§3.2 依赖图）；本单元源码直接 include（#include <rw/...>、
                // "rw/..."、<Eigen...、"Eigen/..." 四种形态）即绕过登记边＝
                // 红线命中。注意 Win32（windows.h 等）不在禁令内——§1.4
                // 明确 kernel32 是 execution 的设计内依赖（随 EX-T06 隔离
                // 于 src/win32/）。
                if (line.find("#include <rw") != std::string::npos
                    || line.find("#include \"rw") != std::string::npos
                    || line.find("#include <Eigen") != std::string::npos
                    || line.find("#include \"Eigen") != std::string::npos) {
                    ADD_FAILURE() << "execution 禁直接包含 Eigen/rw::math: "
                                  << rel.string() << ":" << lineno << " → " << line;
                }
            }
        }
    }
}

/**
 * 零表外单元边（R-1/R-2）：include 面的 sdurws/ird/* 白名单＝
 * {core, evidence, project, execution}。
 *
 * ARCH §3.5 中 execution 的编译期出边仅 core/evidence/project 三条
 * （diagnostics 边按注入形态不落编译期 include——§3.3，P-EX-8）；runtime/
 * policy 与业务域单元零命中即 acceptance 2"零业务域互链"的 include 面证据。
 * 本任务自身另受 P-EX-1 处置约束：连 core/evidence/project 头也零消费
 * （仅链接边），白名单放行三者是为 ARCH §3.5 登记边的后续消费任务
 * （EX-T02+）预留的合法面（PRJ-T01 同款口径）。
 */
TEST(ExecutionBuild, NoCrossUnitInclude_DT_BUILD_R1_R2)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "execution" / sub)) {
            const auto text = readFile(unitRoot() / "execution" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                if (!isIncludeDirective(line)) { continue; }
                const auto pos = line.find("sdurws/ird/");
                if (pos == std::string::npos) { continue; }
                // 提取 sdurws/ird/<unit>/ 的 <unit> 段与白名单比对——
                // ARCH §3.5 中 execution 的编译期出边仅三条；自身头（同单元
                // 内部包含）天然合法。段后无 '/'（如行尾直接结束）时取到
                // 行尾——真实 include 路径必含 "/<file>.hpp"，此分支仅兜底
                // 畸形行，取出的整段不匹配白名单即照常报错。
                const auto unitBegin = pos + std::string("sdurws/ird/").size();
                const auto unitEnd = line.find('/', unitBegin);
                const auto unit = (unitEnd == std::string::npos)
                    ? line.substr(unitBegin)
                    : line.substr(unitBegin, unitEnd - unitBegin);
                if (unit != "core" && unit != "evidence" && unit != "project"
                    && unit != "execution") {
                    ADD_FAILURE() << "execution 仅可依赖 core/evidence/project"
                                     "（ARCH §3.5 三条登记编译链接边；diagnostics"
                                     " 经注入不落 include——P-EX-8），发现: "
                                  << rel.string() << " → " << line;
                }
            }
        }
    }
}

/**
 * P-EX-3 具名自证：runtime/policy 零编译依赖（include 面）。
 *
 * 任务指令与 ARCH §3.5 的出入（本卡 §15.3 P-EX-3 登记）：runtime/policy
 * 不在依赖表白名单内，execution 经 §3.3 最小注入接口消费其能力
 * （IExecutionModelService/ICompileCacheJudge/INameResolverAdapter＋
 * ICancelSignal 适配 runtime；policy 经快照 PolicyRef 值传递——CON-06），
 * 适配器归 L5。本断言钉住 include 面零 sdurws/ird/runtime 与
 * sdurws/ird/policy：出现即私建表外编译依赖（SA-10 表外边＝构建失败）；
 * 如架构侧补登边，注入层可原样退役为直连（D-17——接口形状不变），届时
 * 本用例随单元卡增量修订同步放行，不可静默越界。
 */
TEST(ExecutionBuild, RuntimePolicyZeroCompileFace_DT_BUILD_PEX3)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "execution" / sub)) {
            const auto text = readFile(unitRoot() / "execution" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (!isIncludeDirective(line)) { continue; }
                // 仅认 include 指令行中的单元路径段——注入接口注释中对
                // runtime/policy 的文字提及（§3.3 设计说明）不是编译依赖。
                if (line.find("sdurws/ird/runtime") != std::string::npos
                    || line.find("sdurws/ird/policy") != std::string::npos) {
                    ADD_FAILURE() << "execution 零 runtime/policy 编译依赖"
                                     "（P-EX-3 处置：经 §3.3 注入消费，不私建"
                                     "表外边）: "
                                  << rel.string() << ":" << lineno << " → " << line;
                }
            }
        }
    }
}

/** 公共头路径布局（R-2，EX-BLD-1 第 2 项）：include/ 下文件仅位于
 *  sdurws/ird/execution/ 命名空间根。 */
TEST(ExecutionBuild, PublicHeaderPathLayout_DT_BUILD_R2)
{
    const auto incDir = unitRoot() / "execution" / "include";
    for (const auto& rel : collectCppFiles(incDir)) {
        // 私有实现头不得混入公共 include 根（R-2）：任何出现在 include/ 的
        // 文件必须精确处于 sdurws/ird/execution/ 之下；实现细节头只能放
        // src/ 以相对路径包含（§3.5"私有实现头不入 include/"纪律——
        // win32/ProcessLauncher.hpp 等 EX-T06 落地时同此约束）。
        const auto generic = rel.generic_string();
        EXPECT_EQ(generic.substr(0, std::string("sdurws/ird/execution/").size()),
                  "sdurws/ird/execution/")
            << "公共头越出命名空间根（无私有头出 include 红线）: " << generic;
    }
    SUCCEED() << "公共头路径布局合规（当前保留位期零契约头，规则随 EX-T02+ 落头自动生效）";
}
