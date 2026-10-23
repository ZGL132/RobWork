$ErrorActionPreference='Stop'
$repo='C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-batch4-r1-b46e9b34/repo'
Set-Location $repo
$oldCommit='c7e1469b40ca7c1dbba55a3b6bd8a443fc6086c5'
$freeze='877f6afee38446ab43c5e6a6fd9b09593925aedb'
$path='RobWork/doc/industrial-robot-design/reuse/functions/modeling.json'
$old=(@(git show ($oldCommit+':'+$path))-join "`n")|ConvertFrom-Json
$new=Get-Content $path -Raw|ConvertFrom-Json
$a=$old.functions|Where-Object functionId -eq 'modeling.makeLinkPlaceholderCylinder'
$b=$new.functions|Where-Object functionId -eq 'modeling.makeLinkPlaceholderCylinder'
$a.requirements=$b.requirements
$b.PSObject.Properties.Remove('designRefs')
$unchanged=($a|ConvertTo-Json -Depth 12 -Compress)-ceq($b|ConvertTo-Json -Depth 12 -Compress)
$b=$new.functions|Where-Object functionId -eq 'modeling.makeLinkPlaceholderCylinder'
$row=Get-Content $path -Raw|ConvertFrom-Json|Select-Object -ExpandProperty functions|Where-Object functionId -eq 'modeling.makeLinkPlaceholderCylinder'
$contract=Get-Content RobWork/doc/industrial-robot-design/tasks/foundation/WP-13-T07.json -Raw|ConvertFrom-Json
$unit=[IO.File]::ReadAllText((Join-Path $repo 'RobWork/doc/industrial-robot-design/units/modeling.md'))
$requirements=[IO.File]::ReadAllText((Join-Path $repo 'RobWork/doc/industrial-robot-design/REQUIREMENTS.md'))
$facts=[ordered]@{reviewedCommit=$freeze;previousFailedCommit=$oldCommit;finding='B4-P2-01';functionId=$row.functionId;requirements=$row.requirements;designRefs=$row.designRefs;onlyRequirementsAndDesignRefsChanged=$unchanged;status=$row.status;decision=$row.decision;unitSection25Exists=$unit.Contains('### 2.5 ');unitSection52Exists=$unit.Contains('### 5.2 ');unitHasExplicitMdl04At01PlaceholderMapping=$unit.Contains('| 连杆几何 Drawables＋自动连杆圆柱（autoLink/regenerateLinkHelpers/computeLinkPose） | MDL-04、AT-01');contractTaskId=$contract.taskId;contractIncludesMdl04=('MDL-04'-in$contract.requirements);contractHasAt01=(($contract.acceptance-join "`n").Contains('AT-01'));contractHasPlaceholderAcceptance=(($contract.acceptance-join "`n").Contains('连杆占位圆柱生成'));requirementMdl04Exists=$requirements.Contains('| MDL-04 |');sourceUnchanged=(git diff $oldCommit $freeze -- $row.sources.path).Length-eq0}
$facts|ConvertTo-Json -Depth 12|Set-Content (Join-Path $PSScriptRoot 'rework-checks.json') -Encoding utf8
$row|ConvertTo-Json -Depth 12|Set-Content (Join-Path $PSScriptRoot 'reviewed-row.json') -Encoding utf8
$contract|ConvertTo-Json -Depth 12|Set-Content (Join-Path $PSScriptRoot 'WP-13-T07.json') -Encoding utf8
rg -n '^\| MDL-04 |连杆几何 Drawables|^### (2\.5|5\.2) |几何生成辅助.*v0.2' RobWork/doc/industrial-robot-design/REQUIREMENTS.md RobWork/doc/industrial-robot-design/units/modeling.md > (Join-Path $PSScriptRoot 'requirement-design-evidence.log')
git rev-parse HEAD > (Join-Path $PSScriptRoot 'head.txt')
git status --short > (Join-Path $PSScriptRoot 'status-final.log')
$facts|ConvertTo-Json -Depth 6
