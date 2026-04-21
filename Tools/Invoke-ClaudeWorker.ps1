[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$Prompt,

    [Parameter(Mandatory = $false)]
    [string]$PromptFile,

    [Parameter(Mandatory = $false)]
    [string]$Model = "opus",

    [Parameter(Mandatory = $false)]
    [ValidateSet("text", "json", "stream-json")]
    [string]$OutputFormat = "text"
)

if ([string]::IsNullOrWhiteSpace($Prompt) -and [string]::IsNullOrWhiteSpace($PromptFile))
{
    throw "需要提供 -Prompt 或 -PromptFile。"
}

if (-not [string]::IsNullOrWhiteSpace($PromptFile))
{
    if (-not (Test-Path -LiteralPath $PromptFile))
    {
        throw "PromptFile 不存在: $PromptFile"
    }

    $Prompt = Get-Content -LiteralPath $PromptFile -Raw -Encoding UTF8
}

$ClaudeArgs = @(
    "-p",
    "--model", $Model,
    "--dangerously-skip-permissions",
    "--output-format", $OutputFormat,
    $Prompt
)

& claude @ClaudeArgs
$ExitCode = $LASTEXITCODE
if ($ExitCode -ne 0)
{
    exit $ExitCode
}
