#ifndef RWS_STRUCTUREOPTIMIZATION_CANDIDATEPREVIEWCONTROLLER_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANDIDATEPREVIEWCONTROLLER_HPP

#include "StructureOptimizationTypes.hpp"

#include <QString>

#include <memory>

// 前置声明 Qt 临时目录类，避免包含头文件以加快编译速度
class QTemporaryDir;

namespace rws {

/**
 * @brief 3D 视口预览宿主抽象接口 (展台控制器)。
 * 
 * 优化插件 (StructureOptimizerPlugin) 会实现该接口，
 * 为 CandidatePreviewController 提供访问与控制 RobWorkStudio 主视口场景的能力。
 */
class IWorkCellPreviewHost
{
public:
    virtual ~IWorkCellPreviewHost() = default;

    /**
     * @brief 获取宿主主视口当前正在加载展示的 WorkCell (场景) 绝对路径。
     * @return QString 当前场景 XML 文件路径
     */
    virtual QString currentWorkCellPath() = 0;

    /**
     * @brief 命令宿主主视口打开并渲染指定路径下的 WorkCell 场景。
     * @param path 待渲染场景 XML 文件的绝对路径
     * @param error [out] 可选的输出错误字符串
     * @return true 场景加载并渲染成功；false 加载失败
     */
    virtual bool openWorkCell(const QString& path, QString* error) = 0;
};

/**
 * @brief 候选解 3D 视口预览控制器类。
 * 
 * 负责管理在 RobWorkStudio 3D 中央视口中对变异后的候选解进行实时模型渲染与恢复。
 * 核心设计遵循安全无污染原则：预览前自动备份原始场景路径，预览时将变异后的模型导出至
 * 临时文件夹并加载展示，结束预览时自动恢复原场景并销毁临时文件。
 */
class CandidatePreviewController
{
public:
    /**
     * @brief 构造函数，绑定预览宿主。
     * @param host 实现 IWorkCellPreviewHost 接口的宿主指针 (通常为 StructureOptimizerPlugin)
     */
    explicit CandidatePreviewController(IWorkCellPreviewHost* host);

    /**
     * @brief 析构函数。
     * 
     * 确保在控制器销毁时自动调用 clearPreview()，将宿主 3D 视口安全还原回原始场景，
     * 防止残留临时模型。
     */
    ~CandidatePreviewController();

    /**
     * @brief 在 3D 视口中预览指定的候选解方案。
     * 
     * 内部流程：
     *  1. 若首次预览，通过 _host 备份当前场景的原始文件路径；
     *  2. 根据 candidate 的设计变量 values 对 problem 中的模型进行突变并合并环境快照；
     *  3. 在临时目录 QTemporaryDir 下生成临时预览 XML 文件；
     *  4. 调用 _host->openWorkCell() 让主画面渲染变异后的新机械臂；
     *  5. 记录当前预览的候选解索引 _previewedCandidateIndex。
     * 
     * @param problem 优化问题定义 (包含基线模型与场景快照)
     * @param candidate 待预览的候选解评估结果对象 (包含该解的变量值 values)
     * @param error [out] 可选的输出错误描述信息
     * @return true 预览成功切换；false 突变或文件生成失败
     */
    bool preview(const StructureOptimizationProblem& problem,
                 const StructureCandidateResult& candidate,
                 QString* error = nullptr);

    /**
     * @brief 清除当前 3D 预览，还原主视口。
     * 
     * 内部流程：
     *  1. 调用 _host->openWorkCell(_sourceWorkCellPath) 重新加载原始场景文件；
     *  2. 重置备份路径 _sourceWorkCellPath；
     *  3. 销毁临时目录 _temporaryDirectory，擦除临时 XML 文件；
     *  4. 重置预览索引 _previewedCandidateIndex 为 -1。
     */
    void clearPreview();

    /**
     * @brief 获取当前正在 3D 视口中预览的候选解索引。
     * @return int 候选解索引编号 (如 0, 1, 5)；若当前未在预览任何候选解则返回 -1。
     */
    int previewedCandidateIndex() const;

private:
    IWorkCellPreviewHost* _host = nullptr;              //!< 关联的 3D 视口宿主接口指针
    QString _sourceWorkCellPath;                          //!< 开始预览前备份的原始场景 XML 文件绝对路径
    std::unique_ptr<QTemporaryDir> _temporaryDirectory;  //!< 用于存放临时预览 XML 模型的智能指针托管目录
    int _previewedCandidateIndex = -1;                   //!< 当前正在预览的候选解索引 (缺省 -1)
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_CANDIDATEPREVIEWCONTROLLER_HPP