/**
 * @file   AssemblyManifestContractTest.cpp
 * @brief  装配完备性扫描面契约测试（UI-T83——治理 T2/IRD-NOTE-006 裁决落地）。
 *
 * 设计依据：
 *   - units/ui.md §11.6（生产装配清单与接缝登记——清单为必需装配集合唯一权威）；
 *   - traceability/production-assembly-manifest.json（ird-assembly-manifest/1）；
 *   - 落地方案 docs/superpowers/plans/2026-10-17-assembly-completeness-gate-plan.md §2.B2；
 *   - 先例形态：requirements/contract_test/PluginGraphContractTest.cpp（IRD_*_UNIT_ROOT
 *     注入＋std::filesystem 文本扫描——构建图契约的单元内复核面）。
 *
 * 三条断言（与 Tier A 结构面 validate-docs.ps1 交叉独立复算，防单侧失明）：
 *   ①扫描所得 moduleId 集合 == 清单 wired:true 集合（双向锁定——多挂＝未登记、
 *     漏挂＝登记失实；F-911 型"实现存在、生产无人挂接"自此有机器断言）；
 *   ②同一 moduleId 生产调用点 ≥2 ＝ 重复注册失败；
 *   ③required:true 且 wired:false 必须挂 open waiver（测试直读 findings.json）。
 *
 * 解析口径（诚实边界）：清单与 findings 均为治理脚本控制的文件，此处用定向
 * 迷你解析（键序无关、按 JSON 键名提取）而非完整 JSON 库——schema 演进时
 * 本测试与清单同批更新（两者都只经治理批落地）；同一属性的完整校验由
 * validate-docs.ps1（Tier A 结构面）独立承载。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <algorithm>  // std::count（调用点行号计算）

namespace fs = std::filesystem;

namespace
{

/// industrialrobot 根（IRD_UI_UNIT_ROOT 注入——BuildRedLineTest 同款）。
/// 注意：宏值形如 "…/ui/.."，lexically_normal 会留下尾分隔符（"a/ui/.."
/// →"a/"）——必须剥掉，否则后续 parent_path 链第一跳被空尾段吃掉、仓库根
/// 差一级（实测教训，2026-10-17）。
const fs::path& unitRoot()
{
    static const fs::path dir = [] {
        fs::path p = fs::path{IRD_UI_UNIT_ROOT}.lexically_normal();
        if (!p.empty() && (p.native().back() == L'/' || p.native().back() == L'\\')) {
            p = p.parent_path();
        }
        return p;
    }();
    return dir;
}

/// 仓库真正根：industrialrobot → rwslibs → src → RobWorkStudio → RobWork → 仓库根
/// （清单与 findings 在仓库根下的设计树内）。
fs::path repoRoot()
{
    fs::path p = unitRoot();
    for (int i = 0; i < 5; ++i) {
        p = p.parent_path();
    }
    return p;
}

/// 读取文本文件（不存在/不可读＝测试装配错误，显性失败——PluginGraphContractTest 同款）。
std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << path.string();
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// 路径任一组件含 "test" 即测试面（契约口径：扫描排除一切 *test* 面——
/// harness/app 与测试目录的 attachModule 不构成生产挂接证据）。
bool isTestFace(const fs::path& rel)
{
    for (const auto& part : rel) {
        const std::string s = part.string();
        if (s.find("test") != std::string::npos || s == "app") {
            return true;
        }
    }
    return false;
}

/// 生产装配扫描面：ui/plugin、ui/src，以及各域目录下的 assembly/ 子树
/// （枚举 industrialrobot 子目录动态发现——新域落 assembly/ 即自动纳入扫描）。
std::vector<fs::path> productionScanDirs()
{
    std::vector<fs::path> dirs;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(unitRoot(), ec)) {
        if (!entry.is_directory()) {
            continue;
        }
        const fs::path domainAssembly = entry.path() / "assembly";
        if (fs::exists(domainAssembly, ec)) {
            dirs.push_back(domainAssembly);
        }
    }
    dirs.push_back(unitRoot() / "ui" / "plugin");
    dirs.push_back(unitRoot() / "ui" / "src");
    return dirs;
}

/// 递归收集扫描面内 .cpp/.hpp/.h 文件（相对 unitRoot 路径 + 全文内容）。
struct SourceFile
{
    fs::path rel;
    std::string content;
};

std::vector<SourceFile> collectProductionSources()
{
    std::vector<SourceFile> files;
    std::error_code ec;
    for (const fs::path& dir : productionScanDirs()) {
        for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".hpp" && ext != ".h") {
                continue;
            }
            fs::path rel = fs::relative(entry.path(), unitRoot(), ec);
            if (ec || isTestFace(rel)) {
                continue;
            }
            files.push_back(SourceFile{rel, readFile(entry.path())});
        }
    }
    return files;
}

/// kModuleHandle constexpr 定义（值 + 定义文件相对路径）——解析调用点 token 用。
struct HandleDef
{
    std::string value;
    fs::path rel;
};

std::vector<HandleDef> collectHandleDefs(const std::vector<SourceFile>& sources)
{
    // 形如：inline constexpr char kModuleHandle[] = "modeling";
    static const std::regex defRe{R"re(kModuleHandle\s*\[\s*\]\s*=\s*"([^"]+)")re"};
    std::vector<HandleDef> defs;
    for (const auto& f : sources) {
        for (auto it = std::sregex_iterator(f.content.begin(), f.content.end(), defRe);
             it != std::sregex_iterator{}; ++it) {
            defs.push_back(HandleDef{(*it)[1].str(), f.rel});
        }
    }
    return defs;
}

/// 解析一个 attachModule 首参 token 为 moduleId：
///   - 字符串字面量（"requirements"）→ 直接取值；
///   - 限定名（modeling::kModuleHandle）→ 取限定词，在定义集中找定义文件
///     路径含该限定词目录组件的候选——候选值全部一致则解析成功，
///     不一致（歧义）或零候选（未知 token）＝失败（新域挂接必须先登记清单）。
std::string resolveToken(const std::string& token, const std::vector<HandleDef>& defs)
{
    if (!token.empty() && token.front() == '"') {
        return token.substr(1, token.size() - 2);
    }
    static const std::string suffix = "::kModuleHandle";
    std::string qualifier;
    bool qualified = false;
    if (token.size() > suffix.size()
        && token.compare(token.size() - suffix.size(), suffix.size(), suffix) == 0) {
        qualifier = token.substr(0, token.size() - suffix.size());
        qualified = true;
    } else if (token == "kModuleHandle") {
        qualifier.clear();
        qualified = false;  // 未限定名：仅在全部定义同值时可解析
    } else {
        ADD_FAILURE() << "attachModule 首参形态未识别（既非字面量也非 kModuleHandle 句柄）: "
                      << token << "——登记 handleSymbol 或改用字面量";
        return {};
    }
    std::set<std::string> candidates;
    for (const auto& d : defs) {
        bool match = !qualified;  // 未限定名：所有定义都是候选
        for (const auto& part : d.rel) {
            if (qualified && part.string() == qualifier) {
                match = true;
                break;
            }
        }
        if (match) {
            candidates.insert(d.value);
        }
    }
    if (candidates.empty()) {
        ADD_FAILURE() << "attachModule 句柄 token 无可解析定义（未知 token＝新域挂接必须"
                         "先登记 production-assembly-manifest.json）: "
                      << token;
        return {};
    }
    if (candidates.size() > 1) {
        ADD_FAILURE() << "attachModule 句柄 token 解析歧义（多候选值不同——登记"
                         "handleSymbol 厘清）: "
                      << token;
        return {};
    }
    return *candidates.begin();
}

/// 生产挂接调用点（moduleId + 文件:行——重复注册断言与报错定位用）。
struct CallSite
{
    std::string moduleId;
    std::string where;
};

/// 扫描全部生产源里的 attachModule( 调用并解析 moduleId。
/// 注意调用可跨行（首参在下一行——UiPlugin.cpp:2485 实际形态），故用内容级
/// 正则（\s 含换行）而非行扫描。
std::vector<CallSite> scanCallSites(const std::vector<SourceFile>& sources,
                                    const std::vector<HandleDef>& defs)
{
    static const std::regex callRe{
        R"re(attachModule\s*\(\s*("[^"]*"|[A-Za-z_][A-Za-z0-9_:]*))re"};
    std::vector<CallSite> sites;
    for (const auto& f : sources) {
        for (auto it = std::sregex_iterator(f.content.begin(), f.content.end(), callRe);
             it != std::sregex_iterator{}; ++it) {
            // 排除成员定义/声明签名：`X::attachModule(const …)` 的限定定义形态
            // （匹配起点前最近非空白字符是 ':'）与首参为类型（const）的签名行
            // ——它们不是调用点，不得进入集合。
            auto prev = it->position(0);
            while (prev > 0 && std::isspace(static_cast<unsigned char>(f.content[prev - 1]))) {
                --prev;
            }
            if (prev > 0 && f.content[prev - 1] == ':') {
                continue;
            }
            const std::string token = (*it)[1].str();
            if (token == "const") {
                continue;
            }
            const std::string moduleId = resolveToken(token, defs);
            if (moduleId.empty()) {
                continue;  // resolveToken 已 ADD_FAILURE 报因
            }
            const auto line = static_cast<size_t>(
                1 + std::count(f.content.begin(), (*it)[0].first, '\n'));
            sites.push_back(CallSite{moduleId, f.rel.string() + ":" + std::to_string(line)});
        }
    }
    return sites;
}

/// 清单条目（定向迷你解析——键序无关，见文件头"解析口径"说明）。
struct ManifestEntry
{
    std::string moduleId;
    bool required = false;
    bool wired = false;
    std::string waiverFinding;
    std::string handleSymbol;  ///< 清单 handleSymbol 列（wired 条目须与 token 同源——UI-T84/F-916）
    std::string wiredAnchor;   ///< 清单 wiredAnchor 列（"<仓库相对路径>:<行>"——UI-T84 格式复核）
};

/// 解析 production-assembly-manifest.json 的 draftModules 数组：按 "moduleId"
/// 键出现位置切分条目，条目内提取 required/wired/waiver.finding。
std::vector<ManifestEntry> parseManifest(const std::string& text)
{
    static const std::regex idRe{R"re("moduleId"\s*:\s*"([^"]+)")re"};
    std::vector<size_t> entryStarts;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), idRe);
         it != std::sregex_iterator{}; ++it) {
        entryStarts.push_back(static_cast<size_t>((*it)[0].first - text.begin()));
    }
    std::vector<ManifestEntry> entries;
    for (size_t i = 0; i < entryStarts.size(); ++i) {
        const size_t begin = entryStarts[i];
        const size_t end = (i + 1 < entryStarts.size()) ? entryStarts[i + 1] : text.size();
        const std::string chunk = text.substr(begin, end - begin);
        ManifestEntry e;
        std::smatch m;
        if (std::regex_search(chunk, m, idRe)) {
            e.moduleId = m[1].str();
        }
        if (std::regex_search(chunk, m, std::regex{R"re("wired"\s*:\s*(true|false))re"})) {
            e.wired = m[1].str() == "true";
        }
        if (std::regex_search(chunk, m, std::regex{R"re("required"\s*:\s*(true|false))re"})) {
            e.required = m[1].str() == "true";
        }
        if (std::regex_search(chunk, m, std::regex{R"re("finding"\s*:\s*"([^"]+)")re"})) {
            e.waiverFinding = m[1].str();
        }
        if (std::regex_search(chunk, m,
                              std::regex{R"re("handleSymbol"\s*:\s*"([^"]+)")re"})) {
            e.handleSymbol = m[1].str();
        }
        if (std::regex_search(chunk, m,
                              std::regex{R"re("wiredAnchor"\s*:\s*"([^"]+)")re"})) {
            e.wiredAnchor = m[1].str();
        }
        entries.push_back(e);
    }
    return entries;
}

/// 查 findings.json 中指定 finding 的 status（定向解析：定位 "id": "<FID>"
/// 后，截取到下一个 "id" 键为止的窗口找 "status"——单条 finding 的 status
/// 必在其自身键值区内；找不到＝<unparsed>，无该 id＝<not-found>）。
std::string findingStatus(const std::string& findingsText, const std::string& findingId)
{
    const std::string needle = "\"id\"\\s*:\\s*\"" + findingId + "\"";
    std::smatch m;
    if (!std::regex_search(findingsText, m, std::regex{needle})) {
        return "<not-found>";
    }
    const size_t windowBegin = static_cast<size_t>(m[0].second - findingsText.begin());
    const size_t hardEnd = std::min(findingsText.size(), windowBegin + 8000);
    std::string window = findingsText.substr(windowBegin, hardEnd - windowBegin);
    const size_t nextId = window.find("\"id\"");
    if (nextId != std::string::npos) {
        window = window.substr(0, nextId);
    }
    std::smatch sm;
    if (std::regex_search(window, sm, std::regex{R"re("status"\s*:\s*"([^"]+)")re"})) {
        return sm[1].str();
    }
    return "<unparsed>";
}

/// 共享装载（扫描面 + 清单 + findings）——三个 TEST 复用，任何装配缺位
/// 显性失败。
struct AssemblyWorld
{
    std::vector<CallSite> callSites;
    std::vector<ManifestEntry> manifest;
    std::string findingsText;
};

const AssemblyWorld& world()
{
    static const AssemblyWorld w = [] {
        AssemblyWorld out;
        const auto sources = collectProductionSources();
        const auto defs = collectHandleDefs(sources);
        out.callSites = scanCallSites(sources, defs);
        out.manifest = parseManifest(readFile(
            repoRoot() / "RobWork" / "doc" / "industrial-robot-design" / "traceability"
            / "production-assembly-manifest.json"));
        out.findingsText = readFile(
            repoRoot() / "RobWork" / "doc" / "industrial-robot-design" / "traceability"
            / "findings.json");
        return out;
    }();
    return w;
}

}  // namespace

// =====================================================================
// 断言①——扫描集合与清单 wired 集合双向锁定（F-911 型漏接线的机器断言）
// =====================================================================
TEST(AssemblyManifest, ScanSetMatchesWiredSetBidirectional_UI_T83)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-05"},
                  std::vector<std::string>{"TIER-A-SCAN", "IRD-NOTE-006", "ACC1-bidirectional"});
    const auto& w = world();

    ASSERT_FALSE(w.manifest.empty()) << "清单 draftModules 解析为空（解析器或清单损坏）";

    std::set<std::string> scanned;
    for (const auto& s : w.callSites) {
        scanned.insert(s.moduleId);
    }
    std::set<std::string> wired;
    for (const auto& e : w.manifest) {
        if (e.wired) {
            wired.insert(e.moduleId);
        }
    }
    // 方向一：生产多挂＝清单未登记（未知 token 强制登记的集合面）
    for (const auto& id : scanned) {
        EXPECT_TRUE(wired.count(id) > 0)
            << "生产存在 attachModule 挂接但清单未登记 wired:true: " << id
            << "（先改 production-assembly-manifest.json 再改代码——登记先行）";
    }
    // 方向二：清单登记失实＝生产无调用点（F-911 型反向：wired:true 但零调用）
    for (const auto& id : wired) {
        EXPECT_TRUE(scanned.count(id) > 0)
            << "清单登记 wired:true 但生产扫描面零 attachModule 调用: " << id
            << "（登记失实——改回 wired:false＋waiver，或补接线）";
    }
}

// =====================================================================
// 断言②——重复注册检测（同一 moduleId ≥2 个生产调用点）
// =====================================================================
TEST(AssemblyManifest, NoDuplicateRegistration_UI_T83)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-05"},
                  std::vector<std::string>{"TIER-A-SCAN", "IRD-NOTE-006", "ACC2-no-duplicate"});
    const auto& w = world();
    std::map<std::string, std::vector<std::string>> byModule;
    for (const auto& s : w.callSites) {
        byModule[s.moduleId].push_back(s.where);
    }
    for (const auto& [id, wheres] : byModule) {
        if (wheres.size() <= 1u) {
            continue;
        }
        ADD_FAILURE()
            << "moduleId 生产挂接调用点 ≥2（重复注册——草稿源挂接纪律是每模块恰一次）: "
            << id;
        for (const auto& where : wheres) {
            ADD_FAILURE() << "  调用点: " << where;
        }
    }
}

// =====================================================================
// 断言③——required 且未接线必须挂 open waiver（与 validate-docs 交叉复算）
// =====================================================================
TEST(AssemblyManifest, RequiredUnwiredCarriesOpenWaiver_UI_T83)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-05"},
                  std::vector<std::string>{"TIER-A-SCAN", "IRD-NOTE-006", "ACC3-waiver-open"});
    const auto& w = world();
    for (const auto& e : w.manifest) {
        if (!e.required || e.wired) {
            continue;
        }
        EXPECT_FALSE(e.waiverFinding.empty())
            << "required:true 且 wired:false 必须携带 waiver.finding: " << e.moduleId;
        if (e.waiverFinding.empty()) {
            continue;
        }
        const std::string status = findingStatus(w.findingsText, e.waiverFinding);
        EXPECT_EQ(status, "open")
            << "豁免必须挂在 open finding 上（finding 已关闭而豁免未删＝登记失实）: "
            << e.moduleId << " waiver→" << e.waiverFinding << " status=" << status;
    }
}

/** wired 条目的 handleSymbol↔token 一致性＋锚点格式（UI-T84——F-916 消账）：
 *  ①wired ⇒ handleSymbol 非空且恰为 "<moduleId>::kModuleHandle"（与扫描
 *  面经 constexpr 定义解析出的 token 同源——清单手写列与代码真值不可漂
 *  移）；②wired ⇒ wiredAnchor 非空且为 "<路径>:<行号>" 格式、路径在
 *  industrialrobot/ 下真实存在（格式+存在性双重复核，与 validate-docs
 *  结构面交叉独立）。 */
