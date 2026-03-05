# Requires: PowerShell 5.1+
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

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
	throw "git is required but not found in PATH."
}

$repoRoot = (git rev-parse --show-toplevel).Trim()
if (-not $repoRoot) {
	throw "Unable to locate git repository root."
}

Set-Location $repoRoot
Write-Host "Repository: $repoRoot"

$staged = git diff --name-only --cached
$unstaged = git diff --name-only
$untracked = git ls-files --others --exclude-standard

$stagedCount = @($staged).Count
$unstagedCount = @($unstaged).Count
$untrackedCount = @($untracked).Count

Write-Host ("Staged   : {0}" -f $stagedCount)
Write-Host ("Unstaged : {0}" -f $unstagedCount)
Write-Host ("Untracked: {0}" -f $untrackedCount)

if ($stagedCount -eq 0 -and $unstagedCount -eq 0 -and $untrackedCount -eq 0) {
	Write-Ok "Workspace is clean."
	exit 0
}

Write-Fail "Workspace is not clean. Release branch cut is blocked."
Write-Host ""

if ($stagedCount -gt 0) {
	Write-Host "Top staged changes:"
	@($staged) | Select-Object -First 20 | ForEach-Object { Write-Host "  [S] $_" }
}
if ($unstagedCount -gt 0) {
	Write-Host "Top unstaged changes:"
	@($unstaged) | Select-Object -First 20 | ForEach-Object { Write-Host "  [U] $_" }
}
if ($untrackedCount -gt 0) {
	Write-Host "Top untracked files:"
	@($untracked) | Select-Object -First 20 | ForEach-Object { Write-Host "  [?] $_" }
}

Write-Host ""
Write-Host "Suggested next step: split cleanup/feature/release changes into separate commits."
exit 2
