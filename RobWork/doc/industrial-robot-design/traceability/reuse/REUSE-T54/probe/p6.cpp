// 逐案探针：每案前打印标签，崩溃即知凶手。仅探针用，不入产品树。
#include <boost/json.hpp>
#include <boost/system/system_error.hpp>
#include <cstdio>
#include <string>
namespace bj = boost::json;

static void run(const char* label, const std::string& text, std::size_t depth = 32)
{
    std::printf("== %s\n", label);
    bj::parse_options opts;
    opts.max_depth = depth;
    try {
        bj::value v = bj::parse(text, bj::storage_ptr(), opts);
        std::printf("   OK kind=%d\n", static_cast<int>(v.kind()));
        if (v.is_object())
            for (auto& kv : v.get_object())
                std::printf("   key='%s' kind=%d\n", kv.key_c_str(),
                            static_cast<int>(kv.value().kind()));
        if (v.is_int64())
            std::printf("   int64=%lld\n", static_cast<long long>(v.get_int64()));
        if (v.is_uint64())
            std::printf("   uint64=%llu\n", static_cast<unsigned long long>(v.get_uint64()));
        if (v.is_double())
            std::printf("   double=%.17g\n", v.get_double());
        if (v.is_string()) {
            const std::string s(v.get_string().c_str(), v.get_string().size());
            std::printf("   strlen=%zu bytes=", s.size());
            for (unsigned char c : s) std::printf("%02x", c);
            std::printf("\n");
        }
    } catch (const boost::system::system_error& e) {
        std::printf("   ERR val=%d msg=%s\n", static_cast<int>(e.code().value()),
                    e.code().message().c_str());
    } catch (const std::exception& e) {
        std::printf("   EXC %s\n", e.what());
    }
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    run("dup-key", "{\"a\":1,\"a\":2}");
    run("float", "{\"a\":10000.5}");
    run("exp", "{\"a\":1e4}");
    run("bool", "{\"a\":true}");
    run("null", "{\"a\":null}");
    run("leading-zero", "{\"a\":007}");
    run("uint64max", "{\"a\":18446744073709551615}");
    run("uint64over", "{\"a\":18446744073709551616}");
    run("int64min", "{\"a\":-9223372036854775808}");
    run("int64under", "{\"a\":-9223372036854775809}");
    run("nul-escape", R"({"a":"x\u0000y"})");
    run("lone-high", R"({"a":"\ud800"})");
    run("lone-low", R"({"a":"\udc00"})");
    run("surrogate-pair", R"({"a":"\ud83d\ude00"})");
    run("raw-ctrl", "{\"a\":\"x\x01y\"}");
    run("raw-utf8", "{\"a\":\"\xe6\x9c\xba\"}");
    run("escape-slash", R"({"a":"\/"})");
    run("escape-upper-hex", R"({"a":"\u0041"})");
    run("bom", "\xEF\xBB\xBF{}");
    run("depth64-ok", std::string(64, '[') + std::string(64, ']'), 64);
    run("depth65-over", std::string(65, '[') + std::string(65, ']'), 64);
    run("trailing", "{\"a\":1}x");
    run("empty", "");
    std::printf("ALL DONE\n");
    return 0;
}
