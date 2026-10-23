# REUSE-T01 第九批返工独立复验

**PASS，仅关闭B9-P2-01/02登记返工。** 冻结 `a7171137a77048e2f1102b80051fb66fce52daa9`，对照原FAIL `dd27799a1afd6ba080c7ad7b4ec9d6db9da86e38`。本结论不是T01/T02整体通过或生产迁移批准。

- B9-P2-01三处qualifiedSymbol已对应真实作用域：jointStaticTransform为 `sdurws::ird::runtime::(anonymous namespace)::jointStaticTransform`；MidSolveDegradedView为全局 `::MidSolveDegradedView`；ObservedView补文件匿名namespace后，仍保留gtest TestBody局部类作用域。产品源码未变，与原轮实读namespace边界一致，未通过移动实现来迁就登记。
- B9-P2-02的findDevice已取997–1003，纳入 `rw::core::Ptr<const rw::models::SerialDevice>` 完整返回类型；独立重算规范化SHA为 `86fa247a2b003a68f5fd67c54a293c61926414cc76c285d468b52db6a91a2967`，与登记一致。其余3项源码区段摘要亦重算匹配，四条均维持decision/status pending。逐条记录见binding-check.json。
- 原轮FAIL归档 `traceability/acceptance/REUSE-T01-batch9-r1-20261023/` 与原TEMP根42文件逐一比较：42均存在，39逐字节相同；acceptance.md/check.ps1/replay-search.ps1仅CRLF/LF差异，规范换行后42内容全部相同。原SHA、FAIL、两项发现及日志均保留，见archive-check.json。
- runtime反例独立归档 `REUSE-T01-runtime-counterexample-20261023/` 与其原TEMP根25文件比对：25均在，22逐字节相同，另外3仅换行符差异，归一后25全部相同，见runtime-archive-check.json。报告仍明确“缺陷复现有效”与产品行为FAIL的区别，产品2 total/1 passed/1 failed、exit1、缺陷OPEN及runtime owner修复边界未改变。本次未重跑C++。
- 原FAIL到返工冻结的产品目录及runtime-probe diff为空（git diff --quiet exit0）。未改规范、黄金、反例、容差或上游；没有将发现转为T02已完成，也没有给kinematics重建WC或业务补偿权限。

真实治理门禁task/docs/reuse均exit0，passed=3、skipped=0、failed=0。完整日志及各exit独立保存；机器仍输出functions=239、closedFiles=0，不能以此代替整体语义验收。

独立shared clone、detached冻结HEAD，clone/checkout exit0，前后工作树干净。未改父实施树，未提交、推送、合入或派子代理。仅写此TEMP根报告/日志。

NOT RUN：C++反例/GUI（明确无需重复）；未变19条之外消费链或8条历史检索不再重跑；未验其他批次、全库DeepEvidence或全T01/T02退出。归档本次报告时仅取TEMP根文件，排除repo/。
