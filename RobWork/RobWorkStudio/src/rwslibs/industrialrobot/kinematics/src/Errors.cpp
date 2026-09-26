/**
 * @file   Errors.cpp
 * @brief  kinematics 错误契约的实现——错误码 token 转发表与域错误→稳定
 *         码映射数据（本任务零映射行，见 Errors.hpp 文件头阶段纪律）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.2 IFkEvaluator @错误 行（4 值收编出处）、
 *     §5.2 非法输入行（IllegalQ 语义）、§9.6 表（映射数据的目标面——
 *     任务列最早 T03，本任务零行）
 *   - 先例：requirements/src/Errors.cpp（WP-14-T02 同款——switch 全枚举
 *     token 转发＋映射 switch 全枚举，无 default，新增枚举值漏登记时
 *     编译器告警暴露）
 *   - 任务契约 tasks/foundation/WP-15-T02.json acceptance 4
 *
 * 确定性（NFR-COR-02）：两张 switch 表均为编译期固定表——同码同串、
 * 同码同映射，跨进程一致；无查表容器、无动态初始化（头内 inline 函数
 * 消费静态存储期字面量）。
 */

#include <sdurws/ird/kinematics/Errors.hpp>

namespace sdurws::ird::kinematics {

std::string_view kinematicsErrorCodeToken(KinematicsErrorCode code) noexcept
{
    // token 转发表：枚举成员名原文（§9.2 @错误 行原文序）。switch 全枚举
    // 且无 default——后续任务表尾追加枚举值而漏登本表时，MSVC C4062
    // （枚举分支未处理）告警即在构建面暴露遗漏（requirements 同款防线）。
    switch (code) {
    case KinematicsErrorCode::IllegalQ:
        return "IllegalQ";
    case KinematicsErrorCode::NoDevice:
        return "NoDevice";
    case KinematicsErrorCode::NoTcp:
        return "NoTcp";
    case KinematicsErrorCode::FrameUnresolved:
        return "FrameUnresolved";
    }
    // 控制流不可达（全枚举已覆盖）；return 以满足无 default 路径的返回
    // 约定——不引入 default 分支是为了保住编译器漏项告警。
    return "IllegalQ";
}

std::optional<std::string_view> kinematicsDiagCode(KinematicsErrorCode code) noexcept
{
    // 映射数据表：§9.6 已到消费任务的码行。当前（WP-15-T02）§9.6 全表
    // 15 行任务列最早为 T03（KIN-NO-DEVICE/KIN-NO-TCP/KIN-NEAR-SINGULAR）
    // ——零映射行，全表 nullopt（分批纪律："注册义务随各消费任务展开"
    // ——契约 acceptance 4 原文）。T03 起在此表尾追加映射行（switch 分支
    // 返回 DiagCodes.hpp 同名码值常量），禁止私定新码值凑数。
    switch (code) {
    case KinematicsErrorCode::IllegalQ:
        break;  // 无 §9.6 同义码行（q 维度/非有限是调用方错误值面——§5.2）
    case KinematicsErrorCode::NoDevice:
        break;  // KIN-NO-DEVICE（§9.6 T03 行）——映射随 T03 FK 消费者登记
    case KinematicsErrorCode::NoTcp:
        break;  // KIN-NO-TCP（§9.6 T03 行）——映射随 T03 FK 消费者登记
    case KinematicsErrorCode::FrameUnresolved:
        break;  // 无 §9.6 同义码行（名称解析语义归快照名称表——PA-3/R-4）
    }
    return std::nullopt;  // 暂无已登记映射码——调用方不得产诊断（值面返回）
}

}  // namespace sdurws::ird::kinematics
