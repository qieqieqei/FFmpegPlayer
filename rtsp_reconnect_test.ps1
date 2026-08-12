# RTSP reconnect test script
# phase1: normal playback
# phase2: kill pusher (network down) -> player should request reconnect
# phase3: restart pusher -> player should reconnect automatically
$ErrorActionPreference = "Continue"

$mtxExe   = "C:\Users\bbitti\.openclaw\workspace\mediamtx\mediamtx.exe"
$mtxDir   = "C:\Users\bbitti\.openclaw\workspace\mediamtx"
$playerExe = "C:\Users\bbitti\.openclaw\workspace\regress\golden_src\x64\Release\FFmpegPlayer.exe"
$workDir  = "C:\Users\bbitti\.openclaw\workspace\regress\golden_src"
$logDir   = "C:\Users\bbitti\.openclaw\workspace\rtsp_test"
# Pusher source: lavfi synthetic (testsrc2 + sine), endless, clean timestamps, GOP=1s (IDR every second).
# Do NOT use -stream_loop: timestamp jump at loop point corrupts the aac queue and stalls the muxer.
$ffArgs = @("-re", "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=30", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-c:v", "libx264", "-preset", "veryfast", "-tune", "zerolatency", "-g", "30", "-keyint_min", "30", "-sc_threshold", "0", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "96k", "-f", "rtsp", "rtsp://127.0.0.1:8554/test")
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
# Clear old logs (player.log is append-mode; avoid false matches from previous runs)
Remove-Item "$logDir\player.log","$logDir\mtx_out.txt","$logDir\mtx_err.txt","$logDir\ff_err.txt","$logDir\ff_err2.txt","$logDir\pl_out.txt","$logDir\pl_err.txt" -Force -ErrorAction SilentlyContinue

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

# ---------- 1. start mediamtx ----------
$mtx = Start-Proc $mtxExe @() "$logDir\mtx_out.txt" "$logDir\mtx_err.txt" $mtxDir
Start-Sleep -Seconds 3
if ($mtx.HasExited) { Write-Output "FATAL: mediamtx exited"; exit 1 }
Write-Output "STEP1 mediamtx pid=$($mtx.Id)"

# ---------- 2. start ffmpeg pusher ----------
$ff = Start-Proc "ffmpeg" $ffArgs "$logDir\ff_out.txt" "$logDir\ff_err.txt" $workDir
Start-Sleep -Seconds 6
if ($ff.HasExited) { Write-Output "FATAL: ffmpeg push exited"; Stop-Process -Id $mtx.Id -Force; exit 1 }
Write-Output "STEP2 ffmpeg pushing pid=$($ff.Id)"

# ---------- 3. start player ----------
$pl = Start-Proc $playerExe @("-v","--log-file","$logDir\player.log","rtsp://127.0.0.1:8554/test") "$logDir\pl_out.txt" "$logDir\pl_err.txt" $workDir
Write-Output "STEP3 player pid=$($pl.Id)"

# ---------- phase1: normal playback ----------
if (Check-Log "$logDir\player.log" "Render frame" 20) {
    Write-Output "PASS phase1: playing normally"
} else {
    Write-Output "FAIL phase1: no render frames"
    Get-Content "$logDir\player.log" -ErrorAction SilentlyContinue | Select-Object -Last 8
    Stop-Process -Id $pl.Id,$ff.Id,$mtx.Id -Force -ErrorAction SilentlyContinue
    exit 1
}

# ---------- phase2: cut the stream ----------
Stop-Process -Id $ff.Id -Force
Write-Output "STEP4 ffmpeg killed (network down)"
if (Check-Log "$logDir\player.log" "requesting reconnect|Network stream error" 15) {
    Write-Output "PASS phase2: reconnect requested"
} else {
    Write-Output "FAIL phase2: no reconnect triggered"
    Stop-Process -Id $pl.Id,$mtx.Id -Force -ErrorAction SilentlyContinue
    exit 1
}

# ---------- phase3: restore the stream ----------
$ff2 = Start-Proc "ffmpeg" $ffArgs "$logDir\ff_out2.txt" "$logDir\ff_err2.txt" $workDir
Write-Output "STEP5 ffmpeg restarted (network back)"
if (Check-Log "$logDir\player.log" "Reconnect success" 30) {
    Write-Output "PASS phase3: reconnected"
} else {
    Write-Output "FAIL phase3: reconnect not confirmed"
    Get-Content "$logDir\player.log" -ErrorAction SilentlyContinue | Select-Object -Last 10
}

# Check rendering after reconnect (only lines after the reconnect marker)
Start-Sleep -Seconds 40
$afterReconnect = Select-String -Path "$logDir\player.log" -Pattern "Reconnect success|Network reconnect" -ErrorAction SilentlyContinue | Select-Object -Last 1
if ($afterReconnect) {
    $cutLine = $afterReconnect.LineNumber
    $render = Select-String -Path "$logDir\player.log" -Pattern "Render frame" -ErrorAction SilentlyContinue | Where-Object { $_.LineNumber -gt $cutLine } | Select-Object -Last 1
    if ($render) { Write-Output "PASS phase4: rendering after reconnect ($($render.Line.Trim()))" }
    else { Write-Output "FAIL phase4: no rendering after reconnect" }
} else {
    Write-Output "FAIL phase4: no reconnect marker in log"
}
Get-Content "$logDir\player.log" -ErrorAction SilentlyContinue | Select-Object -Last 12

# ---------- cleanup ----------
Stop-Process -Id $pl.Id,$ff2.Id,$mtx.Id -Force -ErrorAction SilentlyContinue
Write-Output "DONE cleanup"
