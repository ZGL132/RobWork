/**
 * @file LocalNameValidation.hpp
 * @brief 建模输入质量的私有名称规则，供规范桥和F12管理事务共用。
 *
 * 依据modeling §4.3-A/V-26：本函数只判输入，不合法化/消歧/拼剥前缀；
 * runtime仍是名称映射权威。规则从CanonicalBridge原样提取，无新语义。
 */
#ifndef IRD_MODELING_SRC_LOCALNAMEVALIDATION_HPP
#define IRD_MODELING_SRC_LOCALNAMEVALIDATION_HPP
#include <string_view>
namespace sdurws::ird::modeling::inputquality {
/**
 * @brief 验证已有局部名称字符集[A-Za-z0-9_.-]，非空。
 * @param name [in] 原始UTF-8文本，只读、不修剪或替换字符。
 * @return 全部字符合法为true，否则false；不抛异常。纯函数、确定性、线程安全。
 */
inline bool isLegalLocalName(std::string_view name) noexcept
{
    if(name.empty()){return false;}
    for(const char ch:name){
        const bool legal=(ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')
            ||(ch>='0'&&ch<='9')||ch=='_'||ch=='.'||ch=='-';
        if(!legal){return false;}
    }
    return true;
}
}
#endif
