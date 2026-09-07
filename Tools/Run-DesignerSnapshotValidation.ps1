param(
    [string]$ProjectPath = "",
    [string]$EditorPath = "",
    [string]$LogPath = "",
    [string]$DesignerPngPath = "",
    [string]$AssetObjectPath = "",
    [string]$ResultJsonPath = "",
    [switch]$KeepExistingEditor,
    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "design2umg_verify_lib\Design2UMG.Validation.ps1")

function Set-D2UJsonContent {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Json
    )

    $encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, $Json, $encoding)
}

$ProjectPath = Resolve-D2URequiredProjectPath $ProjectPath
$EditorPath = Resolve-D2URequiredEditorPath $EditorPath
$ProjectRoot = Split-Path -Parent $ProjectPath
if ([string]::IsNullOrWhiteSpace($AssetObjectPath)) {
    throw "AssetObjectPath is required for designer snapshot validation. Pass -AssetObjectPath explicitly."
}
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path $ProjectRoot "Saved\Logs\$([System.IO.Path]::GetFileNameWithoutExtension($ProjectPath)).log"
}
if ([string]::IsNullOrWhiteSpace($DesignerPngPath)) {
    throw "DesignerPngPath is required for designer snapshot validation. Pass -DesignerPngPath explicitly."
}
if ([string]::IsNullOrWhiteSpace($ResultJsonPath)) {
    throw "ResultJsonPath is required for designer snapshot validation. Pass -ResultJsonPath explicitly."
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

Stop-D2UEditorsUnlessKeeping -KeepExistingEditor:$KeepExistingEditor

$startTime = Get-Date
New-D2UDirectoryForPath $DesignerPngPath
New-D2UDirectoryForPath $ResultJsonPath
$designerDir = Split-Path -Parent $DesignerPngPath

$specPath = Join-Path $designerDir "designer-validation.spec.json"
$spec = [ordered]@{
    mode = "asset"
    source_path = $AssetObjectPath
    render_png_path = $DesignerPngPath
    result_json_path = $ResultJsonPath
}
Set-D2UJsonContent -Path $specPath -Json ($spec | ConvertTo-Json -Depth 8)

$specArg = ($specPath -replace '\\', '/')
$argumentLine = @(
    ('"{0}"' -f $ProjectPath),
    '-NoSplash',
    '-NoSound',
    '-nop4',
    ('"-ExecCmds=Design2UMG.AutomationRender {0} --quit"' -f $specArg),
    '-log'
) -join ' '

$editorExitCode = Invoke-D2UEditorAndWait -EditorPath $EditorPath -ArgumentLine $argumentLine

$resultItem = Assert-D2UFreshFile `
    -Path $ResultJsonPath `
    -StartTime $startTime `
    -MissingMessage "Designer validation result json not found: $ResultJsonPath (editor exit code $editorExitCode)" `
    -StaleMessageFormat "Designer validation result is stale. LastWriteTime={0}, StartTime={1}"

$result = Get-Content $ResultJsonPath -Raw | ConvertFrom-Json
if (-not $result.success) {
    throw "Designer validation reported failure: $($result.error_message)"
}

$designerPng = Assert-D2UFreshFile `
    -Path $DesignerPngPath `
    -StartTime $startTime `
    -MissingMessage "Designer screenshot not found: $DesignerPngPath" `
    -StaleMessageFormat "Designer screenshot is stale. LastWriteTime={0}, StartTime={1}"

Assert-D2UAutomationRenderSuccessLog $LogPath

if ($editorExitCode -ne 0) {
    Write-Warning "UnrealEditor exited with code $editorExitCode, but fresh designer outputs were generated and AutomationRender reported success."
}

Write-Host "Designer snapshot validation succeeded."
Write-Host "Designer PNG : $DesignerPngPath"
Write-Host "LastWriteTime : $($designerPng.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
Write-Host "Asset Path : $($result.generated_asset_path)"
