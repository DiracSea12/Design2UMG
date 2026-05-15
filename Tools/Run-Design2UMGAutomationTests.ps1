param(
    [string]$ProjectPath = "",
    [string]$StartupMap = "",
    [string]$EditorPath = "",
    [string]$LogPath = "",
    [string]$Tests = "Design2UMG",
    [switch]$KeepExistingEditor,
    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "design2umg_verify_lib\Design2UMG.Validation.ps1")

$ProjectPath = Resolve-D2URequiredProjectPath $ProjectPath
$EditorPath = Resolve-D2URequiredEditorPath $EditorPath
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path (Split-Path -Parent $PSScriptRoot) "Saved\Logs\Design2UMG-Automation.log"
}

if ($ValidateOnly) {
    Write-Host "[OK] Design2UMG automation test paths resolved."
    Write-Host "ProjectPath : $ProjectPath"
    Write-Host "EditorPath  : $EditorPath"
    Write-Host "LogPath     : $LogPath"
    exit 0
}

Stop-D2UEditorsUnlessKeeping -KeepExistingEditor:$KeepExistingEditor

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

$editorExitCode = Invoke-D2UEditorAndWait -EditorPath $EditorPath -ArgumentLine $argumentLine

if ($editorExitCode -ne 0) {
    throw "UnrealEditor exited with code $editorExitCode."
}

$logItem = Assert-D2UFreshFile `
    -Path $LogPath `
    -StartTime $startTime `
    -MissingMessage "Automation log not found: $LogPath" `
    -StaleMessageFormat "Automation log is stale. LastWriteTime={0}, StartTime={1}"

$logContent = Get-Content $LogPath -Raw
if ($logContent -notmatch "\*\*\*\* TEST COMPLETE\. EXIT CODE: 0 \*\*\*\*") {
    throw "Automation log does not contain a successful automation exit code."
}

Write-Host "Automation tests succeeded."
Write-Host "Tests       : $Tests"
Write-Host "Log         : $LogPath"
Write-Host "LastWriteTime : $($logItem.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
