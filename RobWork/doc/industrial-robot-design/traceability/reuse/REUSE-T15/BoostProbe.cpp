/** @file BoostProbe.cpp @brief GAP-CORE-FNV-01：验证已安装固定精度后端，不修改依赖。 */
#include <boost/multiprecision/cpp_int.hpp>
#include <type_traits>
#include <iostream>
#include <string>
#include <cstdint>
using boost::multiprecision::uint128_t;
// public operator 未声明 noexcept；核后端为 unchecked/void 固定存储，见适用性记录。
static_assert(std::is_same<uint128_t::backend_type, boost::multiprecision::cpp_int_backend<128,128,boost::multiprecision::unsigned_magnitude,boost::multiprecision::unchecked,void>>::value, "固定精度无分配后端");

int main() {
    std::string text;
    while(std::getline(std::cin,text)) {
        uint128_t state=(uint128_t(0x6C62272E07BB0142ULL)<<64)|0x62B821756295C58DULL;
        const uint128_t prime=(uint128_t(1)<<88)|(uint128_t(1)<<8)|0x3B;
        for(std::size_t i=0;i<text.size();i+=2) {
            state ^= static_cast<std::uint8_t>(std::stoul(text.substr(i,2),nullptr,16));
            state *= prime;
        }
        std::cout<<std::hex<<static_cast<std::uint64_t>(state>>64)<<' '<<static_cast<std::uint64_t>(state)<<'\n';
    }
}
