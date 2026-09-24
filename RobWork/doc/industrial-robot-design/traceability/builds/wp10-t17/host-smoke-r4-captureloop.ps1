# 连拍取证循环：每 0.8s 全屏一帧，上限 900 帧（约 12 分钟自动停）。
# 用途：所有者人工操作宿主 GUI 期间的连续取证（瞬时状态行捕获承载面）。
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$shots='C:/Users/zgl18/AppData/Local/Temp/wp10-t17-smoke-r3/shots'
for($i=0;$i -lt 900;$i++){
  $n='{0:d4}' -f $i
  $b=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp=New-Object System.Drawing.Bitmap $b.Width,$b.Height
  $g=[System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size)
  $bmp.Save("$shots/loop-$n.png",[System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose();$bmp.Dispose()
  Start-Sleep -Milliseconds 450
}
Write-Output 'CAPTURE-LOOP-END'
