<#
.SYNOPSIS
  Checks the release folder of a version and, if it is published, the GitHub release against it. Builds nothing and
  publishes nothing: building, committing, pushing and creating the release is tools/release.sh (28.11.2026).

.DESCRIPTION
  Local checks (folder "PS5 Cooling and System Center v<version>", the version comes from src/ps5tm.h):
  - exactly the five release files: the ELF, FAKE86921.zip, FAKE86922.zip, LIESMICH.txt, payload-manager.json
    (Thumbs.db and desktop.ini are tolerated; a *.pkg is an error, no tile package has shipped since 19.10.2026)
  - the ELF is an ELF and names its version
  - each zip: stored without compression, every entry under "<tile>/", eboot.bin first, sce_sys/param.json present,
    and the release ELF lies byte for byte inside the eboot.bin (each tile carries its own copy of the app)
  - LIESMICH.txt names the version; payload-manager.json names the version, links this release's ELF and carries its SHA-256
  GitHub checks (unless -SkipGitHub, and only when the release exists): the same five files, every digest equal to the
  local file, and whether the release is the latest one.

.EXAMPLE
  .\tools\pre-release-check.ps1

.EXAMPLE
  .\tools\pre-release-check.ps1 -Version 1.98.0 -SkipGitHub
#>

param(
  [string]$Repo = "strongt1me/ps5-cooling-system-center-pro",
  [string]$Version,
  [string]$Tag,
  [switch]$SkipGitHub,
  # kept so that an old call says what to do instead of failing on an unknown parameter
  [switch]$CreateOrUpdateRelease
)

$ErrorActionPreference = "Stop"

if ($CreateOrUpdateRelease) {
  throw ("-CreateOrUpdateRelease is gone (28.11.2026): it published only three of the five files, with placeholder notes. " +
         "Publish with tools/release.sh <X.Y.Z>; replace one file of a published release with " +
         "gh release upload <tag> <file> --repo $Repo --clobber.")
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $projectRoot

if (-not $Version) {
  $header = Get-Content -Raw -Path (Join-Path $projectRoot "src/ps5tm.h")
  $m = [regex]::Match($header, '^\s*#define\s+PS5TM_VERSION\s+"([^"]+)"', [System.Text.RegularExpressions.RegexOptions]::Multiline)
  if (-not $m.Success) { throw "Could not read PS5TM_VERSION from src/ps5tm.h" }
  $Version = $m.Groups[1].Value
}
if (-not $Tag) { $Tag = "v$Version" }

$errors = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$text) { $script:errors.Add($text); Write-Host "FAIL: $text" -ForegroundColor Red }
function Write-Ok([string]$text) { Write-Host "ok: $text" }

# Latin-1 maps every byte to one char, so byte searches become ordinal string searches
$latin1 = [System.Text.Encoding]::GetEncoding(28591)

Write-Host "== Pre-release check ==" -ForegroundColor Cyan
Write-Host "Project: $projectRoot"
Write-Host "Version: $Version (tag $Tag)"

$releaseDir = Join-Path $projectRoot "PS5 Cooling and System Center v$Version"
if (-not (Test-Path $releaseDir)) {
  throw "Release folder not found: $releaseDir (tools/release.sh builds it)"
}

$elfName = "PS5_Cooling_System_Center_v$Version.elf"
$tiles = @("FAKE86921", "FAKE86922")
$expected = @($elfName, "FAKE86921.zip", "FAKE86922.zip", "LIESMICH.txt", "payload-manager.json")

# --- the folder holds the five files and nothing else -------------------------------------------------------------
$present = @(Get-ChildItem -Path $releaseDir -File | ForEach-Object { $_.Name })
foreach ($n in $expected) {
  if ($n -notin $present) { Add-Failure "missing in the release folder: $n" }
}
foreach ($n in $present) {
  if ($n -like "*.pkg") { Add-Failure "a tile package lies in the folder: $n (none has shipped since 19.10.2026)" }
  elseif ($n -notin $expected -and $n -notin @("Thumbs.db", "desktop.ini")) { Add-Failure "a file that is not part of a release: $n" }
}
if ($errors.Count -gt 0) {
  throw "The release folder is incomplete; the other checks need all five files."
}

$elfPath = Join-Path $releaseDir $elfName
$elfBytes = [System.IO.File]::ReadAllBytes($elfPath)
$elfSha = (Get-FileHash -Algorithm SHA256 -Path $elfPath).Hash.ToLowerInvariant()

# --- the ELF ------------------------------------------------------------------------------------------------------
if ($elfBytes.Length -lt 64 -or $elfBytes[0] -ne 0x7F -or $elfBytes[1] -ne 0x45 -or $elfBytes[2] -ne 0x4C -or $elfBytes[3] -ne 0x46) {
  Add-Failure "$elfName does not start with an ELF header"
} elseif ($latin1.GetString($elfBytes).IndexOf($Version, [System.StringComparison]::Ordinal) -lt 0) {
  Add-Failure "$elfName does not contain its version $Version"
} else {
  Write-Ok "$elfName ($($elfBytes.Length) bytes, sha256 $($elfSha.Substring(0, 16))...)"
}

