import pathlib,json,subprocess,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';sha='85e561539a4038e37231dcfcb506109a5a188151';base='b83fa7a3fc8ff233e56736adabd53f11c914dbad'
def read(p):return p.read_text(encoding='utf-8-sig')
def load(n):return json.loads(read(ev/n))
def git(*a):return subprocess.check_output(['git',*a],cwd=repo,encoding='utf-8')
assert git('rev-parse','HEAD').strip()==sha and git('status','--porcelain')==''
(ev/'git-status-end.log').write_text('')
assert len(load('rows.json'))==24 and sum(x['new'] for x in load('changes.json'))==21
assert len(load('bindings.json'))==192 and all(x['match'] for x in load('bindings.json'))
assert all(x['matches'] for x in load('definition-cross-check.json'))
assert len(load('search-replay.json'))==5 and all(x['exitMatches'] and x['stdoutSetMatches'] and x['stderrMatches'] for x in load('search-replay.json'))
tc=load('target-checks.json');failures=[x for x in tc if not x['match']];assert len(failures)==5 and all(x['missing']==['sdurws_ird_ui_test'] and not x['extra'] for x in failures)
gates={n:int(read(ev/f'{n}.exit.txt').strip()) for n in ['task','reuse','docs']};assert all(v==0 for v in gates.values())
assert int(read(ev/'semantic-check.exit.txt').strip())==0 and int(read(ev/'diff-check.exit.txt').strip())==0
regions=[('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/CMakeLists.txt',416,427),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/CMakeLists.txt',777,791),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/CMakeLists.txt',918,934),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/CMakeLists.txt',1045,1062),('RobWork/RobWork/src/rw/graphics/WorkCellScene.cpp',824,851),('RobWork/RobWork/src/rw/graphics/DrawableNode.hpp',198,207),('RobWork/RobWork/src/rwlibs/opengl/RenderFrame.cpp',24,119),('RobWork/RobWork/src/rwlibs/opengl/RenderLines.cpp',28,79),('RobWork/RobWork/src/rwlibs/opengl/RenderText.cpp',155,240),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp',4724,4818),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/gui_test/HostView3DGatewayGuiTest.cpp',154,170),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/gui_test/HostView3DGatewayGuiTest.cpp',474,497),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/HostPresentationAdapters.cpp',376,420)]
parts=[]
for path,start,end in regions:
 lines=read(repo/path).splitlines();parts.append(path+'\n'+'\n'.join(f'{i+1}: {lines[i]}' for i in range(start-1,min(end,len(lines)))))
