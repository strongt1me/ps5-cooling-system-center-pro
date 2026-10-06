<#
.SYNOPSIS
  Prüft, ob der PS5 Temperature Manager läuft und ob der ICC-Lüfterpfad
  tatsächlich wirkt.

.EXAMPLE
  .\diagnose.ps1 -Ps5Ip 192.168.1.50
  .\diagnose.ps1 -Ps5Ip 192.168.1.50 -FanTest
  .\diagnose.ps1 -Ps5Ip 192.168.1.50 -AutoPort
#>
param(
  [Parameter(Mandatory = $true)][string]$Ps5Ip,
  [int]$Port = 8086,
  # Sucht automatisch auf typischen Ports (8086, 8080, 8770), falls der Zielport nicht antwortet.
  [switch]$AutoPort,
  # Führt zusätzlich den Wirkungstest durch (ändert kurzzeitig die Schwelle).
  [switch]$FanTest
)

$ErrorActionPreference = 'Stop'
$base = ""

function Test-PortOpen([string]$ip, [int]$p) {
  try {
    return (Test-NetConnection -ComputerName $ip -Port $p -WarningAction SilentlyContinue).TcpTestSucceeded
  }
  catch {
    return $false
  }
}

function Write-Head($text) {
  Write-Host ""
  Write-Host "=== $text ===" -ForegroundColor Cyan
}

function Get-Status {
  Invoke-RestMethod -Uri "$base/api/v1/status" -TimeoutSec 5
}

function Resolve-BaseUrl {
  param([string]$ip, [int]$preferred, [switch]$scan)

  if (Test-PortOpen $ip $preferred) {
    return "http://${ip}:${preferred}"
  }

  if (-not $scan) {
    return ""
  }

  foreach ($candidate in @(8086, 8080, 8770)) {
    if ($candidate -eq $preferred) { continue }
    if (Test-PortOpen $ip $candidate) {
      return "http://${ip}:${candidate}"
    }
  }

  return ""
}

Write-Head "Stufe 0: Verbindungsziel prüfen"
$resolved = Resolve-BaseUrl -ip $Ps5Ip -preferred $Port -scan:$AutoPort
if ([string]::IsNullOrWhiteSpace($resolved)) {
  Write-Host "  FEHLER - Kein offener Port gefunden." -ForegroundColor Red
  Write-Host "  Geprüfter Port: $Port"
  if ($AutoPort) {
    Write-Host "  Zusätzliche Ports: 8086, 8080, 8770"
  }
  Write-Host ""
  Write-Host "  Nächste Schritte:"
  Write-Host "   - Payload/ELF auf der PS5 neu laden"
  Write-Host "   - IP-Adresse prüfen"
  Write-Host "   - Erneut mit -AutoPort testen"
  exit 1
}

$base = $resolved
Write-Host "  OK - Zieladresse: $base" -ForegroundColor Green

# --------------------------------------------------------------- Stufe 1
Write-Head "Stufe 1: Ist der Payload erreichbar?"
try {
  $status = Get-Status
  Write-Host "  OK - Web-API antwortet auf $base" -ForegroundColor Green
}
catch {
  Write-Host "  FEHLER - keine Antwort von $base" -ForegroundColor Red
  Write-Host ""
  Write-Host "  Mögliche Ursachen:"
  Write-Host "   - Payload läuft nicht (nochmal an Port 9021 senden)"
  Write-Host "   - Falsche IP-Adresse"
  Write-Host "   - Anderer Port konfiguriert (Standard 8086; alt: 8080/8770)"
  Write-Host "   - PS5 ist im Ruhezustand"
  exit 1
}

# --------------------------------------------------------------- Stufe 2
Write-Head "Stufe 2: Sensorik"
$t = $status.temperatures
if ($t.cpu_valid) { Write-Host ("  CPU: {0} Grad C" -f $t.cpu_c) -ForegroundColor Green }
else              { Write-Host "  CPU: kein gültiger Messwert" -ForegroundColor Yellow }
if ($t.soc_valid) { Write-Host ("  SoC: {0} Grad C" -f $t.soc_c) -ForegroundColor Green }
else              { Write-Host "  SoC: kein gültiger Messwert" -ForegroundColor Yellow }
if ($t.gpu_valid) { Write-Host ("  Grafik: {0} Grad C (Kanal 7)" -f $t.gpu_c) }
Write-Host ("  Firmware: 0x{0:X8}  Gruppe {1}" -f $status.firmware.raw_version, $status.firmware.group)

# --------------------------------------------------------------- Stufe 3
Write-Head "Stufe 3: ICC-Lüfteradapter"
$f = $status.fan
if ($f.available) {
  Write-Host "  OK - /dev/icc_fan geöffnet, ioctl akzeptiert" -ForegroundColor Green
  Write-Host ("  Aktive Schwelle: {0} Grad C   Zielstufe: {1} %" -f $f.threshold_c, $f.target_duty_pct)
  if ($f.measured_valid) {
    Write-Host ("  Gemessene Ist-Drehzahl: {0} %" -f $f.measured_duty_pct)
  } else {
    Write-Host "  Ist-Drehzahl nicht lesbar" -ForegroundColor Yellow
  }
}
else {
  Write-Host "  FEHLER - Lüfteradapter nicht verfügbar" -ForegroundColor Red
  Write-Host ("  Meldung: {0}" -f $status.messages.fan) -ForegroundColor Yellow
  Write-Host ""
  Write-Host "  Deutung des errno:"
  Write-Host "   errno 2  (ENOENT) -> /dev/icc_fan existiert nicht."
  Write-Host "                        kstuff ist nicht geladen. Erst kstuff starten."
  Write-Host "   errno 1  (EPERM)  -> Rechteausweitung hat nicht gegriffen."
  Write-Host "   errno 13 (EACCES) -> dito. Payload über elfldr im JB-Kontext starten."
  Write-Host "   anderer errno     -> bitte melden."
  Write-Host ""
  Write-Host "  Wirkungstest wird übersprungen." -ForegroundColor Yellow
  exit 2
}

