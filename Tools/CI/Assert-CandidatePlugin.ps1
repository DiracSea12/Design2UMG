# Checks the installed source before rebuilding the real project's editor.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [Parameter(Mandatory = $true)][string]$ProjectFile,
    [switch]$PassThru
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $SourceRoot).Path
$project = (Resolve-Path -LiteralPath $ProjectFile).Path
$projectRoot = Split-Path -Parent $project
$projectData = Get-Content -LiteralPath $project -Raw | ConvertFrom-Json
$engineDirectory = Join-Path $EngineRoot 'Engine'
$roots = @((Join-Path $engineDirectory 'Plugins'), (Join-Path $EngineRoot 'Enterprise/Plugins'), (Join-Path $projectRoot 'Plugins'), (Join-Path $projectRoot 'Mods'))
foreach ($root in @($engineDirectory, $projectRoot)) {
    foreach ($directory in @('Platforms', 'Restricted')) {
        $path = Join-Path $root $directory
        if (Test-Path -LiteralPath $path -PathType Container) {
            $roots += Get-ChildItem -LiteralPath $path -Directory | ForEach-Object { Join-Path $_.FullName 'Plugins' }
        }
    }
}
if ($projectData.PSObject.Properties['AdditionalPluginDirectories']) {
    foreach ($directory in $projectData.AdditionalPluginDirectories) {
        $roots += if ([IO.Path]::IsPathRooted($directory)) { $directory } else { Join-Path $projectRoot $directory }
    }
}
$visited = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$plugins = @($roots | Select-Object -Unique | ForEach-Object {
    if (Test-Path -LiteralPath $_ -PathType Container) {
        $pending = [Collections.Generic.Stack[string]]::new()
        $pending.Push($_)
        while ($pending.Count) {
            $directory = $pending.Pop()
            $item = Get-Item -LiteralPath $directory
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                $item = $item.ResolveLinkTarget($true)
                if (-not $item) { throw "Plugin directory link has no target: $directory" }
            }
            $directory = $item.FullName
            if (-not $visited.Add($directory)) { continue }
            $descriptors = @(Get-ChildItem -LiteralPath $directory -File -Filter '*.uplugin')
            if ($descriptors.Count) {
                $descriptors | Where-Object Name -eq 'Design2UMG.uplugin' | Select-Object -ExpandProperty FullName
                continue
            }
            Get-ChildItem -LiteralPath $directory -Directory | ForEach-Object { $pending.Push($_.FullName) }
        }
    }
} | Sort-Object -Unique)
if ($plugins.Count -ne 1) {
    throw "Expected exactly one installed Design2UMG plugin; found $($plugins.Count): $($plugins -join ', ')"
}
$installed = Split-Path -Parent $plugins[0]

function Get-ProductFiles([string]$Root) {
    $files = @{ 'Design2UMG.uplugin' = Join-Path $Root 'Design2UMG.uplugin' }
    foreach ($directory in @('Source', 'Config')) {
        $path = Join-Path $Root $directory
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            throw "Required product directory missing: $path"
        }
        foreach ($file in (Get-ChildItem -LiteralPath $path -Recurse -File)) {
            $relative = $file.FullName.Substring($Root.TrimEnd('/', '\').Length + 1).Replace('\', '/')
            $files[$relative] = $file.FullName
        }
    }
    return $files
}

$expected = Get-ProductFiles $source
$actual = Get-ProductFiles $installed
$differences = @()
foreach ($name in @(@($expected.Keys) + @($actual.Keys) | Sort-Object -Unique)) {
    if (-not $expected.ContainsKey($name) -or -not $actual.ContainsKey($name)) {
        $differences += $name
    }
    elseif ((Get-FileHash -LiteralPath $expected[$name] -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $actual[$name] -Algorithm SHA256).Hash) {
        $differences += $name
    }
}
if ($differences.Count) {
    throw "Installed plugin differs from candidate: $($differences -join ', ')"
}
Write-Host "[PASS] Candidate source matches installed plugin: $installed"
Write-Host 'Binary identity requires the subsequent editor rebuild; this check alone is not runtime evidence.'
if ($PassThru) { Write-Output $installed }
