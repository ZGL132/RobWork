/**
 * @file   main.cpp
 * @brief  testdata lint 工具（TK-T03 交付）——全量扫描黄金数据集＋需求 ID 字典校验。
 *
 * 设计依据：
 *   - units/testkit.md §9 TK-T03 行（tools/testdata 校验工具：lint＋全量扫描）、
 *     §4.2.2（datasetId/目录一致＋coveredRequirements 对字典逐项校验）、§4.5（CI 可
 *     独立运行本工具全量扫描）
 *
 * 用法（仓库根）：
 *   sdurws_ird_testdata_lint [--root <testdata 目录>] [--dict <requirements-ids.json>]
 * 缺省：root＝SDURWS_IRD_TESTDATA_DIR 环境变量/编译默认；dict＝<root>/requirements-ids.json
 *
 * 退出码：0＝全部通过；1＝存在违规（逐条输出）；2＝环境错误（目录缺失等）。
 */

#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>

#include <fstream>
#include <sstream>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace sdurws::ird::testkit;

namespace {

/// 需求 ID 字典装载（requirements-ids.json——ird-requirements-ids/1）。
std::set<std::string> loadIdDict(const fs::path& dictPath)
{
    std::ifstream in(dictPath, std::ios::binary);
    if (!in) {
        std::cerr << "lint-env: 字典无法读取: " << dictPath.string() << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const JsonValue root = parseJson(ss.str());
    const auto* schema = root.find("schemaVersion");
    const auto* entries = root.find("ids");
    if (!root.isObject() || schema == nullptr || !schema->isString()
        || schema->text != "ird-requirements-ids/1"
        || entries == nullptr || !entries->isArray()) {
        throw TestKitError(TestKitErrorKind::DatasetInvalid, "lint 字典 schemaVersion/ids 非法");
    }
    std::set<std::string> ids;
    for (const auto& id : entries->items) {
        if (!id.isString() || id.text.empty() || !ids.insert(id.text).second) {
            throw TestKitError(TestKitErrorKind::DatasetInvalid, "lint 字典 ID 非字符串、空或重复");
        }
    }
    return ids;
}

}  // namespace

int main(int argc, char** argv) try
{
    std::string rootArg;
    std::string dictArg;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--root" && i + 1 < argc) { rootArg = argv[++i]; }
        else if (a == "--dict" && i + 1 < argc) { dictArg = argv[++i]; }
    }

    // 数据根：参数 > 环境变量/编译默认（TestPaths 两级解析）。
    // --root 经环境变量传导到 GoldenDataset::load 内部的 TestPaths 解析
    //（编译默认不可运行期覆写——env 是 TestPaths 的运行期覆写口）。
    if (!rootArg.empty()) {
#ifdef _MSC_VER
        (void)_putenv_s("SDURWS_IRD_TESTDATA_DIR", rootArg.c_str());
#else
        setenv("SDURWS_IRD_TESTDATA_DIR", rootArg.c_str(), 1);
#endif
    }
    fs::path root = rootArg.empty() ? goldenDataRoot() : fs::path{rootArg};
    const fs::path goldenDir = root / "golden";
    if (!fs::exists(goldenDir)) {
        std::cerr << "lint-env: golden 目录不存在: " << goldenDir.string() << "\n";
        return 2;
    }

    // 字典属于 testdata 根，默认与显式路径必须消费同一文件（F-016）。
    const fs::path dictPath = dictArg.empty()
        ? root / "requirements-ids.json" : fs::path{dictArg};
    const auto dict = loadIdDict(dictPath);
    std::cout << "lint: 字典 " << dict.size() << " 个需求 ID（" << dictPath.string() << "）\n";

    int failures = 0;
    std::size_t scanned = 0;
    for (const auto& datasetDir : fs::directory_iterator(goldenDir)) {
        if (!datasetDir.is_directory()) { continue; }
        const auto dirName = datasetDir.path().filename().string();
        // 每数据集取版本目录（latest 语义不在此——全量扫描所有版本目录）。
        for (const auto& versionDir : fs::directory_iterator(datasetDir)) {
            if (!versionDir.is_directory()) { continue; }
            const std::string version = versionDir.path().filename().string();
            ++scanned;
            const auto failuresBefore = failures;
            try {
                const GoldenDataset ds = GoldenDataset::load({dirName, version});
                // 引用必须真正装载：只检查 manifest 中的档案名不能证明容差可用。
                const auto& manifest = ds.manifest();
                (void)ToleranceProfile::load(root / "tolerance" / manifest.toleranceProfileId
                                             / ("v" + manifest.toleranceProfileVersion + ".json"));
                // lint：coveredRequirements 逐项对字典校验（TK-MAN 反例"需求 ID 不在字典"）。
                for (const auto& req : ds.manifest().coveredRequirements) {
                    if (dict.count(req) == 0) {
                        std::cerr << "lint-fail: " << dirName << "/" << version
                                  << ": coveredRequirements ID 不在字典: " << req << "\n";
                        ++failures;
                    }
                }
                for (const auto& at : manifest.coveredAt) {
                    if (dict.count(at) == 0) {
                        std::cerr << "lint-fail: " << dirName << "/" << version
                                  << ": coveredAt ID 不在字典: " << at << "\n";
                        ++failures;
                    }
                }
                // 仅零新增违规才标 ok（失败时标 ok 会误导人工排查）。
                if (failures == failuresBefore) {
                    std::cout << "lint-ok: " << dirName << "/" << version << "\n";
                }
            } catch (const TestKitError& e) {
                std::cerr << "lint-fail: " << dirName << "/" << version << ": " << e.what()
                          << "\n";
                ++failures;
            }
        }
    }

    std::cout << "lint: 扫描 " << scanned << " 个数据集版本，违规 " << failures << "\n";
    return failures == 0 ? 0 : 1;
}
catch (const TestKitError& e) {
    std::cerr << "lint-fail: " << e.what() << "\n";
    return e.kind() == TestKitErrorKind::EnvUnavailable ? 2 : 1;
}
catch (const fs::filesystem_error& e) {
    std::cerr << "lint-env: " << e.what() << "\n";
    return 2;
}
