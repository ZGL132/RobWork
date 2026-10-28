$ErrorActionPreference='Stop'
$repo='C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork'
$probeRoot=$PSScriptRoot
$projectPath=Join-Path $repo 'build/RobWorkStudio/src/rwslibs/industrialrobot/policy/sdurws_ird_policy_test.vcxproj'
[xml]$project=Get-Content -LiteralPath $projectPath -Raw
$group=$project.Project.ItemDefinitionGroup | Where-Object {$_.Condition -like '*Release|x64*'}
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$arguments=@('/nologo','/std:c++17','/EHsc','/MD','/O2','/utf-8','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN','/DGTEST_LINKED_AS_SHARED_LIBRARY=1')
$arguments+='/I"D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/include"'
$arguments+= '/DIRD_POLICY_UNIT_ROOT=\"'+$repo+'/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/..\"'
foreach($include in ($group.ClCompile.AdditionalIncludeDirectories -split ';')) {
    if($include -and -not $include.StartsWith('%')){$arguments+='/I"'+$include+'"'}
}
$arguments+='/Fo"'+$probeRoot+'/PolicySessionBenchmark.obj"'
$arguments+='/Fe"'+$probeRoot+'/PolicySessionBenchmark.exe"'
$arguments+='"'+$probeRoot+'/PolicySessionBenchmark.cpp"'
$arguments+='/link'
foreach($dependency in ($group.Link.AdditionalDependencies -split ';')) {
    if($dependency.StartsWith('..')) {$dependency=[IO.Path]::GetFullPath((Join-Path (Split-Path $projectPath) $dependency))}
    if($dependency -and -not $dependency.StartsWith('%')) {$arguments+='"'+$dependency+'"'}
}
$arguments | Set-Content -LiteralPath (Join-Path $probeRoot 'probe-compile.rsp') -Encoding UTF8
& cl.exe ('@'+(Join-Path $probeRoot 'probe-compile.rsp')) *> (Join-Path $probeRoot 'benchmark-build.log')
$code=$LASTEXITCODE
Get-Content (Join-Path $probeRoot 'benchmark-build.log') -Tail 20
exit $code