# --------------------------------------------------------------- Stufe 4
if (-not $FanTest) {
  Write-Host ""
  Write-Host "Für den Wirkungstest erneut mit -FanTest aufrufen." -ForegroundColor Cyan
  Write-Host "Er setzt die Schwelle kurz auf 45 und dann auf 80 Grad C und misst,"
  Write-Host "ob die Ist-Drehzahl reagiert. Der Lüfter wird dabei hörbar lauter."
  exit 0
}

Write-Head "Stufe 4: Wirkungstest - reagiert der Lüfter wirklich?"

$cfg = Invoke-RestMethod -Uri "$base/api/v1/config" -TimeoutSec 5
$originalMode = $cfg.mode
$originalThreshold = $cfg.fan_threshold_c
Write-Host "  Ursprünglicher Modus: $originalMode, gespeicherte Schwelle $originalThreshold Grad C (beides wird am Ende wiederhergestellt)"

function Set-Mode($mode) {
  $body = @{ mode = $mode } | ConvertTo-Json -Compress
  Invoke-RestMethod -Uri "$base/api/v1/config" -Method Put -Body $body `
      -ContentType 'application/json' -TimeoutSec 5 | Out-Null
}

function Set-Threshold($c) {
  $body = @{ threshold_c = $c } | ConvertTo-Json -Compress
  Invoke-RestMethod -Uri "$base/api/v1/fan/threshold" -Method Post -Body $body `
      -ContentType 'application/json' -TimeoutSec 5 | Out-Null
}

function Watch-Duty($label, $seconds) {
  $samples = @()
  for ($i = 0; $i -lt $seconds; $i += 3) {
    $s = Get-Status
    $samples += $s.fan.measured_duty_pct
    Write-Host ("    {0,3}s  Temp {1,3} C   Ist-Drehzahl {2,3} %" -f `
        $i, [Math]::Max($s.temperatures.cpu_c, $s.temperatures.soc_c), `
        $s.fan.measured_duty_pct)
    Start-Sleep -Seconds 3
  }
  return $samples
}

try {
  # Ohne "observe" würde die Automatik die Testschwelle sofort überschreiben.
  Set-Mode 'observe'

  Write-Host ""
  Write-Host "  A) Schwelle 45 Grad C (aggressiv - Lüfter soll hochdrehen)" -ForegroundColor Cyan
  Set-Threshold 45
  $low = Watch-Duty 'niedrig' 30

  Write-Host ""
  Write-Host "  B) Schwelle 80 Grad C (leise - Lüfter soll runterdrehen)" -ForegroundColor Cyan
  Set-Threshold 80
  $high = Watch-Duty 'hoch' 30
}
finally {
  # Was im Beobachtungsmodus festgehalten wird, speichert die App als
  # fan_threshold_c. Nur den Modus zurückzustellen ließ die Schwelle bei 80
  # stehen - der leisesten und heißesten Einstellung - und überschrieb den
  # gespeicherten Wert still. Sie wird zurückgesetzt, solange noch Beobachten
  # gilt: In der Automatik würde derselbe Aufruf die Zieltemperatur ändern.
  $thresholdBack = $false
  try {
    Set-Mode 'observe'
    if ($null -ne $originalThreshold) {
      Set-Threshold ([int]$originalThreshold)
      $thresholdBack = $true
    }
  }
  catch {
    Write-Host "  Warnung: Die Schwelle $originalThreshold Grad C ließ sich nicht zurücksetzen: $($_.Exception.Message)" -ForegroundColor Yellow
  }

  Set-Mode $originalMode
  Write-Host ""
  Write-Host "  Modus '$originalMode' wiederhergestellt." -ForegroundColor Green
  if ($thresholdBack) {
    Write-Host "  Schwelle $originalThreshold Grad C wiederhergestellt." -ForegroundColor Green
  }
}

# --------------------------------------------------------------- Auswertung
Write-Head "Ergebnis"
$avgLow  = ($low  | Measure-Object -Average).Average
$avgHigh = ($high | Measure-Object -Average).Average
Write-Host ("  Mittlere Ist-Drehzahl bei Schwelle 45 C: {0:N1} %" -f $avgLow)
Write-Host ("  Mittlere Ist-Drehzahl bei Schwelle 80 C: {0:N1} %" -f $avgHigh)
$delta = $avgLow - $avgHigh
Write-Host ("  Unterschied: {0:N1} Prozentpunkte" -f $delta)
Write-Host ""

if ($delta -ge 5) {
  Write-Host "  ICC-Pfad WIRKT - der Lüfter folgt der Schwelle." -ForegroundColor Green
  Write-Host "  Die automatische Regelung kann verwendet werden."
}
elseif ($delta -ge 2) {
  Write-Host "  Schwache Reaktion." -ForegroundColor Yellow
  Write-Host "  Test bei warmer Konsole wiederholen (z.B. nach 10 Minuten Spiel)."
  Write-Host "  Bei kalter Konsole regelt die Firmware ohnehin kaum."
}
else {
  Write-Host "  KEINE Reaktion messbar." -ForegroundColor Red
  Write-Host "  Der ioctl meldet Erfolg, bewirkt aber nichts."
  Write-Host "  War die Konsole kalt (unter 45 C)? Dann bei Last wiederholen."
  Write-Host "  Sonst bitte die Ausgabe oben und die Protokoll-Seite melden."
}
