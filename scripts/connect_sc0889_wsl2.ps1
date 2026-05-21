param(
    [string]$Pattern = "Raspberry Pi Debug Probe|CMSIS-DAP|Picoprobe|Debug Probe"
)

$ErrorActionPreference = "Stop"

Write-Host "Checking usbipd..."
if (-not (Get-Command usbipd -ErrorAction SilentlyContinue)) {
    Write-Error "usbipd not found. Install usbipd-win first."
    exit 1
}

Write-Host "Keeping WSL awake..."
wsl.exe -- bash -lc "true" | Out-Null

Write-Host "Looking for Debug Probe / CMSIS-DAP device..."
$lines = usbipd list | Out-String -Stream
$matches = @($lines | Where-Object { $_ -match $Pattern })

if ($matches.Count -eq 0) {
    Write-Host ""
    Write-Host "No matching Debug Probe found. Current USB devices:"
    usbipd list
    Write-Host ""
    Write-Host "Check that the Raspberry Pi Debug Probe is plugged into the PC."
    exit 1
}

if ($matches.Count -gt 1) {
    Write-Host "Multiple possible devices found:"
    for ($i = 0; $i -lt $matches.Count; $i++) {
        Write-Host "[$i] $($matches[$i])"
    }
    $choice = Read-Host "Select device number"
    $deviceLine = $matches[[int]$choice]
} else {
    $deviceLine = $matches[0]
}

$busid = ($deviceLine -split "\s+")[0]

Write-Host ""
Write-Host "Selected device:"
Write-Host $deviceLine
Write-Host "BUSID: $busid"
Write-Host ""

if ($deviceLine -notmatch "Shared|Attached") {
    Write-Host "Device is not shared yet. Running usbipd bind..."
    Write-Host "If this fails, rerun this PowerShell script as Administrator."
    usbipd bind --busid $busid
} else {
    Write-Host "Device already shared or attached."
}

Write-Host ""
Write-Host "Attaching device to WSL2..."
usbipd attach --wsl --busid $busid

Write-Host ""
Write-Host "usbipd status:"
usbipd list | Select-String $busid

Write-Host ""
Write-Host "Checking from inside WSL2:"
wsl.exe -- bash -lc "lsusb | grep -Ei 'Raspberry|CMSIS|DAP|Debug|Picoprobe' || lsusb"

Write-Host ""
Write-Host "Done. The Debug Probe should now be visible inside WSL2."
Write-Host "When finished, detach with:"
Write-Host "  usbipd detach --busid $busid"
