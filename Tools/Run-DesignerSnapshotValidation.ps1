param(
    [string]$ProjectPath = "G:\UE5\FPSRoguelike\FPSRoguelike.uproject",
    [string]$EditorPath = "G:\UE5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe",
    [string]$LogPath = "G:\UE5\FPSRoguelike\Saved\Logs\FPSRoguelike.log",
    [string]$DesignerPngPath = "G:\UE5\FPSRoguelike\Saved\Design2UMG\WBP_lunaris__design_system_components.designer.png",
    [string]$AssetObjectPath = "/Game/Design2UMG/PenConvert_Output/WBP_lunaris__design_system_components.WBP_lunaris__design_system_components",
    [string]$ResultJsonPath = "G:\UE5\FPSRoguelike\Saved\Design2UMG\WBP_lunaris__design_system_components.designer.result.json",
    [switch]$KeepExistingEditor
)

$ErrorActionPreference = "Stop"

function Resolve-RenderSize {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ImagePath
    )

    if (-not (Test-Path $ImagePath)) {
        return @{ Width = 1920; Height = 1080 }
    }

    Add-Type -AssemblyName System.Drawing
    $image = $null
    try {
        $image = [System.Drawing.Image]::FromFile($ImagePath)
        return @{ Width = [int]$image.Width; Height = [int]$image.Height }
    }
    finally {
        if ($null -ne $image) {
            $image.Dispose()
        }
    }
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
$renderSize = Resolve-RenderSize -ImagePath $DesignerPngPath
$spec = [ordered]@{
    mode = "asset"
    source_path = $AssetObjectPath
    render_png_path = $DesignerPngPath
    result_json_path = $ResultJsonPath
    render_width = $renderSize.Width
    render_height = $renderSize.Height
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
