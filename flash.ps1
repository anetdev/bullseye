param(
    [string]$Port = "COM3"
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "   Bullseye Case-Friendly USB Flash    " -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$platformioHome = if ($env:PLATFORMIO_HOME_DIR) { $env:PLATFORMIO_HOME_DIR } else { Join-Path $env:USERPROFILE ".platformio" }

$py = Join-Path $platformioHome "penv\Scripts\python.exe"
if (-not (Test-Path $py)) {
    $pyCmd = Get-Command python -ErrorAction SilentlyContinue
    if ($pyCmd) { $py = $pyCmd.Source }
}

$pio = Join-Path $platformioHome "penv\Scripts\pio.exe"
if (-not (Test-Path $pio)) {
    $pioCmd = Get-Command pio -ErrorAction SilentlyContinue
    if ($pioCmd) { $pio = $pioCmd.Source }
}

$esptool = Join-Path $platformioHome "packages\tool-esptool\esptool.exe"
$boot_app0 = Join-Path $platformioHome "packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"

$firmware = ".pio\build\waveshare_esp32s3_touch_lcd_128\firmware.bin"
$bootloader = ".pio\build\waveshare_esp32s3_touch_lcd_128\bootloader.bin"
$partitions = ".pio\build\waveshare_esp32s3_touch_lcd_128\partitions.bin"

Write-Host "`n[1/3] Building firmware..." -ForegroundColor Yellow
& $pio run
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

Write-Host "`n[2/3] Putting ESP32-S3 into ROM Bootloader via software..." -ForegroundColor Yellow
& $py -c @"
import serial, time
try:
    s = serial.Serial(None, 115200, timeout=1)
    s.port = '$Port'
    s.dtr = False
    s.rts = False
    s.open()
    s.write(b'b\n')
    time.sleep(0.4)
    s.close()
    print('Software bootloader signal sent successfully.')
except Exception as e:
    print(f'Note: {e}')
"@

Start-Sleep -Milliseconds 600

Write-Host "`n[3/3] Uploading firmware to $Port..." -ForegroundColor Green
& $esptool --chip esp32s3 -p $Port -b 921600 --before no-reset --after hard-reset write-flash -z --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0000 $bootloader 0x8000 $partitions 0xe000 $boot_app0 0x10000 $firmware

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nFlash Successful! Board rebooted to normal app." -ForegroundColor Green
} else {
    Write-Host "`nFlash Failed!" -ForegroundColor Red
}