(ev/'upstream-wiring-evidence.log').write_text('\n\n'.join(parts)+'\n',encoding='utf-8')
finding=dict(id='B15-P2-01',priority=2,title='QtText五条关系漏记ui_test真实同源编译目标',locations=[dict(path='RobWork/doc/industrial-robot-design/reuse/functions/ui.json',line=n) for n in [1837,1902,1967,2032,2097]]+[dict(path='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/host-preview-audit.md',line=23)],evidence='ui/CMakeLists.txt:416-426的if(TARGET sdurws)块通过target_sources(sdurws_ird_ui_test PRIVATE ...)在425行直接编入plugin/QtTextBillboard.cpp；其五个完整定义均属于该TU，矩阵只列plugin/studio。',affectedFunctionIds=[x['functionId'] for x in failures],impact='本批重点为精确文件/编译目标/操作权限，遗漏该真实测试编译目标使QtText图形及Frame借持/wTf使用的待核范围不完整。治理门禁仅核目标存在，不能替代实际编译归属。此项为审计登记缺漏，不声称新增授权或产品GUI故障。',requestedCorrection='将sdurws_ird_ui_test补入五条QtText关系的targets；在说明中记录if(TARGET sdurws)条件下的同源测试编译及精确权限仍待核。不得因此扩HOST清单或新增grant；若修改说明，同步24条关联auditEvidence摘要。无需改产品或为此跑C++/GUI。')
observations=[
 ('定义、作用域与摘要','24条列明定义逐段实读且与batch15-definitions逐项一致，含单行析构、全部构造initializer list和跨行返回类型。anonymous namespace位于sdurws::ird::ui内；ensureSessionPreviewAnchor返回Ptr<WorkCell>、currentTcpPose返回optional<Transform3D<>>、rasterizeAndUpload返回QtTextBillboard::TextTexture。192项源码区段/inputs/auditEvidence规范化摘要全匹配。'),
 ('真实目标','Preview相关定义确属plugin/studio；Gateway8定义确属plugin/studio及条件gui_test；均在if(TARGET sdurws)集成路径。QtText五定义另有ui_test同源编译，已列B15-P2-01，19/24目标集合匹配。未以代码同源传播权限。'),
 ('预览与锚','draw优先studio WC，空时复用scene WC，皆空才构造仅WORLD会话锚并setWorkCell/setWorldNode；不发布studio工程模型，不授计算模型构造。前置成功后先clear，再逐项addRender/addFrameAxis，只存名且不消费返回句柄；中途异常无回滚。clear逐名清当前scene，未找节点的上游removeDrawable(name)确返回true，产品false注释不正确但未修改。切场景残留只列待核，非复现。'),
 ('Gateway8定义','构造必需pick/selection/nameMap；handle按nameMap反解后分发和selectBusiness，返回true不依赖dispatch布尔。applyPresentation依赖void payload装配类型，先apply新对象再release旧；具体HostWorkCellPresentationObject::remove为空动作，未泛称清空WC。applyPreview仅draw true后更新缓存，onSceneCleared和removePreview对称清理。GuiTest的draw替身不足证明真实后端异常保旧。currentTcpPose真实用worldTframe；UiPlugin装配取JointDevice::getEnd与studio State，未证明工具TCP偏移/多TCP。'),
 ('渲染候选与GL差异','RenderFrame为公开Render，DrawableNode::setTransform是公开API且addRender返回DrawableNode，可组合任意位置轴，故不能以addFrameAxis帧原点限制排除复用。RenderFrame实体/线型GLU、palette、alpha/状态差异保留；RenderLines真实double转float，使用配置_alpha而非回调alpha，且GL_POINTS/逐点分色/选中圈不能整体当线集迁移。'),
 ('QtText5定义','Qt/QFont/QImage/QPainter/QString已提供字体栅格化；RenderText实际GLUT bitmap且受RW_HAVE_GLUT条件，不适合直接替换CJK。QtText调用上游inverse/Rotation，挂帧Render消费Frame::wTf；不是另写运动学。static map按context指针切换清理但不主动删除纹理，缓存预算/地址复用/上传失败/UNPACK与进入GL状态恢复未证。PreviewColorScope栈保存不自动覆盖独立挂帧QtText路径，这些均被说明为静态待核，未冒称已复现。'),
 ('候选与权限边界','HOST-FRAME/TEXT新增及HOST-SCENE扩展均保持candidate；7项上游/测试/CMake绑定匹配，5条检索独立重放exit0且命中集合/stderr一致。permissions.json与ARCH权威无diff，四项HOST封闭清单及grants=[]未改；QtText未列入例外而Frame操作明确待核，pending不授新文件/target/construct权限。'),
 ('任务与范围','任务仅增加3个B15产物允许路径；README明确21新增+3细化、pending不扩权限、GUI/GL未跑、不关闭T01。全部24关系pending；其余HOST和UiPlugin装配未借本批结项。')]
