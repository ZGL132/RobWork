#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONDOCUMENT_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONDOCUMENT_HPP

namespace rws {

/**
 * @brief S60 当前权威 JSON 文档的版本目录。
 *
 * 根 Envelope 与每个 canonical 分区分别编号。这样某个分区演进时可以在不
 * 伪造整个文档版本的前提下做精确迁移；这些整数是持久化协议的一部分，不能
 * 直接使用 C++ enum 的序号，也不能因为重排成员而改变。
 */
struct StructureOptimizationDocument
{
    static constexpr int SchemaVersion = 1;
    static constexpr int DesignSpaceSchemaVersion = 1;
    static constexpr int BindingSchemaVersion = 1;
    static constexpr int PlanSchemaVersion = 1;
    static constexpr int ObjectivesSchemaVersion = 1;
    static constexpr int ConstraintsSchemaVersion = 1;
    static constexpr int ConfigSchemaVersion = 1;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONDOCUMENT_HPP
