# REUSE-T01：Xacro与通道协议内部审计

本批补COV-06/07，逐定义审读XacroExpand.cpp与ChannelProtocol.cpp；矩阵精确绑定所有列明函数及小端六函数分组，不把整个modeling/execution文件标业务专属。project/reporting codec及测试工具/脚本其余覆盖仍待后续，不构成全T01退出。产品与上游未改。

## Xacro复用与边界

XacroExpandService::expand已真实调用pugi::xml_document::load_buffer，消费节点/属性/offset_debug，不是自写XML解析器。文件为一次调用栈上Engine持文档/文本/节点与作用域，零磁盘访问；输入必须来自io已验证字节与依赖树，provenance使用原摘要/路径/substitutions，参数清单失败也保留，成功才发expandedBytes。pugixml是原有vcpkg依赖，未增加引入渠道。

LineIndex用标准upper_bound把偏移转行列，Import.cpp另有同类定位逻辑，可在T05审查单元内最小共用，不跨owner建立新源管理平台；已有RelKeyNormalizer私有头统一折叠键，不能再加第二normalize。flushDiags按源键/行列/序排；Engine诊断用既有码值/IO token，但cause/action中中文文本仍需按diagnostics文案权威复核，不能因为码来自注册表就认定文案合规。错误token表不是稳定诊断码注册表，禁止另造权威。

escapeXmlText/escapeXmlAttribute仍手写转义，emitElement还手拼XML标签。上游DOMParser/BoostXMLParser真实可load/save；既有pugixml有xml_node::print/xml_document::save和xml_writer，可优先调查输出薄适配，不能声称库无序列化。DOM通用接口并未直接承载pugixml offset_debug源定位；先不替掉已复用解析器，不按每字符建立adapter。输出空元素、实体转义、引号、属性序、文本/CDATA归并、声明与产物确定字节/摘要需回归；输出树可能额外占内存，不能为去掉两个短函数无依据改成双DOM平台。本批只读安装公开pugixml头，未执行其输出适用性探测。

substituteText是受控${标识符}单遍替换，替换值不二次展开，$(ROS...)、表达式、条件/insert_block拒绝；resolveSymbol先substitutions首匹配再层栈内后写覆盖；macro按定义序登记、按签名序绑定默认/必需/多参，include只拼目标根的子元素，递归预算只计宏/include，普通XML层级不计。不能用通用replace/完整ROS解释器放宽拒绝边界，更不能调用shell或网络处理Xacro。全范围检索无对应受控Xacro API只形成候选排除证据，未授权新解释器或永久豁免；T05保留业务子集编排前仍需核需求与最小共享点。

detectIncludeCycle用已验证树Include边做防御DFS，且emitElement有拼接栈防线；并不重新打开文件。expand六步输入/缺失/环/解析/展开/装配均审读，源快照生命周期闭合。源码总字节护栏只在普通元素收尾检查，注释“逐点有界”不能据此视为峰值内存已证；本批未实测预算极值，不改动。已有XacroExpandTest包含宏/include/拒代码/定位/深度/无草稿/确定性/同URDF边界，尚未重跑；非生产正向证据。

## 通道字节与协议

put/get U16/U32/U64六个小端函数与put/get Str/Bytes、digestToHex分别审读。BINArchive存在，不能笼统称自写二进制编码无上游能力：标量writeValue直接写宿主表示，字符串doWrite以stream << s写长度、doRead以>>读，vector/标量路径也未提供本协议长度上限和固定LE语义。IOUtil::isLittleEndian只是宿主探测，不是跨平台LE编码器。因此不能整体替掉IRDCHN/1或复制BINArchive内核。本批保留候选，T05再核既有同渠道低层endian原语是否可薄用；不新建IPC/serialization平台，不改变冻结线路版本/字节。摘要已委托core ContentDigester，十六进制文本映射不是第二SHA核，但与其他消费点重复，统一时应沿core既有输出边界，不私造摘要权威。

encode/decodeIdentityBlock消费core五元组规范文本；encodeFrame发magic/version/type/flags/seq/length/identity/payload。FrameAssembler增量收帧，错误闩、长度/身份守卫与消费过半压缩；FrameSequencer首帧建立基线，乱序窗口/重复丢弃/缺口时钟/连续吐出；fragmentMessage只允许DispatchRequest/FinalOutput，空消息仍一片，MessageReassembler校验分片头/片序/数量/总长并用末片身份出帧。这些业务协议与通用archive/线程队列不同，不用新的上游模型表示替换传输身份；worker继续独立进程，零宿主WorkCell指针或Widgets。

Hello/Ack、Dispatch、Progress、Result/Checkpoint、Final、Error全部codec定义审读；Progress复用ProgressReport::make，身份调用core fromCanonical，nullopt用于对端非法数据，manifestDigestHex/computeContentIdentity/contentIdentityMatches消费唯一摘要点。通道Error仅传码/消息，稳定文本来源仍由diagnostics核对，不能以编码器自造诊断。真实消费者在现有Win32通道与worker源中已检索，完整生产消息往返仍须T05运行，不把ChannelProtocolTest等同宿主接线。

额外静态待核：attempt固定24字节但解码拒绝填满无NUL（最长att-加20位可能受影响）；分片部分状态只保存total/count/index/bytes，跨片type/identity一致性由外层保证与否待核；seq极值/缓冲峰值/fragmentSize极值需边界验收。本批没有复现实跑，不报已复现缺陷、不顺手修算法。所有关系pending，既有库存/提案不变，C++/GUI/性能未运行。
