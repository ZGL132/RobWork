# REUSE-T10 独立验收：FAIL（近反平行向量数值回归）

验收冻结实现SHA：e453006ea6fd33f2b325a63d0c88d3db88110748；输入路径修正SHA：a467cc731f875bda94dbb4c05276431cb9979e5a。后者未改变EAA代码、补丁、验证脚本或二进制，本记录沿用前者的真实原件。共享工作树其后提交不自动纳入本次结论。验收者仅审查/运行测试，没有修复、提交、远程操作或派发其他代理。

## 阻断缺陷 P1

位置：`RobWork/RobWork/src/rw/math/EAA.cpp:107-124`。双向量构造只在`cross.norm2()==0`时选取确定性反平行轴，否则直接将cross归一化。归一化输入仍有浮点舍入误差；反平行或极近反平行时，cross相消残差可能既很小又不垂直于输入。将其归一化后乘pi，放大为错误旋转轴，未保持旧版的反平行边界行为。

反例使用原API，无随机输入、无非单位向量：

```cpp
a = normalize(Vector3D<>(.3,.4,.5));
b = normalize(-a);
actual = EAA<>(a,b).toRotation3D()*a;
```

a=(0.42426406871192845,0.56568542494923801,0.70710678118654746)，b=(-0.42426406871192851,-0.56568542494923812,-0.70710678118654757)。双精度norm分别0.99999999999999989、1；dot=-1，cross=(0,0,-2.7755575615628914e-17)。补丁错误选择-z旋转轴，保持a.z为正，最大元素误差1.4142135623730949；旧实现误差1.6653345369377348e-16。

第二反例令p=normalize(cross(a,(1,0,0)))，b=normalize(-cos(eps)*a+sin(eps)*p)，eps=1e-12：补丁误差2.1327409066962399e-5，旧版7.8093087552133511e-13。两者均明显倒退，不能以小角精度改进或用户接受微基准开销放行。

`boundary.log/xml`是真实3项测试结果：2通过、1失败、0跳过；失败项含4个epsilon的参数循环，不将循环次数冒充独立测试数。eps=1e-8补丁改善至0，而旧版约7.81e-9；eps=1e-5补丁约1.42e-12、旧版约8.76e-12。这两个历史精度限制与上述灾难性回归分开陈述，不宣称旧版所有边界均通过。

## 其他独立验证

原始测试证据在工作树`.cache/t10-independent/`。上游EAA/RPY/Quaternion16通过、CONFIG消费者16通过、modeling474通过、modeling契约17通过、runtime301通过+1原有子进程worker占位跳过，均0失败；每个可执行文件单独以绝对路径运行，使用VS2022 x64环境及实际补丁math DLL，保存各自XML、日志及产品报告。不能用这些通过结果覆盖新增反例失败。

精确补丁PS7、PS5.1各13通过、0失败/跳过；反向patch检查exit0；冻结树单独调用真实`CheckApprovedUpstreamPatches`exit0，仅授予EAA.cpp、EAATest.cpp和0004资产。静态检查批准/提案/patch摘要、基线blob、当前源码摘要及Git full-index diff绑定；未发现确定性越权路径，批准身份仍依赖明确用户授权和独立审查。

公共EAA.hpp未改变，API/ABI形状及axis()原1e-6阈值保持；新增独立LegacyAxisBoundary和SignedSmallAngleAnalyticAxes两项通过。没有新增模型、状态、缓存、线程行为或产品旋转核心。准确性维度FAIL；架构和改动简洁性未发现额外阻断。性能旧中位0.027129、新0.043428微秒（约1.60倍），原样本存在波动；实施时未有预算不能自行接受。主会话后续通知所有者已明确接受此项微基准开销，实际授权证据由主会话另存，该接受不能覆盖准确性FAIL或视为产品生产路径预算。

原e453冻结全量validator真实exit1：`REUSE-MISSING : RobWork/RobWork/src/rw/common/BinArchive.hpp`，Windows路径大小写掩盖Git对象名BINArchive.hpp。该问题由主会话在a467更正，正在以a467冻结树重验；本记录不预判PASS。

## 反例对照与制品来源

Diagnostic.cpp打印实际模块路径。补丁DLL：本工作树`.cache/t07-integrated/RobWork/bin/Release/sdurw_math.dll`，SHA256=6ae5d025ea7d9a2497a4816a1bc7ae2cdf2d972260b972c7fb344c3affefd445。旧对照DLL：D盘`build/RobWork/bin/Release/sdurw_math.dll`，SHA256=964c44d6dc84f48e8783ffdda5aafd1e057325e582fa6a987ecf99f88029793c；D盘EAA.cpp的Git blob=802c7debc448bd1148d623f58a34a5b19ee22088，与冻结提交父版本相同。旧DLL仅用于独立旧行为对照，未冒充补丁制品或重新编译的冻结基线。

Boundary.cpp和Diagnostic.cpp通过真实RobWork CONFIG导出目标`RW::sdurw_math`链接；EIGEN_DONT_ALIGN_STATICALLY=1、C++17、Release、/utf-8与上游一致。首次新增CONFIG夹具因继承vcpkg包装器解析上游C:\local失败；按原CONFIG消费者相同选项VCPKG_OVERRIDE_FIND_PACKAGE_NAME=vcpkg_find_package配置后成功，未改上游或vcpkg。日志保留真实失败与后续成功于.cache，提交证据保留成功构建/配置和真实测试FAIL。

结论：本能力独立验收FAIL。必须修正反平行轴稳定性并对新冻结实现重验后再决定放行；不关闭任何迁移债务，不合入，不操作远程。
