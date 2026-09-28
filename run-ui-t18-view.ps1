# Temp launcher: PATH prefix (F-320 list) + --rwsplugin direct load, main build tree.
# Run dir under %TEMP%; console gate lines captured to console-*.log there.
$root = 'D:\10_Source_Repos\21_robot\RobWork'
$runDir = Join-Path $env:TEMP 'ird-ui-run-view1'
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$pathPre = "$root\build\RobWorkStudio\bin\Release;$root\build\RobWork\bin\Release;$root\vcpkg\installed\x64-windows\bin;D:\software\Qt\6.11.1\msvc2022_64\bin;D:\software\Miniconda3;"
$env:PATH = $pathPre + $env:PATH

$dll = "$root\build\RobWorkStudio\libs\Release\sdurws_ird_ui_plugin.dll"
$p = Start-Process -FilePath "$root\build\RobWorkStudio\bin\Release\RobWorkStudio.exe" -ArgumentList '--rwsplugin', "`"$dll`"" -WorkingDirectory $runDir -PassThru -RedirectStandardOutput "$runDir\console-stdout.log" -RedirectStandardError "$runDir\console-stderr.log"
Write-Output ("PID=" + $p.Id)
Write-Output ("RUNDIR=" + $runDir)
