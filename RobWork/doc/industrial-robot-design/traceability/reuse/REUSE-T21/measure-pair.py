from pathlib import Path
import subprocess,hashlib,json
root=Path.cwd();ev=root/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T21';build=root/'build/c4-production-pair';build.mkdir(exist_ok=True)
source='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Compare.cpp'
old=subprocess.check_output(['git','show','2136a89d5:'+source]);(build/'old-compare.cpp').write_bytes(old)
inc=f'/I "{root}/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include" /I "{root}/vcpkg/installed/x64-windows/include"'
lines=['@echo off','call "D:/software/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat"','if errorlevel 1 exit /b 1',f'cl /std:c++17 /O2 /EHsc /utf-8 /nologo {inc} /c /DcloseWithin=oldCloseWithin /DallCloseWithin=oldAllCloseWithin /DruntimeAbsoluteTolerance=oldRuntimeAbsoluteTolerance old-compare.cpp /Fo:old-compare.obj','if errorlevel 1 exit /b 1',f'cl /std:c++17 /O2 /EHsc /utf-8 /nologo {inc} /c "{root/source}" /Fo:current-compare.obj','if errorlevel 1 exit /b 1',f'cl /std:c++17 /O2 /EHsc /utf-8 /nologo {inc} "{ev}/performance-pair.cpp" old-compare.obj current-compare.obj /Fe:performance-pair.exe','exit /b %errorlevel%']
(ev/'build-performance-pair.cmd').write_text('\n'.join(lines)+'\n',encoding='utf-8')
with (ev/'performance-pair-build.log').open('w') as out:
 r=subprocess.run(['cmd','/d','/c',str(ev/'build-performance-pair.cmd')],cwd=build,stdout=out,stderr=subprocess.STDOUT)
assert r.returncode==0
with (ev/'performance-pair.log').open('w') as out:
 r=subprocess.run([str(build/'performance-pair.exe')],stdout=out,stderr=subprocess.STDOUT)
assert r.returncode==0
print((ev/'performance-pair.log').read_text())
meta={'baseline':'2136a89d5cd65e7183c734956df68291862e0fc0','oldSourceRawSha256':hashlib.sha256(old).hexdigest(),'currentSourceRawSha256':hashlib.sha256((root/source).read_bytes()).hexdigest(),'exeRawSha256':hashlib.sha256((build/'performance-pair.exe').read_bytes()).hexdigest(),'acceptance':'NOT ACCEPTED; owner approved implementation only'}
(ev/'performance-pair.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
