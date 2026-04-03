# Requires: PowerShell 5.1+
param(
    [string]$RepoRoot = (Get-Location).Path
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

$publicDocGlobs = @(
    'README.md',
    'KNOWN_LIMITATIONS.md',
    'CONTRIBUTING.md',
    'SECURITY.md',
    'Docs/ValidationProtocol.md',
    'Docs/AdmissibleRegressionInvestigationMethod.md',
    'Docs/Design2UMG_TechnicalDesign.md',
    'Docs/PrototypeFlowPlan.md',
    'Docs/ReleaseBlocker_FinalScope_20260329.md',
    'Docs/ValidationGovernance/*.md',
    'Tools/Release/*.md'
)

$forbiddenPathPatterns = @(
    'G:\UE5\',
    'C:\Users\Administrator',
    'Design2UMG_Host_',
    'HostProject.uproject'
)

$failures = @()

foreach ($entry in $forbiddenTopLevel) {
    if (Test-Path -LiteralPath $entry) {
        $failures += "Forbidden top-level export content present: $entry"
    }
}

$publicDocs = @()
foreach ($glob in $publicDocGlobs) {
    $publicDocs += Get-ChildItem -Path $glob -File -ErrorAction SilentlyContinue
}
$publicDocs = $publicDocs | Sort-Object FullName -Unique

foreach ($doc in $publicDocs) {
    $content = Get-Content -LiteralPath $doc.FullName -Raw
    foreach ($pattern in $forbiddenPathPatterns) {
        if ($content -like "*$pattern*") {
            $failures += "Forbidden path leak '$pattern' found in $($doc.FullName)"
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
