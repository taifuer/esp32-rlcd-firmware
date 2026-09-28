# Collect only allowlisted AI Chat diagnostics; never start a conversation.
[CmdletBinding()]
param(
    [ValidatePattern('^COM[0-9]+$')][string]$Port = 'COM5',
    [ValidateRange(15, 900)][int]$Seconds = 900,
    [switch]$SelfTest
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Deny by default: no raw stream, arbitrary error text or headers are printed
# or persisted. Only complete, anchored, known firmware messages are allowed.
$patterns = @{
    conversation = @(
        '(?:ws_connected|session_created|session_update_sent|session_updated|session_finished)',
        'transport_end=[0-3] close_code=\d{1,5} expected=[01]'
    )
    usb_commands = @(
        'CHAT unavailable',
        'CHAT result=(?:not_tested|success|failed|cancelled) error=(?:none|authentication|access|model|rate_limit|timeout|network|tls|service|resource|connection_closed|unknown) ws=[01] created=[01] update=[01] ready=[01] end=[0-3] close=\d{1,5}',
        'BOOT version=[0-9]+\.[0-9]+\.[0-9]+(?:-dev\.[0-9]+)? reset=(?:POWER ON|EXTERNAL|SOFTWARE|PANIC|INT WDT|TASK WDT|WATCHDOG|DEEP SLEEP|BROWNOUT|SDIO|USB|JTAG|EFUSE|POWER GLITCH|CPU LOCKUP|UNKNOWN) recovery=(?:yes|no) manual=(?:yes|no) faults=\d+ previous_phase=(?:DISPLAY|STORAGE|HARDWARE|SERVICES|MAIN LOOP|HEALTHY|EARLY START) current_phase=(?:DISPLAY|STORAGE|HARDWARE|SERVICES|MAIN LOOP|HEALTHY|EARLY START) main_stack_free_at_checkpoint=\d+ console_stack_free=\d+ internal_free=\d+ internal_min=\d+ previous_main_stack_free=\d+ previous_internal_free=\d+ previous_internal_min=\d+',
        'NETWORK state=(?:uninitialized|starting|provisioning|connecting|synchronizing|synchronized|retry_wait|error|unknown) configured=(?:yes|no) station_connected=(?:yes|no) automatic=(?:yes|no) failure=(?:none|wifi|ntp|service|unknown) last_error=ESP_[A-Z0-9_]+'
    )
    network_time = @(
        'online network session acquired',
        'online network session released; station (?:kept|stopped)',
        'online network session (?:could not start|Wi-Fi shutdown failed): ESP_[A-Z0-9_]+'
    )
    audio_diagnostics = @(
        'cloud conversation failed: ESP_[A-Z0-9_]+',
        'cloud conversation cancelled; transient audio and text cleared',
        'AI conversation completed after \d+ turn\(s\), \d+ms'
    )
    rlcd_firmware = @(
        'KEY long press: preparing cloud voice generation=\d+',
        'KEY released: cloud voice capture requested',
        'KEY short press: continuing AI conversation',
        'BOOT short press: AI conversation ended',
        'cloud conversation (?:feedback dismissed|cleanup still in progress)',
        'cloud voice (?:start unavailable; using offline voice|could not start): ESP_[A-Z0-9_]+'
    )
    websocket_client = @(
        '(?:Started|Client was not started)',
        '(?:There are no transports valid, stop websocket client|There are no transport|Failed to lock ws-client tasks, exiting the task\.\.\.|Error receive data during initial connection|Error receive data|Connection already closing/closed, skipping abort|Did not get TCP close within expected delay|Connection terminated while waiting for clean TCP close|Failed to allocate\.\.\.|Failed to create websocket transport|Error create websocket task|No scheme found)',
        '(?:esp_websocket_client_create_transport|esp_websocket_client_init|esp_websocket_new_buf)\(\d+\): Memory exhausted',
        'esp_transport_connect\(\) failed with -?\d+, (?:transport_error=(?:ESP_[A-Z0-9_]+|UNKNOWN ERROR), tls_error_code=-?\d+, tls_flags=-?\d+, )?esp_ws_handshake_status_code=-?\d+, errno=-?\d+',
        'esp_transport_(?:read\(\) failed with|write\(\) returned|poll_read\(\) returned) -?\d+, (?:transport_error=(?:ESP_[A-Z0-9_]+|UNKNOWN ERROR), tls_error_code=-?\d+, tls_flags=-?\d+, )?errno=-?\d+',
        'Error, no PONG received for more than \d+ seconds after PING',
        'Could not lock ws-client within \d+ timeout'
    )
    'esp-tls' = @(
        '\[sock=-?\d+\] select\(\) timeout',
        'Failed to open new connection(?: in specified timeout)?'
    )
    'esp-tls-mbedtls' = @(
        'mbedtls_ssl_handshake returned -0x[0-9a-fA-F]+',
        'create_ssl_handle failed, returned \[0x[0-9a-fA-F]+\] \(ESP_[A-Z0-9_]+\)'
    )
    transport_ws = @(
        'HTTP upgrade failed',
        'Error read response for Upgrade header',
        'Sec-WebSocket-Accept not found',
        'Header size exceeded buffer size',
        'Got CLOSE frame with status code=\d+',
        'Connection terminated gracefully',
        'Connection cannot be terminated gracefully within timeout=-?\d+'
    )
}

function Get-SafeLine([string]$Line) {
    $clean = ($Line -replace '\x1B\[[0-9;]*m', '').TrimEnd("`r")
    $parsed = [regex]::Match($clean, '\A(?<level>[IWE]) \((?<tick>\d+)\) (?<tag>[a-z_-]+): (?<body>[^\r\n]*)\z')
    if (-not $parsed.Success) { return $null }
    $tag = $parsed.Groups['tag'].Value
    if (-not $patterns.ContainsKey($tag)) { return $null }
    $body = $parsed.Groups['body'].Value
    foreach ($pattern in $patterns[$tag]) {
        if ([regex]::IsMatch($body, '\A(?:' + $pattern + ')\z')) {
            return ('device_ms=' + $parsed.Groups['tick'].Value + ' ' + $tag + ': ' + $body)
        }
    }
    return $null
}

if ($SelfTest) {
    $accepted = @(
        'I (123) websocket_client: Started',
        'I (125) conversation: transport_end=2 close_code=1008 expected=0',
        'I (126) usb_commands: CHAT result=failed error=connection_closed ws=1 created=1 update=1 ready=0 end=2 close=1008',
        'E (200) websocket_client: esp_transport_connect() failed with -1, transport_error=ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT, tls_error_code=0, tls_flags=0, esp_ws_handshake_status_code=0, errno=0',
        'E (202) esp-tls: [sock=54] select() timeout',
        'W (9000) audio_diagnostics: cloud conversation failed: ESP_ERR_TIMEOUT'
    )
    $rejected = @(
        'E (1) transport_ws: Error write Upgrade header GET /api-ws HTTP/1.1',
        'Authorization: Bearer REDACT_TEST_TOKEN',
        'I (125) conversation: transport_end=2 close_code=1008 expected=0 reason=SECRET',
        'I (126) usb_commands: CHAT result=failed error=SECRET ws=1 created=1 update=1 ready=0 end=2 close=1008',
        'E (2) websocket_client: request headers Authorization: REDACT_TEST_TOKEN',
        'I (3) websocket_client: Started REDACT_TEST_TOKEN',
        'prefix I (4) websocket_client: Started',
        'E (5) esp-tls: Failed to open new connection to SECRET_HOST',
        "I (6) websocket_client: Started`nAuthorization: REDACT_TEST_TOKEN"
    )
    foreach ($line in $accepted) { if ($null -eq (Get-SafeLine $line)) { throw 'Safe sample rejected' } }
    foreach ($line in $rejected) { if ($null -ne (Get-SafeLine $line)) { throw 'Unsafe sample accepted' } }
    Write-Output 'FILTER_SELFTEST_OK'
    exit 0
}

$serial = $null
try {
    $target = Get-CimInstance Win32_SerialPort | Where-Object { $_.DeviceID -eq $Port -and $_.PNPDeviceID -match 'VID_303A&PID_1001' }
    if ($null -eq $target) { throw 'Expected ESP32 serial port not present' }
    $serial = New-Object System.IO.Ports.SerialPort($Port, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.ReadTimeout = 200
    $serial.WriteTimeout = 1000
    $serial.NewLine = "`n"
    $serial.Open()
    Write-Output ('MONITOR_OPEN port=' + $Port + ' duration_s=' + $Seconds)
    $deadline = (Get-Date).AddSeconds($Seconds)
    $queryAt = (Get-Date).AddSeconds(3)
    $queried = $false
    $buffer = ''
    $lastChat = ''
    $nextChatQuery = $queryAt
    $overflow = $false
    while ((Get-Date) -lt $deadline) {
        if (-not $queried -and (Get-Date) -ge $queryAt) {
            $serial.WriteLine('GET_BOOT')
            $serial.WriteLine('GET_NETWORK')
            $queried = $true
            Write-Output 'READ_ONLY_QUERIES_SENT'
        }
        if ($queried -and (Get-Date) -ge $nextChatQuery) {
            $serial.WriteLine('GET_CHAT')
            $nextChatQuery = (Get-Date).AddSeconds(5)
        }
        $chunk = $serial.ReadExisting()
        foreach ($character in $chunk.ToCharArray()) {
            if ($character -eq "`n") {
                if (-not $overflow) {
                    $safe = Get-SafeLine $buffer
                    if ($null -ne $safe) {
                        $chatBody = $safe -replace '^device_ms=\d+ ', ''
                        if (-not $chatBody.StartsWith('usb_commands: CHAT ') -or $chatBody -ne $lastChat) {
                            Write-Output $safe
                        }
                        if ($chatBody.StartsWith('usb_commands: CHAT ')) { $lastChat = $chatBody }
                    }
                }
                $buffer = ''
                $overflow = $false
            } elseif (-not $overflow) {
                $buffer += $character
                if ($buffer.Length -gt 4096) { $overflow = $true; $buffer = '' }
            }
        }
        Start-Sleep -Milliseconds 100
    }
    Write-Output 'MONITOR_FINISHED'
} catch {
    Write-Output ('MONITOR_ERROR type=' + $_.Exception.GetType().Name + ' hresult=' + $_.Exception.HResult)
    exit 1
} finally {
    if ($null -ne $serial) { if ($serial.IsOpen) { $serial.Close() }; $serial.Dispose() }
}
