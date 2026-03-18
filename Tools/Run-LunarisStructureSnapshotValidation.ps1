param(
    [string]$ProjectPath = "G:\UE5\FPSRoguelike\FPSRoguelike.uproject",
    [string]$EditorPath = "G:\UE5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe",
    [string]$LogPath = "G:\UE5\FPSRoguelike\Saved\Logs\FPSRoguelike.log",
    [string]$UassetPath = "G:\UE5\FPSRoguelike\Content\Design2UMG\PenConvert_Output\WBP_lunaris__design_system_components.uasset",
    [string]$SavedPngPath = "G:\UE5\FPSRoguelike\Saved\Design2UMG\WBP_lunaris__design_system_components.png",
    [string]$SourceJsonPath = "G:\UE5\FPSRoguelike\Saved\Design2UMG\WBP_lunaris__design_system_components.source-tree.json",
    [string]$SavedJsonPath = "G:\UE5\FPSRoguelike\Saved\Design2UMG\WBP_lunaris__design_system_components.saved-tree.json",
    [string]$TestName = "Design2UMG.Editor.StructureSnapshot.LunarisSavedAsset",
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
    throw "UnrealEditor-Cmd exited with code $($process.ExitCode)."
}

$requiredFiles = @($UassetPath, $SavedPngPath, $SourceJsonPath, $SavedJsonPath)
foreach ($path in $requiredFiles) {
    if (-not (Test-Path $path)) {
        throw "Expected file not found: $path"
    }

    $item = Get-Item $path
    if ($item.LastWriteTime -le $startTime) {
        throw "Stale output: $path | LastWriteTime=$($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')) | StartTime=$($startTime.ToString('yyyy-MM-dd HH:mm:ss'))"
    }
}

$logContent = Get-Content $LogPath -Raw
if ($logContent -notmatch "Source structure snapshot saved:") {
    throw "Validation log does not contain 'Source structure snapshot saved:'"
}

if ($logContent -notmatch "Saved asset structure snapshot saved:") {
    throw "Validation log does not contain 'Saved asset structure snapshot saved:'"
}

if ($logContent -notmatch "\*\*\*\* TEST COMPLETE\. EXIT CODE: 0 \*\*\*\*") {
    throw "Validation log does not contain a successful automation exit code."
}

Write-Host "Lunaris structure snapshot validation succeeded."
foreach ($path in $requiredFiles) {
    $item = Get-Item $path
    Write-Host "$path | $($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
}
