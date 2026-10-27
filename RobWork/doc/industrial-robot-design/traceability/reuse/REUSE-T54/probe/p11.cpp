#include <boost/json.hpp>
#include <cstdio>
#include <string>
namespace bj = boost::json;
int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Q0 raw-object-u0001\n");
    bj::value a = bj::parse(R"({"a":"\u0001"})");
    std::printf("Q1 ok kind=%d\n", static_cast<int>(a.kind()));
    std::printf("Q2 top-level-string-u0001-raw\n");
    bj::value b = bj::parse(R"("\u0001")");
    std::printf("Q3 ok kind=%d\n", static_cast<int>(b.kind()));
    return 0;
}
