$ErrorActionPreference='Stop'
foreach($suite in @('standalone_modeling','standalone_modeling_contract','standalone_modeling_gui')) {
 & pwsh -NoProfile -File build/modeling-completion-20261020-b3f/run.ps1 -Mode test -Name $suite
 if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}
