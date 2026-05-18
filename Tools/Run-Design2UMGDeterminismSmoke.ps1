param(
    [string]$ProjectPath = "",
    [string]$EditorPath = "",
    [string]$ReferenceRoot = "",
    [string]$OutputRoot = "",
    [string]$DesignRoot = "G:\UE5\FPSRoguelike\Designs",
    [string]$LogCmds = "",
    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"

$pythonCommand = Get-Command python -ErrorAction SilentlyContinue
if (-not $pythonCommand) {
    throw "python is required to run determinism smoke."
}

$helperPath = Join-Path $PSScriptRoot "design2umg_determinism_smoke.py"
if (-not (Test-Path -LiteralPath $helperPath -PathType Leaf)) {
    throw "Determinism smoke helper not found: $helperPath"
}

$arguments = @($helperPath)
if ($ValidateOnly) {
    $arguments += @(
        "validate-inputs",
        "--project", $ProjectPath,
        "--editor", $EditorPath,
        "--reference-root", $ReferenceRoot,
        "--design-root", $DesignRoot
    )
}
else {
    $arguments += @(
        "run",
        "--project", $ProjectPath,
        "--editor", $EditorPath,
        "--reference-root", $ReferenceRoot,
        "--output-root", $OutputRoot,
        "--design-root", $DesignRoot
    )
    if (-not [string]::IsNullOrWhiteSpace($LogCmds)) {
        $arguments += @("--log-cmds", $LogCmds)
    }
}

& $pythonCommand.Source @arguments
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "Determinism smoke failed with exit code $exitCode."
}
