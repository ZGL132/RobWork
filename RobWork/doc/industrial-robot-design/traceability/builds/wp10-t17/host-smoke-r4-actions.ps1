# =====================================================================
# host-smoke-r4-actions.ps1 —— UI-T17 冒烟 r4 补充驱动（闭环第 2 版工具）
# 与已入库 host-smoke-r3-driver.ps1 的差异（取证管线迭代，acc-attempt1
# 五版脚本先例）：click+shot 同进程完成（消除跨 pwsh 进程启动延迟），
# 支持 burst 连拍（Draining 瞬时状态行捕获取证）、可配置点击后延时。
# =====================================================================
param(
  [Parameter(Mandatory=$true)][string]$Action,
  [string]$Name='shot',
  [string]$KeysText='',
  [int]$X=0,[int]$Y=0,[int]$WaitMs=500,
  [int]$Burst=4,[int]$BurstGapMs=350
)
$ErrorActionPreference='Continue'
$shots='C:/Users/zgl18/AppData/Local/Temp/wp10-t17-smoke-r3/shots'
$pidFile='C:/Users/zgl18/AppData/Local/Temp/wp10-t17-smoke-r3/host.pid'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName Microsoft.VisualBasic
if(-not ('IrdSmokeNative' -as [type])){
  Add-Type -Namespace IrdSmoke -Name Native -MemberDefinition @"
[DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
[DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);
"@
}
function Get-HostProcess{
  if(-not (Test-Path $pidFile)){return $null}
  $id=Get-Content $pidFile -ErrorAction SilentlyContinue
  if(-not $id){return $null}
  return Get-Process -Id $id -ErrorAction SilentlyContinue
}
function Shot([string]$name,[int]$preMs=0){
  if($preMs -gt 0){Start-Sleep -Milliseconds $preMs}
  $b=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp=New-Object System.Drawing.Bitmap $b.Width,$b.Height
  $g=[System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size)
  $bmp.Save("$shots/$name.png",[System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose();$bmp.Dispose()
  Write-Output "SHOT $name"
}
switch($Action){
  'clickshot'{
    [IrdSmoke.Native]::SetCursorPos($X,$Y)|Out-Null
    Start-Sleep -Milliseconds 150
    [IrdSmoke.Native]::mouse_event(0x0002,0,0,0,[UIntPtr]::Zero)
    [IrdSmoke.Native]::mouse_event(0x0004,0,0,0,[UIntPtr]::Zero)
    Write-Output "CLICK $X,$Y"
    Shot $Name $WaitMs
  }
  'clickburst'{
    [IrdSmoke.Native]::SetCursorPos($X,$Y)|Out-Null
    Start-Sleep -Milliseconds 150
    [IrdSmoke.Native]::mouse_event(0x0002,0,0,0,[UIntPtr]::Zero)
    [IrdSmoke.Native]::mouse_event(0x0004,0,0,0,[UIntPtr]::Zero)
    Write-Output "CLICK $X,$Y burst=$Burst gap=${BurstGapMs}ms"
    for($i=1;$i -le $Burst;$i++){ Shot ("$Name-b$$i") $BurstGapMs }
  }
  'keys'{
    $p=Get-HostProcess
    if($null -eq $p){Write-Output 'KEYS-FAIL no-host';exit 1}
    [Microsoft.VisualBasic.Interaction]::AppActivate($p.Id)|Out-Null
    Start-Sleep -Milliseconds 150
    [System.Windows.Forms.SendKeys]::SendWait($KeysText)
    Start-Sleep -Milliseconds $WaitMs
    Write-Output "KEYS '$KeysText'"
  }
  'shot'{ Shot $Name $WaitMs }
  default{ Write-Output "UNKNOWN $Action"; exit 2 }
}
