param(
    [string]$ProjectPath = "",
    [string]$EditorPath = "",
    [string]$LogPath = "",
    [string]$SourcePath = "",
    [string]$TargetNodeId = "",
    [string]$OutputPath = "",
    [string]$SavedPngPath = "",
    [string]$SourceJsonPath = "",
    [string]$SavedJsonPath = "",
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
if ([string]::IsNullOrWhiteSpace($SourcePath)) {
    throw "SourcePath is required for structure snapshot validation. Pass -SourcePath explicitly."
}
if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
    throw "SourcePath not found: $SourcePath"
}
if ([string]::IsNullOrWhiteSpace($TargetNodeId)) {
    throw "TargetNodeId is required for structure snapshot validation. Pass -TargetNodeId explicitly."
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    throw "OutputPath is required for structure snapshot validation. Pass -OutputPath explicitly."
}
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path $ProjectRoot "Saved\Logs\$([System.IO.Path]::GetFileNameWithoutExtension($ProjectPath)).log"
}
if ([string]::IsNullOrWhiteSpace($SavedPngPath)) {
    throw "SavedPngPath is required for structure snapshot validation. Pass -SavedPngPath explicitly."
}
if ([string]::IsNullOrWhiteSpace($SourceJsonPath)) {
    throw "SourceJsonPath is required for structure snapshot validation. Pass -SourceJsonPath explicitly."
}
if ([string]::IsNullOrWhiteSpace($SavedJsonPath)) {
    throw "SavedJsonPath is required for structure snapshot validation. Pass -SavedJsonPath explicitly."
}
if ([string]::IsNullOrWhiteSpace($ResultJsonPath)) {
    throw "ResultJsonPath is required for structure snapshot validation. Pass -ResultJsonPath explicitly."
}

if ($ValidateOnly) {
    Write-Host "[OK] Structure snapshot validation paths resolved."
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

Assert-D2UEditorIsolation -KeepExistingEditor:$KeepExistingEditor

$startTime = Get-Date
New-D2UDirectoryForPath $SavedPngPath
New-D2UDirectoryForPath $ResultJsonPath
$savedDir = Split-Path -Parent $SavedPngPath

$specPath = Join-Path $savedDir "structure-validation.spec.json"
$spec = [ordered]@{
    mode = "pen"
    source_path = $SourcePath
    node_id = $TargetNodeId
    output_path = $OutputPath
    render_png_path = $SavedPngPath
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
    -MissingMessage "Structure validation result json not found: $ResultJsonPath (editor exit code $editorExitCode)" `
    -StaleMessageFormat "Structure validation result is stale. LastWriteTime={0}, StartTime={1}"

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

Set-D2UJsonContent -Path $SourceJsonPath -Json ($result.source_structure | ConvertTo-Json -Depth 100)
Set-D2UJsonContent -Path $SavedJsonPath -Json ($result.asset_structure | ConvertTo-Json -Depth 100)

$requiredFiles = @($SavedPngPath, $SourceJsonPath, $SavedJsonPath)
foreach ($path in $requiredFiles) {
    $null = Assert-D2UFreshFile `
        -Path $path `
        -StartTime $startTime `
        -MissingMessage "Expected file not found: $path" `
        -StaleMessageFormat "Stale output: {2} | LastWriteTime={0} | StartTime={1}"
}

Assert-D2UAutomationRenderSuccessLog $LogPath

if ($editorExitCode -ne 0) {
    Write-Warning "UnrealEditor exited with code $editorExitCode, but fresh structure outputs were generated and AutomationRender reported success."
}

Write-Host "Structure snapshot validation succeeded."
foreach ($path in $requiredFiles) {
    $item = Get-Item $path
    Write-Host "$path | $($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
}
Write-Host "Generated asset path : $($result.generated_asset_path)"
