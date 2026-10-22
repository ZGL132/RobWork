# B30：摘要核心、规范文本与公开散列

审计 Digest.cpp/.hpp（含宏、匿名helper、填充lambda、inline比较与std::hash），反查消费者、原DigestTest及core构建。逐段摘要/真实符号以core矩阵为准；本批没有生产改动，没有关闭整文件，不授T02。

## 复用事实与边界

search.json保留两层检索：源码/测试定向与RW/RWSim完整目录的SHA/crypto检索，再核文件名、StringUtil和gtest/CMake。未找到对应SHA-256服务是当前范围检索事实，不宣称未知实现绝不存在。StringUtil的toUInt等面向数值，不等价cv-/cid-、64位小写hex及失败双轨；这些产品值契约应保留，不能为复用丢掉强类型。

直接复用原core/test/DigestTest.cpp七例和原TestMainReport/testkit，沿用父探测的上游导出目标与GTest；新增独立目标仅审计CNG，无生产依赖变化，无算法副本。首次配置因子目录GTest imported目标作用域失败，保留原日志；根目录使用同一个find_package后成功构建。CNG为本机SDK参照，不伪装成RobWork能力，不向catalog添加虚构上游路径。

## 实测

最终13跑：11passed、2failed、0skipped。原七例通过；新增CNG已知空/abc、14种长度（0/1/55/56/63/64/65/119/120/127/128/129/4097/1000000）下产品整块与17字节分块、CNG DuplicateHash与产品值复制分叉、规范文本/拒绝/保留值四例通过。CNG句柄只在测试局部持有并释放；复制测试不是批准生产句柄实现。两项公开ContentVersion/ContentIdentity hash分别失败，独立FNV整数oracle的32字节0..31折叠应为d84cf67ff3e90eee，实际3981071475404768780；补强已有F-965，不另建同因缺陷，不声称SHA-256错误。

oracle.py另用Python hashlib冻结边界摘要，可供独立验收第三方核对；C++探测不调用该脚本生成期望，不把产品输出回填向量。原core全64例沿用B28未变版本报告，本批只重编译并执行原摘要七例，不冒称重新执行全core。

## 后续最小适配

GAP-DIGEST-01仅是待批准提案。vcpkg有Boost.Hash2/OpenSSL port，但未检出当前安装的相应头/包；未安装、未联网拉取、未核其API，不称成熟候选已可链接。优先在同渠道验证跨平台成熟实现，保留ContentDigester/update/finalize及Digest256值契约，移除自持压缩/轮常量，而非新增通用crypto facade或双后端运行时。CNG当前仅作独立数值证据，不能单独满足跨平台及依赖红线。

消费者canonical序列化归各owner，core不接管；诊断/evidence权威不变。大于ULONG/超长消息、故障注入、并发/跨平台/性能、全部消费者键兼容性尚未验，保持pending。FNV修正必须另行核既有约定与消费者兼容性，不借SHA提案暗中更换散列算法。
