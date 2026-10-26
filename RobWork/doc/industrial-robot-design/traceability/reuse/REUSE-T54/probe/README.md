# REUSE-T54 Boost.JSON 行为探针（2026-10-27）

探针目的：REUSE-T54 迁移实施前钉住 Boost.JSON 1.92（vcpkg x64-windows 动态库）对
迁移决策敏感的解析行为——重复键/结构空白/浮点字面量/越界整数/孤立代理/\u0000/深度/
serialize 字节面。探针为独立编译的最小程序（不入产品树），原件在本目录，逐案输出见
`probe-run-output.log`。

## 结论（与迁移设计的对应）

| 探针 | 输入 | 结果 | 对迁移决策的含义 |
| --- | --- | --- | --- |
| p4 | `{"a":1,"a":2}` | OK，obj.size=1（坍缩保留后值） | 重复键拒绝必须由本包 canonical 词法门自持（boost 不可依赖） |
| p5 | `{ "a" : 1 }` | OK（接受结构空白） | 结构空白拒绝由词法门自持 |
| p6 | `10000.5`/`1e4` | OK kind=double | 浮点/指数拒绝由词法门自持 |
| p6 | `true`/`null` | OK kind=bool/null | 字面量拒绝由词法门自持 |
| p6 | `007` | ERR syntax error | 前导零 boost 自拒（双重防线） |
| p6 | `18446744073709551615` | OK kind=uint64 | uint64 上界 round-trip 用例可过 |
| p6 | `18446744073709551616`/`-9223372036854775809` | OK kind=double | 越界整数落 double kind→字段层拒绝（码不变） |
| p6 | `\ud800`/`\udc00` | ERR（syntax/illegal leading surrogate） | 孤立代理 boost 自拒（双重防线） |
| p6 | `\ud83d\ude00` | OK | 代理对合成 |
| p6 | `{"a":"x\x01y"}`（裸控制字符） | ERR syntax error | 裸控制字符 boost 自拒 |
| p6 | 裸 UTF-8 `\xe6\x9c\xba` | OK | 非 ASCII 原始字节拒绝由词法门自持 |
| p6/p13 | `\/`、`\u0041`（大写 hex） | OK | 与被删旧引擎 parseEscape/parseHex4 同语义（解析端容忍） |
| p6 | BOM 前缀 | ERR | 与旧非 ASCII 拒绝同向 |
| p6 | 64 层嵌套 / 65 层（max_depth=64） | OK / ERR too deep | 与旧 kMaxDepth=64 边界一致 |
| p6 | 尾随内容 / 空输入 | ERR extra data / incomplete | 与旧"恰好耗尽"语义一致 |
| p7 | `\u0000` | string bytes 78 00 79 | NUL 保留（与旧 appendUtf8 同语义） |
| p7/p13 | `serialize()` | 非 ASCII 原生 UTF-8 直出、控制字符 `\u0001` 小写 | dump 侧不能换 boost 序列化器（§4.8 纯 ASCII 小写 \uXXXX 字节面）——自持编码器决策证据 |
| p14 | 链接构建后 sdurws_ird_project.lib 实测 | 白空格文档被产品库拒绝（code=StoreCorrupt） | 产品通道端到端行为复核 |

boost 异常载体＝`boost::system::system_error`（parse 抛出；未捕获逃逸 main 在 UCRT
下表现为 abort 0xC0000409——产品侧 parse 全程转 StoreError，不受影响）。

## 事故记录（探针基建，与库行为无关）

首版探针可执行文件曾以 0xC0000409 退出且无输出。根因有二：
1. bash heredoc 经工具链 JSON 转义把源码中的 `\uXXXX` 序列写成原始控制字节
   （p10/p12 复现实证：源码意图 `\u0001` 文本、实际落盘 0x01 裸字节）——修复＝改用
   文件工具直写（本目录全部源件均此路径）；
2. 尾部 serialize 段未包 try/catch 的未捕获异常逃逸 main（abort），叠加 stdout 全缓冲
   使崩溃前输出丢失——修复＝`setvbuf` 无缓冲＋逐案 try/catch（p6 起）。

修复后逐案全部跑通（probe-run-output.log RC=0），无一例为库自身缺陷。
