param(
    [Parameter(Mandatory = $true)]
    [string]$NdkRoot
)

$ErrorActionPreference = 'Stop'
$compilerPath = Join-Path $NdkRoot 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
if (-not (Test-Path -LiteralPath $compilerPath -PathType Leaf)) {
    throw "Android NDK compiler was not found: $compilerPath"
}

$outputDirectory = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$commonArguments = @(
    '--target=aarch64-linux-android35',
    '-O2', '-Wall', '-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-unused-function',
    '-Isrc/core', '-Isrc/devices', '-DTARGET_CONFIG_H="target.h"',
    '-fPIE', '-pie', '-pthread'
)
$payloadSources = @(
    'src/core/main.c', 'src/core/util.c', 'src/core/kaslr.c',
    'src/core/fops.c', 'src/core/pipe.c', 'src/core/root.c', 'src/core/umh_helper.c'
)

Push-Location -LiteralPath $PSScriptRoot
try {
    & $compilerPath @commonArguments @payloadSources '-o' 'build/payload'
    if ($LASTEXITCODE -ne 0) { throw 'The payload build failed.' }

    & $compilerPath @commonArguments 'src/core/su_daemon.c' '-ldl' '-o' 'build/pompomsu-helper'
    if ($LASTEXITCODE -ne 0) { throw 'The helper build failed.' }
} finally {
    Pop-Location
}

Get-FileHash -LiteralPath (Join-Path $outputDirectory 'payload'), (Join-Path $outputDirectory 'pompomsu-helper') -Algorithm SHA256
