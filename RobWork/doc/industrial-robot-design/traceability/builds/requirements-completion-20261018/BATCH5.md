# WP-14-T12 第五批：镜像/四阵列完整参数与解除关联原子撤销

所有者在本会话先后批准两份具体八节设计：ArrayParams层参数/圆法向兼容扩展，以及EditBatch.updatedPoints保身份原子更新。任务契约只放行TemplateArray.hpp这一精确公共头，其余公共头和assembly未放行；不新增跨域依赖。

生产表单支持线性方向/数量/间距、矩形独立行列层/三向间距及方向、圆心/半径/起始角/角步长/法向、任意三维折线顶点及弧长间距；逐次同源领域预览，非法不可确认，取消保持入参/草稿。镜像可设置源参考系和任意法向；当前引用输入仍为规范对象标识，真实名称选择和跨系解析待后续宿主验证，组件通过不提升为全功能通过。

领域矩形多层及非Z圆面真实数值/顺序/配方重生成已验证；默认单层/默认+Z不写新增配方键，兼容旧结果。计数乘积/折线转整数前检查资源上限100000条。损坏整数配方整串解析，拒绝后缀截断。

解除关联只提交命中批次的已关联条目为updatedPoints，保留身份及引用；领域在候选工作集统一校验，一次入栈，非法/缺失/重复/重叠目标整批不变。真实按钮一次undo/redo恢复全批。

真实执行（VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe、GUI串行）：

- array-red：生产参数控件缺失和解除关联一次undo不能恢复全批均实际失败。
- array-domain-red：2×3×2只生成6条、非Z圆面被忽略及零法向仍接受均实际失败。
- array-first-green：领域两例通过，GUI测试首次按ObjectId排序首项误取生成首点，修正为按生成名称定位；不改领域生成顺序。
- array-second-green：四构型/解除关联/非法取消通过，镜像非法标识CoreError外溢实际失败；改用core非抛出解析后通过。
- array-corrupt-recipe-red：新增损坏count3=2oops实际错误接受；严格解析修复后最终回归通过。
- batch5-integrated-final与batch5-standalone-final：每种302跑，301通过+1旧占位跳过、0失败（模型218通过+1跳过、契约16通过、GUI67通过）。逐exe XML/ird-test-report/console保留。
- 集成正式宿主/requirements三目标/ird_gates和独立三目标最终构建退出0。构建日志见batch5-build。

旧占位跳过为GuiRegistration.V22RegisteredNotExecuted_ACC1。原120项尚未全部正式宿主验证，特别是三维创建/拖动、真实runtime恢复与联合消费，继续实施；本批不宣称最终验收通过。
