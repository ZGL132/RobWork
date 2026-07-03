// =============================================================================
//  文件: RobotModelBuilderWidget.hpp
//  说明: 整个 RobotModelBuilder 插件的"UI/交互层"声明。
//        它是 Plugin 创建并嵌入到 RobWorkStudio 的 QWidget,负责:
//          1) 用 QFormLayout/QTabWidget/QTableWidget 等组织表单;
//          2) 在用户编辑时把表单数据收集为 RobotModelSpec;
//          3) 触发 XML 预览、保存,并通过信号通知 Plugin 去加载生成的场景;
//          4) 与 RobotModelXmlWriter 协作完成"建模 -> 预览 -> 落盘"全流程。
// =============================================================================
#ifndef RWS_ROBOTMODELBUILDER_WIDGET_HPP
#define RWS_ROBOTMODELBUILDER_WIDGET_HPP

#include "RobotModelSpec.hpp"

#include <QWidget>

// 前向声明 Qt 类型,避免在头文件中引入过多 Qt 头文件,缩短编译时间
class QCheckBox;
class QComboBox;
class QLineEdit;
class QTableWidget;
class QTextEdit;

namespace rws {

class RobotModelBuilderWidget : public QWidget
{
    Q_OBJECT
  public:
    /// 构造函数:创建子控件并填入默认值
    explicit RobotModelBuilderWidget (QWidget* parent = NULL);

  Q_SIGNALS:
    /// 当用户点击 "Save and Load" 完成后,由 Widget 发出场景文件路径
    /// 由 RobotModelBuilderPlugin::loadSceneFile 负责真正加载
    void loadSceneRequested (const QString& filename);

  private Q_SLOTS:
    /// 把 UI 恢复为"通用 6 轴机器人"出厂默认
    void resetDefaults ();
    /// 根据当前 UI 生成 XML 预览(同时校验输入合法性)
    void generatePreview ();
    /// 把 XML 写入磁盘(但不加载场景)
    void saveXml ();
    /// 保存 + 让 Plugin 加载新场景
    void saveAndLoad ();
    /// 弹出目录选择对话框,选择保存目录
    void browseSaveDirectory ();
    /// 切换建模方式(DH / Joint+RPY+Pos)时的 UI 同步
    void modeChanged (int index);
    /// Poses 标签页:新增一行空位姿
    void addPose ();
    /// Poses 标签页:删除当前选中行(至少保留 1 行)
    void removeSelectedPose ();

  private:
    /// 构建整个 UI 控件树(在构造函数中调用)
    void buildUi ();
    /// 用 spec 的数据回填 UI(在 resetDefaults 中使用)
    void fillFromSpec (const RobotModelSpec& spec);
    /// 把当前 UI 数据收集为 spec(用于校验/预览/保存)
    RobotModelSpec collectSpec () const;
    /// 在生成预览/保存前,先用纯文本规则校验每个表格的输入,把错误追加到 errors
    bool validateTableInput (QStringList& errors) const;
    /// 用 spec 回填 DH + JointRPY+Pos 两个表格
    void fillKinematicsTables (const RobotModelSpec& spec);
    /// 用 spec 回填 Drawables 表格
    void fillDrawablesTable (const RobotModelSpec& spec);
    /// 用 spec 回填关节限位表格
    void fillLimitsTable (const RobotModelSpec& spec);
    /// 用 spec 回填预设位姿表格
    void fillPosesTable (const RobotModelSpec& spec);
    /// 用 spec 回填动力学 tab 的链接表与力限表
    void fillDynamicsTab (const RobotModelSpec& spec);
    /// 把 errors 显示在 MessageBox 与状态栏
    void showErrors (const QStringList& errors);
    /// 设置底部状态栏文本
    void setStatus (const QString& message);

    /// 安全读取 QTableWidget 单元格文本(空指针返回空串,自动 trim)
    static QString itemText (const QTableWidget* table, int row, int column);
    /// 读取单元格并转 double(失败返回 0.0)
    static double itemDouble (const QTableWidget* table, int row, int column);
    /// 把 "x y z" 形式的文本解析为 std::array<double,3>
    static bool parseVector3 (const QString& text, std::array< double, 3 >& values);
    /// 把 "x1 ... x6" 形式的文本解析为 std::array<double,6>(惯量)
    static bool parseVector6 (const QString& text, std::array< double, 6 >& values);
    /// 设置单元格文本,可选择是否可编辑(用于把自动生成的字段锁住)
    static void setItem (QTableWidget* table, int row, int column, const QString& value,
                         bool editable = true);
    /// 判断 Drawable 名是否匹配 "Link{i}To{i+1}",决定是否自动从关节几何生成
    static bool isAutoLinkDrawable (const QString& name);
    /// 把 std::array<double,3> 格式化为 "x y z"
    static QString vectorText (const std::array< double, 3 >& values);
    /// 把 std::array<double,6> 格式化为 "x1 x2 x3 x4 x5 x6"
    static QString vectorText6 (const std::array< double, 6 >& values);

  private:
    // ---- 基本信息 ----
    QLineEdit* _robotName;        // 机器人名(也是文件名前缀)
    QLineEdit* _saveDirectory;    // XML 输出目录
    QComboBox* _mode;             // 建模方式下拉框(DH / Joint+RPY+Pos)

    // ---- 选项开关 ----
    QCheckBox* _showFrameAxes;    // 是否在每个 Frame/Joint 上画坐标轴
    QCheckBox* _generateDrawables;// 是否输出 <Drawable> 节点
    QCheckBox* _generateScene;    // 是否额外生成 Scene.wc.xml
    QCheckBox* _generateDwc;      // 是否生成 DynamicWorkCell

    // ---- 动力学 ----
    QLineEdit* _baseFrame;        // 动力学基座 frame 名
    QLineEdit* _baseMaterial;     // 动力学基座材料名

    // ---- 运动学 ----
    QTableWidget* _dhTable;           // DH 参数表(alpha / a / d / offset)
    QTableWidget* _transformTable;    // Joint+RPY+Pos 表

    // ---- 可视化 / 限位 / 位姿 ----
    QTableWidget* _drawablesTable;    // Drawable 列表
    QTableWidget* _limitsTable;       // 关节限位表
    QTableWidget* _posesTable;        // 预设位姿表

    // ---- 动力学详细 ----
    QTableWidget* _dynamicsLinksTable; // link 的质量/质心/惯量/材料
    QTableWidget* _forceLimitsTable;   // 各关节力限

    // ---- XML 预览 ----
    QTextEdit* _serialPreview;       // <SerialDevice> XML 预览
    QTextEdit* _scenePreview;        // Scene XML 预览
    QTextEdit* _dwcPreview;          // DWC XML 预览

    // ---- 状态 ----
    QLineEdit* _status;              // 底部状态栏
};

}    // namespace rws

#endif