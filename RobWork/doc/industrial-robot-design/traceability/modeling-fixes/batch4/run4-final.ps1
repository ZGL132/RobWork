$ErrorActionPreference='Stop'
$reviewRepo='C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork'
Set-Location -LiteralPath $reviewRepo
$reviewDev='D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
cmd /d /s /c "`"$reviewDev`" -no_logo -arch=x64 -host_arch=x64 && set" | ForEach-Object {
 if($_ -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}
}
$env:PATH="$reviewRepo\build\RobWorkStudio\bin\Release;$reviewRepo\build\RobWork\bin\Release;$reviewRepo\build\RobWorkSim\bin\Release;D:\software\QT\6.11.1\msvc2022_64\bin;D:\10_Source_Repos\21_robot\RobWork\vcpkg\installed\x64-windows\bin;$env:SDURWS_IRD_PYTHON_DIR;"+$env:PATH
$env:QT_QPA_PLATFORM='windows'
dumpbin /dependents "$reviewRepo\build\RobWorkStudio\bin\Release\sdurws_ird_ui_test.exe" > build/modeling-fixes/batch4/ui-dependents.log
foreach($test in @('unit','contract','ui')){
 $name=switch($test){'unit'{'sdurws_ird_modeling_test'} 'contract'{'sdurws_ird_modeling_contract_test'} 'ui'{'sdurws_ird_ui_test'}}
 $args=@("--gtest_output=xml:build/modeling-fixes/batch4/$test-final.xml","--ird_report=build/modeling-fixes/batch4/$test-final.ird-test-report.json")
 if($test -eq 'ui'){$args+='--gtest_filter=HostCompilePipelineTest.*'}
 & "$reviewRepo\build\RobWorkStudio\bin\Release\$name.exe" @args *> "build/modeling-fixes/batch4/$test-final.log"
 Write-Output "$test exit=$LASTEXITCODE"
 Get-Content "build/modeling-fixes/batch4/$test-final.log" -Tail 10
}
& pwsh -NoProfile -File build/modeling-review-20261028/reproduce-probes.ps1 *> build/modeling-fixes/batch4/probe-final-script.log
Get-Content build/modeling-fixes/batch4/probe-final-script.log
foreach($file in @('replay-build.log','replay.log','replay.xml','replay.ird-test-report.json','replay-header.log')){
 Copy-Item -LiteralPath "build/modeling-review-20261028/$file" -Destination "build/modeling-fixes/batch4/final-$file"
}
