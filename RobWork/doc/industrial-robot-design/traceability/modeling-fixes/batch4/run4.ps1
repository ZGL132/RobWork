$ErrorActionPreference='Stop'
$reviewRepo='C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork'
Set-Location -LiteralPath $reviewRepo
$reviewDev='D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
cmd /d /s /c "`"$reviewDev`" -no_logo -arch=x64 -host_arch=x64 && set" | ForEach-Object {
 if($_ -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}
}
$env:PATH="$reviewRepo\build\RobWorkStudio\bin\Release;D:\software\QT\6.11.1\msvc2022_64\bin;D:\10_Source_Repos\21_robot\RobWork\vcpkg\installed\x64-windows\bin;"+$env:PATH
$env:QT_QPA_PLATFORM='windows'
foreach($test in @('unit','contract','gui','ui')){
 $name=switch($test){'unit'{'sdurws_ird_modeling_test'} 'contract'{'sdurws_ird_modeling_contract_test'} 'gui'{'sdurws_ird_modeling_gui_test'} 'ui'{'sdurws_ird_ui_test'}}
 $args=@("--gtest_output=xml:build/modeling-fixes/batch4/$test.xml","--ird_report=build/modeling-fixes/batch4/$test.ird-test-report.json")
 if($test -eq 'ui'){$args+='--gtest_filter=HostCompilePipelineTest.*'}
 & "$reviewRepo\build\RobWorkStudio\bin\Release\$name.exe" @args *> "build/modeling-fixes/batch4/$test.log"
 Write-Output "$test exit=$LASTEXITCODE"
 Get-Content "build/modeling-fixes/batch4/$test.log" -Tail 10
}
& pwsh -NoProfile -File build/modeling-review-20261028/reproduce-probes.ps1 *> build/modeling-fixes/batch4/probe-script.log
Get-Content build/modeling-fixes/batch4/probe-script.log
foreach($file in @('replay-build.log','replay.log','replay.xml','replay.ird-test-report.json','replay-header.log')){
 Copy-Item -LiteralPath "build/modeling-review-20261028/$file" -Destination "build/modeling-fixes/batch4/$file"
}
