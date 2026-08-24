# EngineeringRequirements 工具栏图标设计

## 目标

为 RobWorkStudio 的 `EngineeringRequirements` 插件新增一枚与现有插件图标一致的 64×64 透明 PNG，并让四个工程插件在工具栏中只显示图标而非文字：

- EngineeringRequirements
- RobotModelBuilder
- KinematicAnalysis
- StructureOptimizer

现有 RobotModelBuilder、KinematicAnalysis 与 StructureOptimizer 图标保持不变。

## 图标设计

`EngineeringRequirements` 图标使用“工程需求蓝图”语义：

- 主体是金属灰、带浅色高光的三维线框立方体，表达机械臂工作空间与工程对象。
- 前景是一张深青蓝的规格卡片，以两条浅色横线和一个勾选标记表达需求定义与已记录的约束。
- 背景保持完全透明，留出至少 4px 安全边距；主体在 64×64 与工具栏缩放后仍可清晰辨认。
- 颜色、边缘对比和轻微阴影与现有三枚 64×64 PNG 保持一致，避免引入文字、渐变背景或高饱和大面积配色。

## 资源与接入

- 在 `RobWorkStudio/src/rwslibs/engineeringrequirements/` 新增 `engineeringrequirements_icon.png` 与 `resources.qrc`。
- 将资源加入 EngineeringRequirements 的 CMake target。
- 插件构造函数从空 `QIcon()` 改为加载 `:/engineeringrequirements/engineeringrequirements_icon.png`。

## 工具栏行为

- 找到 RobWorkStudio 将插件名作为工具栏按钮文本的创建处。
- 对上述四个插件的动作启用图标显示并隐藏可见文字，保持菜单文本与 action 的 `toolTip`/`statusTip` 为完整插件名称。
- 不改变插件加载、菜单项、快捷键或其他插件的工具栏显示。

## 验证

1. 构建受影响的 EngineeringRequirements 插件与 RobWorkStudio 应用目标。
2. 在 Windows 的 Visual Studio x64 环境中，以 `QT_QPA_PLATFORM=windows` 单独启动 RobWorkStudio。
3. 检查四个工具栏入口都仅显示对应图标，EngineeringRequirements 图标在浅色工具栏上清晰可辨，悬停能显示完整名称。
4. 确认三枚原有图标未被修改，菜单中的插件名称不受影响。
