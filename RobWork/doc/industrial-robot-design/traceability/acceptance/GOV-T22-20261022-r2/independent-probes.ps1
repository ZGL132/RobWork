param([string]$RepoRoot, [string]$OutputDir)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
$sha = '4281543becc1a64f83087e2a752826480f8d2ce4'
if ((& git -C $RepoRoot rev-parse HEAD).Trim() -ne $sha) { throw '独立探针必须先在固定被验 SHA 上运行。' }
[IO.Directory]::CreateDirectory($OutputDir) | Out-Null
$validator = Join-Path $RepoRoot 'RobWork/scripts/industrialrobot/validate-reuse.ps1'
$helper = Join-Path $RepoRoot 'RobWork/scripts/industrialrobot/reuse-contract.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($validator,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw '被验脚本解析失败。' }
# 原样提取当前 SHA 函数，不运行完整入口，不复制或修改实现逻辑。
foreach ($f in $ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -in @('Fail','SafePath','ReadText','Digest')},$false)) {
    . ([scriptblock]::Create($f.Extent.Text))
}
. $helper
$script:results = @()
function PermissionProbe([string]$name,[string]$path,[string]$source,[string]$expected,$grants) {
    $actual = 'allow'; $diagnostic = ''
    try { CheckSourcePermission $path $source $grants } catch { $actual='reject'; $diagnostic=$_.Exception.Message }
    $script:results += [ordered]@{name=$name;path=$path;sourceSha256=(Digest $source);expected=$expected;actual=$actual;passed=($actual -eq $expected);diagnostic=$diagnostic}
}
$path = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/src/CommandPalette.cpp'
$text = [IO.File]::ReadAllText((Join-Path $RepoRoot $path))
$lines = [IO.File]::ReadAllLines((Join-Path $RepoRoot $path))
PermissionProbe '真实CommandPalette完整文件无上游grant' $path $text 'allow' @{}
PermissionProbe '首轮真实Qt片段188-189无grant' $path ($lines[187..188] -join "`n") 'allow' @{}
$query=@{}; $query[$path]=[pscustomobject]@{categories=@('state','model');operations=@('query')}
PermissionProbe '首轮真实Qt片段仅query无需State写' $path ($lines[187..188] -join "`n") 'allow' $query
PermissionProbe '真实setQ共享State写无grant仍拒绝' 'probe.cpp' 'void probe(){ device->setQ(q, sharedState); }' 'reject' @{}
$query=@{}; $query['probe.cpp']=[pscustomobject]@{categories=@('state');operations=@('query')}
$state='void probe(rw::kinematics::StateData& data, rw::kinematics::State& sharedState){ data.setData(sharedState, values); }'
PermissionProbe 'StateData共享State写query仍拒绝' 'probe.cpp' $state 'reject' $query
$private=@{}; $private['probe.cpp']=[pscustomobject]@{categories=@('state');operations=@('write-state')}
PermissionProbe 'StateData明确写权限允许' 'probe.cpp' $state 'allow' $private
PermissionProbe 'StateData限定成员写query拒绝' 'probe.cpp' 'void probe(){ rw::kinematics::StateData::setData(state, values); }' 'reject' $query
PermissionProbe '相机同名setTransform无grant' 'probe.cpp' 'void probe(){ viewer->setTransform(camera); }' 'allow' @{}
PermissionProbe '文案类型名不产生权限' 'probe.cpp' 'void probe(){ const char* label="WorkCell State"; }' 'allow' @{}
# 检查真实被追踪且带 BOM 的同一文件；读取按原有换行口径规范化后比较全文和摘要。
$path='RobWork/scripts/industrialrobot/validate-reuse.ps1'
$bytes=[IO.File]::ReadAllBytes((Join-Path $RepoRoot $path))
$tracked = (& git -C $RepoRoot ls-files --error-unmatch -- $path 2>$null); $trackedCode=$LASTEXITCODE
$hasBom=$bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191
$Head=''; $working=ReadText $path; $workingHash=Digest $working
$Head=$sha; $frozen=ReadText $path; $frozenHash=Digest $frozen
$normalizedWorking=($working -replace "`r`n","`n").TrimEnd("`n")+"`n"
$normalizedFrozen=($frozen -replace "`r`n","`n").TrimEnd("`n")+"`n"
$script:results += [ordered]@{name='真实BOM追踪文件两种读取全文摘要一致';path=$path;tracked=($trackedCode -eq 0);hasUtf8Bom=$hasBom;workingFirstCodePoint=[int]$working[0];frozenFirstCodePoint=[int]$frozen[0];workingHash=$workingHash;frozenHash=$frozenHash;normalizedTextEqual=($normalizedWorking -ceq $normalizedFrozen);passed=($hasBom -and $trackedCode -eq 0 -and $workingHash -ceq $frozenHash -and $normalizedWorking -ceq $normalizedFrozen)}
$report=[ordered]@{schemaVersion='ird-independent-probes/1';reviewedCommit=$sha;runtime=$PSVersionTable.PSVersion.ToString();validatorSha256=(Get-FileHash $validator -Algorithm SHA256).Hash;helperSha256=(Get-FileHash $helper -Algorithm SHA256).Hash;driverSha256=(Get-FileHash $PSCommandPath -Algorithm SHA256).Hash;passed=@($script:results | Where-Object {$_.passed}).Count;failed=@($script:results | Where-Object {-not $_.passed}).Count;skipped=0;notRun=0;tests=$script:results}
$json=$report | ConvertTo-Json -Depth 15
[IO.File]::WriteAllText((Join-Path $OutputDir 'independent-probes.json'),$json,[Text.UTF8Encoding]::new($false))
$json
if ($report.failed) { exit 1 }
