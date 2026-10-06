<#
.SYNOPSIS
  Prüft, ob der Abschnitt "KURZDIAGNOSE (PC-SEITE)" in allen
  Release-LIESMICH-Dateien vorhanden und mit der Vorlage identisch ist.

.EXAMPLE
  .\tools\check-release-liesmich-sync.ps1

.EXAMPLE
  .\tools\check-release-liesmich-sync.ps1 -ReleaseNamePattern "PS5 Cooling*"

.EXAMPLE
  .\tools\check-release-liesmich-sync.ps1 -TemplateOnly

.NOTES
  Die Release-Ordner liegen nicht in git (.gitignore). Auf einem frischen
  Checkout - in der CI - gibt es sie nicht, und ein Vergleich mit null Ordnern
  wäre "bestanden", ohne etwas zu prüfen. Deshalb gilt:
    - Lokal (Standard): ohne gefundenen Release-Ordner endet das Skript mit
      Fehler (falsches Verzeichnis oder falsches Muster).
    - -TemplateOnly (CI): prüft nur, was in git liegt: die Vorlage ist an
      beiden Enden begrenzt und nennt die Dateien der Version aus src/ps5tm.h.
#>
param(
  [string]$TemplatePath = ".\docs\LIESMICH.txt",
  [string]$ReleaseNamePattern = "PS5 Cooling*",
  [string]$SectionStartHeading = "KURZDIAGNOSE (PC-SEITE)",
  [string]$SectionEndHeading = "ZUM NAMEN",
  [string]$HeaderPath = ".\src\ps5tm.h",
  [switch]$TemplateOnly,
  [switch]$Fix
)

$ErrorActionPreference = "Stop"

function ConvertTo-NormalizedText([string]$text) {
  # Normalize line endings and trim outer blank space for stable compare.
  return ($text -replace "`r`n", "`n" -replace "`r", "`n").Trim()
}

function Get-HeadingBlock {
  param(
    [string]$Text,
    [string]$StartHeading,
    [string]$EndHeading
  )

  $start = $Text.IndexOf($StartHeading)
  if ($start -lt 0) {
    return $null
  }

  $end = $Text.IndexOf($EndHeading, $start + $StartHeading.Length)
  if ($end -lt 0) {
    return $Text.Substring($start)
  }

  return $Text.Substring($start, $end - $start)
}

function Update-HeadingBlock {
  param(
    [string]$Text,
    [string]$StartHeading,
    [string]$EndHeading,
    [string]$ReplacementBlock
  )

  $start = $Text.IndexOf($StartHeading)
  if ($start -lt 0) {
    return $null
  }

  $end = $Text.IndexOf($EndHeading, $start + $StartHeading.Length)
  if ($end -lt 0) {
    return ($Text.Substring(0, $start).TrimEnd() + "`r`n`r`n" + $ReplacementBlock.Trim() + "`r`n")
  }

  $prefix = $Text.Substring(0, $start).TrimEnd()
  $suffix = $Text.Substring($end)
  return ($prefix + "`r`n`r`n" + $ReplacementBlock.Trim() + "`r`n`r`n" + $suffix.TrimStart())
}

if (-not (Test-Path $TemplatePath)) {
  throw "Vorlage nicht gefunden: $TemplatePath"
}

$templateText = Get-Content -Raw -Path $TemplatePath
$templateBlock = Get-HeadingBlock -Text $templateText -StartHeading $SectionStartHeading -EndHeading $SectionEndHeading
if ($null -eq $templateBlock) {
  throw "Abschnitt '$SectionStartHeading' in der Vorlage nicht gefunden: $TemplatePath"
}
$templateBlockNorm = ConvertTo-NormalizedText $templateBlock

# Was in git liegt, wird immer geprüft - auch lokal.
# 1. Der Abschnitt muss hinten begrenzt sein: Get-HeadingBlock nimmt sonst
#    stillschweigend den ganzen Rest der Datei, und ein verschobener Abschnitt
#    würde als "identisch" durchgehen.
$startAt = $templateText.IndexOf($SectionStartHeading)
if ($templateText.IndexOf($SectionEndHeading, $startAt + $SectionStartHeading.Length) -lt 0) {
  throw "Abschnitt '$SectionEndHeading' fehlt hinter '$SectionStartHeading' in der Vorlage: $TemplatePath"
}

