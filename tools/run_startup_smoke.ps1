[CmdletBinding()]
param(
    [string]$GameDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\STALKER Clear Sky - EE',
    [int]$StartupTimeoutSeconds = 45,
    [int]$ObservationSeconds = 12,
    [string]$ScreenshotPath,
    [switch]$BringToFrontForScreenshot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$appId = 2427420
$runtimeLog = Join-Path $GameDirectory 'cs4x3ui.log'

if ($ScreenshotPath) {
    Add-Type -AssemblyName System.Drawing
    Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class StartupSmokeNative
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Point
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr window, out Rect rect);

    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr window, ref Point point);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr window, int command);
}
'@
}

function Read-SharedTextFile {
    param([Parameter(Mandatory)][string]$Path)

    $stream = [IO.FileStream]::new(
        $Path,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
    )
    try {
        $reader = [IO.StreamReader]::new($stream)
        try {
            return $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

function Save-WindowClientCapture {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][IntPtr]$Window
    )

    $rect = [StartupSmokeNative+Rect]::new()
    if (-not [StartupSmokeNative]::GetClientRect($Window, [ref]$rect)) {
        throw 'GetClientRect failed for the game window.'
    }
    $origin = [StartupSmokeNative+Point]::new()
    if (-not [StartupSmokeNative]::ClientToScreen($Window, [ref]$origin)) {
        throw 'ClientToScreen failed for the game window.'
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) {
        throw "Game client area is invalid: ${width}x${height}."
    }

    $parent = Split-Path -Parent ([System.IO.Path]::GetFullPath($Path))
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $bitmap = [Drawing.Bitmap]::new($width, $height)
    try {
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen(
                $origin.X,
                $origin.Y,
                0,
                0,
                [Drawing.Size]::new($width, $height)
            )
        } finally {
            $graphics.Dispose()
        }
        $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $bitmap.Dispose()
    }

    return [pscustomobject]@{
        path = [System.IO.Path]::GetFullPath($Path)
        width = $width
        height = $height
    }
}

if (Get-Process -Name 'xrEngine' -ErrorAction SilentlyContinue) {
    throw 'xrEngine.exe is already running.'
}
if ($ScreenshotPath -and -not $BringToFrontForScreenshot) {
    throw 'Screenshot capture requires -BringToFrontForScreenshot because desktop capture is occlusion-sensitive.'
}

Remove-Item -LiteralPath $runtimeLog -Force -ErrorAction SilentlyContinue
Start-Process "steam://rungameid/$appId"

$deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
do {
    Start-Sleep -Milliseconds 500
    $process = Get-Process -Name 'xrEngine' -ErrorAction SilentlyContinue
} while (-not $process -and [DateTime]::UtcNow -lt $deadline)

if (-not $process) {
    throw "Steam did not start xrEngine within $StartupTimeoutSeconds seconds."
}

try {
    Start-Sleep -Seconds $ObservationSeconds
    $process.Refresh()
    if ($process.HasExited) {
        throw 'xrEngine exited during the startup observation period.'
    }

    $logText = if (Test-Path -LiteralPath $runtimeLog -PathType Leaf) {
        Read-SharedTextFile -Path $runtimeLog
    } else {
        ''
    }
    $lines = $logText -split '\r?\n'
    $capture = $null
    if ($ScreenshotPath) {
        $process.Refresh()
        if ($process.MainWindowHandle -eq [IntPtr]::Zero) {
            throw 'xrEngine has no main window for the requested screenshot.'
        }
        [void][StartupSmokeNative]::ShowWindow($process.MainWindowHandle, 9)
        if (-not [StartupSmokeNative]::SetForegroundWindow(
            $process.MainWindowHandle
        )) {
            throw 'Unable to bring xrEngine to the foreground for capture.'
        }
        Start-Sleep -Milliseconds 750
        if ([StartupSmokeNative]::GetForegroundWindow() -ne
            $process.MainWindowHandle) {
            throw 'xrEngine is not the foreground window; refusing an occluded screenshot.'
        }
        $capture = Save-WindowClientCapture `
            -Path $ScreenshotPath `
            -Window $process.MainWindowHandle
    }
    $result = [pscustomobject]@{
        processId = $process.Id
        responding = $process.Responding
        backend = $lines |
            Where-Object { $_ -match 'DirectInput backend:' } |
            Select-Object -Last 1
        hooksInstalled = $logText -match 'Runtime hooks installed'
        runtimeActive = $logText -match 'centered 16:10 runtime active'
        resetLine = $lines |
            Where-Object { $_ -match 'UI reset:' } |
            Select-Object -Last 1
        screenshot = $capture
    }

    if (-not $result.responding -or
        -not $result.hooksInstalled -or
        -not $result.runtimeActive -or
        -not $result.resetLine) {
        $result | Format-List | Out-Host
        throw 'Startup smoke test did not observe a healthy active runtime.'
    }

    $result | ConvertTo-Json -Depth 3
} finally {
    if (-not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(15000)) {
            throw 'xrEngine did not close within 15 seconds.'
        }
    }
}
