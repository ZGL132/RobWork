# B31：单位、数量模板及逐元素C4比较

本批审读Units.cpp/.hpp、Compare.cpp/.hpp、原UnitsTest/CompareTest与core目标的相关注册。覆盖注册表、隐藏helper、Quantity模板/14实例、inline与Q条件编译；矩阵逐段登记，不关闭整文件，不修改产品。

## 更小复用边界

角度三个token的比例与rw::math::Deg2Rad实测精确一致，可直接用已允许math常量。新核rw::core::DOMCoreBasisTypes::getUnit（原core目标，不是loader）有13个产品共同token同值，另支持产品当前未注册inch；kg/N*m抛rw::core::Exception。仅返回double比例、无量纲，也无法区分m与rad（均1），故不能整体替代强类型/冻结词表/错误双轨。UTIL-UNITS保留原loader候选、补core候选，纠正“只有重型loader可用”的判断；core当前仅允许math，不为候选自行扩大依赖。未来IO已有DOM上下文可按权限复核，不能让core为了查常量新增DOM门面。

Math::isNaN实测拒NaN但不拒Inf，不能冒充完整有限检测；现有Quantity/Tolerance/closeWithin自持有限表达式可优先标准库isfinite，不新建检测服务。Q::operator==是逐项精确等值，实测near Q不等但C4可通过；范数型/对称近似不能代替相对reference的逐元素C4。C7数值权威仍是需求机械转写，不能换成上游任意epsilon。

## 真实反例F-966

value=DBL_MAX、reference=-DBL_MAX、relative=1.5、absolute=0均有限且Tolerance::make接受；C4精确有理数约去DBL_MAX为2<=1.5，必须false。当前diff和limit都溢出为Inf，Inf<=Inf使标量返回true；vector和真实Q公共重载同样误绿。三个新增断言均真实失败，oracle.py用Python Fraction独立冻结结论，不用long double（MSVC与double同宽）或第二套浮点算法互证。这里只证明该合法极值族偏差，不声称普通工程阈值全部错误。T01不修算法；后续先核成熟能力与原C4契约，必要最小修正须提案确认，不用norm近似改变需求。

## 测试复用与尚未验收

原UnitsTest七例及CompareTest八例直接编译原文件（含原Q用例），15passed；新七例4passed3failed；总22跑19passed3failed0skipped。配置显式在审计构建将真实RW::sdurw_math供给core以启用Q；测试有#error阻止未编译Q时静默变绿。没有修改原core CMake，原B28/B30无关构建与证据未变。沿用原TestMainReport/testkit，不复制测试框架。

次正规数denorm_min从mm到mm经各一次乘除下溢0是观察记录，SI纯构造保留原值；当前单位契约明定各一次乘除、不承诺位往返，本批不私自更改舍入政策或把characterization通过称为正确性证明。UnitToken默认无效而kind/symbol依赖已注册前置；Quantity::tryParse/displayValueIn应继续核调用方前置，不执行未定义行为来伪造拒绝证据。Tolerance公共成员可绕过make的约束尚未判定，不据此自行收窄接口。

跨平台浮点/极端长序列/全部消费者阈值、上游完整套件与产品打包未执行；全部新行pending，F-966开放，独立验收只可授本批局部结论。
