# WP-24-T09 构建与验证留痕（traceability/builds/wp24-t09/）

任务：WP-24-T09 方案 A 正式产品路径退役（宿主融合收口，B.1 批次序 12 收尾；SA-18 D13）
分支：wp24-t09　base：79d1d8e1c07c4ae9172e8cc33bd8a597d414f5f9
日期：2026-09-29（单会话顺序执行——实施与验收由同一会话按角色切换完成）

## 留痕清单

| 文件 | 内容 | 结论 |
| --- | --- | --- |
| `retirement-checklist.md` | **退役清单核对表（本任务核心交付）**——B1-SPEC §5.4 四条件核对、UI-T23 草案 A/B/C 组逐项复核处置、处置登记（`_plugin`/`_app`/PropertyView/WorkcellEditor/动态加载）、零未引用目标与废弃构建选项审查、启动脚本/安装规则/About/依赖清单同步、staging 七项判据、验证汇总 | 四条件全满足；A 组保留＋移出交付路径、B 组三标记保留（forbidden 边界＋冒烟依赖——逐项依据登记）、C 组逐项复核登记；处置全部未越出 B1-SPEC §2.1 处置表口径 |
| `build_release.log` | 集成模式全量构建（仓库根 build/＋RWS_BUILD_INDUSTRIALROBOT=ON，`cmake --build build --config Release`；含 CMake 重配置） | **零错误**（退出码 0；grep -ci error＝0） |
| `cmake_config_smoke.log` | 独立冒烟模式配置（`build/ird-smoke-wp24-t09`；vcpkg toolchain 绝对路径＋Qt 前缀——F-007 口径；相对 toolchain 路径在 cmake -S 变体下解析失败改绝对路径，如实留痕） | 配置成功；产品/插件目标按登记不注册（集成树专属 STATUS 消息在场） |
| `build_smoke.log` | 独立冒烟模式全量构建 | **零错误**（退出码 0；"error" 字样 16 处均为 ErrorsTest.cpp 等文件名——WP-24-T08 同口径） |
| `install.log` | staging 组件化安装（清空目录后 `cmake --install build --config Release --prefix build/staging-wp24-t09 --component IRD_PRODUCT`） | 退出码 0；bin/plugins/share 三目录落位；框架自带未分组安装规则（include/lib 开发交付物）经组件过滤不入树 |
| `staging-tree-listing.txt` | staging 安装树全清单（56 文件） | 七项判据③④⑤的零命中证据（grep `ird_ui_plugin/_app/testkit/testdata/PropertyView/WorkcellEditor`＝0）；①⑥结构证据 |
| `staging-smoke.ps1` / `staging-smoke-run.log` / `staging-smoke-1-main-window.png` | staging 安装树**绝对路径直启**冒烟（PATH 只留 System32——vcpkg/构建树/Qt 全剥离；净室 CWD；主窗口截图；WM_CLOSE 优雅退出） | **STAGING_SMOKE_PASS**（SMOKE_EXIT=0；Dev 日志"插件诊断栈就绪"＋"宿主已关闭工作单元"两行事实）——acceptance 2② 无路径依赖判据实证 |
| `staging-smoke-about.ps1` / `staging-smoke-about-run.log` / `staging-smoke-3-palette-about.png` / `staging-smoke-4-about-dialog.png` | staging 直启 About 交互冒烟（Ctrl+Shift+P 命令面板→about→help.about→关于对话框截图→Esc→WM_CLOSE） | **STAGING_ABOUT_SMOKE_PASS**（SMOKE_EXIT=0）——acceptance 2⑦：三域"已装配"计数（建模 1/10、需求 1/9、运动学 2/8）与装配日志逐项一致；白名单余项"未装配"、版本基线"未装载"诚实占位 |
| `ird-gates-base.log` / `ird-gates-head.log` | 门禁引擎收集态两跑（base＝detached worktree @ 79d1d8e1 独立配置构建；head＝任务分支主构建树） | 引擎照实 FATAL（库内存量命中基线 65 条）；命中行全量收集 |
| `ird-gates-{base,head}-norm2.txt` | 路径归一命中集（剥离 worktree/主树路径前缀后排序） | **65↔65，diff 零差异——零新增命中**（目标图不变的机器实证；-norm.txt 为未剥离前缀的中间产物一并留痕）；base 态配置需三前置件补齐（vcpkg toolchain＋Boost 缓存变量〔CMake 4.3 FindBoost 漂移〕＋ini 模板拷入〔F-429 口径第三次冷启实录〕——`ird-gates-base-config.log` 如实留痕） |
| `ird-gates-base-config.log` | base 态独立配置日志（三次尝试：缺 Boost→CMake 4.3 FindBoost 漂移→喂 Boost_INCLUDE_DIR/Boost_DIR 缓存变量＋拷入 F-429 ini 模板后成功） | 环境漂移如实登记：PATH 的 cmake 已升 4.3.1（FindBoost 缺失），主构建树系旧版配置缓存健全——增量构建不受影响 |

## 启动冒烟与验证口径

- staging 直启＝契约 acceptance 2② 的判据本体（start_studio.bat 属开发辅助入口，不构成证据）；smoke 脚本自写 UTF-8 运行日志（GBK 控制台管道乱码规避——F-207/F-231/F-309 家族口径）。
- 首轮直启冒烟曾出现 CloseMainWindow 后 20 s 未退（kill 收场）——诊断复跑定位为初始化偶发时序（关闭指令早于装配完成被丢；窗口枚举证明无模态对话框阻塞），正式版脚本加长等待至 22 s＋二次关闭兜底后连续两轮 PASS，如实登记。
- ird_gates 退出码 1＝引擎对库内存量命中基线的照实 FATAL（WP-24-T08 同口径）——本任务真实结论以收集态两跑归一对比承载（65↔65 零差异），不粉饰引擎事实。

## 诚实边界

- About 面本任务零代码变更（核查登记于 retirement-checklist.md §5——数据驱动清单语义不受退役影响；版本基线接入属装配任务增量，acceptance 4 禁止越界重构）；
- B 组三处 deprecated 自持导航标记**保留未删**（落点在契约 forbiddenFiles 三域 plugin/ 私有面，且删除破坏 `--auto` 冒烟断言——acceptance 4 反向适用；逐项依据见 retirement-checklist.md §2.2），产品宿主装配面不呈现该区域；
- VC 运行时未捆入安装树（系统自带；离线安装包归后续交付任务，不在本契约 acceptance 内）；
- 契约 verify 第 2 条在库内存量基线上的真实结论以收集态两跑对比承载，引擎 FATAL 事实如实保留。