TEST(AssemblyManifest, WiredEntriesDeclareConsistentHandleSymbolAndAnchor_UI_T84)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-05"},
                  std::vector<std::string>{"TIER-A-SCAN", "IRD-NOTE-006", "F-916"});
    const auto& w = world();
    static const std::regex anchorRe{R"re(([^:]+):(\d+)$)re"};
    for (const auto& e : w.manifest) {
        if (!e.wired) {
            continue;
        }
        EXPECT_FALSE(e.handleSymbol.empty())
            << "wired 条目必须登记 handleSymbol: " << e.moduleId;
        EXPECT_EQ(e.handleSymbol, e.moduleId + "::kModuleHandle")
            << "handleSymbol 与模块 token 不同源（清单手写漂移）: " << e.moduleId;
        EXPECT_FALSE(e.wiredAnchor.empty())
            << "wired 条目必须登记 wiredAnchor: " << e.moduleId;
        std::smatch m;
        ASSERT_TRUE(std::regex_match(e.wiredAnchor, m, anchorRe))
            << "wiredAnchor 须为 \"<路径>:<行号>\" 格式: " << e.wiredAnchor;
        const fs::path anchorFile = unitRoot() / fs::path{m[1].str()};
        EXPECT_TRUE(fs::exists(anchorFile))
            << "wiredAnchor 路径不存在（锚点失实）: " << anchorFile.string();
    }
}
