/**
 * @file   SelPanelCommandCatalog.cpp
 * @brief  selection 插件装配登记目录的实现翻译单元（零 Qt——纯值构造
 *         与键派生；挂位/域键词表唯一书写点在 assembly/ 公共头，命令
 *         token 自持常量在本目录头——对账口径见其文件头注）。
 *
 * 设计依据：SelPanelCommandCatalog.hpp 文件头（登记面与键族口径）。
 * 零计算红线执行面：本 TU 全部为常量表构造与字符串拼接（键派生）——
 * 无任何选型计算符号（契约测试全文词表扫描钉住）。
 */

#include "SelPanelCommandCatalog.hpp"

namespace sdurws::ird::selection {

std::vector<SelCommandDescriptor> selDomainCommands()
{
    // 一条描述符（回填入口——§16 T10 行；本域零会话命令，与 dynamics
    // 五会话命令的差异登记于 SelCommandDescriptor 类型注）。
    // 标题键按 ui.md §3.5 键族程序化派生："cmd." + <token> + ".title"
    // ——派生规则单一、零字面复制（token 改名时键随词表联动，不会
    // 出现键与 token 漂移；词表改动本身即跨版本契约变更，必须走单元
    // 卡增量修订——自持常量注）。
    std::vector<SelCommandDescriptor> commands;
    SelCommandDescriptor descriptor;
    descriptor.token = std::string(kSelBackfillCommandToken);
    descriptor.titleKey = "cmd." + descriptor.token + ".title";
    commands.push_back(std::move(descriptor));
    return commands;
}

std::string selReadinessDomainKey()
{
    // 词表常量的 string 便利形态（kSelDomainKey＝"selection"——§6.5）。
    return std::string(kSelDomainKey);
}

}  // namespace sdurws::ird::selection
