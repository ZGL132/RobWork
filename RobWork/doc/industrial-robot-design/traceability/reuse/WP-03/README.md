# WP-03／GOV-T25 既有能力交接

当前为交接准备与原证据核对，**不是独立验收**。核查树 `e98cf3deb2c8f5680edc766e963235fe4954f25d` 对应T20实现版本 `9276f19b26bfe095cbe2ccf460a64dec39bf45e9`，原标题已经所有者授权更正，产品源码与原核查输入不变；原始结果见 [handoff-input-check.txt](handoff-input-check.txt)。本包正式起始版本为T20裁决文档提交 `2ac114f33d2102d0b7848d8d4d51345147fa32b8`，送验时以本包提交SHA及该起点显式核对范围。原任务均保持 ready；没有源码替换、C++构建或重写历史证据。

| 包／原实施版本 | 本轮核实与可复用证据 | 待独验／明确遗留 |
| --- | --- | --- |
| T13／`f378f01c5` | [原README](../REUSE-T13/README.md)、[适用性](../REUSE-T13/suitability.md)；manifest 7/7源一致；原runtime 305、modeling 474、冒烟195通过，生产断开19通过＋13失败；2026-10-25模型真值定向核验（offset正负/轴/安装/限位/旋转重跑305＋独立oracle 25项）验清，见[t13-model-truth-20261025](t13-model-truth-20261025/README.md)，不替代独验 | 移动offset正负/关节轴/安装旋转/限位及旋转回归（已由上述核验复现）；一般轴滚转消费者核对、OBJ→Indexed、非Windows网格并发与上游mask差异；上游原例8通过＋1失败，不能报全绿；独验NOT RUN |
| T15／`091e53353` | [原README](../REUSE-T15/README.md)、[verification](../REUSE-T15/verification.json)、[精确oracle](../REUSE-T15/production-oracle.json)、[已接受性能](../REUSE-T15/performance-release.json)；生产/测试4/4一致，原scopeInputs29同＋1变；1017组精确oracle，core69/runtime305/execution27/project282/冒烟67原结果直接引用 | 唯一变化core/CMake来自WP-01供给修复，不是FNV算法变化；旧冒烟配置不冒称当前配置，当前供给引用[GOV-T24独验](../../acceptance/GOV-T24-20261024-r1.json)。原已接受性能仅适用本FNV范围；独验NOT RUN |
| T16／`091e53353` | [原README](../REUSE-T16/README.md)、[verification](../REUSE-T16/verification.json)；manifest5/5、scopeInputs39/39一致；diagnostics122/契约26/冒烟122原通过记录，原夹具错误121通过＋1失败仍保留 | keep1、5→2/2→5、超长后缀及目录/非普通文件边界；不新增多进程轮转、断电持久性或磁盘满承诺；独验NOT RUN |
| T07／既有CouplingMath集成 | [任务与原证据入口](../../../tasks/REUSE-T07.json)；manifest3/3一致，保留CouplingMath已替换上游SVD，不重写 | 未定位到该任务独立PASS记录，不能因已集成关闭；原性能约4.662倍，尚未获接受，不能因已集成关闭；独验核奇异/病态阈值、矩形SVD、秩亏/缩放与诊断分类，不外用旋转容差 |
| T09／`8db7a7e78`＋`aee6ac5bf`＋`b0997f8d8` | [原README](../REUSE-T09/README.md)、[任务](../../../tasks/REUSE-T09.json)；manifest4/4一致；保留189业务/69契约、断开/性能原件，catalog verified已整合 | 独立验收待实际独立记录；原性能+17.7%/+34.3%按原应用场景审查，不自判满足预算；不重做Smooth/TimeParam |
| T12／`bc212f245`及后续覆盖修复 | [原README后续补全](../drivetrain-linalg/README.md)、[任务](../../../tasks/REUSE-T12.json)；manifest4/4一致；42业务/15契约及两处生产断开原件保留 | 性能对比NOT RUN、独验待完成。旧CMake REUSE-COVERAGE已由2026-10-23补全消除，当前[复用机检](../REUSE-T05/setcheck-max-matching/wp02-20261024/validate-docs-final.log)也未命中；不把该历史失败继续当现行阻断 |

核对方法：标准库读取原manifest及verification.scopeInputs，按原raw或reuse UTF-8/LF摘要与当前文件比较；只解析既有gtest XML逐例计数，不运行原测试。acceptance目录按确切REUSE任务ID定向检索未找到对应独立记录；该结论只限仓库现有可定位记录，不证明未归档的外部验收不存在。

数据流/边界维持原契约：runtime独占WorkCell模型、移动关节offset为长度且q_auth=offset+q_rw；core只维护值身份，FNV委托既有Boost固定uint128；diagnostics维护唯一日志与稳定诊断体系；不更改公共签名、不扩大owner或worker句柄边界。EAA撤销继续有效，局部1e-6不传播到FK/限位或物理映射。

**独验者的下一步**：以当前送验代码版本核对以上原件及相关实际输入，T15供给变化引用GOV-T24；T17/T19/WP-02改变的runtime视图/消费者不能依靠旧整模块结果声称当前整模块通过，只对实质受影响的链追加验证。必要一次独验由所有者另开独立会话或人工实施，不自动派代理；可用现有任务/脚本和原夹具，不重配全库、不重建未变模块。T13模型真值已于2026-10-25定向核验验清（[记录](t13-model-truth-20261025/README.md)；只核验、不自签独验），「放行依赖它的普通IK/RNE」裁决仍归所有者；T15/T16独验和T12性能可独立安排。

本记录不新签任何PASS、性能接受或任务done。发现缺陷先确定精确返工契约；原任务验收条件不能由GOV-T25替代。回滚仅撤销本次交接文档，不改产品/原凭证/原验收。