# 2. Die Vorlage nennt die Dateien der aktuellen Version. Make kopiert sie
#    unverändert in jeden Release-Ordner; nach einer Versionsanhebung ohne
#    Nachziehen stünden dort die Namen der vorigen.
if (-not (Test-Path $HeaderPath)) {
  throw "Kopfdatei nicht gefunden: $HeaderPath (aus dem Projektordner aufrufen)"
}
$versionMatch = [regex]::Match((Get-Content -Raw -Path $HeaderPath),
                               '^\s*#define\s+PS5TM_VERSION\s+"([^"]+)"',
                               [System.Text.RegularExpressions.RegexOptions]::Multiline)
if (-not $versionMatch.Success) {
  throw "PS5TM_VERSION nicht gefunden in $HeaderPath"
}
$version = $versionMatch.Groups[1].Value
if ($templateText.IndexOf("_v$version.elf") -lt 0) {
  throw "Die Vorlage $TemplatePath nennt keine Datei der Version $version (erwartet: ..._v$version.elf). LIESMICH nachziehen."
}

if ($TemplateOnly) {
  Write-Host ""
  Write-Host "=== LIESMICH-Vorlage ===" -ForegroundColor Cyan
  Write-Host ("Vorlage: {0}" -f (Resolve-Path $TemplatePath))
  Write-Host "Abschnitt '$SectionStartHeading' begrenzt, Dateinamen der Version $version vorhanden."
  Write-Host "Release-Ordner wurden nicht verglichen (-TemplateOnly)."
  exit 0
}

$releaseDirs = @(Get-ChildItem -Directory | Where-Object { $_.Name -like $ReleaseNamePattern })
if ($releaseDirs.Count -eq 0) {
  Write-Host ""
  Write-Host "FEHLER: Kein Release-Ordner passt auf '$ReleaseNamePattern' - es wurde nichts verglichen." -ForegroundColor Red
  Write-Host "Aus dem Projektordner aufrufen, oder -TemplateOnly, wo es keine Release-Ordner gibt (CI)."
  exit 1
}
$results = New-Object System.Collections.Generic.List[object]

foreach ($dir in $releaseDirs) {
  $liesmichPath = Join-Path $dir.FullName "LIESMICH.txt"

  if (-not (Test-Path $liesmichPath)) {
    $results.Add([pscustomobject]@{
      Release = $dir.Name
      Status = "MISSING_FILE"
      Details = "LIESMICH.txt fehlt"
      Path = $liesmichPath
    })
    continue
  }

  $text = Get-Content -Raw -Path $liesmichPath
  $block = Get-HeadingBlock -Text $text -StartHeading $SectionStartHeading -EndHeading $SectionEndHeading

  if ($null -eq $block) {
    $results.Add([pscustomobject]@{
      Release = $dir.Name
      Status = "MISSING_SECTION"
      Details = "Abschnitt fehlt"
      Path = $liesmichPath
    })
    continue
  }

  $blockNorm = ConvertTo-NormalizedText $block
  if ($blockNorm -eq $templateBlockNorm) {
    $results.Add([pscustomobject]@{
      Release = $dir.Name
      Status = "OK"
      Details = "Abschnitt identisch"
      Path = $liesmichPath
    })
  }
  else {
    if ($Fix) {
      $updatedText = Update-HeadingBlock -Text $text -StartHeading $SectionStartHeading -EndHeading $SectionEndHeading -ReplacementBlock $templateBlock
      if ($null -ne $updatedText) {
        Set-Content -Path $liesmichPath -Value $updatedText -Encoding UTF8
      }
    }

    $results.Add([pscustomobject]@{
      Release = $dir.Name
      Status = if ($Fix) { "FIXED" } else { "DIFFERS" }
      Details = if ($Fix) { "Abweichung wurde auf Vorlage gesetzt" } else { "Abschnitt vorhanden, aber inhaltlich abweichend" }
      Path = $liesmichPath
    })
  }
}

$total = $results.Count
$ok = ($results | Where-Object { $_.Status -eq "OK" }).Count
$fixed = ($results | Where-Object { $_.Status -eq "FIXED" }).Count
$problems = ($results | Where-Object { $_.Status -ne "OK" -and $_.Status -ne "FIXED" })

Write-Host ""
Write-Host "=== LIESMICH-Sync-Check ===" -ForegroundColor Cyan
Write-Host ("Vorlage: {0}" -f (Resolve-Path $TemplatePath))
Write-Host ("Gefundene Release-Ordner: {0}" -f $total)
Write-Host ("OK: {0}" -f $ok)
if ($Fix) {
  Write-Host ("Gefixt: {0}" -f $fixed)
}
Write-Host ("Probleme: {0}" -f $problems.Count)
Write-Host ""

if ($problems.Count -gt 0) {
  $problems | Sort-Object Release | Format-Table -AutoSize
  exit 1
}

$results | Sort-Object Release | Select-Object -First 20 | Format-Table -AutoSize
exit 0
