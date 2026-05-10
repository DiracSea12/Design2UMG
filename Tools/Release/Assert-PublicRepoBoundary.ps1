# Requires: PowerShell 5.1+
param(
    [string]$RepoRoot = (Get-Location).Path,
    [ValidateSet('SourceWorkspace', 'Package', 'ExportWorkspace')]
    [string]$Mode = 'SourceWorkspace',
    [switch]$CheckExportWorkspaceLayout,
    [switch]$CheckPackageWorkspaceLayout
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

function Get-RelativeBoundaryPath {
    param(
        [string]$BasePath,
        [string]$Path
    )

    $baseFull = [IO.Path]::GetFullPath($BasePath).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $pathFull = [IO.Path]::GetFullPath($Path)
    if ($pathFull.StartsWith($baseFull, [StringComparison]::OrdinalIgnoreCase)) {
        return $pathFull.Substring($baseFull.Length)
    }

    return $pathFull
}

function Test-BoundaryPathPattern {
    param(
        [string]$RelativePath,
        [string]$Pattern
    )

    $normalized = $RelativePath.Replace('\', '/')
    $normalizedPattern = $Pattern.Replace('\', '/')

    if ($normalized -eq $normalizedPattern -or $normalized -like "$normalizedPattern/*" -or $normalized -like $normalizedPattern) {
        return $true
    }

    if ($normalized -like "*/$normalizedPattern" -or $normalized -like "*/$normalizedPattern/*") {
        return $true
    }

    if ($normalizedPattern -notlike '*/*') {
        foreach ($segment in ($normalized -split '/')) {
            if ($segment -like $normalizedPattern) {
                return $true
            }
        }
    }

    return $false
}

$resolvedRoot = (Resolve-Path $RepoRoot).Path
Set-Location $resolvedRoot

if ($CheckExportWorkspaceLayout) {
    $Mode = 'ExportWorkspace'
}
if ($CheckPackageWorkspaceLayout) {
    $Mode = 'Package'
}

$commonForbiddenRootNames = @(
    '.claude',
    '.codex',
    '.cowork-temp',
    '.vs',
    'DerivedDataCache',
    'DDC',
    '.cache',
    '.pytest_cache',
    'openspec',
    'CLAUDE.md',
    'AGENTS.md'
)

$packageForbiddenRootNames = @(
    'Intermediate',
    'Saved'
)

$exportForbiddenRootNames = @(
    'Binaries',
    'Intermediate',
    'Saved'
)

$forbiddenRootReviewPatterns = @(
    '*REVIEW*.md',
    '*Review*.md',
    'code_review_report*.md',
    'OPEN_SOURCE_PROJECT_REVIEW*.md',
    'OPEN_SOURCE_READINESS_REVIEW*.md',
    'CODEX_*REVIEW*.md'
)

$commonForbiddenRecursivePathPatterns = @(
    'openspec',
    'Saved/Design2UMG/Automation',
    'Saved/Design2UMG/RejectedAutomation',
    'Intermediate',
    'DerivedDataCache',
    'DDC',
    '.cache',
    '.pytest_cache',
    'Design2UMG_Host_*',
    'HostProject.uproject'
)

$exportForbiddenRecursivePathPatterns = @(
    'Binaries'
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

if ($Mode -in @('Package', 'ExportWorkspace')) {
    $forbiddenRootNames = @($commonForbiddenRootNames)
    $forbiddenRecursivePathPatterns = @($commonForbiddenRecursivePathPatterns)

    if ($Mode -eq 'Package') {
        $forbiddenRootNames += $packageForbiddenRootNames
    }
    if ($Mode -eq 'ExportWorkspace') {
        $forbiddenRootNames += $exportForbiddenRootNames
        $forbiddenRecursivePathPatterns += $exportForbiddenRecursivePathPatterns
    }

    foreach ($entry in ($forbiddenRootNames | Sort-Object -Unique)) {
        if (Test-Path -LiteralPath $entry) {
            $failures += "Forbidden $Mode content present at root: $entry"
        }
    }

    foreach ($pattern in $forbiddenRootReviewPatterns) {
        $matches = Get-ChildItem -LiteralPath $resolvedRoot -File -Filter $pattern -ErrorAction SilentlyContinue
        foreach ($match in $matches) {
            $failures += "Forbidden root review report present in $Mode workspace: $($match.Name)"
        }
    }

    foreach ($pattern in ($forbiddenRecursivePathPatterns | Sort-Object -Unique)) {
        $matches = Get-ChildItem -LiteralPath $resolvedRoot -Recurse -Force -ErrorAction SilentlyContinue |
            Where-Object {
                $relative = Get-RelativeBoundaryPath -BasePath $resolvedRoot -Path $_.FullName
                Test-BoundaryPathPattern -RelativePath $relative -Pattern $pattern
            } |
            Select-Object -First 10

        foreach ($match in $matches) {
            $relative = Get-RelativeBoundaryPath -BasePath $resolvedRoot -Path $match.FullName
            $failures += "Forbidden $Mode path matched '$pattern': $relative"
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

if ($Mode -eq 'SourceWorkspace') {
    $trackedPublicSurfaceFiles = @()
    foreach ($file in $publicSurfaceFiles) {
        $relative = Get-RelativeBoundaryPath -BasePath $resolvedRoot -Path $file.FullName
        git -C $resolvedRoot ls-files --error-unmatch -- $relative *> $null
        if ($LASTEXITCODE -eq 0) {
            $trackedPublicSurfaceFiles += $file
        }
    }
    $publicSurfaceFiles = $trackedPublicSurfaceFiles
}

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

$failures = @($failures | Sort-Object -Unique)

if ($failures.Count -eq 0) {
    Write-Ok "Public repo boundary checks passed for mode: $Mode."
    exit 0
}

Write-Fail "Public repo boundary checks failed."
foreach ($failure in $failures) {
    Write-Host "  - $failure"
}
exit 2
