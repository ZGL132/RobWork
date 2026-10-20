# WP-13-T24 B4c-3b-2 WIP检查点

**整体仍不通过：正式宿主冲突窗口/旧稿落盘/可见迁移和47项全三维未闭合。**

- [PASS] 生产完整修订reader已绑定，建模onResult原先实际为空，现按草稿源挂接成功后绑定。原草稿投影携真实branch，L5适配按该branch/base和权威tip读取详情；非Stale零额外查询，读取失败不伪造tip。
- [PASS] 真实ProjectStore既有链增加详情断言，stale-detail-red实际失败为空详情。修复后核对原base、当前tip、人读摘要、差异对象名；无效身份/缺修订/错分支拒绝，非Stale原样返回；零项目修订。
- [PASS] 十套件1499通过、0失败、0跳过，含Windows三套GUI226；没有新增测试项，只扩展既有真实链断言。最终XML与ird报告对账，core由XML派生，checkpoint-summary.py可重算。
- [PASS] VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe逐个运行窗口，runner全程跨树互斥。已查看正式宿主modeling-tour/mtour-5-apply-failed.png，显示未应用草稿树和无模型三维空态；不把空视图说成模型显示正确。
- [PASS] 集成全量Release、独立三目标、verify-task三条通过；存量编译警告保留。manifest-anchor-red为清单行锚漂移导致59通过、1失败；仅同步现行装配清单后完整60项契约重跑通过，不改断言或冻结证据。
- [FAIL] 正式modeling-tour：策略编辑推进HEAD后旧草稿被真实Stale拒绝，旧脚本仍假设可直接应用，故两条期望失败。失败截图和日志完整保留；新增前置出口正常退出1，未再发生上批-1073741819访问冲突。失败出口生效不等于产品全链通过。
- [NOT RUN] 真正冲突三选项/旧稿apply-retained落盘/可见迁移、新完整工作集正式窗口闭环、47项全部三维与全新上下文独立验收。继续下一批，不自动迁锚或覆盖外部修订来让tour变绿。

仅本地WIP、不推送、不合入、不使用子代理，F06/F07未修改或集成。previous-checkpoint-postchecks保存上批封存后检查日志；本批封存后检查留运行目录，下批只增保存。run.ps1记录本机路径，其他检出需适配。
