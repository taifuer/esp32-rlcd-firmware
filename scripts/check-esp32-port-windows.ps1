[CmdletBinding()]
param(
    [ValidatePattern('^COM[0-9]+$')]
    [string]$Port = 'COM5'
)

$device = Get-CimInstance Win32_SerialPort |
    Where-Object { $_.DeviceID -eq $Port } |
    Select-Object -First 1

if ($null -eq $device) {
    Write-Error "Windows does not currently expose $Port."
    exit 2
}

if ($device.PNPDeviceID -notmatch 'VID_303A&PID_1001') {
    Write-Error "$Port is not the expected Espressif USB Serial/JTAG device: $($device.PNPDeviceID)"
    exit 3
}

Write-Output "Target confirmed: $Port"
Write-Output "PNP device: $($device.PNPDeviceID)"
