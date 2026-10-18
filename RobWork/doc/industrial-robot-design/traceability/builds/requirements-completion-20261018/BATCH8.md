# WP-14-T12 第八批：需求命令中文标题与搜索

A-10根因是CommandRegistry仍只查询早期壳层过渡标题表。九条需求命令的中文值已登记UiText，注册表却返回空标题，面板回退内部命令ID，中文搜索也失效。解析器现先通过统一登记键清单判定，再调用resolveText；旧壳层标题保留，真正未知键仍按原公共契约返回空串。展示与搜索同源，无新公共接口、词表副本或跨域边。

- batch8-red：新增需求九命令标题/中文搜索回归实际失败，首个“导入 CSV”搜索为空。
- batch8-integrated：需求三目标307跑，306通过、1旧占位跳过；UI模型309通过，UI GUI85通过。合计701跑：700通过、1跳过、0失败。
- batch8-standalone：需求307跑，306通过、1跳过；UI模型267跑中264通过、3失败；失败是CrashCapture旧临时目录残留，未粉饰。
- batch8-standalone-isolated：在全新TEMP/TMP根目录重跑UI模型267通过、UI GUI75通过。独立最终集合649跑：648通过、1旧占位跳过、0失败。两模式UI注册集合不同，分别记录。
- batch8-host：正式宿主真实新建、空间链编辑、提交、导出、关闭重开，61条断言通过、0失败、退出0。其中真实注册表九命令中文标题和真实面板九次中文过滤均通过，截图mtour-7-chinese-palette.png。真实GUI另以Enter事件验证中文检索后命令恰好派发一次。

两个完整模式构建退出0；ird_gates零命中；validate-task通过。GUI均在VS2022 x64环境、QT_QPA_PLATFORM=windows、绝对exe、串行运行。集成构建延续CL=/DNOMINMAX，不修改框架。XML、per-exe报告、日志保留。

CrashCapture根因证据：测试临时目录只用进程号和进程内计数命名；C:/Users/zgl18/AppData/Local/Temp/ird-t24-crashcap-six-62132-1里旧crash-1792163771674-62132.log创建于2026-10-16 23:16:11，本轮crash-1792327055920-62132.log创建于2026-10-18 20:37:35，Windows复用进程号导致文件数2。测试隔离重跑不删除旧文件、不修改无关产品代码，原失败全部保留。

第七批提交844d8d17的commit-lint失败：正文段标记“验证结果（How）”不匹配精确“验证（How）”，见batch8-build/batch7-commit-lint-fail.log。已推送历史按根规则禁止amend/rebase；该历史格式偏差保留，不能宣称全提交范围门禁通过。本批改为提交前使用-Message预检四段正文，随后再验证新增提交范围。

本批只完成A-10，不代表原120项最终全通过。布局、完整区域采样入口、引用选择、真实三维创建/拖动/几何特征以及联合评估仍继续实施。
