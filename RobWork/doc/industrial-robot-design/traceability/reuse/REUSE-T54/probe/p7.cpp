// P5 探针补充：serialize 输出形态（dump 侧不换的决策证据）＋\u0000 字节。
#include <boost/json.hpp>
#include <cstdio>
#include <string>
namespace bj = boost::json;

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    // \u0000 往返字节
    bj::value n = bj::parse(R"({"a":"x\u0000y"})");
    const bj::string& s = n.get_object().at("a").get_string();
    std::printf("nul-escape size=%zu bytes=", s.size());
    for (unsigned char c : std::string(s.c_str(), s.size())) std::printf("%02x", c);
    std::printf("\n");
    // serialize：非 ASCII 是否原样 UTF-8 输出（决定 dump 侧不换编码器）
    bj::value w = bj::parse("{\"a\":\"\xe6\x9c\xba\",\"b\":[1,2]}");
    std::printf("serialize-utf8: %s\n", bj::serialize(w).c_str());
    // serialize：控制字符转义形态（大小写/位宽参考）
    bj::value c = bj::parse("\"a\x01b\"");
    std::printf("serialize-ctrl: %s\n", bj::serialize(c).c_str());
    // serialize：斜杠与引号
    bj::value q = bj::parse(R"("a/b\"c")");
    std::printf("serialize-quote: %s\n", bj::serialize(q).c_str());
    return 0;
}
