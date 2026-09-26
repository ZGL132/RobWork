/**
 * @file   Errors.cpp
 * @brief  kinematics 错误契约的实现——错误码 token 转发表与域错误→稳定
 *         码映射数据（映射行随消费任务落位：T03 起 NoDevice/NoTcp 两行，
 *         见 kinematicsDiagCode 函数注）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.2 IFkEvaluator @错误 行（4 值收编出处）、
 *     §5.2 非法输入行（IllegalQ 语义）、§9.6 表（映射数据的目标面——
 *     分批纪律：行随其生产者接口的落位任务登记）
 *   - 先例：requirements/src/Errors.cpp（WP-14-T02 同款——switch 全枚举
 *     token 转发＋映射 switch 全枚举，无 default，新增枚举值漏登记时
 *     编译器告警暴露）
 *   - 任务契约 tasks/foundation/WP-15-T02.json acceptance 4（表落地）＋
 *     tasks/foundation/WP-15-T03.json（映射两行随消费任务登记）
 *
 * 确定性（NFR-COR-02）：两张 switch 表均为编译期固定表——同码同串、
 * 同码同映射，跨进程一致；无查表容器、无动态初始化（头内 inline 函数
 * 消费静态存储期字面量）。
 */

#include <sdurws/ird/kinematics/Errors.hpp>

#include <sdurws/ird/kinematics/DiagCodes.hpp>  // kKinNoDevice/kKinNoTcp——映射目标的注册码常量（T03 行）

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
    // 映射数据表：§9.6 已到消费任务的码行。WP-15-T03（FK 与位姿指标）
    // 落位 IFkEvaluator/KinTypes/Fk——NoDevice/NoTcp 两值的生产者面到站
    // （Fk.cpp 结构化错误素材→Evaluators.cpp 评估期诊断），映射两行按
    // §9.6 表序登记（码值＝DiagCodes.hpp 同名常量 kKinNoDevice/kKinNoTcp
    // ——禁字符串拼码，映射面与码面同串由 DiagCodesTest 交叉核对）。
    // IllegalQ/FrameUnresolved 仍无 §9.6 同义码行（q 非法是调用方错误值
    // 面——§5.2；帧未解析语义归快照名称表——PA-3/R-4），维持 nullopt；
    // KIN-NEAR-SINGULAR 的产生面随 policy 阈值消费面落位（T02 CMake 注
    // 的 policy 边消费分工 T04/T07）——本表只映射域错误，不映射评价级
    // 诊断（评价级诊断由其生产者直接经注册码产出）。
    switch (code) {
    case KinematicsErrorCode::IllegalQ:
        break;  // 无 §9.6 同义码行（q 维度/非有限是调用方错误值面——§5.2）
    case KinematicsErrorCode::NoDevice:
        return kKinNoDevice;  // §9.6 行 1（error——无可用设备；T03 消费者）
    case KinematicsErrorCode::NoTcp:
        return kKinNoTcp;  // §9.6 行 2（error——TCP 未配置/悬空；T03 消费者）
    case KinematicsErrorCode::FrameUnresolved:
        break;  // 无 §9.6 同义码行（名称解析语义归快照名称表——PA-3/R-4）
    }
    return std::nullopt;  // 无已登记映射码——调用方不得产诊断（值面返回）
}

}  // namespace sdurws::ird::kinematics
