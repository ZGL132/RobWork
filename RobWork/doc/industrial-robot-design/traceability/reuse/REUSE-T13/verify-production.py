from pathlib import Path
import subprocess,os,json,time
root=Path.cwd(); ev=root/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T13'
src=root/'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/WorkCellCompiler.cpp'
exe=root/'.cache/integrated/RobWorkStudio/bin/Release/sdurws_ird_runtime_test.exe'
env=os.environ.copy();env['PATH']=';'.join(str(root/p) for p in ['.cache/integrated/RobWork/bin/Release','.cache/integrated/RobWorkStudio/bin/Release','.cache/integrated/RobWorkSim/bin/Release'])+';D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Miniconda3;'+env['PATH']
def run(args,log):
 with open(log,'wb') as f:return subprocess.run([str(x) for x in args],env=env,stdout=f,stderr=subprocess.STDOUT).returncode
def build(name):
 return run(['pwsh','-NoProfile','-Command',"& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation; cmake --build .cache/integrated --config Release --target sdurws_ird_runtime_test -j 12; exit $LASTEXITCODE"],ev/name)
def tests(name,filt,repeat=None):
 args=[str(exe),'--gtest_filter='+filt,'--gtest_output=xml:'+str(ev/(name+'.xml')),'--ird_report='+str(ev/(name+'-report.json'))]
 if repeat: args.append('--gtest_repeat='+str(repeat))
 start=time.perf_counter();rc=run(args,ev/(name+'.log'));return {'command':subprocess.list2cmdline(args),'exitCode':rc,'seconds':time.perf_counter()-start}
original=src.read_bytes();(root/'.cache/foundation/production-final.cpp').write_bytes(original)
results={}
try:
 # 聚合断开实验只在隔离工作树内临时改真实生产文件，各失效返回由用例观察。
 s=original.decode('utf-8').replace('getTransform(joint.zeroOffset)','getTransform(0.0)')
 s=s.replace('return model3d->toGeometryData();','return {}; // 实验：断开OBJ转换返回值')
 s=s.replace('return rw::loaders::STLFile::load(tempPath.string());','return {}; // 实验：断开STL加载返回值')
 s=s.replace('workCell.add(carrier.template cast<rw::models::Object>());','(void)carrier; // 实验：不消费上游几何载体')
 s=s.replace('return rw::math::Transform3D<double>::makeLookAt(\r\n        rw::math::Vector3D<double>(0, 0, 0), -axis, up).R();','return rw::math::Rotation3D<double>::identity(); // 实验：丢弃对齐')
 s=s.replace('return rw::math::Transform3D<double>(span * 0.5, align);','return rw::math::Transform3D<double>(rw::math::Vector3D<double>(0,0,0), align);')
 s=s.replace('return m_workCell->findFrame(std::string(name));','return nullptr; // 实验：丢弃查询结果')
 s=s.replace('m_workCell->findDevice(std::string(name)).template','m_workCell->findDevice(name == "no_such_device" ? "IRB_T" : std::string(name)).template')
 s=s.replace('return m_workCell->getFrames().size();','return 0; // 实验：丢弃帧清单')
 s=s.replace('return m_workCell->getDefaultState();','auto state = m_workCell->getDefaultState();\r\n    auto dev = m_workCell->findDevice("IRB_T");\r\n    dev->setQ(rw::math::Q(dev->getDOF(), 1.0), state);\r\n    return state; // 实验：扰动上游默认状态')
 assert s!=original.decode('utf-8')
 mutant=s.encode('utf-8');src.write_bytes(mutant);(ev/'disconnected-source.txt').write_bytes(mutant)
 assert build('disconnected-build.log')==0
 results['disconnected']=tests('disconnected','WorkCellCompilerTest.*:WorkCellGeometryTest.*:WorkCellConstViewTest.*')
 assert results['disconnected']['exitCode']!=0
finally:
 src.write_bytes(original)
 assert build('restore-build.log')==0
results['normal']=tests('runtime','-ContractSuite.ChildProcessWorker_RT_ID_1')
assert results['normal']['exitCode']==0
(ev/'final-runs.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:{a:b for a,b in v.items() if a!='command'} for k,v in results.items()},ensure_ascii=False))
