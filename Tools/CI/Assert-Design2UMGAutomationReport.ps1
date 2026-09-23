[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$TestFilter,
    [Parameter(Mandatory = $true)][string]$ReportPath,
    [Parameter(Mandatory = $true)][string]$LogPath,
    [Parameter(Mandatory = $true)][long]$ProcessExitCode,
    [Parameter(Mandatory = $true)][string]$SummaryPath,
    [Parameter(Mandatory = $true)][ValidateSet('NullRHI', 'RenderOffscreen')][string]$RenderMode
)

$ErrorActionPreference = 'Stop'
$failures = [System.Collections.Generic.List[string]]::new()
$counts = [ordered]@{
    discovered = 0
    executed = 0
    succeeded = 0
    succeeded_with_warnings = 0
    failed = 0
    not_run = 0
    in_process = 0
    skipped = 0
}

if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) {
    $failures.Add("Automation log missing: $LogPath")
}
else {
    $logContent = Get-Content -LiteralPath $LogPath -Raw
    if ($logContent -notmatch '\*\*\*\* TEST COMPLETE\. EXIT CODE: 0 \*\*\*\*') {
        $failures.Add('Automation success marker missing from log')
    }
    # Restrict this check to PluginManager warnings so automation test names and Design2UMG runtime diagnostics do not look like loader conflicts.
    $design2UMGPluginIdentity = '(?:plugin\s+[''"]?Design2UMG[''"]?|Design2UMG\s+plugin)'
    $duplicatePluginAction = '(?:duplicate(?:d)?|same\s+(?:name|version)|already\s+(?:exists|enabled)|ignored)'
    $duplicatePluginWarningPattern = "(?i)LogPluginManager:\s*Warning:[^\r\n]*(?:${duplicatePluginAction}[^\r\n]*${design2UMGPluginIdentity}|${design2UMGPluginIdentity}[^\r\n]*${duplicatePluginAction})[^\r\n]*"
    if ($logContent -match $duplicatePluginWarningPattern) {
        $failures.Add('Duplicate Design2UMG plugin warning found in log')
    }
}

if ($ProcessExitCode -ne 0) {
    $failures.Add("Editor process exited with code $ProcessExitCode")
}

if (-not (Test-Path -LiteralPath $ReportPath -PathType Leaf)) {
    $failures.Add("Automation report missing: $ReportPath")
}
else {
    try {
        $report = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
        foreach ($field in @('succeeded', 'succeededWithWarnings', 'failed', 'notRun', 'inProcess', 'tests')) {
            if ($null -eq $report.$field) {
                throw "Report field missing: $field"
            }
        }

        $counts.succeeded = [int]$report.succeeded
        $counts.succeeded_with_warnings = [int]$report.succeededWithWarnings
        $counts.failed = [int]$report.failed
        $counts.not_run = [int]$report.notRun
        $counts.in_process = [int]$report.inProcess
        $counts.discovered = @($report.tests).Count
        $counts.executed = $counts.succeeded + $counts.succeeded_with_warnings + $counts.failed
        $counts.skipped = $counts.discovered - $counts.executed - $counts.not_run - $counts.in_process

        if ($counts.discovered -le 0) { $failures.Add('Automation report discovered zero tests') }
        if ($counts.failed -gt 0) { $failures.Add("Automation report contains $($counts.failed) failed tests") }
        if ($counts.not_run -gt 0) { $failures.Add("Automation report contains $($counts.not_run) tests that did not run") }
        if ($counts.in_process -gt 0) { $failures.Add("Automation report contains $($counts.in_process) incomplete tests") }
        if ($counts.skipped -ne 0) { $failures.Add("Automation report contains $($counts.skipped) skipped or unclassified tests") }
        if ($counts.executed -ne $counts.discovered) {
            $failures.Add("Executed/discovered mismatch: $($counts.executed)/$($counts.discovered)")
        }
    }
    catch {
        $failures.Add("Automation report is invalid: $($_.Exception.Message)")
    }
}

$summary = [ordered]@{
    test_filter = $TestFilter
    render_mode = $RenderMode
    process_exit_code = $ProcessExitCode
    report_path = $ReportPath
    log_path = $LogPath
    counts = $counts
    passed = ($failures.Count -eq 0)
    failures = @($failures)
}
$summaryDirectory = Split-Path -Parent $SummaryPath
if ($summaryDirectory) {
    New-Item -ItemType Directory -Path $summaryDirectory -Force | Out-Null
}
[System.IO.File]::WriteAllText(
    $SummaryPath,
    ($summary | ConvertTo-Json -Depth 5),
    [System.Text.UTF8Encoding]::new($false))

if ($failures.Count -gt 0) {
    throw ($failures -join '; ')
}

Write-Host "[PASS] ${TestFilter}: discovered=$($counts.discovered), executed=$($counts.executed), failed=0, not_run=0"
