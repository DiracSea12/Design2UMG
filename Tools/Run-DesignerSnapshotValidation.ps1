param(
    [string]$ProjectPath = "",
    [string]$EditorPath = "",
    [string]$LogPath = "",
    [string]$DesignerPngPath = "",
    [string]$AssetObjectPath = "/Game/Design2UMG/PenConvert_Output/WBP_lunaris__design_system_components.WBP_lunaris__design_system_components",
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
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path $ProjectRoot "Saved\Logs\$([System.IO.Path]::GetFileNameWithoutExtension($ProjectPath)).log"
}
if ([string]::IsNullOrWhiteSpace($DesignerPngPath)) {
    $DesignerPngPath = Join-Path $ProjectRoot "Saved\Design2UMG\WBP_lunaris__design_system_components.designer.png"
}
if ([string]::IsNullOrWhiteSpace($ResultJsonPath)) {
    $ResultJsonPath = Join-Path $ProjectRoot "Saved\Design2UMG\WBP_lunaris__design_system_components.designer.result.json"
}

if ($ValidateOnly) {
    Write-Host "[OK] Designer snapshot validation paths resolved."
    Write-Host "ProjectPath      : $ProjectPath"
    Write-Host "EditorPath       : $EditorPath"
    Write-Host "LogPath          : $LogPath"
    Write-Host "DesignerPngPath  : $DesignerPngPath"
    Write-Host "ResultJsonPath   : $ResultJsonPath"
    exit 0
}

if (-not $KeepExistingEditor) {
    Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 800
}

$startTime = Get-Date
$designerDir = Split-Path -Parent $DesignerPngPath
if (-not [string]::IsNullOrWhiteSpace($designerDir)) {
    New-Item -ItemType Directory -Path $designerDir -Force | Out-Null
}

$resultDir = Split-Path -Parent $ResultJsonPath
if (-not [string]::IsNullOrWhiteSpace($resultDir)) {
    New-Item -ItemType Directory -Path $resultDir -Force | Out-Null
}

$specPath = Join-Path $designerDir "lunaris-designer-validation.spec.json"
$spec = [ordered]@{
    mode = "asset"
    source_path = $AssetObjectPath
    render_png_path = $DesignerPngPath
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
    throw "Designer validation result json not found: $ResultJsonPath (editor exit code $editorExitCode)"
}

$resultItem = Get-Item $ResultJsonPath
if ($resultItem.LastWriteTime -le $startTime) {
    throw "Designer validation result is stale. LastWriteTime=$($resultItem.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')), StartTime=$($startTime.ToString('yyyy-MM-dd HH:mm:ss'))"
}

$result = Get-Content $ResultJsonPath -Raw | ConvertFrom-Json
if (-not $result.success) {
    throw "Designer validation reported failure: $($result.error_message)"
}

if (-not (Test-Path $DesignerPngPath)) {
    throw "Designer screenshot not found: $DesignerPngPath"
}

$designerPng = Get-Item $DesignerPngPath
if ($designerPng.LastWriteTime -le $startTime) {
    throw "Designer screenshot is stale. LastWriteTime=$($designerPng.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')), StartTime=$($startTime.ToString('yyyy-MM-dd HH:mm:ss'))"
}

$logContent = Get-Content $LogPath -Raw
if ($logContent -notmatch "Design2UMG\.AutomationRender succeeded:") {
    throw "Validation log does not contain 'Design2UMG.AutomationRender succeeded:'"
}

if ($editorExitCode -ne 0) {
    Write-Warning "UnrealEditor exited with code $editorExitCode, but fresh designer outputs were generated and AutomationRender reported success."
}

Write-Host "Designer snapshot validation succeeded."
Write-Host "Designer PNG : $DesignerPngPath"
Write-Host "LastWriteTime : $($designerPng.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
Write-Host "Asset Path : $($result.generated_asset_path)"
