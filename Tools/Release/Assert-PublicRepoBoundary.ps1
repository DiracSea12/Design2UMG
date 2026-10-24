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
    'Resources/PencilFontWorker',
    'Tools/BuildPencilFontWorker.py',
    'Tools/PencilFontWorker',
    'Fonts',
    'PenDesigns',
    'FigmaExports',
    'CODE_QUALITY_OPTIMIZATION_PLAN.md',
    'Saved'
)

$exportForbiddenRootNames = @(
    'Resources/PencilFontWorker',
    'Fonts',
    'PenDesigns',
    'FigmaExports',
    'CODE_QUALITY_OPTIMIZATION_PLAN.md',
    'Binaries',
    'Intermediate',
    'Saved'
)

$forbiddenRootReviewPatterns = @(
    '*REVIEW*.md',
    '*Review*.md',
    '*review*.md',
    'CODE_REVIEW*.md',
    'code_review_report*.md',
    'code_review*.md',
    'design2umg_code_review*.md',
    'OPEN_SOURCE_PROJECT_REVIEW*.md',
    'OPEN_SOURCE_READINESS_REVIEW*.md',
    'CODEX_*REVIEW*.md'
)

# Local-only files that must never be tracked (case-insensitive regex on repo-relative paths).
$sourceForbiddenTrackedRegexes = @(
    '^Resources/PencilFontWorker/',
    '^Saved/',
    '^\.cowork-temp/',
    '^\.claude/',
    '^\.codex/',
    '^\.vs/',
    '^openspec/',
    '^progress\.txt$',
    '^ocr_result\.txt$',
    '^CLAUDE\.md$',
    '^AGENTS\.md$',
    '^Design2UMG图片/',
    '^(capture_editor_screenshot|capture_umg_screenshot|compare_coordinates|screenshot|show_expected_coords|test_import|verify_coords|verify_design_size)\.py$',
    '^screenshot\.ps1$',
    '^editor_screenshot\.png$'
)

$forbiddenInternalReviewPathPatterns = @(
    'Docs/ReviewActionPlan.md',
    'Docs/ReviewRemainingWork.md'
)

$commonForbiddenRecursivePathPatterns = @(
    'openspec',
    'Saved/Design2UMG/Automation',
    'Saved/Design2UMG/RejectedAutomation',
    'DerivedDataCache',
    'DDC',
    '.cache',
    '.pytest_cache',
    'Design2UMG_Host_*',
    'HostProject.uproject'
)

$exportForbiddenRecursivePathPatterns = @(
    'Binaries',
    'Intermediate'
)

$publicSurfaceGlobs = @(
    'README.md',
    'README.en.md',
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
    'Tools/design2umg_auto_verify_suite.py',
    'Tools/*.json',
    'Resources/FontToolText.json',
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
    'G:/UE5/',
    'C:\Users\Administrator',
    'Design2UMG_Host_',
    'HostProject.uproject'
)

$selfPath = (Resolve-Path -LiteralPath $PSCommandPath).Path
$failures = @()

if ($Mode -in @('Package', 'ExportWorkspace')) {
    foreach ($required in @(
        'LICENSE', 'README.md', 'README.en.md', 'CHANGELOG.md',
        'KNOWN_LIMITATIONS.md', 'Design2UMG.uplugin',
        'Tools/FigmaExporter/manifest.json', 'Tools/FigmaExporter/code.js',
        'Tools/FigmaExporter/ui.html', 'Tools/FigmaExporter/README.md',
        'Examples/SimpleCard.pen', 'Resources/PencilFontWorkerDownload.json', 'Resources/FontToolText.json'
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $resolvedRoot $required) -PathType Leaf)) {
            $failures += "Required public file missing: $required"
        }
    }
}

if ($Mode -eq 'SourceWorkspace') {
    $trackedPaths = @(git -C $resolvedRoot -c core.quotepath=false ls-files 2>$null)
    if ($LASTEXITCODE -eq 0) {
        foreach ($trackedPath in $trackedPaths) {
            $relative = $trackedPath.Replace('\', '/')

            foreach ($regex in $sourceForbiddenTrackedRegexes) {
                if ($relative -match $regex) {
                    $failures += "Forbidden local-only file tracked in SourceWorkspace: $relative"
                }
            }

            if ($relative -notlike '*/*') {
                foreach ($pattern in $forbiddenRootReviewPatterns) {
                    if ($relative -like $pattern) {
                        $failures += "Forbidden root review report tracked in SourceWorkspace: $relative"
                    }
                }
            }

            foreach ($pattern in $forbiddenInternalReviewPathPatterns) {
                if (Test-BoundaryPathPattern -RelativePath $relative -Pattern $pattern) {
                    $failures += "Forbidden internal review/planning markdown tracked in SourceWorkspace: $relative"
                }
            }
        }
    }
}

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

# UAT retains generated includes and UBT's editor module import libraries.
if ($Mode -eq 'Package') {
    foreach ($file in (Get-ChildItem -LiteralPath $resolvedRoot -Recurse -Force -File)) {
        $relative = (Get-RelativeBoundaryPath -BasePath $resolvedRoot -Path $file.FullName).Replace('\', '/')
        if ($relative -match '(^|/)Intermediate/' -and
            $relative -notmatch '^Intermediate/Build/(?:[^/]+/)+Inc/(?<generatedModule>Design2UMG(?:Core|Editor))/UHT/(?:Timestamp|\k<generatedModule>Classes\.h|[^/]+\.generated\.h|[^/]+\.gen\.cpp)$' -and
            $relative -notmatch '^Intermediate/Build/Win64/(?:[^/]+/)?UnrealEditor(?:NU)?/Development/(?<module>Design2UMG(?:Core|Editor))/UnrealEditor-\k<module>\.lib$') {
            $failures += "Unexpected packaged Intermediate file: $relative"
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
        $trackedMatches = @(git -C $resolvedRoot ls-files -- $relative 2>$null)
        if ($LASTEXITCODE -eq 0 -and $trackedMatches.Count -gt 0) {
            $trackedPublicSurfaceFiles += $file
        }
    }
    $publicSurfaceFiles = $trackedPublicSurfaceFiles
}

foreach ($file in $publicSurfaceFiles) {
    if ((Resolve-Path -LiteralPath $file.FullName).Path -eq $selfPath) {
        continue
    }
    $relative = Get-RelativeBoundaryPath -BasePath $resolvedRoot -Path $file.FullName
    foreach ($pattern in $forbiddenInternalReviewPathPatterns) {
        if (Test-BoundaryPathPattern -RelativePath $relative -Pattern $pattern) {
            $failures += "Forbidden internal review/planning markdown present in $Mode public surface: $relative"
        }
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
