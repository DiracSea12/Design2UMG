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

function Resolve-RequiredProjectPath {
    param([string]$Value)
    $candidate = $Value
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        $candidate = $env:DESIGN2UMG_RUNTIME_PROJECT
    }
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        throw "Runtime project path is required. Pass -ProjectPath or set DESIGN2UMG_RUNTIME_PROJECT. HostProject fallback is not supported."
    }
    $resolved = [System.IO.Path]::GetFullPath($candidate)
    $lower = $resolved.ToLowerInvariant()
    $hostProjectFileName = ("hostproject" + ".uproject")
    $generatedProjectMarker = ("design2umg" + "_host_")
    if ($lower.EndsWith($hostProjectFileName) -or $lower.Contains("hostproject") -or $lower.Contains($generatedProjectMarker)) {
        throw "Refusing temporary or generated runtime project: $resolved"
    }
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Runtime project not found: $resolved"
    }
    return $resolved
}

function Resolve-RequiredEditorPath {
    param([string]$Value)
    $candidate = $Value
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        $candidate = $env:DESIGN2UMG_UNREAL_EDITOR
    }
    if ([string]::IsNullOrWhiteSpace($candidate) -and -not [string]::IsNullOrWhiteSpace($env:UE_ROOT)) {
        $candidate = Join-Path $env:UE_ROOT "Engine\Binaries\Win64\UnrealEditor.exe"
    }
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        throw "UnrealEditor path is required. Pass -EditorPath, set DESIGN2UMG_UNREAL_EDITOR, or set UE_ROOT."
    }
    $resolved = [System.IO.Path]::GetFullPath($candidate)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "UnrealEditor not found: $resolved"
    }
    return $resolved
}

$ProjectPath = Resolve-RequiredProjectPath $ProjectPath
$EditorPath = Resolve-RequiredEditorPath $EditorPath
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
