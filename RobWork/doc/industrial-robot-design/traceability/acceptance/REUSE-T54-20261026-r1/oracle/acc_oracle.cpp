// REUSE-T54 独立验收 oracle 驱动（独立验收员构造，非实施产物）。
// 依据：units/project.md §4.4.1 HeadRecord 字段表列序＋§4.8 canonical 词法契约
// ＋PersistenceFormat.hpp 冻结常量（kSchemaVersionCurrent=10000 等）。
// 全部期望值独立推导（黄金字节按 §4.4.1 表列序手拼、数值边界为数学常量），
// 不引用产品测试代码；只调用迁移后 codec 公共入口，验证行为契约。
// 输出：逐项 PASS/FAIL 行；进程退出码 0＝全过，1＝存在 FAIL。
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "Codec.hpp"  // 私有实现头（验收驱动与测试同单元形态）

#include <cstdint>
#include <cstdio>
#include <string>

using namespace sdurws::ird::project;
namespace codec = sdurws::ird::project::codec;
namespace core = sdurws::ird::core;

static int gFail = 0;
static void report(const char* name, bool ok, const std::string& note = "")
{
    std::printf("%s %s%s\n", ok ? "PASS" : "FAIL", name, note.empty() ? "" : ("  | " + note).c_str());
    if (!ok) { ++gFail; }
}

/// 期望语句抛出指定 StoreError 码。
template <typename Fn>
static bool expectStoreCode(Fn&& fn, StoreErrorCode expected, std::string& got)
{
    try {
        fn();
        got = "no-throw";
        return false;
    } catch (const StoreError& e) {
        got = (e.code() == expected ? "code-match" : "wrong-code");
        got += " detail=" + std::string(e.what()).substr(0, 80);
        return e.code() == expected;
    } catch (const std::exception& e) {
        got = std::string("wrong-exception: ") + e.what();
        return false;
    }
}

