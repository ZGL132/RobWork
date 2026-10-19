# UX-14/WP-14-T12：验证构建树、安装树与来源同字节，且资源目标能修复陈旧副本。
$ErrorActionPreference='Stop'
$manualRoot='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
$manualSource="$manualRoot/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/share/user-manual/index.html"
$manualBuilt="$manualRoot/build/RobWorkStudio/bin/share/ird/user-manual/index.html"
$manualInstalled="$manualRoot/build/requirements-close-20261019/install/share/ird/user-manual/index.html"
$expected=(Get-FileHash -LiteralPath $manualSource -Algorithm SHA256).Hash
foreach($manualPath in @($manualBuilt,$manualInstalled)) {
    if((Get-FileHash -LiteralPath $manualPath -Algorithm SHA256).Hash -ne $expected) {
        throw "手册部署字节不一致：$manualPath"
    }
    "PASS same-bytes $manualPath SHA256=$expected"
}
try {
    # 仅改本任务构建产物，不动来源/安装包/用户文件；finally兜底恢复。
    Set-Content -LiteralPath $manualBuilt -Value 'stale deployment fixture' -Encoding utf8
    if((Get-FileHash -LiteralPath $manualBuilt -Algorithm SHA256).Hash -eq $expected) {
        throw '未能构造陈旧副本反例'
    }
    'PASS stale-fixture-differs'
    & cmake --build "$manualRoot/build" --config Release --target sdurws_ird_ui_manual
    if($LASTEXITCODE -ne 0){throw '手册资源目标构建失败'}
    if((Get-FileHash -LiteralPath $manualBuilt -Algorithm SHA256).Hash -ne $expected) {
        throw '手册资源目标没有刷新陈旧副本'
    }
    'PASS resource-target-restores-source-bytes'
} finally {
    if((Get-FileHash -LiteralPath $manualBuilt -Algorithm SHA256).Hash -ne $expected) {
        Copy-Item -LiteralPath $manualSource -Destination $manualBuilt
    }
}
