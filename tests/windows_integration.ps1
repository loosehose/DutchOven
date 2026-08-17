[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DutchOvenPath,

    [Parameter(Mandatory = $true)]
    [string]$CanaryPath,

    [Parameter(Mandatory = $true)]
    [string]$ServerAddress,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 65535)]
    [int]$ServerPort
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Condition {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "integration assertion failed: $Message"
    }
}

function Invoke-CanaryClient {
    param([string]$Path, [string]$Address, [int]$Port)
    $process = Start-Process -FilePath $Path -ArgumentList @('client', $Address, $Port) `
        -PassThru -Wait -WindowStyle Hidden
    return $process.ExitCode
}

function Wait-ForBlock {
    param([string]$LogPath, [System.Diagnostics.Process]$Process)
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            throw "DutchOven exited before publishing a block event (exit $($Process.ExitCode))"
        }
        if ((Test-Path $LogPath) -and
            (Select-String -Path $LogPath -Pattern '"event":"block"' -SimpleMatch -Quiet)) {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw 'DutchOven did not publish a block event within 10 seconds'
}

function Assert-NoWfpResidue {
    param([string]$StatePath)
    & netsh.exe wfp show state file=$StatePath | Out-Null
    $hits = @(
        Select-String -Path $StatePath -Pattern @(
            'DutchOven timed application block',
            'DutchOven dynamic session',
            'DutchOven dynamic gate'
        ) -SimpleMatch
    ).Count
    Remove-Item $StatePath -Force
    Assert-Condition ($hits -eq 0) "expected zero DutchOven WFP objects, found $hits"
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
Assert-Condition ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) `
    'an elevated Administrator token is required'
Assert-Condition (Test-Path $DutchOvenPath -PathType Leaf) 'DutchOven executable is missing'
Assert-Condition (Test-Path $CanaryPath -PathType Leaf) 'canary executable is missing'

$unicodeMarker = ([string][char]0x9A8C) + ([string][char]0x8BC1)
$root = Join-Path $env:TEMP "DutchOven-Integration-$unicodeMarker-$PID"
$targetPath = Join-Path $root 'target-canary.exe'
$controlPath = Join-Path $root 'control-canary.exe'
$normalLog = Join-Path $root 'normal.jsonl'
$normalError = Join-Path $root 'normal.err'
$crashLog = Join-Path $root 'crash.jsonl'
$crashError = Join-Path $root 'crash.err'
$gate = $null

try {
    New-Item -ItemType Directory -Path $root | Out-Null
    Copy-Item $CanaryPath $targetPath
    Copy-Item $CanaryPath $controlPath

    $deadline = (Get-Date).AddSeconds(10)
    do {
        $baselineTarget = Invoke-CanaryClient -Path $targetPath -Address $ServerAddress `
            -Port $ServerPort
        if ($baselineTarget -eq 0) { break }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    Assert-Condition ($baselineTarget -eq 0) 'target canary could not establish its baseline connection'
    Assert-Condition ((Invoke-CanaryClient -Path $controlPath -Address $ServerAddress `
        -Port $ServerPort) -eq 0) `
        'control canary could not establish its baseline connection'

    $normalArguments = "--app `"$targetPath`" --period-ms 10000 --block-ms 8000 " +
        "--duration-ms 10000 --json"
    $gate = Start-Process -FilePath $DutchOvenPath -ArgumentList $normalArguments -PassThru `
        -RedirectStandardOutput $normalLog -RedirectStandardError $normalError -WindowStyle Hidden
    Wait-ForBlock -LogPath $normalLog -Process $gate
    $blockedTargetExit = Invoke-CanaryClient -Path $targetPath -Address $ServerAddress `
        -Port $ServerPort
    Assert-Condition ($blockedTargetExit -ne 0) `
        "target canary unexpectedly connected during the block interval (exit $blockedTargetExit)"
    $controlExit = Invoke-CanaryClient -Path $controlPath -Address $ServerAddress `
        -Port $ServerPort
    Assert-Condition ($controlExit -eq 0) `
        "untargeted control canary was affected by the block interval (exit $controlExit)"
    $gate.WaitForExit()
    $normalOutput = Get-Content $normalLog -Raw
    $normalErrorText = ((Get-Content $normalError -ErrorAction SilentlyContinue) -join "`n").Trim()
    Assert-Condition ($normalOutput -match
        '"event":"cleanup","filters":0,"session_closed":true,"close_status":0') `
        'normal gate did not publish a successful cleanup record'
    Assert-Condition ([string]::IsNullOrEmpty($normalErrorText)) `
        "normal gate wrote an error: $normalErrorText"
    Assert-Condition ((Invoke-CanaryClient -Path $targetPath -Address $ServerAddress `
        -Port $ServerPort) -eq 0) `
        'target canary did not recover after normal cleanup'
    Assert-NoWfpResidue -StatePath (Join-Path $root 'normal-wfp.xml')

    $crashArguments = "--app `"$targetPath`" --period-ms 30000 --block-ms 30000 " +
        "--duration-ms 30000 --json"
    $gate = Start-Process -FilePath $DutchOvenPath -ArgumentList $crashArguments -PassThru `
        -RedirectStandardOutput $crashLog -RedirectStandardError $crashError -WindowStyle Hidden
    Wait-ForBlock -LogPath $crashLog -Process $gate
    $crashBlockedExit = Invoke-CanaryClient -Path $targetPath -Address $ServerAddress `
        -Port $ServerPort
    Assert-Condition ($crashBlockedExit -ne 0) `
        "target canary unexpectedly connected before forced termination (exit $crashBlockedExit)"
    Stop-Process -Id $gate.Id -Force
    $gate.WaitForExit()
    Start-Sleep -Milliseconds 500
    Assert-Condition ((Invoke-CanaryClient -Path $targetPath -Address $ServerAddress `
        -Port $ServerPort) -eq 0) `
        'dynamic-session cleanup did not recover after forced termination'
    Assert-NoWfpResidue -StatePath (Join-Path $root 'crash-wfp.xml')

    [pscustomobject]@{
        Result = 'PASS'
        BaselineTargetConnected = $true
        BlockedTargetRejected = $true
        UntargetedControlConnected = $true
        BlockedTargetExitCode = $blockedTargetExit
        ControlExitCode = $controlExit
        NormalCleanupRecovered = $true
        ForcedTerminationRecovered = $true
        WfpResidue = 0
    } | ConvertTo-Json
}
finally {
    if ($gate -and -not $gate.HasExited) {
        Stop-Process -Id $gate.Id -Force -ErrorAction SilentlyContinue
        $gate.WaitForExit()
    }
    if (Test-Path $root) {
        for ($attempt = 0; $attempt -lt 10; $attempt++) {
            Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
            if (-not (Test-Path $root)) { break }
            Start-Sleep -Milliseconds 100
        }
        Assert-Condition (-not (Test-Path $root)) 'temporary integration directory was not removed'
    }
}
