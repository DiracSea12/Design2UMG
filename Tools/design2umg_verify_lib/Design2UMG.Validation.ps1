function Resolve-D2URequiredProjectPath {
    param([string]$Value)
    $candidate = $Value
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        $candidate = $env:DESIGN2UMG_RUNTIME_PROJECT
    }
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        throw "Runtime project path is required. Pass -ProjectPath or set DESIGN2UMG_RUNTIME_PROJECT. HostProject fallback is not supported."
    }
    $resolved = [System.IO.Path]::GetFullPath($candidate)
    $lower = $resolved.ToLowerInvariant()
    $hostProjectFileName = ("hostproject" + ".uproject")
    $generatedProjectMarker = ("design2umg" + "_host_")
    if ($lower.EndsWith($hostProjectFileName) -or $lower.Contains("hostproject") -or $lower.Contains($generatedProjectMarker)) {
        throw "Refusing temporary or generated runtime project: $resolved"
    }
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Runtime project not found: $resolved"
    }
    return $resolved
}

$script:D2UEditorRunMutex = $null
$script:D2UEditorRunMutexName = "Global\Design2UMG.UnrealEditorRun"

function Enter-D2UEditorRunLock {
    if ($null -ne $script:D2UEditorRunMutex) {
        return
    }

    $mutex = New-Object -TypeName System.Threading.Mutex -ArgumentList @($false, $script:D2UEditorRunMutexName)
    $acquired = $false
    try {
        try {
            $acquired = $mutex.WaitOne([TimeSpan]::FromMinutes(30))
        }
        catch [System.Threading.AbandonedMutexException] {
            $acquired = $true
        }

        if (-not $acquired) {
            throw "Timed out waiting for the shared UnrealEditor validation lock."
        }

        $script:D2UEditorRunMutex = $mutex
    }
    catch {
        $mutex.Dispose()
        throw
    }
}

function Exit-D2UEditorRunLock {
    if ($null -eq $script:D2UEditorRunMutex) {
        return
    }

    $mutex = $script:D2UEditorRunMutex
    $script:D2UEditorRunMutex = $null
    try {
        $mutex.ReleaseMutex()
    }
    finally {
        $mutex.Dispose()
    }
}

function Resolve-D2URequiredEditorPath {
    param([string]$Value)
    $candidate = $Value
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        $candidate = $env:DESIGN2UMG_UNREAL_EDITOR
    }
    if ([string]::IsNullOrWhiteSpace($candidate) -and -not [string]::IsNullOrWhiteSpace($env:UE_ROOT)) {
        $candidate = Join-Path $env:UE_ROOT "Engine\Binaries\Win64\UnrealEditor.exe"
    }
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        throw "UnrealEditor path is required. Pass -EditorPath, set DESIGN2UMG_UNREAL_EDITOR, or set UE_ROOT."
    }
    $resolved = [System.IO.Path]::GetFullPath($candidate)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "UnrealEditor not found: $resolved"
    }
    return $resolved
}

function Assert-D2UEditorIsolation {
    param([switch]$KeepExistingEditor)
    Enter-D2UEditorRunLock
    if (-not $KeepExistingEditor -and (Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue)) {
        Exit-D2UEditorRunLock
        throw "An UnrealEditor session is already running. Save and close it before validation, or explicitly use -KeepExistingEditor for debugging. No editor was terminated."
    }
}

function New-D2UDirectoryForPath {
    param([string]$Path)
    $directory = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
}

function Invoke-D2UEditorAndWait {
    param(
        [string]$EditorPath,
        [string]$ArgumentLine
    )
    Enter-D2UEditorRunLock
    try {
        $process = Start-Process -FilePath $EditorPath -ArgumentList $ArgumentLine -PassThru
        Wait-Process -Id $process.Id
        $process.Refresh()
        return $process.ExitCode
    }
    finally {
        Exit-D2UEditorRunLock
    }
}

function Assert-D2UFreshFile {
    param(
        [string]$Path,
        [datetime]$StartTime,
        [string]$MissingMessage,
        [string]$StaleMessageFormat
    )
    if (-not (Test-Path -LiteralPath $Path)) {
        throw $MissingMessage
    }

    $item = Get-Item -LiteralPath $Path
    if ($item.LastWriteTime -le $StartTime) {
        throw ([string]::Format(
            $StaleMessageFormat,
            $item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'),
            $StartTime.ToString('yyyy-MM-dd HH:mm:ss'),
            $Path
        ))
    }

    return $item
}

function Assert-D2UAutomationRenderSuccessLog {
    param([string]$LogPath)
    $logContent = Get-Content -LiteralPath $LogPath -Raw
    if ($logContent -notmatch "Design2UMG\.AutomationRender succeeded:") {
        throw "Validation log does not contain 'Design2UMG.AutomationRender succeeded:'"
    }
}
