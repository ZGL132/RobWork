# B28：身份、来源、诊断值契约与事件原语

审计Identity.cpp、Provenance.cpp（含SourcedValue模板）、Evaluation.cpp、DiagData.cpp、Events.cpp及相关公共头、原core测试和CMake；逐功能区段见core矩阵。本批不把core整单元认定业务专属，不修改生产源码、不授T02准入。

## 身份哈希反例

units/core.md §4.1明确std::hash使用FNV-1a 128。Identity.cpp的addShifted/addMulSmall直接改同一hi/lo，后续项读取被前项修改的状态，且不是从零累加原状态乘素数。独立Python任意精度整数按异或后乘(2^88+2^8+0x3b)、模2^128冻结期望；常数直接源自既有契约与实现注释，不从产品输出生成。空向量通过，三个非空固定向量失败，ObjectId公开hash失败；新探测4项2passed2failed。实际影响含六类Id及Digest.hpp中ContentVersion/ConfigStamp哈希调用（后两者仅静态核对，未在新探测执行）；不据此声称内容SHA-256或持久化字节错误。原core目标64passed0failed0skipped，证明旧回归漏测，不能用原绿覆盖反例。

后续先查成熟现有依赖是否可按冻结FNV契约最小适配，确定容器/持久化消费者及算法约定能否更改；无可用API则提交最小扩展/修正提案获批，不在T01直接重写128位乘法。身份文本解析、保留值和强类型不是上游StateStructure局部编号的同义功能；生成器已有std::mt19937_64契约，上游Random共享种子/状态语义不同，不能仅为了复用替换成全局随机源。

## 事件候选与权限

直接编译上游gtest/core/EventTest.cpp，原两个用例通过；新增上游Event适用性一项通过，验证订阅顺序、obj/id退订和重复退订。沿用既有GTest、原testkit报告器、导出目标，不复制上游测试或事件实现。Event.hpp内直接遍历可变listener列表，无锁/异步队列/RAII寿命保证；execution生产总线还要求投递线程、快照、退订与drain，因此仅列候选，不能称可直接替换。core当前只允许math依赖，不为候选Event扩大core构造权/依赖；可在未来execution宿主适配边界核对基础订阅表复用。

ReferenceEventBus标注测试专用，但实现位于core/src/Events.cpp并与DTO同入core STATIC；可称实现进入产品库归档，尚不能断言最终exe保留或实际生产使用，须符号及消费者核查。它是语义夹具，不能无条件删除独立测试证据。测试内回调退订的快照行为与上游直接列表遍历有差异，未执行并发/重入/完整交付总线。

## 值契约承重边界

SourcedValue四态、ValueProvenance与TaskOutcome等词表是core公共契约，DiagnosticRecord是值载体；诊断稳定码/文案权威仍为diagnostics，结果信封/证据仍归evidence，不向业务下放另造权。methodTag规则表和hpp写小写字符集，但同表例子MDL-05/hollow-cylinder含大写，实现接受大写，属于文档矛盾待确认；本批不擅自改拒绝规则。模板的noexcept泛型拷贝/比较约束及公共聚合可变性尚未做异常类型反例，保持pending。

## 实测与限制

最初exe退出0xC0000135无测试输出；dumpbin列RobWorkSim DLL，PATH补其bin/libs后执行。首次顶层组合构建probe成功但EXCLUDE_FROM_ALL的原core测试项目在顶层solution不可达；随后直接MSBuild生成的原ird-core/sdurws_ird_core_test.vcxproj成功，未改产品CMake。最终probe六项4passed2failed0skipped（含上游原两项），原core64passed0failed0skipped；XML及原生报告均保留。不称产品已修复，不称独立验收通过，不运行远程同步。
