/** @file FnvProbe.cpp @brief 调用真实产品FNV入口；期望由独立Python产生。 */
#include <sdurws/ird/core/Identity.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <new>
static std::atomic<unsigned> allocations{0};
static bool observe=false;
void* operator new(std::size_t n) { if(observe)++allocations; if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
int main(int argc,char**) {
    std::vector<std::uint8_t> bytes;
    if(argc>1) {
        bytes.resize(32);for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(i);
        for(auto n:{16,32}) {
            std::uint64_t hi,lo; volatile std::uint64_t sink=0;
            const auto start=std::chrono::steady_clock::now();observe=true;
            for(int i=0;i<1000000;++i) {sdurws::ird::core::detail::fnv1a128(bytes.data(),n,&hi,&lo);sink^=hi^lo;}
            observe=false;
            std::cout<<n<<' '<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<' '<<allocations<<' '<<sink<<'\n';
        }
        return allocations?1:0;
    }
    std::string text;
    while(std::getline(std::cin,text)) {
        bytes.clear();for(std::size_t i=0;i<text.size();i+=2)bytes.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i,2),nullptr,16)));
        std::uint64_t hi,lo;observe=true;
        sdurws::ird::core::detail::fnv1a128(bytes.empty()?nullptr:bytes.data(),bytes.size(),&hi,&lo);
        observe=false;
        std::cout<<std::hex<<hi<<' '<<lo<<'\n';
    }
    return allocations?1:0;
}
