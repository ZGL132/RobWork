$ErrorActionPreference='Stop'
$acceptRoot='C:/Users/zgl18/AppData/Local/Temp/ird-accept-b28-68277e2f39'
$clone=Join-Path $acceptRoot 'clone'
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> (Join-Path $acceptRoot 'vs-environment.log')
Set-Location $clone
cmake -S RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01 -B (Join-Path $acceptRoot 'build') -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_TOOLCHAIN_FILE=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake' '-DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake' '-DRWSIM_TARGETS_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkSim/cmake/targets' '-DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows;D:/software/Qt/6.11.1/msvc2022_64' *> (Join-Path $acceptRoot 'configure.log')
$rc=$LASTEXITCODE; Set-Content (Join-Path $acceptRoot 'configure.exit.txt') $rc; if($rc){exit $rc}
cmake --build (Join-Path $acceptRoot 'build') --config Release --target ird_reuse_audit_probe *> (Join-Path $acceptRoot 'probe-build.log')
$rc=$LASTEXITCODE; Set-Content (Join-Path $acceptRoot 'probe-build.exit.txt') $rc; if($rc){exit $rc}
MSBuild.exe (Join-Path $acceptRoot 'build/ird-core/sdurws_ird_core_test.vcxproj') /p:Configuration=Release /p:Platform=x64 /v:minimal *> (Join-Path $acceptRoot 'core-build.log')
$rc=$LASTEXITCODE; Set-Content (Join-Path $acceptRoot 'core-build.exit.txt') $rc; if($rc){exit $rc}
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/build/RobWorkSim/bin/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWorkSim/libs/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWork/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;'+$env:PATH
foreach($test in @(@('probe','build/Release/ird_reuse_audit_probe.exe','ReuseCoreAudit.*:EventTest.*'),@('core','build/ird-core/Release/sdurws_ird_core_test.exe','*'))){
 $out=Join-Path $acceptRoot $test[0]; New-Item -ItemType Directory -Path $out -Force | Out-Null
 & (Join-Path $acceptRoot $test[1]) "--gtest_filter=$($test[2])" "--gtest_output=xml:$out/gtest.xml" "--ird_report=$out/ird-test-report.json" *> (Join-Path $out 'stdout.log')
 Set-Content (Join-Path $out 'exit.txt') $LASTEXITCODE
}
