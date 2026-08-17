# Send the current China Standard Time to the firmware over Windows COM.
[CmdletBinding()]
param(
    [ValidatePattern('^COM[0-9]+$')]
    [string]$Port = 'COM5',

    [ValidateRange(1, 30)]
    [int]$ReadSeconds = 5
)

$serial = [System.IO.Ports.SerialPort]::new(
    $Port,
    115200,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.Encoding = [System.Text.Encoding]::ASCII
$serial.NewLine = "`n"
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    $serial.Open()

    # Opening the native USB serial port can reset an ESP32-S3. Let v0.1.1 boot
    # and install its USB receive driver before sending the command.
    Start-Sleep -Seconds 2

    $shanghaiNow = [System.TimeZoneInfo]::ConvertTimeBySystemTimeZoneId(
        [System.DateTimeOffset]::UtcNow,
        'China Standard Time'
    )
    $command = 'SET_TIME {0}' -f $shanghaiNow.ToString('yyyy-MM-dd HH:mm:ss')
    $serial.WriteLine($command)
    Write-Output "Sent to ${Port}: $command"

    $deadline = (Get-Date).AddSeconds($ReadSeconds)
    while ((Get-Date) -lt $deadline) {
        $received = $serial.ReadExisting()
        if ($received.Length -gt 0) {
            Write-Output $received
        }
        Start-Sleep -Milliseconds 100
    }
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}
