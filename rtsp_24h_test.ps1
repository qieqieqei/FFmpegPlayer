# RTSP 24h soak test: mediamtx + lavfi pusher + player
# Every ~30min round: kill pusher (network down) -> verify detect -> restart pusher -> verify reconnect + render resume
# Output: stdout progress lines + per-round summary in summary file (append), player.log truncated to last 3000 lines each round end.
$ErrorActionPreference = "Continue"

$mtxExe   = "C:\Users\bbitti\.openclaw\workspace\mediamtx\mediamtx.exe"
$mtxDir   = "C:\Users\bbitti\.openclaw\workspace\mediamtx"
$playerExe = "D:\application\visual studio\product\FFmpeg_text_claw\x64\Release\FFmpeg_text_claw.exe"
$workDir  = "D:\application\visual studio\product\FFmpeg_text_claw"
$logDir   = "C:\Users\bbitti\.openclaw\workspace\rtsp_24h"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$summary  = "$logDir\summary.txt"
$playerLog = "$logDir\player.log"

$ROUNDS    = 48
$INTERVAL_S = 1680   # 28 min between rounds (round itself ~2min -> ~30min cadence, 24h total)
$DETECT_S  = 20      # max wait for reconnect-request log after kill
$RECONN_S  = 45      # max wait for Reconnect success after pusher restart

