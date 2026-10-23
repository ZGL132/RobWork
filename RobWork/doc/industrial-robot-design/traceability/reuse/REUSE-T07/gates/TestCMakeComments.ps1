# REUSE-T07：IRD-GATE-AD 对 CMake 注释的回归，不复制解析器或权限规则。
# 直接加载实际 helper；无 grant 的受限 C++ include/对象/State 写仍必须拒绝。
param([string]$OutputFile)
$ErrorActionPreference='Stop'
$repo=(git rev-parse --show-toplevel).Trim()
. (Join-Path $repo 'RobWork/scripts/industrialrobot/reuse-contract.ps1')
function Fail([string]$code,[string]$message) { throw "REUSE-${code}: $message" }
$product='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
$rows=@()
$cases=@(
    @{name='real-modeling-cmake';path=$product+'modeling/CMakeLists.txt';text=[IO.File]::ReadAllText((Join-Path $repo ($product+'modeling/CMakeLists.txt')));reject=$false},
    @{name='cmake-line-comment';path=$product+'modeling/CMakeLists.txt';text="# WorkCell State RigidDevice`nadd_library(sample INTERFACE)";reject=$false},
    @{name='cmake-inline-comment';path=$product+'modeling/helper.cmake';text='add_library(sample INTERFACE) # WorkCell State';reject=$false},
    @{name='cpp-line-comment';path=$product+'modeling/src/sample.cpp';text="// WorkCell State`nvoid sample(){}";reject=$false},
    @{name='cpp-model-include-still-rejected';path=$product+'modeling/src/sample.cpp';text="#include <rw/models/WorkCell.hpp>`nvoid sample(){}";reject=$true},
    @{name='cpp-state-write-still-rejected';path=$product+'modeling/src/sample.cpp';text='void sample(rw::kinematics::State& s){ device->setQ(q,s); }';reject=$true},
    @{name='cmake-uncommented-type-still-rejected';path=$product+'modeling/CMakeLists.txt';text='WorkCell';reject=$true},
    @{name='cpp-preprocessor-not-a-comment';path=$product+'modeling/src/sample.cpp';text='#include <rw/kinematics/State.hpp>';reject=$true}
)
foreach($case in $cases) {
    $rejected=$false;$errorText=''
    try { CheckSourcePermission $case.path $case.text @{} } catch { $errorText=$_.Exception.Message;if($errorText -notlike 'REUSE-AD:*'){throw};$rejected=$true }
    $ok=$rejected -eq $case.reject
    $rows+=[pscustomobject]@{name=$case.name;expectedReject=$case.reject;rejected=$rejected;passed=$ok;message=$errorText}
    Write-Output "$($case.name): $(if($ok){'PASS'}else{'FAIL'})"
}
$failed=@($rows|Where-Object {-not $_.passed}).Count
$report=[pscustomobject]@{executed=$rows.Count;passed=$rows.Count-$failed;failed=$failed;skipped=0;helperSha256=(Get-FileHash (Join-Path $repo 'RobWork/scripts/industrialrobot/reuse-contract.ps1')).Hash;cases=$rows}
if($OutputFile){$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $OutputFile -Encoding utf8}
if($failed){exit 1}
