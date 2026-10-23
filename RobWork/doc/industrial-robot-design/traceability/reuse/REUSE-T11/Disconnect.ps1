$ErrorActionPreference='Stop'
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> .cache/t11/mutation-vs.log
$env:PATH="$PWD/.cache/t07-integrated/RobWork/bin/Release;$PWD/.cache/t07-integrated/RobWorkStudio/bin/Release;$PWD/.cache/t07-integrated/RobWorkSim/bin/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Miniconda3;"+$env:PATH
$header=Join-Path $PWD 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/RpyMath.hpp'
$backup=Join-Path $PWD '.cache/t11/normal-RpyMath.hpp'
Copy-Item -LiteralPath $header -Destination $backup
try {
 $text=[IO.File]::ReadAllText($header)
 $text=$text.Replace('return rw::math::RPY<double>(yaw, pitch, roll).toRotation3D();', 'const auto result = rw::math::RPY<double>(yaw, pitch, roll).toRotation3D(); return rw::math::Rotation3D<double>::identity(); // test mutation: discard result')
 [IO.File]::WriteAllText($header,$text,[Text.UTF8Encoding]::new($false))
 Copy-Item -LiteralPath $header -Destination .cache/t11/disconnected-RpyMath.hpp
 cmake --build .cache/t07-integrated --config Release --target sdurws_ird_modeling sdurws_ird_modeling_test -- /p:BuildProjectReferences=false *> .cache/t11/disconnected-build.log
 if ($LASTEXITCODE) { throw 'disconnect build failed' }
 & "$PWD/.cache/t07-integrated/RobWorkStudio/bin/Release/sdurws_ird_modeling_test.exe" '--gtest_filter=MdlTemplate.CreateEditChain_OriginEditField_UI_T53:MdlBasePlacementEdit.EulerCompilesAndCodecRoundTrips_MDL22_AT37' --gtest_output=xml:.cache/t11/disconnected.xml --ird_report=.cache/t11/disconnected-ird-test-report.json *> .cache/t11/disconnected.log
 $LASTEXITCODE | Set-Content .cache/t11/disconnected.exit.txt
 if ($LASTEXITCODE -ne 1) { throw 'disconnect must fail assertion with exit1' }
} finally {
 Copy-Item -LiteralPath $backup -Destination $header -Force
 [IO.File]::SetLastWriteTimeUtc($header,[DateTime]::UtcNow)
 cmake --build .cache/t07-integrated --config Release --target sdurws_ird_modeling sdurws_ird_modeling_test -- /p:BuildProjectReferences=false *> .cache/t11/restored-build.log
 $LASTEXITCODE | Set-Content .cache/t11/restored-build.exit.txt
 if ($LASTEXITCODE) { throw 'restore build failed' }
}
& "$PWD/.cache/t07-integrated/RobWorkStudio/bin/Release/sdurws_ird_modeling_test.exe" '--gtest_filter=MdlTemplate.CreateEditChain_OriginEditField_UI_T53:MdlBasePlacementEdit.EulerCompilesAndCodecRoundTrips_MDL22_AT37' --gtest_output=xml:.cache/t11/restored.xml --ird_report=.cache/t11/restored-ird-test-report.json *> .cache/t11/restored.log
$LASTEXITCODE | Set-Content .cache/t11/restored.exit.txt
