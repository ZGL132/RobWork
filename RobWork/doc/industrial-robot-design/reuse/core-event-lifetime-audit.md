# B32：事件生产装配、句柄生命周期与泛型异常边界

本批补充B28未决的ReferenceEventBus生产消费者和SourcedValue模板异常反例，审读Events/Provenance公共inline、DiagData比较及Errors继承声明。Core.cpp只有注释占位，无新核心。逐功能仍pending，不关闭整文件，不修改产品。

## F-967：生产引用与RAII缺口

UiPlugin.cpp:749在IrdWorkbenchHostPlugin::initialize创建ReferenceEventBus，:755给StoreFactoryPortAdapter，:845/:925分别订阅修订sink，:1022注入内容装配；UiPlugin.hpp:614持有该具体类，:617声称析构即退订。ui/CMakeLists.txt:919/:920注册生产MODULE并编译同源UiPlugin.cpp，:987链接产品依赖。这证明当前冻结源码的生产装配链，补齐B28只能确认core STATIC归档的范围；没有运行GUI，不声称已证明当前最终DLL链接符号或实际用户会话。

core Events.hpp明文“非生产组件”，IDomainEventBus::subscribe及units/core.md:764要求RAII。实际匿名ReferenceSubscription无析构退订，最小探测在bus/sink都存活时让句柄出作用域：subscriberCount仍1，publish仍回调一次，两个期望均失败；不执行悬垂访问。原EventsTest七例全过，其中FIFO用例临时丢弃句柄后仍期待投递，今后恢复RAII也必须同步纠正这个漏测契约，而非删除失败证据或放宽RAII。

## 先复用既有实现，核清线程和订阅语义

原execution/src/EventBus.cpp直接编入审计目标，真实DomainEventBusImpl析构句柄后订阅0且不再回调，通过RAII候选探测。另两例实证：同sink重复订阅时Reference退一个句柄会删全部，execution只删一个；Reference同步回调在发布线程，execution回调在专用线程。后者头注释称与Reference同语义不准确，不能直接换类型洗平。UI当前QTimer队列投递前仍读取m_lastStoreAdapter等非原子宿主状态，不能因已用了QTimer就认定跨线程安全。后续先复用现有总线契约与唯一实现，核UI线程转接、sink/句柄/总线销毁序、退订与在投回调交错及项目切换；禁止再造第三总线或让core变为生产实现owner。

上游rw::core::Event的头、原两用例、示例/共享文档及CMake已实搜；它只提供同步listener表，无RAII/线程/队列/drain。上游原两例在本目标直接编译通过，沿现有GTest/testkit/TestMainReport，不复制框架。CORE-EVENT保持candidate，core目前只允许math，不能为查候选自行扩大依赖。execution可按既有权限在后续审查其内部原语适配，但不能抹掉现有线程与退订保证。

## 泛型约束的观察与承重边界

SourcedValue声明T仅需默认构造及可拷贝，tryValue() noexcept实际复制T，operator== noexcept实际调用T比较。两项死亡测试在子进程安装terminate处理器并固定退出73，确认合法可拷贝探针的抛出复制/比较终止进程；通过只代表观察重现，不表示该行为符合全部契约，也不声称主要double/Quantity消费者已经崩溃。后续先核调用类型及原noexcept契约，裁定收窄T约束或错误轨；不私改返回类型/异常ABI、不新建值框架。methodTag大小写在units/core.md v0.5已有偏差登记待所有者定格，不再当作无记录的全新矛盾。

core的四态/词表与诊断纯值仍属承重契约，稳定码和文案权威归diagnostics，信封/判定归evidence；仅因字段比较与上游容器都用operator==不认定重复核心。Errors.hpp仅继承std::runtime_error构造，不需要新异常服务。

## 真实运行和限制

23跑22passed1failed0skipped，原17例全通过，新6例5passed1failed；唯一失败保持F-967开放。配置首次使用vcpkg工具链遇既有导出包HINTS C:\local转义失败；改为与B30/B31相同的已安装prefix供给（不是产品集成构建），随后补齐当前目录GTest导入作用域及报告入口math/Eigen依赖，所有失败日志保留。最终VS x64配置/构建成功，绝对exe一次运行，无GUI。未安装新依赖、未改产品/upstream/vcpkg；不授全T01/T02或算法替换。
