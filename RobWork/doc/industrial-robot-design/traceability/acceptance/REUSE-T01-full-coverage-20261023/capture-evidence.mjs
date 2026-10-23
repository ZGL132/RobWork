import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const out=path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Z]:)/,'$1'));const root=path.join(out,'repo');const p='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/';
const read=f=>fs.readFileSync(path.join(root,f),'utf8').replace(/^\uFEFF/,'').replace(/\r\n/g,'\n');
const hash=s=>crypto.createHash('sha256').update(s.replace(/\n*$/,'\n')).digest('hex');
const write=(f,x)=>fs.writeFileSync(path.join(out,f),typeof x==='string'?x:JSON.stringify(x,null,2)+'\n');
const idx=JSON.parse(fs.readFileSync(path.join(out,'source-reference-index.json'),'utf8'));const inv=JSON.parse(fs.readFileSync(path.join(out,'inventory-analysis.json'),'utf8'));
// 只提取冻结原文与反查引用，不写矩阵或产品。行号由定义所在原文确定。
const spec=[
 ['FCOV-01','project','事务提交与崩溃恢复','project/src/TxEngine.cpp',287,'sdurws::ird::project::tx::TxEngine::commit','校验计划后发布不可变对象、切换HEAD并发送事件；恢复扫描区分闭包内损坏与闭包外残留。仅ObjectStore发布/读取两条不能覆盖事务编排。'],
 ['FCOV-02','project','命令槽、确认放行与编译/提交','project/src/CommandServiceImpl.cpp',169,'sdurws::ird::project::CommandServiceImpl::submit','互斥执行槽和在途票据，S1形式校验、S2基线，executePlan执行确认/编译/事务并归类失败。Codec登记不覆盖该状态与写权威行为。'],
 ['FCOV-03','project','权威分支历史查询','project/src/QueryPortImpl.cpp',347,'sdurws::ird::project::QueryPortImpl::branchHistory','在writer锁内取权威tip，锁外沿不可变parent链组装RevisionView，悬挂parent拒绝；不是纯字段访问器。'],
 ['FCOV-04','project','归档会话终局与幂等发布','project/src/ArchiveServiceImpl.cpp',853,'sdurws::ird::project::ArchiveServiceImpl::finalize','复核五元组/写权威，重算manifest摘要，互斥仲裁finalize与abandon，幂等/冲突与文件核验。'],
 ['FCOV-05','project','Windows持久性/可见性原语','project/src/win32/AtomicFile.cpp',226,'sdurws::ird::project::win32::AtomicFile::writeThrough','RAII句柄、分块写、flush和close失败路径决定是否允许发布；publishNew/replaceExisting承载不同原子语义，不等于普通文件工具。'],
 ['FCOV-06','policy','策略解析/阈值及规则归一化','policy/src/PolicyParsing.cpp',489,'sdurws::ird::policy::(anonymous namespace)::runParsePipeline','schema拒绝、角色词表/对象解析、阈值合法性、默认供给、规则规范化与冲突判定；已有ByteReader/Writer条目不是此语义。'],
 ['FCOV-07','policy','关节限位与近限位判定','policy/src/JointLimits.cpp',266,'sdurws::ird::policy::JointLimitEvaluator::evaluate','有限性/名称检查、规范排序、有限/连续关节分支，有符号margin与比例、Should/Must和不适用记录；直接影响可行性判定。'],
 ['FCOV-08','policy','碰撞作用域展开与必检保护','policy/src/CollisionEvaluator.cpp',508,'sdurws::ird::policy::(anonymous namespace)::expandRulesAndCheckConflicts','将role/group/object规则展开成对象对，拒绝必检与排除交集；同文件会话构造还决定域/相邻过滤/必检优先，不由CollisionQuery::evaluate已登记区段覆盖。'],
 ['FCOV-09','execution','worker真实进程入口及隔离生命周期','execution/worker/main.cpp',613,'::wWinMain','命令读/心跳线程、Hello握手、分帧派发、物化摘要重算、临时目录及线程收尾；当前runScript为替身评估器，也需审进程/文件边界，不能误称已机器人计算迁移。'],
 ['FCOV-10','execution','结果归档接纳事务与失权重试','execution/src/Admission.cpp',581,'sdurws::ird::execution::ResultAdmission::runArchiveTransaction','复用预留会话、begin失权重取并重绑写面、有限退避、writeBatch/finalize与失败阶段记账；ChannelProtocol编解码不覆盖归档状态。'],
 ['FCOV-11','execution','取消/超时进程树强杀序列','execution/src/Controller.cpp',686,'sdurws::ird::execution::TaskController::forceKillSequence','一次终止保护、运行超时/心跳失联区分、因果诊断顺序与Canceling到Failed转换；同文件poll/requestCancel等未由scheduler submit审计替代。'],
 ['FCOV-12','diagnostics','一次性确认令牌与绑定四元组复核','diagnostics/src/Confirmable.cpp',498,'sdurws::ird::diagnostics::ConfirmableService::submitConfirmation','互斥状态迁移、惰性过期、callbackToken一次性检查、binding重验和失效/确认凭据写入，是身份/放行边界，不是文案DTO。'],
 ['FCOV-13','diagnostics','日志脱敏与失败时封闭降级','diagnostics/src/Redaction.cpp',580,'sdurws::ird::diagnostics::RedactionService::redact','实际执行规则链、累计统计，任何内部失败返回降级文本而非原文；同文件路径清理/UTF8截断/凭据规则需要语义审查，LoggingPipeline登记不能涵盖。'],
 ['FCOV-14','testkit','黄金数据装载与完整性依据','testkit/src/Dataset.cpp',197,'sdurws::ird::testkit::GoldenDataset::load','manifest/schema/版本/单位/容差/edgeCases与完整性清单校验，逐文件size和SHA256检查；决定黄金对照是否可信，不能靠JsonLite骨架审计代替。'],
 ['FCOV-15','testkit','头内集合等价与最大二分匹配','testkit/include/sdurws/ird/testkit/SetCheck.hpp',163,'sdurws::ird::testkit::checkSetEquivalent<T>','身份重复、身份匹配/逐字段容差、Kuhn增广路径数值一一匹配及稳定序检查；真实模板算法，不能作为简单DTO或三行断言样板跳过。'],
 ['FCOV-16','core','公共容差公式及头内合法性工厂','core/src/Compare.cpp',18,'sdurws::ird::core::closeWithin','C4非有限拒绝/相对绝对阈值与逐元素比较；关联Compare.hpp::Tolerance::make内联合法性判定和C7默认转写，决定大量测试/生产数值结论。'],
 ['FCOV-17','foundation-governance','构建依赖/权限门禁的实际解析与规则消费','cmake/ird_gates.cmake',152,'ird_classify_target','目标形态分类后，顶层IRD_EDGES扫描应用自边/测试/工具/白名单和Qt规则。20个ird_add_gtest只登记测试注册宏，不覆盖这些门禁或主CMake接线。'],
 ['FCOV-18','foundation-governance','独立FK黄金生成器','testdata/golden/rt-fk-equation/1.0.0/generate/make_fk_equation.mjs',53,'<ES module>::closedFormFk','自行累计平面3R姿态/关节原点/轴，写expected/fk.json；需审适用域/offset/单位/独立性及实际消费，不因生成脚本或独立oracle身份而豁免。'],
 ['FCOV-19','foundation-governance','独立动力学及功率积分黄金生成器','testdata/golden/dyn-two-link-analytic/1.0.0/generate/make_dyn_two_link_golden.mjs',77,'<ES module>::rneaReference','闭式二连杆质量/重力/科氏/惯性/摩擦分项；同模块trapezoid/mapCase计算功率能量、包络与身份，必须明确参考实现独立性及适用域。'],
 ['FCOV-20','foundation-governance','碰撞接触边界黄金生成器','testdata/golden/pol-collision-analytic/1.0.0/generate/make_pol_collision_analytic.mjs',41,'<ES module>::handDerived','手算轴对齐盒距离/接触/碰撞，含直立/倒挂与独立自检，决定测试oracle边界；只审RobWork生产碰撞适配不能覆盖脚本。']
];
function braceEnd(lines,start){
 // 保留字符位置/换行的词法近似；避开注释、普通字符串/字符字面量内括号。
 const joined=lines.slice(start-1).join('\n');let depth=0,found=false,quote=null,comment=null,escape=false;
 for(let i=0;i<joined.length;i++){const c=joined[i],n=joined[i+1];if(comment==='line'){if(c==='\n')comment=null;continue}if(comment==='block'){if(c==='*'&&n==='/'){comment=null;i++}continue}if(quote){if(escape){escape=false;continue}if(c==='\\'){escape=true;continue}if(c===quote)quote=null;continue}if(c==='/'&&n==='/'){comment='line';i++;continue}if(c==='/'&&n==='*'){comment='block';i++;continue}if(c==='"'||c==="'"){quote=c;continue}if(c==='{'){depth++;found=true}if(c==='}'&&found&&--depth===0)return start+joined.slice(0,i).split('\n').length-1;}throw new Error('No end at '+start);
}
const gaps=spec.map(([id,unit,title,rel,startLine,symbol,behavior])=>{
 const f=p+rel,txt=read(f),lines=txt.split('\n');let endLine=rel.endsWith('.cmake')?lines.findIndex((l,i)=>i>=startLine-1&&/^endfunction\(\)/.test(l))+1:braceEnd(lines,startLine);const rs=idx[f]??[];
 if(rs.length)throw new Error('Unexpected references '+f);
 const excerpt=lines.slice(startLine-1,endLine).map((l,i)=>`${i+startLine}: ${l}`).join('\n');write(id+'.frozen.txt',`${f}\n${symbol}\n${excerpt}\n`);
 return {id,priority:'coverage-blocker',unit,title,location:{path:f,qualifiedSymbol:symbol,startLine,endLine,fileSha256:hash(txt),sectionSha256:hash(lines.slice(startLine-1,endLine).join('\n'))},sourceReferenceCount:rs.length,auditedSectionCoveringCount:rs.filter(r=>r.kind==='auditedSection').length,inputOnly:inv.files.find(x=>x.path===f)?.inputOnly??false,behaviorRead:behavior,conclusion:'具体未登记行为；不判定其实现错误、上游缺能力或应删除',requiredAudit:'沿实际定义、调用链、上游候选及语义差异登记或有理由成组；复用/独立oracle/业务协议边界需证据，不在本报告代填矩阵',evidence:id+'.frozen.txt'};
});
write('coverage-gaps.json',{schemaVersion:'ird-coverage-gaps/1',reviewedCommit:inv.summary.frozenCommit,result:'NOT PASS',scope:'20组风险优先具体遗漏，下限而非全量函数清单',gaps});
const old=JSON.parse(read('RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T01-coverage-20261023/coverage.json'));
const cross=old.findings.map(f=>({id:f.id,title:f.title,anchorCount:f.sources.length,currentlyReferencedAnchorCount:f.sources.filter(s=>(idx[s.path]??[]).some(r=>r.startLine<=s.line&&r.endLine>=s.line)).length,anchors:f.sources.map(s=>({path:s.path,line:s.line,symbol:s.symbol,currentlyCoveringRows:(idx[s.path]??[]).filter(r=>r.startLine<=s.line&&r.endLine>=s.line).map(r=>r.functionId)}))}));write('old16-crosswalk.json',cross);
const mjs=inv.files.filter(f=>f.path.endsWith('.mjs')).map(f=>{const text=read(f.path);const functions=[...text.matchAll(/^[\t ]*(?:export\s+)?(?:async\s+)?function\s+([\w$]+)\s*\(/gm)].map(m=>({symbol:m[1],line:text.slice(0,m.index).split('\n').length}));return {path:f.path,dataset:f.path.split('/golden/')[1].split('/')[0],lineCount:f.lineCount,sourceReferences:f.sourceReferences,functions,topLevelPresent:true,fileSha256:hash(text),candidateOnly:true};});write('mjs-function-index.json',mjs);
const extras=inv.currentCodeFilesBeyondBaseline.map(f=>({path:f,sourceReferences:idx[f]?.length??0,sha256:hash(read(f)),functions:[...read(f).matchAll(/^function\s+([\w-]+)/gm)].map(m=>({symbol:m[1],line:read(f).slice(0,m.index).split('\n').length}))}));write('governance-script-index.json',extras);
const dto=[{path:p+'core/include/sdurws/ird/core/Evaluation.hpp',symbol:'EvaluationMode/TaskOutcome/EngineeringStatus/TaskState',classification:'枚举声明可按冻结词表成组；toToken实现仍须单独审',},{path:p+'policy/include/sdurws/ird/policy/Contexts.hpp',symbol:'IPolicyNameContext/IPolicyCallContext/IPolicyBytesSource',classification:'纯虚接口声明可按所有权/注入合同成组，不等于适配器实现已审'},{path:p+'core/include/sdurws/ird/core/DiagData.hpp',symbol:'ComparativeValue::operator== / ComparativeFields::operator==',classification:'简单值转发相等比较可成组，但DiagnosticRecord::make校验不可据此整文件豁免'}];write('groupable-examples.json',dto);
console.log(gaps.map(g=>`${g.id} ${g.location.path.slice(p.length)}:${g.location.startLine}-${g.location.endLine} ${g.location.qualifiedSymbol}`).join('\n'));
console.log('old16 anchors: '+cross.map(f=>`${f.id}=${f.currentlyReferencedAnchorCount}/${f.anchorCount}`).join(', '));
console.log('mjs='+mjs.length+' scripts='+extras.length);
const supplement=[
 ['PARTIAL-01','project/src/ObjectStore.cpp',441,'sdurws::ird::project::objstore::ObjectStore::scanObjects','双向引用/磁盘扫描和可选摘要校验；同文件两项已审区段不覆盖'],
 ['PARTIAL-02','execution/src/WorkerSupervisor.cpp',999,'sdurws::ird::execution::WorkerSupervisor::handleHandshake','协议/manifest拒绝、进程终结及握手后派发；spawnNewWorker区段不覆盖'],
 ['PARTIAL-03','execution/src/Scheduler.cpp',538,'sdurws::ird::execution::TaskScheduler::dispatchOne','并发预算/优先级/FIFO/单项目额度/内联派发；submit区段不覆盖'],
 ['TEST-01','optimization/test/ParetoTest.cpp',78,'(global anonymous namespace)::makeCandidate','构造含基线标记与缺失指标的测试候选；fixture helper应按样板/真实行为分组，不能只审main'],
 ['INLINE-01','core/include/sdurws/ird/core/Compare.hpp',57,'sdurws::ird::core::Tolerance::make','头内有限性与非负合法性工厂，超出纯DTO字段'],
 ['RECOVERY-01','project/src/TxEngine.cpp',871,'sdurws::ird::project::tx::TxEngine::scanForRecovery','区分staging残留、HEAD闭包损坏与闭包外对象，非ObjectStore读取'],
 ['CORE-TEMPLATE-01','testkit/include/sdurws/ird/testkit/SetCheck.hpp',122,'sdurws::ird::testkit::detail::tryAugmentAt<EdgeFn>','递归增广路径/回滚；非简单容差包装']
];
const supplemental=supplement.map(([id,rel,startLine,symbol,meaning])=>{const f=p+rel,txt=read(f),lines=txt.split('\n'),endLine=braceEnd(lines,startLine),refs=idx[f]??[];const intersecting=refs.filter(r=>r.startLine<=endLine&&r.endLine>=startLine);if(intersecting.length)throw new Error('Unexpected overlap '+id);write(id+'.frozen.txt',f+'\n'+symbol+'\n'+lines.slice(startLine-1,endLine).map((l,i)=>`${startLine+i}: ${l}`).join('\n')+'\n');return {id,path:f,qualifiedSymbol:symbol,startLine,endLine,meaning,fileSha256:hash(txt),fileReferenceCount:refs.length,intersectingDeclaredSources:intersecting,allDeclaredSources:refs,evidence:id+'.frozen.txt'};});write('supplemental-gaps.json',supplemental);
