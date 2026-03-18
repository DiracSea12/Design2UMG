param(
    [string]$ProjectPath = "G:\UE5\FPSRoguelike\FPSRoguelike.uproject",
    [string]$EditorPath = "G:\UE5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe",
    [string]$LogPath = "G:\UE5\FPSRoguelike\Saved\Logs\FPSRoguelike.log",
    [string]$DesignerPngPath = "G:\UE5\FPSRoguelike\Saved\Design2UMG\WBP_lunaris__design_system_components.designer.png",
    [string]$TestName = "Design2UMG.Editor.DesignerSnapshot.LunarisSavedAsset",
    [switch]$KeepExistingEditor
)

$ErrorActionPreference = "Stop"

if (-not $KeepExistingEditor) {
    Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 800
}

$startTime = Get-Date
$argumentLine = @(
    ('"{0}"' -f $ProjectPath),
    '-NoSplash',
    '-NoSound',
    '-nop4',
    ('"-ExecCmds=Automation RunTests {0}; Quit"' -f $TestName),
    '"-TestExit=Automation Test Queue Empty"',
    '-log'
) -join ' '

$process = Start-Process -FilePath $EditorPath -ArgumentList $argumentLine -PassThru
Wait-Process -Id $process.Id
$process.Refresh()

if ($process.ExitCode -ne 0) {
    throw "UnrealEditor exited with code $($process.ExitCode)."
}

if (-not (Test-Path $DesignerPngPath)) {
    throw "Designer screenshot not found: $DesignerPngPath"
}

$designerPng = Get-Item $DesignerPngPath
if ($designerPng.LastWriteTime -le $startTime) {
    throw "Designer screenshot is stale. LastWriteTime=$($designerPng.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')), StartTime=$($startTime.ToString('yyyy-MM-dd HH:mm:ss'))"
}

$logContent = Get-Content $LogPath -Raw
if ($logContent -notmatch "Designer screenshot saved:") {
    throw "Validation log does not contain 'Designer screenshot saved:'"
}

if ($logContent -notmatch "\*\*\*\* TEST COMPLETE\. EXIT CODE: 0 \*\*\*\*") {
    throw "Validation log does not contain a successful automation exit code."
}

Write-Host "Designer snapshot validation succeeded."
Write-Host "Designer PNG : $DesignerPngPath"
Write-Host "LastWriteTime : $($designerPng.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
