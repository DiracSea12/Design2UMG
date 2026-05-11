# Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

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

$targetTextPattern = '\.(h|cpp|cs|py|md|json|ya?ml)$'
$failures = @()

foreach ($line in (git ls-files --eol)) {
	if ($line -notmatch "`t") {
		continue
	}

	$parts = $line -split "`t", 2
	if ($parts.Count -lt 2) {
		continue
	}

	$metadata = $parts[0]
	$relativePath = $parts[1]
	if ($relativePath -notmatch $targetTextPattern) {
		continue
	}

	if ($metadata -match 'attr/text eol=crlf' -and $metadata -match 'w/(lf|mixed)') {
		$failures += "EOL mismatch: $line"
	}
}

$sourceFiles = git ls-files Source | Where-Object { $_ -match '\.(h|cpp|cs)$' }
foreach ($relativePath in $sourceFiles) {
	$fullPath = Join-Path $repoRoot $relativePath
	$firstLines = Get-Content -LiteralPath $fullPath -TotalCount 5
	if (($firstLines -join "`n") -notmatch 'Copyright|SPDX-License-Identifier') {
		$failures += "Missing copyright/SPDX header: $relativePath"
	}
}

$failures = @($failures | Sort-Object -Unique)

if ($failures.Count -eq 0) {
	Write-Ok "Text file hygiene checks passed."
	exit 0
}

Write-Fail "Text file hygiene checks failed."
foreach ($failure in $failures) {
	Write-Host "  - $failure"
}
exit 2
