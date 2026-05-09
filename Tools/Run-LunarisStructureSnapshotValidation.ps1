param(
    [string]$ProjectPath = "",
    [string]$EditorPath = "",
    [string]$LogPath = "",
    [string]$SourcePath = "",
    [string]$TargetNodeId = "frame-1761929672442",
    [string]$OutputPath = "/Game/Design2UMG/Automation/validation_pencil-lunaris_lunaris-design-system-components",
    [string]$SavedPngPath = "",
    [string]$SourceJsonPath = "",
    [string]$SavedJsonPath = "",
    [string]$ResultJsonPath = "",
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
$ProjectRoot = Split-Path -Parent $ProjectPath
if ([string]::IsNullOrWhiteSpace($SourcePath)) {
    throw "SourcePath is required for Lunaris structure validation. Pass -SourcePath explicitly."
}
if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
    throw "SourcePath not found: $SourcePath"
}
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path $ProjectRoot "Saved\Logs\$([System.IO.Path]::GetFileNameWithoutExtension($ProjectPath)).log"
}
if ([string]::IsNullOrWhiteSpace($SavedPngPath)) {
    $SavedPngPath = Join-Path $ProjectRoot "Saved\Design2UMG\WBP_lunaris__design_system_components.png"
}
if ([string]::IsNullOrWhiteSpace($SourceJsonPath)) {
    $SourceJsonPath = Join-Path $ProjectRoot "Saved\Design2UMG\WBP_lunaris__design_system_components.source-tree.json"
}
if ([string]::IsNullOrWhiteSpace($SavedJsonPath)) {
    $SavedJsonPath = Join-Path $ProjectRoot "Saved\Design2UMG\WBP_lunaris__design_system_components.saved-tree.json"
}
if ([string]::IsNullOrWhiteSpace($ResultJsonPath)) {
    $ResultJsonPath = Join-Path $ProjectRoot "Saved\Design2UMG\WBP_lunaris__design_system_components.structure.result.json"
}

if ($ValidateOnly) {
    Write-Host "[OK] Lunaris structure validation paths resolved."
    Write-Host "ProjectPath    : $ProjectPath"
    Write-Host "EditorPath     : $EditorPath"
    Write-Host "SourcePath     : $SourcePath"
    Write-Host "LogPath        : $LogPath"
    Write-Host "SavedPngPath   : $SavedPngPath"
    Write-Host "SourceJsonPath : $SourceJsonPath"
    Write-Host "SavedJsonPath  : $SavedJsonPath"
    Write-Host "ResultJsonPath : $ResultJsonPath"
    exit 0
}

if (-not $KeepExistingEditor) {
    Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 800
}

$startTime = Get-Date
$savedDir = Split-Path -Parent $SavedPngPath
if (-not [string]::IsNullOrWhiteSpace($savedDir)) {
    New-Item -ItemType Directory -Path $savedDir -Force | Out-Null
}

$resultDir = Split-Path -Parent $ResultJsonPath
if (-not [string]::IsNullOrWhiteSpace($resultDir)) {
    New-Item -ItemType Directory -Path $resultDir -Force | Out-Null
}

$specPath = Join-Path $savedDir "lunaris-structure-validation.spec.json"
$spec = [ordered]@{
    mode = "pen"
    source_path = $SourcePath
    node_id = $TargetNodeId
    output_path = $OutputPath
    render_png_path = $SavedPngPath
    result_json_path = $ResultJsonPath
}
($spec | ConvertTo-Json -Depth 8) | Set-Content -Path $specPath -Encoding utf8BOM

$specArg = ($specPath -replace '\\', '/')
$argumentLine = @(
    ('"{0}"' -f $ProjectPath),
    '-NoSplash',
    '-NoSound',
    '-nop4',
    ('"-ExecCmds=Design2UMG.AutomationRender {0} --quit"' -f $specArg),
    '-log'
) -join ' '

$process = Start-Process -FilePath $EditorPath -ArgumentList $argumentLine -PassThru
Wait-Process -Id $process.Id
$process.Refresh()
$editorExitCode = $process.ExitCode

if (-not (Test-Path $ResultJsonPath)) {
    throw "Structure validation result json not found: $ResultJsonPath (editor exit code $editorExitCode)"
}

$resultItem = Get-Item $ResultJsonPath
if ($resultItem.LastWriteTime -le $startTime) {
    throw "Structure validation result is stale. LastWriteTime=$($resultItem.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')), StartTime=$($startTime.ToString('yyyy-MM-dd HH:mm:ss'))"
}

$result = Get-Content $ResultJsonPath -Raw | ConvertFrom-Json
if (-not $result.success) {
    throw "Structure validation reported failure: $($result.error_message)"
}
if (-not $result.source_structure) {
    throw "Structure validation result missing source_structure."
}
if (-not $result.asset_structure) {
    throw "Structure validation result missing asset_structure."
}

($result.source_structure | ConvertTo-Json -Depth 100) | Set-Content -Path $SourceJsonPath -Encoding utf8BOM
($result.asset_structure | ConvertTo-Json -Depth 100) | Set-Content -Path $SavedJsonPath -Encoding utf8BOM

$requiredFiles = @($SavedPngPath, $SourceJsonPath, $SavedJsonPath)
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
if ($logContent -notmatch "Design2UMG\.AutomationRender succeeded:") {
    throw "Validation log does not contain 'Design2UMG.AutomationRender succeeded:'"
}

if ($editorExitCode -ne 0) {
    Write-Warning "UnrealEditor exited with code $editorExitCode, but fresh structure outputs were generated and AutomationRender reported success."
}

Write-Host "Lunaris structure snapshot validation succeeded."
foreach ($path in $requiredFiles) {
    $item = Get-Item $path
    Write-Host "$path | $($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
}
Write-Host "Generated asset path : $($result.generated_asset_path)"
