$ErrorActionPreference = 'Stop'
$assertScript = Join-Path $PSScriptRoot '../CI/Assert-Design2UMGAutomationReport.ps1'
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("D2U-CIReport-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

try {
    $logPath = Join-Path $tempRoot 'automation.log'
    $reportPath = Join-Path $tempRoot 'index.json'
    $summaryPath = Join-Path $tempRoot 'summary.json'
    [System.IO.File]::WriteAllText(
        $logPath,
        '**** TEST COMPLETE. EXIT CODE: 0 ****',
        [System.Text.UTF8Encoding]::new($false))

    function Write-Report([int]$Succeeded, [int]$Failed, [int]$NotRun, [int]$InProcess, [int]$TestCount) {
        $tests = for ($index = 0; $index -lt $TestCount; ++$index) {
            [ordered]@{ fullTestPath = "Design2UMG.CI.$index" }
        }
        $report = [ordered]@{
            succeeded = $Succeeded
            succeededWithWarnings = 0
            failed = $Failed
            notRun = $NotRun
            inProcess = $InProcess
            tests = @($tests)
        }
        [System.IO.File]::WriteAllText(
            $reportPath,
            ($report | ConvertTo-Json -Depth 4),
            [System.Text.UTF8Encoding]::new($false))
    }

    function Invoke-Contract([long]$ExitCode = 0) {
        & $assertScript `
            -TestFilter 'Design2UMG.CI' `
            -ReportPath $reportPath `
            -LogPath $logPath `
            -ProcessExitCode $ExitCode `
            -SummaryPath $summaryPath `
            -RenderMode NullRHI
    }

    Write-Report -Succeeded 2 -Failed 0 -NotRun 0 -InProcess 0 -TestCount 2
    Invoke-Contract
    $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
    if (-not $summary.passed -or $summary.counts.discovered -ne 2 -or $summary.counts.executed -ne 2) {
        throw 'A complete report did not pass with the expected counts.'
    }

    foreach ($case in @(
        @{ succeeded = 0; failed = 0; notRun = 0; tests = 0; exitCode = 0 },
        @{ succeeded = 1; failed = 1; notRun = 0; tests = 2; exitCode = 0 },
        @{ succeeded = 1; failed = 0; notRun = 1; tests = 2; exitCode = 0 },
        @{ succeeded = 1; failed = 0; notRun = 0; tests = 1; exitCode = 3221225477 }
    )) {
        Write-Report -Succeeded $case.succeeded -Failed $case.failed -NotRun $case.notRun -InProcess 0 -TestCount $case.tests
        $rejected = $false
        try { Invoke-Contract -ExitCode $case.exitCode } catch { $rejected = $true }
        if (-not $rejected) { throw "Invalid report case was accepted: $($case | ConvertTo-Json -Compress)" }
        $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
        if ($summary.passed) { throw 'A rejected report was recorded as passing.' }
    }

    Write-Report -Succeeded 2 -Failed 0 -NotRun 0 -InProcess 0 -TestCount 2
    [System.IO.File]::WriteAllLines(
        $logPath,
        @(
            '**** TEST COMPLETE. EXIT CODE: 0 ****'
            'LogAutomationCommandLine: Display: Design2UMG.PreBuildResourceNormalizer.ImageTransform.PersistedAndDeduplicated'
            'LogAutomationController: Display: Test Completed. Result={Success} Path={Design2UMG.SafeReimport.SwitcherHostMappedNoDuplicate}'
            'LogDesign2UMG: Warning: SizeBoxSlot ignored Anchors/Offsets/Alignment.'
        ),
        [System.Text.UTF8Encoding]::new($false))
    Invoke-Contract

    foreach ($duplicatePluginWarning in @(
        "LogPluginManager: Warning: The same version (v1) plugin 'Design2UMG' exists at 'A/Design2UMG.uplugin' and 'B/Design2UMG.uplugin' - the second location will be ignored."
        "LogPluginManager: Warning: Plugin 'Design2UMG' already exists at another location and will be ignored."
    )) {
        [System.IO.File]::WriteAllLines(
            $logPath,
            @('**** TEST COMPLETE. EXIT CODE: 0 ****', $duplicatePluginWarning),
            [System.Text.UTF8Encoding]::new($false))
        $rejected = $false
        $failureMessage = ''
        try { Invoke-Contract } catch {
            $rejected = $true
            $failureMessage = $_.Exception.Message
        }
        if (-not $rejected) { throw "Duplicate plugin warning was accepted: $duplicatePluginWarning" }
        if ($failureMessage -notlike '*Duplicate Design2UMG plugin warning found in log*') {
            throw "Duplicate plugin warning failed for the wrong reason: $failureMessage"
        }
        $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
        if ($summary.passed) { throw 'A duplicate plugin warning was recorded as passing.' }
    }

    Write-Host '[PASS] CI automation report contract'
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
