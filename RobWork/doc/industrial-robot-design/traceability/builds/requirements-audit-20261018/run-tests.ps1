# 复现入口：每个 exe 使用独立 XML、逐例报告与控制台日志；失败立即停下供排查。
. "$PSScriptRoot/environment.ps1"
$targets = @('sdurws_ird_requirements_test', 'sdurws_ird_requirements_contract_test', 'sdurws_ird_requirements_gui_test', 'sdurws_ird_ui_test', 'sdurws_ird_ui_gui_test')
foreach ($target in $targets) {
    Write-Output "RUN $target"
    & "$auditBin/$target.exe" "--gtest_output=xml:$auditOut/$target.xml" "--ird_report=$auditOut/$target-ird-test-report.json" *> "$auditOut/$target.log"
    $testExit = $LASTEXITCODE
    Get-Content "$auditOut/$target.log" -Tail 9
    Write-Output "EXIT $target=$testExit"
    if ($testExit -ne 0) { exit $testExit }
}
