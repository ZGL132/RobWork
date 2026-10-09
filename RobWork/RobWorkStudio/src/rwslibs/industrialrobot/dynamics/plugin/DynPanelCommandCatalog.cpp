/**
 * @file   DynPanelCommandCatalog.cpp
 * @brief  dynamics 插件装配登记目录的实现翻译单元（零 Qt——纯值构造
 *         与键派生；词表唯一书写点在 Commands.hpp，本 TU 零字面复制）。
 *
 * 设计依据：DynPanelCommandCatalog.hpp 文件头（登记面与键族口径）。
 * 零计算红线执行面：本 TU 全部为常量表构造与字符串拼接（键派生）——
 * 无评估/统计/排序符号（契约测试全文词表扫描钉住）。
 */

#include "DynPanelCommandCatalog.hpp"

#include <sdurws/ird/dynamics/Commands.hpp> // kCommandTokens（词表——目录
                                            //   现产的数据源；assembly 头
                                            //   零依赖 Commands.hpp，本 TU
                                            //   显式包含）

namespace sdurws::ird::dynamics {

std::vector<DynCommandDescriptor> dynDomainCommands()
{
    // 五条描述符按 §9.5 表行序构造（行序＝kCommandTokens 词表序——
    // 契约测试逐位对账）。标题键按 ui.md §3.5 键族程序化派生：
    //   "cmd." + <token> + ".title"
    // 派生规则单一、零字面复制——token 改名时键随词表联动，不会出现
    // 键与 token 漂移（词表改动本身即跨版本契约变更，必须走单元卡
    // 增量修订——Commands.hpp 词表注）。
    std::vector<DynCommandDescriptor> commands;
    commands.reserve(kCommandTokens.size());
    for (std::string_view token : kCommandTokens) {
        DynCommandDescriptor descriptor;
        descriptor.token = std::string(token);
        descriptor.titleKey = "cmd." + descriptor.token + ".title";
        commands.push_back(std::move(descriptor));
    }
    return commands;
}

std::string dynReadinessDomainKey()
{
    // 词表常量的 string 便利形态（kDynDomainKey＝"dynamics"——§6.5）。
    return std::string(kDynDomainKey);
}

}  // namespace sdurws::ird::dynamics
