import pathlib,subprocess
out=pathlib.Path(__file__).parent
base='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
sections=[('runtime/src/WorkCellCompiler.cpp',190,220),('ui/plugin/KinEvaluationChannel.cpp',552,592),('ui/plugin/KinEvaluationChannel.cpp',623,636),('ui/plugin/UiPlugin.cpp',8295,8313),('modeling/plugin/ModelingUiModule.cpp',990,1032),('optimization/contract_test/ApplierContractTest.cpp',485,502)]
lines=['冻结主线源码取证：b7abcba9；以下内容经git show读取已提交对象，不读取486b未提交工作。']
for name,start,end in sections:
 text=subprocess.check_output(['git','show','b7abcba9:'+base+name]).decode('utf-8').splitlines()
 lines+=['',base+name]+[str(i+1)+': '+text[i] for i in range(start-1,min(end,len(text)))]
t27=subprocess.check_output(['git','show','648c9509:RobWork/doc/industrial-robot-design/traceability/builds/wp13-t27-audit-fixes/ma09-runtime-ird-report.json']).decode('utf-8')
lines+=['','T27提交对象的原生runtime报告（历史证据只读核对，不算本轮执行）：',t27]
(out/'source-check.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
