Push-Location "$PSScriptRoot\..\output\release\simulator\brain-host\Lilium"

Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
$proc = Start-Process -FilePath ".\AppMain.exe" -PassThru
Start-Sleep -Seconds 2

$wshell = New-Object -ComObject Wscript.Shell
$success = $wshell.AppActivate($proc.Id)
Write-Host "AppActivate: $success"
Start-Sleep -Milliseconds 500

# 矢印キー送信
$wshell.SendKeys("{DOWN}")
Start-Sleep -Milliseconds 500
$wshell.SendKeys("{DOWN}")
Start-Sleep -Milliseconds 500
$wshell.SendKeys("{RIGHT}")
Start-Sleep -Milliseconds 500

Start-Sleep -Seconds 1
Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
Pop-Location
