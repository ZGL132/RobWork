from pathlib import Path
import subprocess,hashlib,json,os
root=Path.cwd(); ev=root/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T21'
p=root/'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Compare.cpp'
original=p.read_bytes(); digest=hashlib.sha256(original).hexdigest()
result={'sourceRawSha256':digest,'mutation':'const Wide wideLimit = Wide(product) + Wide(t.absolute); -> const Wide wideLimit = Wide(0);'}
try:
 p.write_bytes(original.replace(b'const Wide wideLimit = Wide(product) + Wide(t.absolute);',b'const Wide wideLimit = Wide(0);'))
 with (ev/'consumption-build.log').open('w') as log:
  r=subprocess.run(['cmake','--build','build','--config','Release','--target','sdurws_ird_core_test','--','/nologo','/verbosity:minimal'],stdout=log,stderr=subprocess.STDOUT);result['mutationBuildExit']=r.returncode
 if r.returncode: raise RuntimeError('mutation build failed')
 exe=root/'build/RobWorkStudio/bin/Release/sdurws_ird_core_test.exe'
 env=os.environ.copy();env['QT_QPA_PLATFORM']='windows';env['PATH']='D:/software/Miniconda3;D:/software/Qt/6.11.1/msvc2022_64/bin;'+str(root/'vcpkg/installed/x64-windows/bin')+';'+str(root/'build/RobWork/bin/Release')+';'+env['PATH']
 with (ev/'consumption-red.log').open('w') as log:
  r=subprocess.run([str(exe),'--gtest_filter=CompareFormula.FiniteOverflowBoundary_UT_UNIT:AllCloseWithin.FiniteOverflowDelegates_UT_UNIT:AllCloseWithinQ.FiniteOverflowDelegates_UT_UNIT','--gtest_output=xml:'+str(ev/'consumption-red.xml'),'--ird_report='+str(ev/'consumption-red-report.json')],stdout=log,stderr=subprocess.STDOUT,env=env);result['mutationTestExit']=r.returncode
finally:
 p.write_bytes(original);result['restoredRawSha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 with (ev/'restored-build.log').open('w') as log:
  r=subprocess.run(['cmake','--build','build','--config','Release','--target','sdurws_ird_core_test','--','/nologo','/verbosity:minimal'],stdout=log,stderr=subprocess.STDOUT);result['restoredBuildExit']=r.returncode
 if r.returncode==0:
  with (ev/'core-restored.log').open('w') as log:
   r=subprocess.run([str(exe),'--gtest_output=xml:'+str(ev/'core-restored.xml'),'--ird_report='+str(ev/'core-restored-report.json')],stdout=log,stderr=subprocess.STDOUT,env=env);result['restoredTestExit']=r.returncode
 (ev/'consumption.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
 print(json.dumps(result))
assert result['mutationTestExit']==1 and result['restoredTestExit']==0 and result['sourceRawSha256']==result['restoredRawSha256']
