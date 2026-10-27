$ErrorActionPreference='Stop'
$root='C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork'
$frozen='4e142e485343e8bade52a8481eefbb447b017989'
Set-Location $root
$out=$PSScriptRoot
$paths=@(
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Backfill.cpp',
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/CanonicalModel.cpp',
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/DescriptionValidator.cpp',
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/requirements/src/TemplateArray.cpp',
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp',
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp',
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelModel.cpp',
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp',
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/Replay.cpp',
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Smooth.cpp',
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/test/KinFkFixture.hpp',
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/HostView3DPreviewBackend.cpp',
'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/src/DriveTrainCodec.cpp',
'RobWork/RobWork/src/rw/math/LinearAlgebra.hpp',
'RobWork/RobWork/src/rw/math/RPY.hpp',
'RobWork/RobWork/src/rw/math/RPY.cpp',
'RobWork/RobWork/src/rw/trajectory/LinearInterpolator.hpp',
'RobWork/RobWork/src/rwlibs/pathoptimization/pathlength/PathLengthOptimizer.hpp',
'RobWork/RobWork/src/rw/common/BINArchive.hpp',
'RobWork/RobWork/src/rwlibs/opengl/RenderLines.hpp',
'RobWork/RobWorkSim/src/rwsim/rwphysics/RK4Integrator.hpp',
'RobWork/RobWorkSim/src/rwsim/CMakeLists.txt'
)
$bindings=foreach($p in $paths){
    $blob=git rev-parse ($frozen+':'+$p)
    if($LASTEXITCODE){throw "missing frozen blob: $p"}
    git diff --quiet $frozen -- $p
    if($LASTEXITCODE){throw "working source differs from frozen: $p"}
    $gitText=@(git show ($frozen+':'+$p))
    $gitText | Set-Content -LiteralPath (Join-Path $out (([IO.Path]::GetFileName($p))+'.'+$bindings.Count+'.frozen.txt')) -Encoding utf8
    [pscustomobject]@{path=$p;gitBlob=$blob;frozenCommit=$frozen;worktreeEqualsFrozen=$true}
}
$matrices=foreach($p in @(git ls-tree -r --name-only $frozen -- RobWork/doc/industrial-robot-design/reuse/functions)){
 $j=(@(git show ($frozen+':'+$p))-join "`n")|ConvertFrom-Json
 [pscustomobject]@{unit=$j.unit;rows=$j.functions.Count;sources=@($j.functions.sources.path|Sort-Object -Unique).Count;decisions=@($j.functions.decision|Sort-Object -Unique);symbols=@($j.functions.sources.symbol)}
}
[pscustomobject]@{frozenCommit=$frozen;startedHead='ce1af19667e6e3e439e15600e5bed7940b171942';rows=($matrices|Measure-Object rows -Sum).Sum;bindings=$bindings;matrices=$matrices;build='NOT RUN: read-only coverage review';gate='NOT RUN: separate scope'}|ConvertTo-Json -Depth 12|Set-Content (Join-Path $out 'frozen-facts.json') -Encoding utf8
git status --short | Set-Content (Join-Path $out 'git-status-end.txt') -Encoding utf8
Write-Output ('Frozen blob verification PASS; '+$bindings.Count+' source/upstream blobs; rows='+($matrices|Measure-Object rows -Sum).Sum)
