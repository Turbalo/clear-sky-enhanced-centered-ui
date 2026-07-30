[CmdletBinding()]
param(
    [ValidateSet('Install', 'Uninstall')]
    [string]$Action = 'Install',
    [string]$GamePath,
    [switch]$Interactive,
    [string]$DiagnosticPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$AppId = '2427420'
$StateDirectoryName = '.cs4x3ui'
$ManifestName = 'install-manifest.json'
$PayloadRoot = Join-Path $PSScriptRoot 'payload'
$ProfilePath = Join-Path $PSScriptRoot 'compatibility-profile.psd1'
$script:SelectedGamePath = $null
$script:CompatibilityResult = $null

if (-not (Test-Path -LiteralPath $ProfilePath -PathType Leaf)) {
    throw "Compatibility profile is missing: '$ProfilePath'."
}
$Profile = Import-PowerShellDataFile -LiteralPath $ProfilePath

function Get-NormalizedPath {
    param([Parameter(Mandatory)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Get-FileSha256 {
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Convert-HexToBytes {
    param([Parameter(Mandatory)][string]$Hex)
    $tokens = $Hex -split '\s+' | Where-Object { $_ }
    $bytes = [byte[]]::new($tokens.Count)
    for ($index = 0; $index -lt $tokens.Count; $index++) {
        $bytes[$index] = [Convert]::ToByte($tokens[$index], 16)
    }
    return $bytes
}

function Read-PeImage {
    param([Parameter(Mandatory)][string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x100 -or
        $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw 'xrEngine.exe is not a valid PE executable.'
    }

    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 24 -gt $bytes.Length -or
        $bytes[$peOffset] -ne 0x50 -or
        $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or
        $bytes[$peOffset + 3] -ne 0) {
        throw 'xrEngine.exe has an invalid PE header.'
    }

    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    $sectionCount = [BitConverter]::ToUInt16($bytes, $peOffset + 6)
    $optionalSize = [BitConverter]::ToUInt16($bytes, $peOffset + 20)
    $optionalOffset = $peOffset + 24
    if ($optionalOffset + $optionalSize -gt $bytes.Length) {
        throw 'xrEngine.exe has a truncated optional header.'
    }
    $optionalMagic = [BitConverter]::ToUInt16($bytes, $optionalOffset)
    $sectionOffset = $optionalOffset + $optionalSize
    $sections = [System.Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $sectionCount; $index++) {
        $offset = $sectionOffset + $index * 40
        if ($offset + 40 -gt $bytes.Length) {
            throw 'xrEngine.exe has a truncated section table.'
        }
        $rawSize = [BitConverter]::ToUInt32($bytes, $offset + 16)
        $rawOffset = [BitConverter]::ToUInt32($bytes, $offset + 20)
        $characteristics = [BitConverter]::ToUInt32($bytes, $offset + 36)
        if (($characteristics -band 0x20000000) -eq 0 -or
            $rawSize -eq 0) {
            continue
        }
        if ([uint64]$rawOffset + [uint64]$rawSize -gt $bytes.Length) {
            throw 'xrEngine.exe has an invalid executable section.'
        }
        $sections.Add([pscustomobject]@{
            virtualAddress = [BitConverter]::ToUInt32($bytes, $offset + 12)
            rawOffset = $rawOffset
            rawSize = $rawSize
        })
    }

    return [pscustomobject]@{
        bytes = $bytes
        machine = $machine
        optionalMagic = $optionalMagic
        sections = $sections
    }
}

function Find-PeSignature {
    param(
        [Parameter(Mandatory)]$Image,
        [Parameter(Mandatory)][byte[]]$Pattern
    )

    $encoding = [Text.Encoding]::GetEncoding(28591)
    $patternText = $encoding.GetString($Pattern)
    $escaped = [regex]::Escape($patternText)
    $rvas = [System.Collections.Generic.List[uint32]]::new()
    foreach ($section in $Image.sections) {
        $sectionText = $encoding.GetString(
            $Image.bytes, [int]$section.rawOffset, [int]$section.rawSize
        )
        foreach ($match in [regex]::Matches(
            $sectionText, $escaped,
            [Text.RegularExpressions.RegexOptions]::CultureInvariant
        )) {
            $rvas.Add([uint32]($section.virtualAddress + $match.Index))
        }
    }
    return @($rvas)
}

function Test-EngineCompatibility {
    param([Parameter(Mandatory)][string]$Path)

    $engine = Join-Path $Path 'xrEngine.exe'
    if (-not (Test-Path -LiteralPath $engine -PathType Leaf)) {
        throw "xrEngine.exe was not found in '$Path'. Select the game directory that directly contains xrEngine.exe."
    }

    $image = Read-PeImage $engine
    $hash = Get-FileSha256 $engine
    $version = (Get-Item -LiteralPath $engine).VersionInfo.FileVersion
    $signatureResults = foreach ($signature in $Profile.Signatures) {
        $rvas = @(Find-PeSignature `
            -Image $image -Pattern (Convert-HexToBytes $signature.Hex))
        $expectedRva = [uint32]$signature.ExpectedRva
        [pscustomobject]@{
            name = [string]$signature.Name
            count = $rvas.Count
            expectedRva = $expectedRva
            foundRvas = $rvas
            passed = (
                $rvas.Count -eq 1 -and $rvas[0] -eq $expectedRva
            )
        }
    }

    $isX64 = $image.machine -eq 0x8664 -and
        $image.optionalMagic -eq 0x20B
    $allSignatures = @(
        $signatureResults | Where-Object { -not $_.passed }
    ).Count -eq 0
    $knownHash = @($Profile.KnownSha256) -contains $hash
    $compatible = $isX64 -and $allSignatures
    $mode = if ($knownHash -and $compatible) {
        'Verified'
    } elseif ($compatible) {
        'Signature-compatible'
    } else {
        'Unsupported'
    }

    return [pscustomobject]@{
        enginePath = $engine
        sha256 = $hash
        fileVersion = $version
        machine = ('0x{0:X4}' -f $image.machine)
        optionalMagic = ('0x{0:X4}' -f $image.optionalMagic)
        isX64 = $isX64
        knownHash = $knownHash
        allSignatures = $allSignatures
        compatible = $compatible
        mode = $mode
        signatures = @($signatureResults)
    }
}

function Get-SteamRoots {
    $roots = [System.Collections.Generic.List[string]]::new()
    $registryPaths = @(
        'HKCU:\Software\Valve\Steam',
        'HKLM:\Software\WOW6432Node\Valve\Steam',
        'HKLM:\Software\Valve\Steam'
    )
    foreach ($registryPath in $registryPaths) {
        if (-not (Test-Path -LiteralPath $registryPath)) {
            continue
        }
        $properties = Get-ItemProperty -LiteralPath $registryPath
        foreach ($name in @('SteamPath', 'InstallPath')) {
            $property = $properties.PSObject.Properties[$name]
            if ($property -and $property.Value) {
                $roots.Add((Get-NormalizedPath $property.Value))
            }
        }
    }

    foreach ($root in @($roots)) {
        $librariesFile = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $librariesFile)) {
            continue
        }
        $content = Get-Content -LiteralPath $librariesFile -Raw
        foreach ($match in [regex]::Matches($content, '"path"\s+"([^"]+)"')) {
            $library = $match.Groups[1].Value -replace '\\\\', '\'
            $roots.Add((Get-NormalizedPath $library))
        }
    }

    return $roots | Sort-Object -Unique
}

function Find-SteamGamePath {
    foreach ($steamRoot in Get-SteamRoots) {
        $manifestPath = Join-Path $steamRoot "steamapps\appmanifest_$AppId.acf"
        if (-not (Test-Path -LiteralPath $manifestPath)) {
            continue
        }
        $manifestText = Get-Content -LiteralPath $manifestPath -Raw
        $match = [regex]::Match($manifestText, '"installdir"\s+"([^"]+)"')
        if ($match.Success) {
            return Get-NormalizedPath (
                Join-Path $steamRoot "steamapps\common\$($match.Groups[1].Value)"
            )
        }
    }
    return $null
}

function Select-GameDirectory {
    param([string]$InitialPath)

    Add-Type -AssemblyName System.Windows.Forms
    $dialog = [System.Windows.Forms.OpenFileDialog]::new()
    try {
        $dialog.Title = 'Select Clear Sky Enhanced Edition xrEngine.exe'
        $dialog.Filter = 'Clear Sky engine (xrEngine.exe)|xrEngine.exe'
        $dialog.FileName = 'xrEngine.exe'
        $dialog.CheckFileExists = $true
        $dialog.Multiselect = $false
        if ($InitialPath -and
            (Test-Path -LiteralPath $InitialPath -PathType Container)) {
            $dialog.InitialDirectory = $InitialPath
        }
        if ($dialog.ShowDialog() -ne
            [System.Windows.Forms.DialogResult]::OK) {
            throw 'Game selection was cancelled.'
        }
        return Get-NormalizedPath (Split-Path -Parent $dialog.FileName)
    } finally {
        $dialog.Dispose()
    }
}

function Resolve-GamePath {
    if ($GamePath) {
        return Get-NormalizedPath $GamePath
    }

    $steamPath = Find-SteamGamePath
    if ($Interactive) {
        return Select-GameDirectory -InitialPath $steamPath
    }
    if ($steamPath) {
        return $steamPath
    }
    throw "Steam installation for AppID $AppId was not found. Run install.cmd to select xrEngine.exe, or pass -GamePath."
}

function Assert-SupportedGame {
    param([Parameter(Mandatory)][string]$Path)

    $script:CompatibilityResult = Test-EngineCompatibility $Path
    if (-not $script:CompatibilityResult.compatible) {
        if (-not $script:CompatibilityResult.isX64) {
            throw 'This is not the 64-bit Enhanced Edition engine. The classic 32-bit Clear Sky engine is incompatible.'
        }
        throw 'The selected xrEngine.exe does not match the required runtime signatures and offsets.'
    }
    return $script:CompatibilityResult
}

function Assert-GameNotRunning {
    if (Get-Process -Name 'xrEngine' -ErrorAction SilentlyContinue) {
        throw 'xrEngine.exe is running. Exit the game before installing or uninstalling the mod.'
    }
}

function Get-PayloadFiles {
    if (-not (Test-Path -LiteralPath $PayloadRoot -PathType Container)) {
        throw "Installer payload is missing: '$PayloadRoot'."
    }
    return Get-ChildItem -LiteralPath $PayloadRoot -Recurse -File
}

function Get-RelativePayloadPath {
    param([Parameter(Mandatory)][string]$Path)
    $root = Get-NormalizedPath $PayloadRoot
    $fullPath = Get-NormalizedPath $Path
    $prefix = "$root\"
    if (-not $fullPath.StartsWith(
        $prefix,
        [System.StringComparison]::OrdinalIgnoreCase
    )) {
        throw "Payload file is outside the payload root: '$fullPath'."
    }
    return $fullPath.Substring($prefix.Length)
}

function Install-Mod {
    param([Parameter(Mandatory)][string]$Path)
    Assert-GameNotRunning
    $compatibility = Assert-SupportedGame $Path

    $stateRoot = Join-Path $Path $StateDirectoryName
    $manifestPath = Join-Path $stateRoot $ManifestName
    if (Test-Path -LiteralPath $manifestPath) {
        $existingManifest = Get-Content -LiteralPath $manifestPath -Raw |
            ConvertFrom-Json
        if ($existingManifest.PSObject.Properties['uninstalledAtUtc']) {
            $historyStem = 'install-manifest.{0}' -f (
                [DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff')
            )
            $historyName = "$historyStem.json"
            $historyIndex = 1
            while (Test-Path -LiteralPath (Join-Path $stateRoot $historyName)) {
                $historyName = "$historyStem.$historyIndex.json"
                $historyIndex++
            }
            Move-Item -LiteralPath $manifestPath -Destination (
                Join-Path $stateRoot $historyName
            )
        } else {
            throw "The mod is already installed. Uninstall it before reinstalling."
        }
    }

    $payloadFiles = @(Get-PayloadFiles)
    $payloadProxy = Join-Path $PayloadRoot 'dinput8.dll'
    if (-not (Test-Path -LiteralPath $payloadProxy -PathType Leaf)) {
        throw 'Payload dinput8.dll is missing.'
    }

    New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null
    $backupRoot = Join-Path $stateRoot 'backup'
    $entries = [System.Collections.Generic.List[object]]::new()
    $chainedProxy = $false
    $manifestTemporaryPath = "$manifestPath.tmp"

    $targetProxy = Join-Path $Path 'dinput8.dll'
    $chainProxy = Join-Path $Path 'dinput8_chain.dll'
    try {
        if (Test-Path -LiteralPath $targetProxy -PathType Leaf) {
            if (Test-Path -LiteralPath $chainProxy) {
                throw 'Both dinput8.dll and dinput8_chain.dll already exist; chain-loading is ambiguous.'
            }
            Move-Item -LiteralPath $targetProxy -Destination $chainProxy
            $chainedProxy = $true
        }

        foreach ($payloadFile in $payloadFiles) {
            $relativePath = Get-RelativePayloadPath $payloadFile.FullName
            $targetPath = Join-Path $Path $relativePath
            $existed = Test-Path -LiteralPath $targetPath -PathType Leaf
            $backupRelativePath = $null

            if ($existed) {
                $backupRelativePath = Join-Path 'backup' $relativePath
                $backupPath = Join-Path $stateRoot $backupRelativePath
                New-Item -ItemType Directory -Path (
                    Split-Path -Parent $backupPath
                ) -Force | Out-Null
                Copy-Item -LiteralPath $targetPath -Destination $backupPath -Force
            }

            $entry = [pscustomobject]@{
                relativePath = $relativePath
                existed = $existed
                backupRelativePath = $backupRelativePath
                installedSha256 = $null
            }
            $entries.Add($entry)
            New-Item -ItemType Directory -Path (
                Split-Path -Parent $targetPath
            ) -Force | Out-Null
            Copy-Item -LiteralPath $payloadFile.FullName -Destination $targetPath -Force
            $entry.installedSha256 = Get-FileSha256 $targetPath
        }

        $manifest = [pscustomobject]@{
            schemaVersion = 1
            installedAtUtc = [DateTime]::UtcNow.ToString('o')
            appId = $AppId
            gamePath = $Path
            engineSha256 = $compatibility.sha256
            engineFileVersion = $compatibility.fileVersion
            compatibilityMode = $compatibility.mode
            chainedProxy = $chainedProxy
            files = $entries
        }
        $manifest | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath $manifestTemporaryPath -Encoding UTF8
        Move-Item -LiteralPath $manifestTemporaryPath -Destination $manifestPath
    } catch {
        Remove-Item -LiteralPath $manifestTemporaryPath -Force -ErrorAction SilentlyContinue
        $rollbackEntries = @($entries)
        [array]::Reverse($rollbackEntries)
        foreach ($entry in $rollbackEntries) {
            $targetPath = Join-Path $Path $entry.relativePath
            Remove-Item -LiteralPath $targetPath -Force -ErrorAction SilentlyContinue
            if ($entry.existed -and $entry.backupRelativePath) {
                $backupPath = Join-Path $stateRoot $entry.backupRelativePath
                if (Test-Path -LiteralPath $backupPath -PathType Leaf) {
                    Copy-Item -LiteralPath $backupPath -Destination $targetPath -Force
                }
            }
        }
        if ($chainedProxy -and
            (Test-Path -LiteralPath $chainProxy -PathType Leaf) -and
            (-not (Test-Path -LiteralPath $targetProxy))) {
            Move-Item -LiteralPath $chainProxy -Destination $targetProxy
        }
        throw
    }
    Write-Host "Installed Clear Sky Centered UI to '$Path'."
    Write-Host "Compatibility: $($compatibility.mode); xrEngine.exe SHA-256: $($compatibility.sha256)"
}

function Uninstall-Mod {
    param([Parameter(Mandatory)][string]$Path)
    Assert-GameNotRunning
    $stateRoot = Join-Path $Path $StateDirectoryName
    $manifestPath = Join-Path $stateRoot $ManifestName
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Install manifest was not found in '$stateRoot'."
    }

    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $warnings = [System.Collections.Generic.List[string]]::new()
    foreach ($entry in $manifest.files) {
        $targetPath = Join-Path $Path $entry.relativePath
        if (Test-Path -LiteralPath $targetPath -PathType Leaf) {
            $currentHash = Get-FileSha256 $targetPath
            if ($currentHash -ne $entry.installedSha256) {
                $warnings.Add("Modified file left in place: $($entry.relativePath)")
                continue
            }
            Remove-Item -LiteralPath $targetPath -Force
        }

        if ($entry.existed) {
            $backupPath = Join-Path $stateRoot $entry.backupRelativePath
            if (-not (Test-Path -LiteralPath $backupPath -PathType Leaf)) {
                $warnings.Add("Backup missing: $($entry.relativePath)")
                continue
            }
            New-Item -ItemType Directory -Path (
                Split-Path -Parent $targetPath
            ) -Force | Out-Null
            Copy-Item -LiteralPath $backupPath -Destination $targetPath -Force
        }
    }

    if ($manifest.chainedProxy) {
        $targetProxy = Join-Path $Path 'dinput8.dll'
        $chainProxy = Join-Path $Path 'dinput8_chain.dll'
        if ((-not (Test-Path -LiteralPath $targetProxy)) -and
            (Test-Path -LiteralPath $chainProxy -PathType Leaf)) {
            Move-Item -LiteralPath $chainProxy -Destination $targetProxy
        } else {
            $warnings.Add('Previous dinput8.dll could not be restored automatically.')
        }
    }

    $manifest | Add-Member -NotePropertyName 'uninstalledAtUtc' `
        -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Host "Uninstalled Clear Sky Centered UI from '$Path'."
    foreach ($warning in $warnings) {
        Write-Warning $warning
    }
}

function Write-DiagnosticReport {
    param([Parameter(Mandatory)][string]$ErrorMessage)

    $path = $DiagnosticPath
    if (-not $path) {
        $path = Join-Path $env:TEMP 'ClearSky-Centered-UI-diagnostics.txt'
    }
    $path = [System.IO.Path]::GetFullPath($path)
    $parent = Split-Path -Parent $path
    if ($parent) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('Clear Sky Enhanced Edition - Centered UI diagnostics')
    $lines.Add("Generated UTC: $([DateTime]::UtcNow.ToString('o'))")
    $lines.Add("Installer action: $Action")
    $lines.Add("Installer path: $PSScriptRoot")
    $lines.Add("Selected game path: $script:SelectedGamePath")
    $lines.Add("Error: $ErrorMessage")
    $lines.Add("Recommended version: $($Profile.RecommendedVersion)")
    $lines.Add("Recommended source: $($Profile.RecommendedSource)")
    $lines.Add("PowerShell: $($PSVersionTable.PSVersion)")
    $lines.Add("Windows: $([Environment]::OSVersion.VersionString)")

    if ($script:CompatibilityResult) {
        $result = $script:CompatibilityResult
        $lines.Add('')
        $lines.Add("Engine: $($result.enginePath)")
        $lines.Add("File version: $($result.fileVersion)")
        $lines.Add("SHA-256: $($result.sha256)")
        $lines.Add("Machine: $($result.machine)")
        $lines.Add("Optional header: $($result.optionalMagic)")
        $lines.Add("Known SHA: $($result.knownHash)")
        $lines.Add("Compatibility mode: $($result.mode)")
        $lines.Add("All signatures passed: $($result.allSignatures)")
        $lines.Add('')
        $lines.Add('Signature results:')
        foreach ($signature in $result.signatures) {
            $found = if ($signature.foundRvas.Count -eq 0) {
                '<none>'
            } else {
                ($signature.foundRvas | ForEach-Object {
                    '0x{0:X}' -f $_
                }) -join ', '
            }
            $lines.Add(
                ('- {0}: pass={1}; count={2}; expected=0x{3:X}; found={4}' -f
                    $signature.name, $signature.passed, $signature.count,
                    $signature.expectedRva, $found)
            )
        }
    }

    $lines | Set-Content -LiteralPath $path -Encoding UTF8
    return $path
}

function Show-InstallerError {
    param([Parameter(Mandatory)][string]$Message)
    if (-not $Interactive) {
        return
    }
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show(
        $Message,
        'Clear Sky Centered UI - installation failed',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error
    ) | Out-Null
}

try {
    $resolvedGamePath = Resolve-GamePath
    $script:SelectedGamePath = $resolvedGamePath
    if ($Action -eq 'Install') {
        Install-Mod $resolvedGamePath
    } else {
        Uninstall-Mod $resolvedGamePath
    }
} catch {
    $message = $_.Exception.Message
    try {
        $reportPath = Write-DiagnosticReport -ErrorMessage $message
    } catch {
        $reportPath = '<unable to write diagnostic report>'
    }
    $recommendation = ''
    if (-not $script:CompatibilityResult -or
        -not $script:CompatibilityResult.compatible) {
        $recommendation = (
            "Install Clear Sky Enhanced Edition $($Profile.RecommendedVersion) " +
            "($($Profile.RecommendedSource)) if your build is incompatible.`r`n`r`n"
        )
    }
    $fullMessage = "$message`r`n`r`n${recommendation}Diagnostic report: $reportPath"
    Write-Error $fullMessage
    Show-InstallerError -Message $fullMessage
    exit 1
}
