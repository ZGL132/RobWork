# 证据保存与检索

## 1. 原始证据

每个任务保留执行命令、退出码/结果与原始日志。gtest 输出沿用 XML＋per-exe ird-test-report.json；脚本使用真实日志及其报告，不伪造 gtest。结论区分 passed/skipped/failed 与未执行。

## 2. 索引是可选快照

普通提交保存当前任务证据即可，不再要求「写证据→git add→重建全库索引→校验」联动。evidence-index.json 允许滞后，不能用其存在/计数代替当前任务验收。

专项索引审计：将需要保存的证据纳入 Git → 运行 index-evidence.ps1 → validate-docs.ps1 -DeepEvidence。显式深查仍检查未跟踪项、路径集合、字节数及摘要；失败不得标通过。

生成物禁止手工伪造，文本摘要按 text-crlf-to-lf-v1 规范化；索引内 current/superseded/historical/unverified 为检索分类，不是任务或流水线状态的权威。

## 3. 历史保留

原始历史证据只增不改。移动/归档前检索引用并更新指向，不能随精简批量删除历史。索引是可再生成的缓存，可用脚本重建。流水线状态本体仍为 pipeline/state.json；普通人工任务不读写它。
