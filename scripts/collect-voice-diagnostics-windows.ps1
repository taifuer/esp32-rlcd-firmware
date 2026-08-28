# Run the fixed voice-reliability matrix over the native ESP32 USB port.
[CmdletBinding()]
param(
    [ValidatePattern('^COM[0-9]+$')]
    [string]$Port = 'COM5',

    [ValidateRange(30, 600)]
    [int]$SessionTimeoutSeconds = 180,

    [switch]$SelfTest,

    [string]$OutputFile = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ExpectedSessionCount = 50
$script:serial = $null
$script:writer = $null
$script:criticalLineBuffer = ''
$script:criticalEventDetected = $false
$script:baselineEstablished = $false
$script:unexpectedResetAfterBaseline = $false

function Save-CriticalSerialLines {
    param([string]$Text)

    $combined = ($script:criticalLineBuffer + $Text).Replace("`r", '')
    $lines = $combined -split "`n"
    $completeCount = $lines.Count - 1
    if ($combined.EndsWith("`n")) {
        $script:criticalLineBuffer = ''
    }
    else {
        $script:criticalLineBuffer = $lines[$lines.Count - 1]
    }

    for ($index = 0; $index -lt $completeCount; $index++) {
        $clean = $lines[$index] -replace "$([char]27)\[[0-9;]*m", ''
        $recordReset = $clean -match 'rst:0x'
        $critical = $clean -match 'Brownout|Guru Meditation|panic|abort\(\)|watchdog|stack overflow'
        if ($recordReset -or $critical) {
            [void]$script:writer.WriteLine("DEVICE_EVENT $clean")
        }
        if ($recordReset -and $script:baselineEstablished) {
            $script:unexpectedResetAfterBaseline = $true
            Write-Host "Unexpected reset after baseline: $clean"
        }
        if ($critical) {
            $script:criticalEventDetected = $true
            Write-Host "Critical device event: $clean"
        }
    }
}

function Read-SerialText {
    $received = $script:serial.ReadExisting()
    if ($received.Length -gt 0) {
        Save-CriticalSerialLines -Text $received
    }
    return $received
}

function Read-ForMilliseconds {
    param([int]$Milliseconds)

    $deadline = (Get-Date).AddMilliseconds($Milliseconds)
    $captured = ''
    while ((Get-Date) -lt $deadline) {
        $captured += Read-SerialText
        Start-Sleep -Milliseconds 100
    }
    return $captured
}

function Get-LastRegexMatch {
    param(
        [string]$Text,
        [string]$Pattern
    )

    $matches = [regex]::Matches($Text, $Pattern)
    if ($matches.Count -eq 0) {
        return $null
    }
    return $matches[$matches.Count - 1]
}

function Get-VoiceSnapshot {
    param([int]$TimeoutSeconds = 3)

    $script:serial.WriteLine('GET_VOICE')
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $captured = ''
    $voicePattern = 'VOICE version=(?<version>\S+) state=(?<state>\S+) initialized=(?<initialized>\S+) model=(?<model>\S+) engine=(?<engine>\S+) microphone=(?<microphone>\S+) running=(?<running>\S+) generation=(?<generation>\d+) elapsed_ms=(?<elapsed>\d+) result=(?<result>\S+) command=(?<command>-?\d+) confidence=(?<confidence>[0-9.]+) last_error=(?<last_error>\S+)'
    $summaryPattern = 'VOICE_DIAG sessions=(?<sessions>\d+) matched=(?<matched>\d+) no_voice=(?<no_voice>\d+) not_understood=(?<not_understood>\d+) cancelled=(?<cancelled>\d+) failed=(?<failed>\d+) errors=(?<errors>\d+) capture_max_ms=(?<capture_max>\d+) wall_max_ms=(?<wall_max>\d+) last_seq=(?<last_seq>\d+) last_outcome=(?<last_outcome>\S+) last_command=(?<last_command>-?\d+) last_confidence_permille=(?<last_confidence>\d+) last_capture_ms=(?<last_capture>\d+) last_wall_ms=(?<last_wall>\d+)'
    $internalPattern = 'VOICE_DIAG_INTERNAL end_first=\d+ end_latest=\d+ trend=-?\d+ min=\d+ largest_first=\d+ largest_latest=\d+ largest_min=\d+ last=\d+/\d+/\d+ last_largest=\d+/\d+/\d+'
    $psramPattern = 'VOICE_DIAG_PSRAM end_first=\d+ end_latest=\d+ trend=-?\d+ min=\d+ largest_first=\d+ largest_latest=\d+ largest_min=\d+ last=\d+/\d+/\d+ last_largest=\d+/\d+/\d+'
    $runtimePattern = 'VOICE_DIAG_RUNTIME stack_hwm_first=(?<stack_first>\d+) stack_hwm_latest=(?<stack_latest>\d+) stack_hwm_min=(?<stack_min>\d+) last_stack=\d+/\d+/\d+ cpu_acquire_failures=(?<acquire_failures>\d+) cpu_release_failures=(?<release_failures>\d+) last_cpu_acquired=(?<last_acquired>\S+) last_cpu_released=(?<last_released>\S+) last_error=(?<runtime_error>-?\d+)'

    while ((Get-Date) -lt $deadline) {
        $captured += Read-SerialText
        if ($captured.Length -gt 65536) {
            $captured = $captured.Substring($captured.Length - 65536)
        }
        $voice = Get-LastRegexMatch -Text $captured -Pattern $voicePattern
        $summary = Get-LastRegexMatch -Text $captured -Pattern $summaryPattern
        $internal = Get-LastRegexMatch -Text $captured -Pattern $internalPattern
        $psram = Get-LastRegexMatch -Text $captured -Pattern $psramPattern
        $runtime = Get-LastRegexMatch -Text $captured -Pattern $runtimePattern
        if ($null -ne $voice -and $null -ne $summary -and
            $null -ne $internal -and $null -ne $psram -and
            $null -ne $runtime) {
            return [pscustomobject]@{
                Version = $voice.Groups['version'].Value
                State = $voice.Groups['state'].Value
                Initialized = $voice.Groups['initialized'].Value
                Model = $voice.Groups['model'].Value
                Engine = $voice.Groups['engine'].Value
                Microphone = $voice.Groups['microphone'].Value
                Running = $voice.Groups['running'].Value
                Result = $voice.Groups['result'].Value
                Command = [int]$voice.Groups['command'].Value
                Sessions = [int]$summary.Groups['sessions'].Value
                Matched = [int]$summary.Groups['matched'].Value
                NoVoice = [int]$summary.Groups['no_voice'].Value
                NotUnderstood = [int]$summary.Groups['not_understood'].Value
                Cancelled = [int]$summary.Groups['cancelled'].Value
                Failed = [int]$summary.Groups['failed'].Value
                Errors = [int]$summary.Groups['errors'].Value
                LastOutcome = $summary.Groups['last_outcome'].Value
                LastCommand = [int]$summary.Groups['last_command'].Value
                StackMinimum = [int]$runtime.Groups['stack_min'].Value
                AcquireFailures = [int]$runtime.Groups['acquire_failures'].Value
                ReleaseFailures = [int]$runtime.Groups['release_failures'].Value
                LastRuntimeError = [int]$runtime.Groups['runtime_error'].Value
                RawLines = @(
                    $voice.Value,
                    $summary.Value,
                    $internal.Value,
                    $psram.Value,
                    $runtime.Value
                )
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "GET_VOICE did not return a complete snapshot within $TimeoutSeconds seconds."
}

function Save-VoiceSnapshot {
    param(
        [string]$Label,
        [object]$Snapshot
    )

    [void]$script:writer.WriteLine("HOST_VOICE_DIAG snapshot=$Label")
    foreach ($line in $Snapshot.RawLines) {
        [void]$script:writer.WriteLine($line)
    }
}

function Wait-ForVoiceReady {
    $deadline = (Get-Date).AddSeconds(45)
    $lastState = 'no response'
    while ((Get-Date) -lt $deadline) {
        try {
            $snapshot = Get-VoiceSnapshot -TimeoutSeconds 3
            $lastState = "initialized=$($snapshot.Initialized) model=$($snapshot.Model) engine=$($snapshot.Engine) microphone=$($snapshot.Microphone) running=$($snapshot.Running)"
            if ($snapshot.Initialized -eq 'yes' -and
                $snapshot.Model -eq 'ready' -and
                $snapshot.Engine -eq 'ready' -and
                $snapshot.Microphone -eq 'ready' -and
                $snapshot.Running -eq 'no') {
                return $snapshot
            }
        }
        catch {
            $lastState = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Voice service was not ready after 45 seconds: $lastState"
}

function Reset-VoiceCounters {
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        $script:serial.WriteLine('RESET_VOICE_DIAG')
        $response = Read-ForMilliseconds -Milliseconds 800
        if ($response -match 'VOICE_DIAG_RESET_OK') {
            $snapshot = Get-VoiceSnapshot -TimeoutSeconds 3
            if ($snapshot.Sessions -eq 0 -and $snapshot.Running -eq 'no') {
                return $snapshot
            }
        }
        Start-Sleep -Milliseconds 300
    }
    throw 'RESET_VOICE_DIAG was not acknowledged with sessions=0.'
}

function Wait-ForSessionNumber {
    param([int]$ExpectedSessions)

    $deadline = (Get-Date).AddSeconds($SessionTimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $snapshot = Get-VoiceSnapshot -TimeoutSeconds 3
        if ($snapshot.Sessions -gt $ExpectedSessions) {
            throw "Unexpected extra voice session: expected $ExpectedSessions, got $($snapshot.Sessions)."
        }
        if ($snapshot.Sessions -eq $ExpectedSessions -and
            $snapshot.Running -eq 'no') {
            return $snapshot
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Voice session $ExpectedSessions did not finish within $SessionTimeoutSeconds seconds."
}

function Add-VoiceCase {
    param(
        [ref]$Cases,
        [string]$Scene,
        [string]$Stimulus,
        [string]$ExpectedResult,
        [int]$ExpectedCommand
    )

    $Cases.Value += [pscustomobject]@{
        Scene = $Scene
        Stimulus = $Stimulus
        ExpectedResult = $ExpectedResult
        ExpectedCommand = $ExpectedCommand
    }
}

function Read-RequiredCondition {
    param(
        [string]$Scene,
        [string]$Name,
        [string]$Prompt
    )

    $value = Read-Host $Prompt
    if ([string]::IsNullOrWhiteSpace($value)) {
        throw "A value is required for $Name."
    }
    $clean = $value.Replace("`r", ' ').Replace("`n", ' ').Trim()
    [void]$script:writer.WriteLine(
        "HOST_VOICE_DIAG condition scene=$Scene name=$Name value=$clean"
    )
}

$phrases = @(
    [pscustomobject]@{ Stimulus = 'hui_dao_zhu_ye'; Command = 1 },
    [pscustomobject]@{ Stimulus = 'cha_kan_shi_jian'; Command = 1 },
    [pscustomobject]@{ Stimulus = 'da_kai_ri_li'; Command = 2 },
    [pscustomobject]@{ Stimulus = 'cha_kan_ri_qi'; Command = 2 },
    [pscustomobject]@{ Stimulus = 'cha_kan_zhuang_tai'; Command = 3 },
    [pscustomobject]@{ Stimulus = 'she_bei_zhuang_tai'; Command = 3 },
    [pscustomobject]@{ Stimulus = 'da_kai_tu_pian'; Command = 4 },
    [pscustomobject]@{ Stimulus = 'cha_kan_tu_pian'; Command = 4 },
    [pscustomobject]@{ Stimulus = 'da_kai_she_zhi'; Command = 5 },
    [pscustomobject]@{ Stimulus = 'cha_kan_she_zhi'; Command = 5 },
    [pscustomobject]@{ Stimulus = 'qu_xiao'; Command = 6 }
)
$testCases = @()
foreach ($scene in @('near_quiet', 'far', 'daily_noise')) {
    foreach ($phrase in $phrases) {
        Add-VoiceCase -Cases ([ref]$testCases) -Scene $scene `
            -Stimulus $phrase.Stimulus -ExpectedResult 'matched' `
            -ExpectedCommand $phrase.Command
    }
}
foreach ($stimulus in @(
    'jin_tian_tian_qi_zen_me_yang',
    'bo_fang_yin_yue',
    'da_kai_lan_ya',
    'she_zhi_nao_zhong',
    'geng_xin_gu_jian'
)) {
    Add-VoiceCase -Cases ([ref]$testCases) -Scene 'irrelevant_chinese' `
        -Stimulus $stimulus -ExpectedResult 'not_understood' `
        -ExpectedCommand 0
}
for ($index = 1; $index -le 4; $index++) {
    Add-VoiceCase -Cases ([ref]$testCases) -Scene 'silence' `
        -Stimulus "silence_$index" -ExpectedResult 'no_voice' `
        -ExpectedCommand 0
}
for ($index = 1; $index -le 4; $index++) {
    Add-VoiceCase -Cases ([ref]$testCases) -Scene 'boot_cancel' `
        -Stimulus "press_boot_during_listening_$index" `
        -ExpectedResult 'cancelled' -ExpectedCommand 0
}
for ($index = 1; $index -le 4; $index++) {
    $alarmStimulus = if ($index -eq 1) {
        'configure_next_minute_then_start_within_5s'
    }
    else {
        'stop_previous_reconfigure_next_minute_then_start_within_5s'
    }
    Add-VoiceCase -Cases ([ref]$testCases) -Scene 'alarm_preempt' `
        -Stimulus "${alarmStimulus}_$index" `
        -ExpectedResult 'cancelled' -ExpectedCommand 0
}
if ($testCases.Count -ne $ExpectedSessionCount) {
    throw "Internal matrix error: expected $ExpectedSessionCount cases, got $($testCases.Count)."
}

if ($SelfTest) {
    $script:writer = [System.IO.StringWriter]::new()
    Save-CriticalSerialLines -Text 'rst:0'
    Save-CriticalSerialLines -Text "x1`nGuru Medi"
    Save-CriticalSerialLines -Text "tation error`ntail"
    $records = $script:writer.ToString()
    if ($records -notmatch 'DEVICE_EVENT rst:0x1' -or
        $records -notmatch 'DEVICE_EVENT Guru Meditation error' -or
        $script:criticalLineBuffer -ne 'tail' -or
        -not $script:criticalEventDetected -or
        $testCases.Count -ne $ExpectedSessionCount) {
        [Console]::Error.WriteLine(
            'Voice diagnostics collector self-test failed.'
        )
        exit 1
    }
    Write-Output "Collector self-test passed; matrix cases=$($testCases.Count)."
    exit 0
}

if ([string]::IsNullOrWhiteSpace($OutputFile)) {
    [Console]::Error.WriteLine(
        'OutputFile is required unless -SelfTest is used.'
    )
    exit 2
}

$exitCode = 0
try {
    $encoding = [System.Text.UTF8Encoding]::new($false)
    $script:writer = [System.IO.StreamWriter]::new(
        $OutputFile, $false, $encoding
    )
    $script:writer.AutoFlush = $true
    $script:serial = [System.IO.Ports.SerialPort]::new(
        $Port,
        115200,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One
    )
    $script:serial.Encoding = [System.Text.Encoding]::ASCII
    $script:serial.NewLine = "`n"
    $script:serial.DtrEnable = $false
    $script:serial.RtsEnable = $false
    $script:serial.Open()

    [void](Read-ForMilliseconds -Milliseconds 2000)
    $ready = Wait-ForVoiceReady
    Save-VoiceSnapshot -Label 'ready' -Snapshot $ready
    Write-Host "Voice service ready on $Port, firmware $($ready.Version)."
    [void]$script:writer.WriteLine(
        "HOST_VOICE_DIAG started_at=$([DateTimeOffset]::Now.ToString('o')) port=$Port version=$($ready.Version) power=normal_usb"
    )

    $resetBeforeWarmup = Reset-VoiceCounters
    Save-VoiceSnapshot -Label 'before_warmup' -Snapshot $resetBeforeWarmup
    Write-Host 'Warm-up: run one supported voice command on the device now.'
    $warmup = Wait-ForSessionNumber -ExpectedSessions 1
    Save-VoiceSnapshot -Label 'warmup' -Snapshot $warmup

    $baseline = Reset-VoiceCounters
    Save-VoiceSnapshot -Label 'baseline' -Snapshot $baseline
    [void](Read-ForMilliseconds -Milliseconds 300)
    $script:baselineEstablished = $true
    Write-Host 'Warm-up complete. Starting the fixed 50-session matrix.'

    $caseMismatches = 0
    $previousScene = ''
    for ($index = 0; $index -lt $testCases.Count; $index++) {
        $case = $testCases[$index]
        $caseNumber = $index + 1
        if ($case.Scene -ne $previousScene) {
            Write-Host "Prepare scene: $($case.Scene). See docs/development.md for conditions."
            [void]$script:writer.WriteLine(
                "HOST_VOICE_DIAG scene_begin=$($case.Scene)"
            )
            switch ($case.Scene) {
            'near_quiet' {
                Read-RequiredCondition -Scene $case.Scene `
                    -Name 'distance' `
                    -Prompt 'Enter the measured near distance (for example 0.5 m)'
            }
            'far' {
                Read-RequiredCondition -Scene $case.Scene `
                    -Name 'distance' `
                    -Prompt 'Enter the measured far distance (for example 2 m)'
            }
            'daily_noise' {
                Read-RequiredCondition -Scene $case.Scene `
                    -Name 'noise' `
                    -Prompt 'Describe the noise source, position, and approximate level'
            }
            }
            $previousScene = $case.Scene
        }
        $marker = "HOST_VOICE_DIAG case=$caseNumber scene=$($case.Scene) stimulus=$($case.Stimulus) expected_result=$($case.ExpectedResult) expected_command=$($case.ExpectedCommand)"
        [void]$script:writer.WriteLine($marker)
        Write-Host "[$caseNumber/$ExpectedSessionCount] $($case.Scene): $($case.Stimulus)"

        $snapshot = Wait-ForSessionNumber -ExpectedSessions $caseNumber
        Save-VoiceSnapshot -Label "case_$caseNumber" -Snapshot $snapshot
        $matchesExpectation =
            $snapshot.LastOutcome -eq $case.ExpectedResult -and
            $snapshot.LastCommand -eq $case.ExpectedCommand
        if (-not $matchesExpectation) {
            $caseMismatches++
        }
        [void]$script:writer.WriteLine(
            "HOST_VOICE_DIAG case_result=$caseNumber matched_expectation=$($matchesExpectation.ToString().ToLowerInvariant()) actual_result=$($snapshot.LastOutcome) actual_command=$($snapshot.LastCommand)"
        )
    }

    $final = Get-VoiceSnapshot -TimeoutSeconds 3
    Save-VoiceSnapshot -Label 'final' -Snapshot $final
    $outcomeTotal = $final.Matched + $final.NoVoice +
        $final.NotUnderstood + $final.Cancelled + $final.Failed
    $hardFailures = @()
    if ($final.Sessions -ne $ExpectedSessionCount) {
        $hardFailures += "sessions=$($final.Sessions)"
    }
    if ($outcomeTotal -ne $ExpectedSessionCount) {
        $hardFailures += "outcome_total=$outcomeTotal"
    }
    if ($final.Errors -ne 0 -or $final.Failed -ne 0) {
        $hardFailures += "errors=$($final.Errors),failed=$($final.Failed)"
    }
    if ($final.AcquireFailures -ne 0 -or
        $final.ReleaseFailures -ne 0) {
        $hardFailures += "cpu_lock=$($final.AcquireFailures)/$($final.ReleaseFailures)"
    }
    if ($final.StackMinimum -le 0) {
        $hardFailures += "stack_hwm_min=$($final.StackMinimum)"
    }
    if ($script:criticalEventDetected) {
        $hardFailures += 'critical_device_event=yes'
    }
    if ($script:unexpectedResetAfterBaseline) {
        $hardFailures += 'unexpected_reset_after_baseline=yes'
    }
    if ($hardFailures.Count -gt 0) {
        throw "Voice reliability integrity checks failed: $($hardFailures -join '; ')"
    }

    [void]$script:writer.WriteLine(
        "HOST_VOICE_DIAG result=complete sessions=$($final.Sessions) acoustic_mismatches=$caseMismatches mechanical_checks=pass resource_review=pending"
    )
    Write-Host "Matrix complete: $($final.Sessions) sessions, $caseMismatches acoustic expectation mismatches. Mechanical checks passed; resource trend review is pending."
}
catch {
    $exitCode = 1
    if ($null -ne $script:writer) {
        [void]$script:writer.WriteLine(
            "HOST_VOICE_DIAG result=failed message=$($_.Exception.Message)"
        )
    }
    [Console]::Error.WriteLine("Voice diagnostics failed: $($_.Exception.Message)")
}
finally {
    if ($null -ne $script:serial -and $script:serial.IsOpen) {
        $script:serial.Close()
    }
    if ($null -ne $script:writer) {
        $script:writer.Dispose()
    }
}

if ($exitCode -ne 0) {
    exit $exitCode
}
Write-Output "Voice reliability log: $OutputFile"
