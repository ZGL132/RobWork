B28 独立验收：FAIL（仅局部审计增量）
冻结 f2e65141259cd43ee5e63799514a733908440355；基点 f6d3de37ddbc8375a334b456b058e3d548141484。
四项整改：ConfigStamp名称错误；qualifiedSymbol不真实；permissionRef统一worker条款不精确；CORE-EVENT操作不能仅申报value。
原件37项raw摘要、历史findings原始前缀及原11行保持；新增69行均pending，生产/上游/vcpkg零变更。
独立VS x64重新配置与编译，core 64 passed/0 failed/0 skipped；probe 4 passed/2 failed/0 skipped（含原EventTest 2 passed），公开ObjectId与非空FNV失败为可信审计反例。独立整数oracle4向量一致。
详见report.json、原生JSON/XML、命令日志；不授全T01/T02，不要求本批修产品。
