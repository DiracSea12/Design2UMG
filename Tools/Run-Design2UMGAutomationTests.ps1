param(
    [string]$ProjectPath = "G:\UE5\FPSRoguelike\FPSRoguelike.uproject",
    [string]$StartupMap = "",
    [string]$EditorPath = "G:\UE5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe",
    [string]$LogPath = "G:\UE5\UnrealEngine\Engine\Plugins\Marketplace\Design2UMG\Saved\Logs\Design2UMG-Automation.log",
    [string]$Tests = "Design2UMG",
    [switch]$KeepExistingEditor
)

$ErrorActionPreference = "Stop"

if (-not $KeepExistingEditor) {
    Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 800
}

# UE automation prefix filtering matches "Design2UMG", not "Design2UMG.*".
# Normalize the common wildcard form so full-suite runs do not fail with "No automation tests matched".
if ($Tests -match '^(?<prefix>.+)\.\*$') {
    $Tests = $Matches['prefix']
}

$logDir = Split-Path -Parent $LogPath
if (-not [string]::IsNullOrWhiteSpace($logDir)) {
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
}

Remove-Item -Path $LogPath -Force -ErrorAction SilentlyContinue
$startTime = Get-Date

$arguments = New-Object System.Collections.Generic.List[string]
$arguments.Add(('"{0}"' -f $ProjectPath))
if (-not [string]::IsNullOrWhiteSpace($StartupMap)) {
    $arguments.Add(('"{0}"' -f $StartupMap))
}
$arguments.Add('-NoSplash')
$arguments.Add('-NoSound')
$arguments.Add('-nop4')
$arguments.Add('-nocefaccelpaint')
$arguments.Add(('"-ExecCmds=Automation RunTests {0}; Quit"' -f $Tests))
$arguments.Add('"-TestExit=Automation Test Queue Empty"')
$arguments.Add(('"-AbsLog={0}"' -f $LogPath))
$argumentLine = $arguments -join ' '

$process = Start-Process -FilePath $EditorPath -ArgumentList $argumentLine -PassThru
Wait-Process -Id $process.Id
$process.Refresh()

if ($process.ExitCode -ne 0) {
    throw "UnrealEditor exited with code $($process.ExitCode)."
}

if (-not (Test-Path $LogPath)) {
    throw "Automation log not found: $LogPath"
}

$logItem = Get-Item $LogPath
if ($logItem.LastWriteTime -le $startTime) {
    throw "Automation log is stale. LastWriteTime=$($logItem.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')), StartTime=$($startTime.ToString('yyyy-MM-dd HH:mm:ss'))"
}

$logContent = Get-Content $LogPath -Raw
if ($logContent -notmatch "\*\*\*\* TEST COMPLETE\. EXIT CODE: 0 \*\*\*\*") {
    throw "Automation log does not contain a successful automation exit code."
}

Write-Host "Automation tests succeeded."
Write-Host "Tests       : $Tests"
Write-Host "Log         : $LogPath"
Write-Host "LastWriteTime : $($logItem.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
