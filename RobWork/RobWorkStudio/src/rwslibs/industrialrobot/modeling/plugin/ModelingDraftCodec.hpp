/**
 * @file ModelingDraftCodec.hpp
 * @brief PM-04/08、AT-21：建模完整工作集的私有磁盘域帧，依据WP-13-T24 B4b。
 * project外层草稿格式不变；此处仅拥有域payload，不拥有修订/文件/运行时真值。
 */
#ifndef IRD_MODELING_PLUGIN_MODELINGDRAFTCODEC_HPP
#define IRD_MODELING_PLUGIN_MODELINGDRAFTCODEC_HPP
#include <sdurws/ird/modeling/Template.hpp>
#include <optional>
#include <string>
namespace sdurws::ird::modeling {
/**
 * @brief 把完整草稿封装为确定性UTF-8 JSON；对象物理量只经唯一RobotDesignCodec。
 * @param ws [in] UI线程持有的完整工作集；不变更它或创建项目身份。
 * @return 私有格式ird-modeling-draft/version=1，保留数组及摘要顺序。
 * @throws std::runtime_error 对象编码失败；不返回半个载荷。
 * 无共享状态、不做IO，调用方持有返回字符串。局部undo栈不进入磁盘。
 */
std::string encodeModelingDraft(const ModelingWorkingSet& ws);
/**
 * @brief 完整解析并核对闭包后返回候选；外部坏数据用错误值，不修改当前工作集。
 * @param payload [in] 新版本JSON或旧根对象hex，最大64MiB（封装解析预算）。
 * @param baseline [in] 当前工作集；仅为旧根载荷补齐确切ID部件，不用于新帧重建。
 * @param error [out] 中文失败原因，成功清空；旧格式缺部件时诚实拒绝。
 * @return 完整候选或nullopt；根ID、来源、m/rad/kg及原Codec位模式保留。
 * 纯值、无共享状态/IO；内存分配异常不吞掉，坏格式不泄漏部分解码状态。
 */
std::optional<ModelingWorkingSet> decodeModelingDraft(const std::string& payload,
    const ModelingWorkingSet& baseline,std::string& error);
}
#endif
