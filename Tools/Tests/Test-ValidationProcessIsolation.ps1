$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../design2umg_verify_lib/Design2UMG.Validation.ps1')

# Replace OS access; exercise the real isolation decision without touching editor sessions.
$script:HasEditor = $true
$script:Locked = $false
$script:Terminated = $false
function Get-Process { if ($script:HasEditor) { [pscustomobject]@{ Id = 1 } } }
function Stop-Process { $script:Terminated = $true }
function Enter-D2UEditorRunLock { $script:Locked = $true }
function Exit-D2UEditorRunLock { $script:Locked = $false }

$rejected = $false
try { Assert-D2UEditorIsolation }
catch { $rejected = $true }
if (-not $rejected -or $script:Locked -or $script:Terminated) {
    throw 'An active editor must be preserved and validation rejected without retaining the lock.'
}

Assert-D2UEditorIsolation -KeepExistingEditor
if (-not $script:Locked -or $script:Terminated) {
    throw 'Explicit debugging must preserve the existing editor.'
}
Exit-D2UEditorRunLock

$script:HasEditor = $false
Assert-D2UEditorIsolation
if (-not $script:Locked -or $script:Terminated) {
    throw 'An empty editor environment must allow validation.'
}
Exit-D2UEditorRunLock
Write-Host '[PASS] Editor isolation: active session, explicit keep, empty environment.'