int main()
{
    // ---- 独立参照常量（验收员从 §4.4.1 表列序与 core 规范文本规则推导）----
    const std::string kProj = "prj-a1b2c3d4e5f60718293a4b5c6d7e8f90";  // prj-+32hex
    const std::string kRev  = "rev-0102030405060708090a0b0c0d0e0f10";
    const std::string kBrn  = "brn-f0e0d0c0b0a090807060504030201000";
    const std::string kHex64 = "3f7a1c9b2d8e4f06a5b4c3d2e1f09876543210fedcba9876543210abcdef0123";
    // 2^64-1 与 2^63 的十进制（数学事实，独立于产品实现）。
    const char* kU64Max = "18446744073709551615";
    const char* kI64MaxPlus1 = "9223372036854775808";  // 2^63——boost uint64 kind 界点

    // ---- 黄金字节（§4.4.1 表列序：formatId,schemaVersion,projectId,revisionId,
    //      revisionSeq,branchId,manifestDigest——紧凑无空白）----
    HeadRecord h;
    h.schemaVersion = 10000;
    h.projectId = core::ProjectId::tryFromCanonical(kProj).value();
    h.revisionId = core::RevisionId::tryFromCanonical(kRev).value();
    h.revisionSeq = 18446744073709551615ULL;  // 2^64-1
    h.branchId = core::BranchId::tryFromCanonical(kBrn).value();
    h.manifestDigest = kHex64;
    const std::string expectGolden =
        std::string("{\"formatId\":\"rwdesign\",\"schemaVersion\":10000")
        + ",\"projectId\":\"" + kProj + "\""
        + ",\"revisionId\":\"" + kRev + "\""
        + ",\"revisionSeq\":" + kU64Max
        + ",\"branchId\":\"" + kBrn + "\""
        + ",\"manifestDigest\":\"" + kHex64 + "\"}";
    const std::string bytes = codec::dump(h);
    report("golden-bytes-u64max", bytes == expectGolden, bytes);

    // ---- uint64 上界 round-trip：dump→parse→dump 幂等＋值保真 ----
    {
        const auto back = codec::parseHeadRecord(bytes);
        report("u64max-roundtrip-value", back.revisionSeq == 18446744073709551615ULL);
        report("u64max-dump-idempotent", codec::dump(back) == bytes);
    }
    // ---- 2^63（uint64 kind 界点，> INT64_MAX）：应接受并 round-trip ----
    {
        const std::string doc =
            std::string("{\"formatId\":\"rwdesign\",\"schemaVersion\":10000")
            + ",\"projectId\":\"" + kProj + "\",\"revisionId\":\"" + kRev + "\""
            + ",\"revisionSeq\":" + kI64MaxPlus1 + ",\"branchId\":\"" + kBrn + "\""
            + ",\"manifestDigest\":\"" + kHex64 + "\"}";
        const auto back = codec::parseHeadRecord(doc);
        report("u64-2^63-accepted-roundtrip",
               back.revisionSeq == 9223372036854775808ULL && codec::dump(back) == doc);
    }
    // ---- 越界/词法拒绝矩阵（码恒 StoreCorrupt）----
    auto headWithSeq = [&](const std::string& seqLiteral) {
        return std::string("{\"formatId\":\"rwdesign\",\"schemaVersion\":10000")
             + ",\"projectId\":\"" + kProj + "\",\"revisionId\":\"" + kRev + "\""
             + ",\"revisionSeq\":" + seqLiteral + ",\"branchId\":\"" + kBrn + "\""
             + ",\"manifestDigest\":\"" + kHex64 + "\"}";
    };
    std::string got;
    report("reject-2^64-overflow",
           expectStoreCode([&] { codec::parseHeadRecord(headWithSeq("18446744073709551616")); },
                           StoreErrorCode::StoreCorrupt, got), got);
    report("reject-negative-seq",
           expectStoreCode([&] { codec::parseHeadRecord(headWithSeq("-1")); },
                           StoreErrorCode::StoreCorrupt, got), got);
    report("reject-leading-zero",
           expectStoreCode([&] { codec::parseHeadRecord(headWithSeq("07")); },
                           StoreErrorCode::StoreCorrupt, got), got);
    report("reject-float",
           expectStoreCode([&] { codec::parseHeadRecord(headWithSeq("7.5")); },
                           StoreErrorCode::StoreCorrupt, got), got);
    report("reject-exponent",
           expectStoreCode([&] { codec::parseHeadRecord(headWithSeq("7e2")); },
                           StoreErrorCode::StoreCorrupt, got), got);
    report("reject-true-literal",
           expectStoreCode([&] { codec::parseHeadRecord(headWithSeq("true")); },
                           StoreErrorCode::StoreCorrupt, got), got);
    {
        const std::string dup = headWithSeq("7");
        const std::string dupDoc = dup.substr(0, dup.size() - 1)
            + ",\"revisionSeq\":8}";
        report("reject-duplicate-key",
               expectStoreCode([&] { codec::parseHeadRecord(dupDoc); },
                               StoreErrorCode::StoreCorrupt, got), got);
    }
    {
        const std::string ws = std::string("{ \"formatId\": \"rwdesign\", \"schemaVersion\": 10000")
             + ",\"projectId\":\"" + kProj + "\",\"revisionId\":\"" + kRev + "\""
             + ",\"revisionSeq\":7,\"branchId\":\"" + kBrn + "\""
             + ",\"manifestDigest\":\"" + kHex64 + "\" }";
        report("reject-whitespace-valid-doc",
               expectStoreCode([&] { codec::parseHeadRecord(ws); },
                               StoreErrorCode::StoreCorrupt, got), got);
    }
    // ---- schemaVersion 数值边界 ----
    {
        const std::string overInt32 = headWithSeq("7");
        const std::string bad = overInt32.substr(0, overInt32.find(",\"projectId\""))
            + ",\"projectId\":\"" + kProj + "\""
            + ",\"revisionId\":\"" + kRev + "\",\"revisionSeq\":7"
            + ",\"branchId\":\"" + kBrn + "\",\"manifestDigest\":\"" + kHex64 + "\"}";
        // schemaVersion=2147483648（2^31，超出 int32 表示）→ corrupt。
        const std::string v31 = bad;  // 占位：真实替换在下方两段构造。
        (void)v31;
        std::string doc31 = headWithSeq("7");
        doc31.replace(doc31.find("\"schemaVersion\":10000"), 21, "\"schemaVersion\":2147483648");
        report("reject-schemaVersion-2^31",
               expectStoreCode([&] { codec::parseHeadRecord(doc31); },
                               StoreErrorCode::StoreCorrupt, got), got);
        std::string docMax = headWithSeq("7");
        docMax.replace(docMax.find("\"schemaVersion\":10000"), 21, "\"schemaVersion\":2147483647");
        report("schemaVersion-int32max-schemafuture",
               expectStoreCode([&] { codec::parseHeadRecord(docMax); },
                               StoreErrorCode::SchemaFuture, got), got);
        std::string docNeg = headWithSeq("7");
        docNeg.replace(docNeg.find("\"schemaVersion\":10000"), 21, "\"schemaVersion\":-10000");
        report("schemaVersion-negative-legacy",
               expectStoreCode([&] { codec::parseHeadRecord(docNeg); },
                               StoreErrorCode::FormatLegacy, got), got);
    }
    // ---- 非整数 kind 落 double 的越界向量（2^64 直接出现在字段）已含上方
    //      reject-2^64-overflow；此处补 float kind 显式向量 1e999（无穷 double）----
    report("reject-1e999-infinity",
           expectStoreCode([&] { codec::parseHeadRecord(headWithSeq("1e999")); },
                           StoreErrorCode::StoreCorrupt, got), got);

    std::printf(gFail == 0 ? "ORACLE-RESULT: ALL PASS\n" : "ORACLE-RESULT: %d FAIL\n", gFail);
    return gFail == 0 ? 0 : 1;
}
