# WP-15批次B／REUSE-T38：io 工具收口——JSON/CSV 双通道 UTF-8 DFA 与 mapSystemError 合流为单元私有单实现

实施日期：2026-10-25；分支 redesign-main；起始 SHA `10fd0a130404…`（WP-15-T37 收口提交，完整 40 位见 git log）。
任务契约：[REUSE-T38](../../../tasks/REUSE-T38.json)。状态＝实施完成待独验（不自签）。

## 改动内容（产品树三文件）

1. `io/src/IoUtf8.hpp`（新增）：io 单元私有共享头（src/，不入 include/ 公共头根——R-2 红线）——`inline kUtf8Dfa` 表、`kUtf8Reject`、`utf8Step` 单步推进、`strictUtf8Check` 全文校验、`mapSystemError` 四分类映射。
2. `io/src/Json.cpp`：删除自持第二份 DFA 表/strictUtf8Check/mapSystemError（原 95-190 行段），改引共享头；原"暂不合流"预留（合流前 Json.cpp:107-109 注释）由本批收口。
3. `io/src/Csv.cpp`：删除自持 DFA 表/kUtf8Reject 与 mapSystemError 副本，改引共享头；`DecodedStream::nextUtf8`/`nextUtf16ToUtf8` 函数体零改动（流式推进保持原内联转移式，仅 DFA 表/kUtf8Reject 引用换共享头 inline 表；`utf8Step` 助手当前仅 `strictUtf8Check` 使用——独立验收勘误后如实更正，2026-10-25）。

## 合流等价 oracle

- `dfa-table-merged-equal.txt`：删除前从两 TU 提取原件比对——kUtf8Dfa 表体归一化后逐字节一致（1283 字符）；mapSystemError 函数体逐行一致（624 字符）。
- 共享头转写核对：IoUtf8.hpp 表体与 `git show 10fd0a130` 下 Json.cpp/Csv.cpp 原件三方逐字节一致（sha256 治理口径均＝`38d42d66a535b9ad76ee360e9eb34a6bb1df51c7707be7d4037d9677c91df9c7`）。
- 行为回归：基线（合流前构建）io_test 132＋contract_test 9 全绿；合流后复跑同计数 141/0/0，testcase 名单逐一核对一致（`baseline-*.xml`/`postfix-*.xml` 及 ird-test-report 原件）。
- 既有黄金向量全部保持：超长编码/代理区/U+10FFFF/BOM/悬空续字节等编码拒绝用例含于 io_test 132 项（CsvEncoding/JsonEncoding 族）。

## 差异语义保留清单（逐项核对）

- JSON 仅 UTF-8：UTF-16 BOM 在 parseBytes 入口剥离判定处拒绝（Json.cpp 内，未移动）；无 BOM 字节流经 strictUtf8Check 全文判定——语义与合流前一致。
- CSV 接受 UTF-16LE/BE（BOM 判定解码）、UTF-8 形态流式逐字节透传（暂存整组放行）、残缺/非法＝-2 稳定拒绝——nextUtf8/nextUtf16ToUtf8 函数体零改动（仅表引用换共享头）。
- mapSystemError 四分类与诊断码（NOT-FOUND/READONLY/LOCK-CONFLICT/PACK-DISK-FULL/保守 ACCESS-DENIED）逐行相同——合流后单实现。
- 受限 JSON parser 主体保留：上游默认 XML DOM 不适配受限 JSON、公开 ptree 丢类型（B8 检索结论）——按矩阵既有证据逐符号解决为 business-specific，非整文件豁免。

## ird_gates

`wp15-t38-ird-gates.log`：引擎直跑 exit 0，R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中。

## 登记给后续包（本批零触碰）

- testkit/JsonLite.cpp：其 UTF-8 合法性由装载层保证，自身只做 \uXXXX 转义与代理对合成（非第三份 DFA 校验副本）；登记 WP-19 时按"转义/代理对语义与 io appendUtf8 的潜在重叠"表述。
- ird_add_gtest 20 单元同形宏共享（TEST-HARNESS）：本批只登记处置，实施留 WP-19/WP-20 单元协调批。

## 矩阵与凭证同步

- functions/io.json：`io.strictUtf8Check` sources 改绑 IoUtf8.hpp 区段并解决；`io.Json.mapSystemError` 更名 `io.IoUtf8.mapSystemError` 改绑共享头区段并解决；`io.Csv.mapSystemError` 行删除（函数合流，凭证由本文件与 changes 承载）；nextUtf8/nextUtf16ToUtf8/Parser 六行/canonicalizeJson 区段随位移刷新并按既有 B8/B10 证据解决；新增 IoUtf8 声明区行、DFA 表区行与 Json.cpp/Csv.cpp 全行铺面声明区行（触碰文件全行覆盖）。
- 逐文件凭证：[reuse/changes/REUSE-T38.json](../../../reuse/changes/REUSE-T38.json)（Digest 口径；IoUtf8.hpp＝new，Json.cpp/Csv.cpp＝business）。

## 未决事项

独立验收未做（不自签）。