$ffArgs = @("-re", "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=30", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-c:v", "libx264", "-preset", "veryfast", "-tune", "zerolatency", "-g", "30", "-keyint_min", "30", "-sc_threshold", "0", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "96k", "-f", "rtsp", "rtsp://127.0.0.1:8554/test")

function Start-Proc([string]$exe, [string[]]$argsList, [string]$out, [string]$err, [string]$wd) {
    $params = @{
        FilePath = $exe
        RedirectStandardOutput = $out
        RedirectStandardError = $err
        PassThru = $true
    }
    if ($argsList -and $argsList.Count -gt 0) { $params.ArgumentList = $argsList }
    if ($wd) { $params.WorkingDirectory = $wd }
    return Start-Process @params
}

function Check-Log([string]$log, [string]$pattern, [int]$waitSec) {
    for ($i = 0; $i -lt $waitSec; $i++) {
        Start-Sleep -Seconds 1
        $hit = Select-String -Path $log -Pattern $pattern -ErrorAction SilentlyContinue
        if ($hit) { return $true }
    }
    return $false
}

function Count-Matches([string]$log, [string]$pattern) {
    return @(Select-String -Path $log -Pattern $pattern -ErrorAction SilentlyContinue).Count
}

function Trim-Log([string]$log, [int]$maxLines) {
    $lines = @(Get-Content -Path $log -ErrorAction SilentlyContinue)
    if ($lines.Count -gt $maxLines) {
        try {
            $tail = $lines | Select-Object -Last $maxLines
            [System.IO.File]::WriteAllLines($log, $tail, (New-Object System.Text.UTF8Encoding $false))
        } catch {
            # player holds the log file without write share -> truncate impossible (observed)
            # log grows ~30 lines/s (Render frame DEBUG), ~7MB/h, ~170MB over 24h: acceptable
            Log "WARN trim skipped (player.log locked by player process)"
        }
    }
}

function Log([string]$msg) {
    $stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    Write-Output "$stamp $msg"
}

# ---------- cleanup leftovers ----------
Get-Process -Name FFmpeg_text_claw,mediamtx,ffmpeg -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Remove-Item "$logDir\*" -Force -ErrorAction SilentlyContinue

# ---------- start mediamtx ----------
$mtx = Start-Proc $mtxExe @() "$logDir\mtx_out.txt" "$logDir\mtx_err.txt" $mtxDir
Start-Sleep -Seconds 3
if ($mtx.HasExited) { Log "FATAL mediamtx exited"; exit 1 }
Log "mediamtx pid=$($mtx.Id)"

# ---------- start pusher ----------
$ff = Start-Proc "ffmpeg" $ffArgs "$logDir\ff_out.txt" "$logDir\ff_err.txt" $workDir
Start-Sleep -Seconds 6
if ($ff.HasExited) { Log "FATAL ffmpeg push exited"; exit 1 }
Log "ffmpeg pusher pid=$($ff.Id)"

# ---------- start player ----------
$pl = Start-Proc $playerExe @("-v","--log-file","$playerLog","rtsp://127.0.0.1:8554/test") "$logDir\pl_out.txt" "$logDir\pl_err.txt" $workDir
Log "player pid=$($pl.Id)"

# ---------- baseline ----------
if (-not (Check-Log $playerLog "Render frame" 30)) {
    Log "FATAL no baseline rendering"; exit 1
}
Log "baseline rendering OK"

$passDetect = 0; $failDetect = 0
$passReconn = 0; $failReconn = 0
$passRender = 0; $failRender = 0

for ($r = 1; $r -le $ROUNDS; $r++) {
    Log "===== ROUND $r/$ROUNDS ====="

    # 1. health: mediamtx alive?
    if ($mtx.HasExited) {
        Log "mediamtx dead, restarting"
        $mtx = Start-Proc $mtxExe @() "$logDir\mtx_out.txt" "$logDir\mtx_err.txt" $mtxDir
        Start-Sleep -Seconds 3
    }
    # 2. health: player alive?
    if ($pl.HasExited) {
        Log "player dead, restarting (round marked FAIL)"
        $failDetect++; $failReconn++; $failRender++
        $pl = Start-Proc $playerExe @("-v","--log-file","$playerLog","rtsp://127.0.0.1:8554/test") "$logDir\pl_out.txt" "$logDir\pl_err.txt" $workDir
        if (-not (Check-Log $playerLog "Render frame" 30)) { Log "FATAL player restart no rendering"; break }
        Add-Content -Path $summary -Value "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') R$r player_restarted FAIL"
        Start-Sleep -Seconds $INTERVAL_S
        continue
    }

    # 3. cut the stream
    Stop-Process -Id $ff.Id -Force -ErrorAction SilentlyContinue
    # anchor checks on NEW lines only: old lines from previous rounds are still in the log
    # (grep-whole-log would match them instantly and report a false PASS)
    $reconnBefore = Count-Matches $playerLog "Reconnect success"
    $detectBefore = Count-Matches $playerLog "Network stream error|requesting reconnect"
    $detectOk = $false
    for ($i = 0; $i -lt $DETECT_S; $i++) {
        Start-Sleep -Seconds 1
        if ((Count-Matches $playerLog "Network stream error|requesting reconnect") -gt $detectBefore) { $detectOk = $true; break }
    }
    if ($detectOk) { $passDetect++; Log "PASS detect (network down)" } else { $failDetect++; Log "FAIL detect" }

    # 4. restore the stream
    Start-Sleep -Seconds 5
    $ff = Start-Proc "ffmpeg" $ffArgs "$logDir\ff_out.txt" "$logDir\ff_err.txt" $workDir
    if ($ff.HasExited) { Log "WARN pusher restart exited immediately" }
    # wait for a NEW Reconnect success line (count increase) - not an old one from a previous round
    $reconnOk = $false
    for ($i = 0; $i -lt $RECONN_S; $i++) {
        Start-Sleep -Seconds 1
        if ((Count-Matches $playerLog "Reconnect success") -gt $reconnBefore) { $reconnOk = $true; break }
    }
    $reconnAfter = Count-Matches $playerLog "Reconnect success"
    if ($reconnOk) { $passReconn++; Log "PASS reconnect ($reconnAfter)" } else { $failReconn++; Log "FAIL reconnect" }

    # 5. render resumed after reconnect?
    Start-Sleep -Seconds 10
    $lastSuccess = Select-String -Path $playerLog -Pattern "Reconnect success" -ErrorAction SilentlyContinue | Select-Object -Last 1
    $ok = $false
    if ($lastSuccess) {
        $cut = $lastSuccess.LineNumber
        $render = Select-String -Path $playerLog -Pattern "Render frame" -ErrorAction SilentlyContinue | Where-Object { $_.LineNumber -gt $cut } | Select-Object -Last 1
        if ($render) { $ok = $true; Log "PASS render resumed ($($render.Line.Trim()))" }
    }
    if ($ok) { $passRender++ } else { $failRender++; Log "FAIL render resumed" }

    $sDetect = if ($detectOk) { 'PASS' } else { 'FAIL' }
    $sReconn = if ($reconnOk) { 'PASS' } else { 'FAIL' }
    $sRender = if ($ok) { 'PASS' } else { 'FAIL' }
    Add-Content -Path $summary -Value "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') R$r detect=$sDetect reconnect=$sReconn render=$sRender"

    # trim player log to keep size bounded
    Trim-Log $playerLog 3000

    if ($r -lt $ROUNDS) {
        Log "sleep $INTERVAL_S s until next round"
        Start-Sleep -Seconds $INTERVAL_S
    }
}

# ---------- final ----------
Log "===== SUMMARY ====="
Log "detect  PASS=$passDetect FAIL=$failDetect"
Log "reconn  PASS=$passReconn FAIL=$failReconn"
Log "render  PASS=$passRender FAIL=$failRender"
Add-Content -Path $summary -Value "FINAL detect=$passDetect/$failDetect reconn=$passReconn/$failReconn render=$passRender/$failRender"
Stop-Process -Id $pl.Id,$ff.Id,$mtx.Id -Force -ErrorAction SilentlyContinue
Log "DONE cleanup"
