# Requires: PowerShell 5.1+
param(
    [string]$RepoRoot = (Get-Location).Path,
    [switch]$CheckExportWorkspaceLayout
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Fail {
	param([string]$Message)
	Write-Host "[FAIL] $Message" -ForegroundColor Red
}

function Write-Ok {
	param([string]$Message)
	Write-Host "[OK] $Message" -ForegroundColor Green
}

$resolvedRoot = (Resolve-Path $RepoRoot).Path
Set-Location $resolvedRoot

$forbiddenTopLevel = @(
    '.claude',
    '.codex',
    '.vs',
    'Binaries',
    'Intermediate',
    'Saved',
    'CLAUDE.md',
    'AGENTS.md'
)

$publicSurfaceGlobs = @(
    'README.md',
    'KNOWN_LIMITATIONS.md',
    'CONTRIBUTING.md',
    'SECURITY.md',
    'CHANGELOG.md',
    'Design2UMG.uplugin',
    'Docs/ValidationProtocol.md',
    'Docs/*.md',
    'Docs/AdmissibleRegressionInvestigationMethod.md',
    'Docs/Design2UMG_TechnicalDesign.md',
    'Docs/PrototypeFlowPlan.md',
    'Docs/ReleaseBlocker_FinalScope_20260329.md',
    'Docs/ValidationGovernance/*.md',
    'Tools/Release/*.md',
    'Tools/Release/*.ps1',
    'Tools/Run-*.ps1',
    'Tools/design2umg_auto_verify.py',
    'Tools/design2umg_auto_verify_batch.py',
    'Tools/*.json',
    'Tools/FigmaExporter/README.md',
    '.github/workflows/*.yml',
    '.github/workflows/*.yaml',
    '.github/*.yml',
    '.github/*.yaml',
    '.github/*.md',
    '.github/ISSUE_TEMPLATE/*.md'
)

$publicSurfaceRecursiveRoots = @(
    @{
        Path = 'Config'
        Include = @('*.ini')
    },
    @{
        Path = 'Source'
        Include = @('*.h', '*.hpp', '*.cpp', '*.c', '*.cs')
    }
)

$forbiddenPathPatterns = @(
    'G:\UE5\',
    'C:\Users\Administrator',
    'Design2UMG_Host_',
    'HostProject.uproject'
)

$selfPath = (Resolve-Path -LiteralPath $PSCommandPath).Path
$failures = @()

if ($CheckExportWorkspaceLayout) {
    foreach ($entry in $forbiddenTopLevel) {
        if (Test-Path -LiteralPath $entry) {
            $failures += "Forbidden top-level export content present: $entry"
        }
    }
}

$publicSurfaceFiles = @()
foreach ($glob in $publicSurfaceGlobs) {
    $publicSurfaceFiles += Get-ChildItem -Path $glob -File -ErrorAction SilentlyContinue
}
foreach ($root in $publicSurfaceRecursiveRoots) {
    if (Test-Path -LiteralPath $root.Path) {
        $publicSurfaceFiles += Get-ChildItem -LiteralPath $root.Path -Recurse -File -Include $root.Include -ErrorAction SilentlyContinue
    }
}
$publicSurfaceFiles = $publicSurfaceFiles | Sort-Object FullName -Unique

foreach ($file in $publicSurfaceFiles) {
    if ((Resolve-Path -LiteralPath $file.FullName).Path -eq $selfPath) {
        continue
    }
    $content = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($pattern in $forbiddenPathPatterns) {
        if ($content -like "*$pattern*") {
            $failures += "Forbidden path leak '$pattern' found in $($file.FullName)"
        }
    }
}

if ($failures.Count -eq 0) {
    Write-Ok "Public repo boundary checks passed."
    exit 0
}

Write-Fail "Public repo boundary checks failed."
foreach ($failure in $failures) {
    Write-Host "  - $failure"
}
exit 2
