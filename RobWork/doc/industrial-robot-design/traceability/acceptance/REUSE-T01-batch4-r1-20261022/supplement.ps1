$ErrorActionPreference='Stop'
Set-Location (Join-Path $PSScriptRoot 'repo')
$out=$PSScriptRoot
git diff --name-only f6ee77c91c36f40a4cd573a24335405afc06f68b c7e1469b40ca7c1dbba55a3b6bd8a443fc6086c5 -- RobWork/RobWorkStudio/src/rwslibs/industrialrobot RobWork/RobWork RobWork/RobWorkSim vcpkg *> (Join-Path $out 'scope.log')
$LASTEXITCODE|Set-Content (Join-Path $out 'scope.exit.txt')
git diff --check f6ee77c91c36f40a4cd573a24335405afc06f68b c7e1469b40ca7c1dbba55a3b6bd8a443fc6086c5 *> (Join-Path $out 'diff-check.log')
$LASTEXITCODE|Set-Content (Join-Path $out 'diff-check.exit.txt')
git status --short *> (Join-Path $out 'status.log')
$LASTEXITCODE|Set-Content (Join-Path $out 'status.exit.txt')
rg -n '^\| MDL-0[24] |连杆几何 Drawables|几何生成辅助.*v0.2' RobWork/doc/industrial-robot-design/REQUIREMENTS.md RobWork/doc/industrial-robot-design/units/modeling.md > (Join-Path $out 'requirement-evidence.log')
$c=Get-Content RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp
509..567|ForEach-Object{'{0}: {1}' -f $_,$c[$_-1]}|Set-Content (Join-Path $out 'fd-lambda-evidence.log')
612..692|ForEach-Object{'{0}: {1}' -f $_,$c[$_-1]}|Set-Content (Join-Path $out 'rk4-section-evidence.log')
$rows=Get-Content (Join-Path $out 'new-rows.json') -Raw|ConvertFrom-Json
$registration=foreach($r in $rows){
 $p=$r.sources[0].path;$file=Split-Path $p -Leaf;$cm=$r.inputs[0].path
 [pscustomobject]@{id=$r.functionId;file=$file;cmake=$cm;sourceMentioned=([IO.File]::ReadAllText((Join-Path $PWD $cm))).Contains($file)}
}
$registration|ConvertTo-Json -Depth 6|Set-Content (Join-Path $out 'source-registration.json')
