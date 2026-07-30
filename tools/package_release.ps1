[CmdletBinding()]
param(
    [string]$Version = '1.0.0',
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$distRoot = Join-Path $projectRoot 'dist'
$packageName = "ClearSky-Enhanced-Centered-UI-$Version"
$stagingRoot = Join-Path $distRoot $packageName
$archivePath = Join-Path $distRoot "$packageName.zip"
$builtProxy = Join-Path $projectRoot "build\$Configuration\dinput8.dll"
$payloadProxy = Join-Path $projectRoot 'installer\payload\dinput8.dll'

function Get-RelativePath {
    param(
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$Path
    )
    $normalizedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $normalizedPath = [System.IO.Path]::GetFullPath($Path)
    $prefix = "$normalizedRoot\"
    if (-not $normalizedPath.StartsWith(
        $prefix,
        [System.StringComparison]::OrdinalIgnoreCase
    )) {
        throw "Path is outside package root: '$normalizedPath'."
    }
    return $normalizedPath.Substring($prefix.Length)
}

if (-not (Test-Path -LiteralPath $builtProxy -PathType Leaf)) {
    throw "Built proxy is missing: '$builtProxy'."
}
if (-not (Test-Path -LiteralPath $payloadProxy -PathType Leaf)) {
    throw "Payload proxy is missing: '$payloadProxy'."
}
if ((Get-FileHash -LiteralPath $builtProxy -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $payloadProxy -Algorithm SHA256).Hash) {
    throw 'Payload dinput8.dll does not match the selected build.'
}

New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
if (Test-Path -LiteralPath $stagingRoot) {
    $resolvedDist = [System.IO.Path]::GetFullPath($distRoot).TrimEnd('\')
    $resolvedStaging = [System.IO.Path]::GetFullPath($stagingRoot)
    if (-not $resolvedStaging.StartsWith(
        "$resolvedDist\",
        [System.StringComparison]::OrdinalIgnoreCase
    )) {
        throw "Refusing to remove staging path outside dist: '$resolvedStaging'."
    }
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue

New-Item -ItemType Directory -Path $stagingRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'installer\install.cmd') `
    -Destination $stagingRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'installer\uninstall.cmd') `
    -Destination $stagingRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'installer\install.ps1') `
    -Destination $stagingRoot
Copy-Item -LiteralPath (
    Join-Path $projectRoot 'installer\compatibility-profile.psd1'
) -Destination $stagingRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'installer\payload') `
    -Destination $stagingRoot -Recurse

foreach ($document in @(
    'README.md',
    'COMPATIBILITY.md',
    'CHANGELOG.md',
    'RELEASE_NOTES.md',
    'THIRD_PARTY_NOTICES.md',
    'LICENSE'
)) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $document) `
        -Destination $stagingRoot
}

$checksums = foreach ($file in Get-ChildItem $stagingRoot -Recurse -File |
    Where-Object Name -ne 'SHA256SUMS.txt' |
    Sort-Object FullName) {
    $relativePath = Get-RelativePath -Root $stagingRoot -Path $file.FullName
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    "$hash *$($relativePath -replace '\\', '/')"
}
$checksums | Set-Content -LiteralPath (
    Join-Path $stagingRoot 'SHA256SUMS.txt'
) -Encoding ASCII

Compress-Archive -LiteralPath $stagingRoot -DestinationPath $archivePath `
    -CompressionLevel Optimal

Write-Host "Package: $archivePath"
Write-Host "SHA-256: $((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash)"
