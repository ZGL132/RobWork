$ErrorActionPreference='Stop'
$batchRoot='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH="$batchRoot/build/RobWorkStudio/bin/Release;$batchRoot/build/RobWork/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;"+$env:PATH
& cmake --build "$batchRoot/build" --config Release --target sdurws_ird_modeling_gui_test *> "$batchRoot/build/modeling-completion-20261020-b3h/entry-red-build.log"
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& "$batchRoot/build/RobWorkStudio/bin/Release/sdurws_ird_modeling_gui_test.exe" '--gtest_filter=ModelingUiModuleHostWiring.EmptyToolManagerEntry_MDL13_AT01' "--gtest_output=xml:$batchRoot/build/modeling-completion-20261020-b3h/entry-red.xml" "--ird_report=$batchRoot/build/modeling-completion-20261020-b3h/entry-red.json" *> "$batchRoot/build/modeling-completion-20261020-b3h/entry-red.log"
$batchExit=$LASTEXITCODE
Get-Content "$batchRoot/build/modeling-completion-20261020-b3h/entry-red.log" -Tail 18
exit $batchExit