report=dict(schemaVersion='ird-independent-acceptance/1',taskId='REUSE-T01',batch='B15',reviewedCommit=sha,comparisonBase=base,verdict='FAIL',scope='仅新增21+细化3关系、HOST-FRAME/TEXT与HOST-SCENE候选、说明/定义/搜索/允许文件及README；不授全UI或全T01 PASS。',findings=[finding],observations=[dict(topic=t,result='FAIL' if t=='真实目标' else 'PASS',detail=v) for t,v in observations],checks=dict(newRelations=21,refinedRelations=3,definitions=24,definitionManifestMatches=24,bindings=192,failedBindings=0,catalogReferences=7,searches=5,searchMismatches=0,targetSetsMatched=19,targetSetsMissingUiTest=5),gates=dict(exitCodes=gates,passed=3,failed=0,skipped=0,scope='治理结构/文档/任务，不代表语义/算法合规'),sourceAndUpstreamDiffEmpty=True,authorityAndPermissionsUnchanged=True,checkoutClean=True,repositoryModified=False,committed=False,pushed=False,merged=False,subagentsCreated=False,wholeT01Pass=False,notRun=['C++构建/产品测试','GUI/GL/真实后端','数值/性能','上游测试执行','DeepEvidence','全T01退出','远程推送/CI'],remoteDelivery=dict(status='NOT DELIVERED / NOT VERIFIED',detail='根据委派说明远程443失败；本轮只本地冻结验收，未推送或独立重试远程，不称交付完成。'),reportRoot=str(ev))
(ev/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
text=f'''# REUSE-T01 B15 独立语义验收

结论：**FAIL，仅B15审计记录，1项需返工**。独立冻结 `{sha}`，父 `{base}`；本地独立Temp clone、longpaths checkout均exit0，初末干净。

## B15-P2-01：QtText遗漏真实ui_test编译目标

`ui/CMakeLists.txt:416-426` 在 `if(TARGET sdurws)` 下直接将 `plugin/QtTextBillboard.cpp` 编入 `sdurws_ird_ui_test`（425行）。该TU五个定义均在本批，但 `reuse/functions/ui.json:1837,1902,1967,2032,2097` 的targets只列plugin/studio；说明第23行只列studio与Gateway的gui_test，亦遗漏此目标。

本批审计强调精确文件/目标/操作核权，遗漏该目标使QtText图形及Frame借持/wTf使用的待核范围不完整。补五条targets和说明中的条件同源测试归属，并保持权限待核，不得扩大HOST清单或新增grant；说明变动后同步24条关联摘要。无需产品改动或为此跑C++。证据见target-checks.json与upstream-wiring-evidence.log。本项不是产品GUI故障或新增授权的指控。

## 其余核对结果

'''+ '\n\n'.join('- **'+t+'：** '+v for t,v in observations)+'''

## 实际验证与限制

validate-task、冻结Head的validate-reuse、validate-docs均exit0，passed=3、failed=0、skipped=0；仅治理结构通过。范围内git diff --check exit0。全部24关系仍pending，三候选仍candidate。相对父产品/上游/vcpkg及permissions/ARCH均无改动。

24定义实读、192摘要、7项catalog引用和5条检索通过；目标集合19通过、5遗漏ui_test，合并为上述1项。详细记录为definition-checks.json/definition-cross-check.json/source-excerpts.log、bindings.json、catalog-bindings.json、search-replay.json及每条搜索原日志；权限边界见permission-boundary.json。

C++构建/产品测试、真实GUI/GL/后端、数值/性能、上游测试执行、DeepEvidence和全T01退出均 **NOT RUN**。假draw只读分析，不替代真实生产后端证据。不授全UI/T01 PASS、生产权限或T02准入。

未实施、未改主工作树/冻结仓库、未提交/推送/合入、未创建子代理。根据委派说明远程443失败，本轮未推送或独立重试远程；**本地验收不是远程交付完成**。报告和证据只写本Temp根，结构化结论见acceptance.json。
'''
(ev/'acceptance.md').write_text(text,encoding='utf-8');print('FAIL B15-P2-01 only; gates=3 PASS; definitions=24 bindings=192 searches=5; target sets=19 match/5 missing ui_test; clean frozen checkout')