# --- the two tile zips --------------------------------------------------------------------------------------------
Add-Type -AssemblyName System.IO.Compression.FileSystem
$elfText = $latin1.GetString($elfBytes)
foreach ($t in $tiles) {
  $zipPath = Join-Path $releaseDir "$t.zip"
  $zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
  try {
    $entries = @($zip.Entries)
    $files = @($entries | Where-Object { -not $_.FullName.EndsWith("/") })
    $outside = @($entries | Where-Object { -not $_.FullName.StartsWith("$t/") })
    $packed = @($files | Where-Object { $_.CompressedLength -ne $_.Length })
    if ($files.Count -eq 0 -or $files[0].FullName -ne "$t/eboot.bin") { Add-Failure "$t.zip: the first entry is not $t/eboot.bin" ; continue }
    if ($outside.Count -gt 0) { Add-Failure "$t.zip: $($outside.Count) entries outside the folder $t/" }
    if ($packed.Count -gt 0) { Add-Failure "$t.zip: $($packed.Count) entries are compressed (the release zips are stored)" }
    if (-not ($files | Where-Object { $_.FullName -eq "$t/sce_sys/param.json" })) { Add-Failure "$t.zip: no $t/sce_sys/param.json" }

    $eboot = $files[0]
    $buffer = New-Object byte[] $eboot.Length
    $stream = $eboot.Open()
    try {
      $read = 0
      while ($read -lt $buffer.Length) {
        $n = $stream.Read($buffer, $read, $buffer.Length - $read)
        if ($n -le 0) { break }
        $read += $n
      }
    } finally { $stream.Dispose() }
    if ($latin1.GetString($buffer).IndexOf($elfText, [System.StringComparison]::Ordinal) -lt 0) {
      Add-Failure "$t.zip: the release ELF does not lie byte for byte in eboot.bin (a tile from another build?)"
    } else {
      Write-Ok "$t.zip ($($files.Count) files, eboot.bin $($eboot.Length) bytes, carries the release ELF)"
    }
  } finally { $zip.Dispose() }
}

# --- LIESMICH and the Payload Manager list ------------------------------------------------------------------------
$liesmich = Get-Content -Raw -Encoding UTF8 -Path (Join-Path $releaseDir "LIESMICH.txt")
if ($liesmich.Contains($Version)) { Write-Ok "LIESMICH.txt names $Version" } else { Add-Failure "LIESMICH.txt does not name $Version" }

try {
  $pm = Get-Content -Raw -Encoding UTF8 -Path (Join-Path $releaseDir "payload-manager.json") | ConvertFrom-Json
  $p = @($pm.payloads)[0]
  $url = "https://github.com/$Repo/releases/download/$Tag/$elfName"
  if (-not $p) { Add-Failure "payload-manager.json has no payload" }
  elseif ($p.version -ne $Tag) { Add-Failure "payload-manager.json names version '$($p.version)', not $Tag" }
  elseif ($p.url -ne $url) { Add-Failure "payload-manager.json links '$($p.url)', not $url" }
  elseif ($p.checksum -ne $elfSha) { Add-Failure "payload-manager.json carries another SHA-256 than the ELF" }
  else { Write-Ok "payload-manager.json (version, link and SHA-256 of the ELF)" }
} catch {
  Add-Failure "payload-manager.json cannot be read: $($_.Exception.Message)"
}

if ($errors.Count -gt 0) { throw "Local release check failed: $($errors.Count) problem(s)." }
Write-Host "Local release assets: OK" -ForegroundColor Green

# --- the published release ----------------------------------------------------------------------------------------
if (-not $SkipGitHub) {
  if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw "gh not found (or pass -SkipGitHub)." }
  & gh auth status 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "gh is not authenticated. Run 'gh auth login' (or pass -SkipGitHub)." }

  $raw = & gh release view $Tag --repo $Repo --json url,assets,isDraft,isPrerelease 2>$null
  if ($LASTEXITCODE -ne 0) {
    Write-Host "GitHub: $Tag is not published yet (tools/release.sh publishes it)." -ForegroundColor Yellow
  } else {
    $release = ($raw -join "`n") | ConvertFrom-Json
    $assets = @($release.assets)
    foreach ($n in $expected) {
      $a = $assets | Where-Object { $_.name -eq $n } | Select-Object -First 1
      $local = (Get-FileHash -Algorithm SHA256 -Path (Join-Path $releaseDir $n)).Hash.ToLowerInvariant()
      if (-not $a) { Add-Failure "GitHub: $n is missing in $Tag" }
      elseif ($a.digest -ne "sha256:$local") { Add-Failure "GitHub: $n differs (local sha256:$local, GitHub '$($a.digest)')" }
      else { Write-Ok "GitHub: $n sha256:$($local.Substring(0, 16))..." }
    }
    foreach ($a in $assets) {
      if ($a.name -notin $expected) { Add-Failure "GitHub: $Tag carries a file that is not part of a release: $($a.name)" }
    }
    if ($release.isDraft) { Add-Failure "GitHub: $Tag is a draft" }
    if ($release.isPrerelease) { Write-Host "note: $Tag is marked as a pre-release" -ForegroundColor Yellow }
    $latest = (& gh api "repos/$Repo/releases/latest" --jq .tag_name 2>$null)
    if ($LASTEXITCODE -eq 0 -and $latest -ne $Tag) { Write-Host "note: the latest release is $latest, not $Tag" -ForegroundColor Yellow }
    if ($errors.Count -gt 0) { throw "GitHub release check failed: $($errors.Count) problem(s)." }
    Write-Host "GitHub release assets: OK" -ForegroundColor Green
    Write-Host "URL: $($release.url)"
  }
}

Write-Host "Pre-release check completed successfully." -ForegroundColor Green
