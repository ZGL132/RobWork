/**
 * @file   KinTypes.cpp
 * @brief  kinematics 值模型的函数面实现——ConfigurationSignature 构型
 *         签名的 canonical 编码（T04 批次；结构体面均在头内直读）。
 *
 * 设计依据：units/kinematics.md §6.1（ConfigurationSignature——q 的
 * canonical 编码（全精度定宽），仅作记录键、不用于去重——I-KIN-3）、
 * §4.4 编码纪律；任务契约 WP-15-T04 acceptance 3（"去重=成对容差比较
 * 而 ConfigurationSignature 精确编码仅作记录键——I-KIN-3 语义在此实现"）。
 *
 * 确定性：位模式直写无格式化（NFR-COR-01/02——同构型同签名、跨进程
 * 逐字节一致）；编码布局与头注契约一致（magic "IRDSIG01"＋dof u32＋
 * 逐自由度 f64 小端）。
 */

#include <sdurws/ird/kinematics/KinTypes.hpp>

#include "CanonicalCodec.hpp"  // 私有编码原语（src/ 内共享——R-2 合规）

namespace sdurws::ird::kinematics {

std::string configurationSignature(const std::vector<double>& q)
{
    // 编码布局（codec 版本 1——KinTypes.hpp 函数注）：magic 8 字节＋
    // dof u32＋逐自由度 f64 位模式。全精度＝不做任何舍入/归一化
    // （±0 的位级区分保留——I-KIN-3"哈希不同但去重等价"语义的编码面）。
    std::vector<std::uint8_t> bytes;
    bytes.reserve(8 + 4 + q.size() * 8);
    const char magic[] = {'I', 'R', 'D', 'S', 'I', 'G', '0', '1'};
    detail::putBytes(bytes, magic, sizeof(magic));
    detail::putU32(bytes, static_cast<std::uint32_t>(q.size()));
    for (const double v : q) {
        detail::putF64(bytes, v);
    }
    return detail::toHex(bytes);
}

}  // namespace sdurws::ird::kinematics
