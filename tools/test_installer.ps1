[CmdletBinding()]
param(
    [string]$GameDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\STALKER Clear Sky - EE',
    [switch]$KeepWork,
    [switch]$CleanStaleWork
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$installer = Join-Path $projectRoot 'installer\install.ps1'
$payload = Join-Path $projectRoot 'installer\payload'
$engineSource = Join-Path $GameDirectory 'xrEngine.exe'
$workRoot = Join-Path $projectRoot (
    'work\installer-integration-' +
    [DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff')
)
$successRoot = Join-Path $workRoot 'success'
$rollbackRoot = Join-Path $workRoot 'rollback'
$unknownShaRoot = Join-Path $workRoot 'unknown-sha'
$incompatibleRoot = Join-Path $workRoot 'incompatible'
$systemProxy = Join-Path $env:WINDIR 'System32\dinput8.dll'
$projectWork = Join-Path $projectRoot 'work'

function Assert-True {
    param(
        [Parameter(Mandatory)][bool]$Condition,
        [Parameter(Mandatory)][string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Get-Sha256 {
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Set-ByteAtRva {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][uint32]$Rva,
        [Parameter(Mandatory)][byte]$Value
    )
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    $sectionCount = [BitConverter]::ToUInt16($bytes, $peOffset + 6)
    $optionalSize = [BitConverter]::ToUInt16($bytes, $peOffset + 20)
    $sectionOffset = $peOffset + 24 + $optionalSize
    for ($index = 0; $index -lt $sectionCount; $index++) {
        $offset = $sectionOffset + $index * 40
        $virtualAddress = [BitConverter]::ToUInt32($bytes, $offset + 12)
        $rawSize = [BitConverter]::ToUInt32($bytes, $offset + 16)
        $rawOffset = [BitConverter]::ToUInt32($bytes, $offset + 20)
        if ($Rva -ge $virtualAddress -and
            $Rva -lt $virtualAddress + $rawSize) {
            $fileOffset = $rawOffset + ($Rva - $virtualAddress)
            $bytes[$fileOffset] = $Value
            [System.IO.File]::WriteAllBytes($Path, $bytes)
            return
        }
    }
    throw ('RVA 0x{0:X} is not backed by a file section.' -f $Rva)
}

function Initialize-TestGame {
    param(
        [Parameter(Mandatory)][string]$Root,
        [switch]$BlockGamedata
    )

    New-Item -ItemType Directory -Path $Root -Force | Out-Null
    Copy-Item -LiteralPath $engineSource `
        -Destination (Join-Path $Root 'xrEngine.exe')
    Copy-Item -LiteralPath $systemProxy `
        -Destination (Join-Path $Root 'dinput8.dll')

    if ($BlockGamedata) {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') `
            -Destination (Join-Path $Root 'gamedata')
        return
    }

    $uiRoot = Join-Path $Root 'gamedata\configs\ui'
    New-Item -ItemType Directory -Path $uiRoot -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') `
        -Destination (Join-Path $uiRoot 'ui_mm_main_16.xml')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'COMPATIBILITY.md') `
        -Destination (Join-Path $uiRoot 'ui_mm_main_c_16.xml')
}

function Invoke-Installer {
    param(
        [Parameter(Mandatory)][ValidateSet('Install', 'Uninstall')]
        [string]$Action,
        [Parameter(Mandatory)][string]$Root,
        [string]$DiagnosticPath
    )
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $arguments = @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', $installer,
            '-Action', $Action,
            '-GamePath', $Root
        )
        if ($DiagnosticPath) {
            $arguments += @('-DiagnosticPath', $DiagnosticPath)
        }
        $output = & powershell.exe @arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorAction
    }
    return [pscustomobject]@{
        exitCode = $exitCode
        output = ($output | Out-String).Trim()
    }
}

if (-not (Test-Path -LiteralPath $engineSource -PathType Leaf)) {
    throw "Supported xrEngine.exe is missing: '$engineSource'."
}
if (-not (Test-Path -LiteralPath $systemProxy -PathType Leaf)) {
    throw "System dinput8.dll is missing: '$systemProxy'."
}
if (Get-Process -Name 'xrEngine' -ErrorAction SilentlyContinue) {
    throw 'xrEngine.exe is running. Exit the game before the installer test.'
}

if ($CleanStaleWork -and (Test-Path -LiteralPath $projectWork)) {
    $resolvedProjectWork = [System.IO.Path]::GetFullPath(
        $projectWork
    ).TrimEnd('\')
    foreach ($directory in Get-ChildItem -LiteralPath $projectWork `
        -Directory -Filter 'installer-integration-*') {
        $resolvedDirectory = [System.IO.Path]::GetFullPath(
            $directory.FullName
        )
        if (-not $resolvedDirectory.StartsWith(
            "$resolvedProjectWork\",
            [System.StringComparison]::OrdinalIgnoreCase
        )) {
            throw "Refusing to remove stale path outside project work: '$resolvedDirectory'."
        }
        Remove-Item -LiteralPath $resolvedDirectory -Recurse -Force
    }
}

New-Item -ItemType Directory -Path $workRoot -Force | Out-Null
try {
    Initialize-TestGame -Root $successRoot
    $originalProxyHash = Get-Sha256 (Join-Path $successRoot 'dinput8.dll')
    $originalMainHash = Get-Sha256 (
        Join-Path $successRoot 'gamedata\configs\ui\ui_mm_main_16.xml'
    )
    $originalMainCHash = Get-Sha256 (
        Join-Path $successRoot 'gamedata\configs\ui\ui_mm_main_c_16.xml'
    )

    $install = Invoke-Installer -Action Install -Root $successRoot
    Assert-True ($install.exitCode -eq 0) "Install failed: $($install.output)"
    $manifestPath = Join-Path $successRoot '.cs4x3ui\install-manifest.json'
    Assert-True (Test-Path -LiteralPath $manifestPath -PathType Leaf) `
        'Install manifest was not created.'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Assert-True ($manifest.files.Count -eq 4) `
        "Manifest contains $($manifest.files.Count) files, expected 4."
    Assert-True ([bool]$manifest.chainedProxy) `
        'Existing dinput8.dll was not recorded as chained.'
    Assert-True (
        (Get-Sha256 (Join-Path $successRoot 'dinput8.dll')) -eq
        (Get-Sha256 (Join-Path $payload 'dinput8.dll'))
    ) 'Installed proxy does not match the payload.'
    Assert-True (
        (Get-Sha256 (Join-Path $successRoot 'dinput8_chain.dll')) -eq
        $originalProxyHash
    ) 'Chained proxy does not match the original proxy.'

    $uninstall = Invoke-Installer -Action Uninstall -Root $successRoot
    Assert-True ($uninstall.exitCode -eq 0) `
        "Uninstall failed: $($uninstall.output)"
    Assert-True (
        (Get-Sha256 (Join-Path $successRoot 'dinput8.dll')) -eq
        $originalProxyHash
    ) 'Uninstall did not restore the original proxy.'
    Assert-True (
        -not (Test-Path -LiteralPath (
            Join-Path $successRoot 'dinput8_chain.dll'
        ))
    ) 'Uninstall left dinput8_chain.dll behind.'
    Assert-True (
        (Get-Sha256 (
            Join-Path $successRoot 'gamedata\configs\ui\ui_mm_main_16.xml'
        )) -eq $originalMainHash
    ) 'Uninstall did not restore ui_mm_main_16.xml.'
    Assert-True (
        (Get-Sha256 (
            Join-Path $successRoot 'gamedata\configs\ui\ui_mm_main_c_16.xml'
        )) -eq $originalMainCHash
    ) 'Uninstall did not restore ui_mm_main_c_16.xml.'
    Assert-True (
        -not (Test-Path -LiteralPath (
            Join-Path $successRoot 'gamedata\textures\ui\ui_hud.dds'
        ))
    ) 'Uninstall left ui_hud.dds behind.'

    Initialize-TestGame -Root $unknownShaRoot
    $unknownEngine = Join-Path $unknownShaRoot 'xrEngine.exe'
    $stream = [System.IO.File]::Open(
        $unknownEngine,
        [System.IO.FileMode]::Append,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::Read
    )
    try {
        $stream.WriteByte(0)
    } finally {
        $stream.Dispose()
    }
    Assert-True (
        (Get-Sha256 $unknownEngine) -ne (Get-Sha256 $engineSource)
    ) 'Unknown-SHA fixture did not change the engine hash.'
    $unknownInstall = Invoke-Installer `
        -Action Install -Root $unknownShaRoot
    Assert-True ($unknownInstall.exitCode -eq 0) `
        "Signature-compatible unknown SHA was rejected: $($unknownInstall.output)"
    $unknownManifest = Get-Content -LiteralPath (
        Join-Path $unknownShaRoot '.cs4x3ui\install-manifest.json'
    ) -Raw | ConvertFrom-Json
    Assert-True (
        $unknownManifest.compatibilityMode -eq 'Signature-compatible'
    ) 'Unknown-SHA install did not record Signature-compatible mode.'
    $unknownUninstall = Invoke-Installer `
        -Action Uninstall -Root $unknownShaRoot
    Assert-True ($unknownUninstall.exitCode -eq 0) `
        "Unknown-SHA uninstall failed: $($unknownUninstall.output)"

    Initialize-TestGame -Root $incompatibleRoot
    $incompatibleEngine = Join-Path $incompatibleRoot 'xrEngine.exe'
    Set-ByteAtRva -Path $incompatibleEngine -Rva 0x743120 -Value 0x90
    $diagnostic = Join-Path $workRoot 'incompatible-diagnostics.txt'
    $incompatibleInstall = Invoke-Installer `
        -Action Install -Root $incompatibleRoot `
        -DiagnosticPath $diagnostic
    Assert-True ($incompatibleInstall.exitCode -ne 0) `
        'Broken-signature fixture unexpectedly installed successfully.'
    Assert-True (Test-Path -LiteralPath $diagnostic -PathType Leaf) `
        'Incompatible build did not produce a diagnostic report.'
    $diagnosticText = Get-Content -LiteralPath $diagnostic -Raw
    Assert-True (
        $diagnosticText -match 'Recommended version: 1\.10\.3\+68-42'
    ) 'Diagnostic report does not recommend the supported game version.'
    Assert-True (
        $diagnosticText -match 'dxUIRender::PushPoint: pass=False'
    ) 'Diagnostic report does not identify the broken signature.'
    Assert-True (
        -not (Test-Path -LiteralPath (
            Join-Path $incompatibleRoot '.cs4x3ui\install-manifest.json'
        ))
    ) 'Rejected build left an active manifest.'

    Initialize-TestGame -Root $rollbackRoot -BlockGamedata
    $rollbackProxyHash = Get-Sha256 (Join-Path $rollbackRoot 'dinput8.dll')
    $failedInstall = Invoke-Installer -Action Install -Root $rollbackRoot
    Assert-True ($failedInstall.exitCode -ne 0) `
        'Rollback fixture unexpectedly installed successfully.'
    Assert-True (
        (Get-Sha256 (Join-Path $rollbackRoot 'dinput8.dll')) -eq
        $rollbackProxyHash
    ) 'Failed install did not restore the original proxy.'
    Assert-True (
        -not (Test-Path -LiteralPath (
            Join-Path $rollbackRoot 'dinput8_chain.dll'
        ))
    ) 'Failed install left dinput8_chain.dll behind.'
    Assert-True (
        -not (Test-Path -LiteralPath (
            Join-Path $rollbackRoot '.cs4x3ui\install-manifest.json'
        ))
    ) 'Failed install left an active manifest.'

    [pscustomobject]@{
        successfulInstall = $true
        successfulUninstall = $true
        chainRestored = $true
        xmlBackupsRestored = $true
        unknownShaAccepted = $true
        incompatibleSignatureRejected = $true
        diagnosticReportCreated = $true
        failedInstallRolledBack = $true
        workRoot = $workRoot
    } | ConvertTo-Json
} finally {
    if (-not $KeepWork -and (Test-Path -LiteralPath $workRoot)) {
        $resolvedProjectWork = [System.IO.Path]::GetFullPath(
            $projectWork
        ).TrimEnd('\')
        $resolvedWork = [System.IO.Path]::GetFullPath($workRoot)
        if (-not $resolvedWork.StartsWith(
            "$resolvedProjectWork\",
            [System.StringComparison]::OrdinalIgnoreCase
        )) {
            throw "Refusing to remove test path outside project work: '$resolvedWork'."
        }
        Remove-Item -LiteralPath $resolvedWork -Recurse -Force
    }
}
