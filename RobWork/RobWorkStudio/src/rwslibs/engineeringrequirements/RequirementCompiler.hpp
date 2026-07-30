#ifndef RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTCOMPILER_HPP
#define RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTCOMPILER_HPP

#include "EngineeringRequirementTypes.hpp"

#include <string>
#include <vector>

namespace rws {

/**
 * @brief 需求编译器与静态校验器 (Requirement Compiler)
 * 
 * 该类是需求工程模块的核心校验与编译引擎，主要负责：
 * 1. 静态合法性检查（ID 唯一性、数值有效性、约束完备性等）；
 * 2. 算子/规则编译（将编辑态 RequirementSet 转换为只读执行态 CompiledRequirementSet）；
 * 3. 产生内容哈希指纹（Fingerprint），实现需求文件与后续运动学优化/路径规划计算结果的强绑定。
 */
class RequirementCompiler {
public:
    /**
     * @brief 对需求集进行详细的静态合法性校验（返回丰富诊断信息）
     * 
     * 逐项检查 RequirementSet 中的每一个关键工位 (PoseTask) 和工作区域 (BoxRegion)：
     * - 检查必填项（如 ID、名称、参考系、TCP 等是否缺失）；
     * - 检查 ID 是否重复；
     * - 校验浮点数合法性（非 NaN/Inf，取值范围如公差 $\ge 0$、置信度 $\in [0,1]$ 等）；
     * - 校验姿态规则（如 AlignFrame 是否指定了 targetFrame）及接近/撤离参数。
     * 
     * @param requirements 待校验的编辑态需求集
     * @return std::vector<RequirementDiagnostic> 包含错误/警告日志、需求 ID、等级及阻断标志的诊断列表
     */
    static std::vector<RequirementDiagnostic> validateDetailed(const RequirementSet& requirements);

    /**
     * @brief 对需求集进行简化的文本校验
     * 
     * 内部直接调用 validateDetailed()，并将诊断结果提炼为纯文本消息列表，方便快速输出或记录日志。
     * 
     * @param requirements 待校验的编辑态需求集
     * @return std::vector<std::string> 诊断错误/警告文本消息列表
     */
    static std::vector<std::string> validate(const RequirementSet& requirements);

    /**
     * @brief 编译需求集：将编辑态数据模型编译为只读执行态模型
     * 
     * 编译流程逻辑：
     * 1. 调用 validateDetailed() 执行全量校验。若存在阻断性错误（blocking == true，通常源于 RequirementLevel::Must 违规），
     *    编译直接宣告失败并返回 false；
     * 2. 过滤掉仅作为参考信息的 RequirementLevel::Info 级别项，以及存在非阻断错误的建议项；
     * 3. 构建 CompiledRequirementSet，并将关键工位转换为只读的 CompiledPoseTask；
     * 4. 自动生成唯一的内容哈希指纹并写入 compiled.requirementFingerprint；
     * 5. 标记 compiled.frozen = true，锁死数据。
     * 
     * @param requirements 输入：待编译的编辑态需求集
     * @param compiled 输出：编译生成的只读执行态需求集（仅在返回 true 时有效）
     * @param error 可选输出：编译失败时的阻断性错误原因描述
     * @return true 编译成功（无阻断性错误）
     * @return false 编译失败
     */
    static bool compile(const RequirementSet& requirements, CompiledRequirementSet& compiled,
                        std::string* error = nullptr);

    /**
     * @brief 计算需求集的内容规范化哈希指纹 (SHA-256 Fingerprint)
     * 
     * 用于确保需求数据的绝对不可篡改性与全流程追溯：
     * 1. 拷贝输入需求集，并将临时副本的 frozen 标志统一归零复位（消除状态干扰）；
     * 2. 将规范化后的需求集序列化为 Compact 格式的 JSON 字节流；
     * 3. 计算该字节流的 SHA-256 哈希值并以十六进制字符串形式返回。
     * 
     * 只要需求集的内容（工位坐标、公差、规则等）有任何微小改动，生成的指纹都会发生改变。
     * 
     * @param requirements 输入的需求集
     * @return std::string 64 位十六进制 SHA-256 指纹字符串
     */
    static std::string fingerprint(const RequirementSet& requirements);
};

} // namespace rws

#endif