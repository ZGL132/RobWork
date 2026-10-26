// P5 探针终案：serialize 对转义控制字符与引号/斜杠的输出形态。
#include <boost/json.hpp>
#include <cstdio>
#include <string>
namespace bj = boost::json;
int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    bj::value c = bj::parse("\"a\\u0001b\"");
    std::printf("ctrl-in size=%zu bytes=", c.get_string().size());
    for (unsigned char ch : std::string(c.get_string().c_str(), c.get_string().size()))
        std::printf("%02x", ch);
    std::printf("\nserialize-ctrl: %s\n", bj::serialize(c).c_str());
    bj::value q = bj::parse("\"a/b\\\"c\"");
    std::printf("serialize-quote: %s\n", bj::serialize(q).c_str());
    return 0;
}
