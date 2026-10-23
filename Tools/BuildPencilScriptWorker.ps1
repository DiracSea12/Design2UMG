param([string]$PluginDir = (Split-Path -Parent $PSScriptRoot), [switch]$Rebuild)
$ErrorActionPreference = 'Stop'
function Get-BinarySHA256([string]$Path) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $algorithm.Dispose() }
}
$PluginDir = [IO.Path]::GetFullPath($PluginDir)
$vendor = Join-Path $PluginDir 'Source/ThirdParty/QuickJS-ng'
$source = Join-Path $PluginDir 'Tools/PencilScriptWorker/WorkerMain.cpp'
$policy = Join-Path $PluginDir 'Source/Design2UMGCore/Public/PencilScriptPolicy.h'
$output = Join-Path $PluginDir 'Resources/PencilScriptWorker/Win64'
$binary = Join-Path $output 'PencilScriptWorker.exe'
$identityFile = Join-Path $output 'identity.json'
$utf8 = New-Object Text.UTF8Encoding($false)
$inputs = @($source, $policy, $PSCommandPath) + @(Get-ChildItem -LiteralPath $vendor -File | Where-Object { $_.Extension -in '.c', '.h', '.txt', '.json' -or $_.Name -eq 'LICENSE' } | ForEach-Object { $_.FullName })
$sha = [Security.Cryptography.SHA256]::Create()
try {
    [string[]]$orderedInputs = $inputs
    [Array]::Sort($orderedInputs, [StringComparer]::Ordinal)
    foreach ($file in $orderedInputs) {
        $relative = $file.Substring($PluginDir.Length).Replace('\', '/')
        $text = [IO.File]::ReadAllText($file).Replace("`r`n", "`n")
        $bytes = $utf8.GetBytes($relative + "`n" + $text + "`n")
        [void]$sha.TransformBlock($bytes, 0, $bytes.Length, $bytes, 0)
    }
    [void]$sha.TransformFinalBlock([byte[]]@(), 0, 0)
    $sourceHash = ([BitConverter]::ToString($sha.Hash)).Replace('-', '').ToLowerInvariant()
} finally { $sha.Dispose() }
if (!$Rebuild -and (Test-Path -LiteralPath $binary) -and (Test-Path -LiteralPath $identityFile)) {
    $identity = Get-Content -LiteralPath $identityFile -Raw | ConvertFrom-Json
    if ($identity.protocol -eq 1 -and $identity.source_sha256 -eq $sourceHash -and $identity.binary_sha256 -eq (Get-BinarySHA256 $binary)) {
        Write-Host 'Pencil script worker identity verified.'
        exit 0
    }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'The bundled script worker is missing or stale; rebuilding it requires Visual Studio 2022 or newer.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -version '[17.0,)' -property installationPath
if (!$installation) { throw 'No Visual Studio 2022+ C++ toolchain found for script worker rebuild.' }
$vcvars = Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
$build = Join-Path $PluginDir 'Intermediate/PencilScriptWorker'
[void][IO.Directory]::CreateDirectory($build)
[void][IO.Directory]::CreateDirectory($output)
$commands = @(
    '@echo off',
    "call `"$vcvars`" >nul",
    'if errorlevel 1 exit /b 1',
    "cl.exe /nologo /c /TC /std:c11 /experimental:c11atomics /MT /O2 /D_GNU_SOURCE /DWIN32_LEAN_AND_MEAN `"$vendor/quickjs.c`" `"$vendor/dtoa.c`" `"$vendor/libregexp.c`" `"$vendor/libunicode.c`" /Fo:`"$build/`"",
    'if errorlevel 1 exit /b 1',
    "lib.exe /nologo /out:`"$build/quickjs.lib`" `"$build/quickjs.obj`" `"$build/dtoa.obj`" `"$build/libregexp.obj`" `"$build/libunicode.obj`"",
    'if errorlevel 1 exit /b 1',
    "cl.exe /nologo /std:c++17 /EHsc /W4 /MT /O2 /I`"$vendor`" /I`"$(Split-Path -Parent $policy)`" `"$source`" /Fe:`"$output/PencilScriptWorker.exe`" /Fo:`"$build/WorkerMain.obj`" /link `"$build/quickjs.lib`" userenv.lib advapi32.lib /STACK:2097152",
    'exit /b %errorlevel%'
)
$commandFile = Join-Path $build 'BuildWorker.cmd'
[IO.File]::WriteAllLines($commandFile, $commands, $utf8)
& $env:ComSpec /d /c "`"$commandFile`""
if ($LASTEXITCODE -ne 0) { throw "Script worker compilation failed: $LASTEXITCODE" }
$identity = [ordered]@{ protocol = 1; engine = 'QuickJS-NG'; version = '0.17.0'; commit = '6d46d07d04041b40f4f49eaa7fdebe44c314c699'; source_sha256 = $sourceHash; binary_sha256 = (Get-BinarySHA256 $binary) }
[IO.File]::WriteAllText($identityFile, ($identity | ConvertTo-Json), $utf8)
Copy-Item -LiteralPath (Join-Path $vendor 'LICENSE') -Destination (Join-Path $output 'QuickJS-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $vendor 'UNICODE-LICENSE.txt') -Destination (Join-Path $output 'Unicode-LICENSE.txt')
Write-Host 'Pencil script worker built and identity recorded.'
