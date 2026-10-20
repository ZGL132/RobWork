$ErrorActionPreference='Stop'
foreach($suite in @('modeling','modeling_contract','modeling_gui','ui','ui_contract','ui_gui')) {
 & pwsh -NoProfile -File build/modeling-completion-20261020-b3f/run.ps1 -Mode test -Name $suite
 if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}
& pwsh -NoProfile -File build/modeling-completion-20261020-b3f/run.ps1 -Mode test -Name core -Filter 'BuildRedLine.*'
exit $LASTEXITCODE
