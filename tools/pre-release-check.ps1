<#
.SYNOPSIS
  One-command pre-release workflow: build, local release checks, optional GitHub release sync.

.EXAMPLE
  .\tools\pre-release-check.ps1

.EXAMPLE
  .\tools\pre-release-check.ps1 -CreateOrUpdateRelease

.EXAMPLE
  .\tools\pre-release-check.ps1 -Repo "strongt1me/ps5-cooling-system-center-pro" -Tag "v1.32.1"

.PARAMETER LlvmMajor
  The LLVM major version the WSL build must run on (default 21; 0 switches the
  check off). The SDK's prospero-llvm-config takes the highest llvm-config-NN it
  finds, so a newer or older LLVM in the distro would otherwise build quietly
  with a different toolchain than the one the releases were made with.
#>

param(
  [string]$Repo = "strongt1me/ps5-cooling-system-center-pro",
  [string]$Tag,
  [switch]$SkipGitHub,
  [switch]$CreateOrUpdateRelease,
  [int]$LlvmMajor = 21
)

$ErrorActionPreference = "Stop"

function ConvertTo-WslPath {
  param([Parameter(Mandatory=$true)][string]$WindowsPath)
  $full = [System.IO.Path]::GetFullPath($WindowsPath)
  if ($full.Length -lt 3 -or $full[1] -ne ':') {
    throw "Windows path expected: $WindowsPath"
  }
  $drive = $full.Substring(0, 1).ToLowerInvariant()
  $rest = $full.Substring(2).Replace('\', '/')
  return "/mnt/$drive$rest"
}

function Assert-Command {
  param([Parameter(Mandatory=$true)][string]$Name)
  if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
    throw "Required command not found: $Name"
  }
}

function Get-VersionFromHeader {
  param([Parameter(Mandatory=$true)][string]$HeaderPath)
  $text = Get-Content -Raw -Path $HeaderPath
  $m = [regex]::Match($text, '^\s*#define\s+PS5TM_VERSION\s+"([^"]+)"',
                      [System.Text.RegularExpressions.RegexOptions]::Multiline)
  if (-not $m.Success) {
    throw "Could not read PS5TM_VERSION from $HeaderPath"
  }
  return $m.Groups[1].Value
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $projectRoot

Assert-Command "wsl"
Assert-Command "make"

$version = Get-VersionFromHeader (Join-Path $projectRoot "src/ps5tm.h")
if (-not $Tag) {
  $Tag = "v$version"
}

# The assets are built from the working tree, and a tag that gh creates points
# at the remote's default branch, not at what was built. Said now, before the
# long build, so there is still time to stop - a warning, not a refusal.
if ($CreateOrUpdateRelease -and (Get-Command git -ErrorAction SilentlyContinue)) {
  $dirty = @(& git -C $projectRoot status --porcelain 2>$null)
  if ($dirty.Count -gt 0) {
    Write-Warning ("The working tree has $($dirty.Count) uncommitted change(s). " +
                   "The release will be built from them and published as $Tag, " +
                   "which may not match any commit. Ctrl+C now to stop.")
  }
}

$sdk = $env:PS5_PAYLOAD_SDK
if (-not $sdk) {
  $userSdk = [Environment]::GetEnvironmentVariable("PS5_PAYLOAD_SDK", "User")
  if ($userSdk) { $sdk = $userSdk }
}
if (-not $sdk) {
  $candidate1 = Join-Path $projectRoot "PS5_PAYLOAD_SDK"
  $candidate2 = Join-Path (Split-Path $projectRoot -Parent) "PS5 SDK usw\PS5_PAYLOAD_SDK"
  if (Test-Path $candidate1) {
    $sdk = $candidate1
  } elseif (Test-Path $candidate2) {
    $sdk = $candidate2
  }
}
if (-not $sdk) {
  throw "PS5_PAYLOAD_SDK is not set and no local fallback was found."
}
if (-not (Test-Path (Join-Path $sdk "toolchain/prospero.mk"))) {
  throw "Invalid PS5_PAYLOAD_SDK: prospero.mk not found under $sdk"
}

$projectWsl = ConvertTo-WslPath $projectRoot
$sdkWsl = ConvertTo-WslPath $sdk

# A space anywhere in the SDK's path breaks the WSL build, far from the cause:
# the SDK's prospero.mk works out PS5_PAYLOAD_SDK again from MAKEFILE_LIST,
# which splits at spaces, and its linker wrapper passes the linker-script path
# unquoted. make then stops with "Error 127" on the first compile. (The project
# folder may contain spaces; only the SDK may not. The Windows route,
# tools/build-windows.sh, gets round it with 8.3 short names, which WSL has not.)
# Both fallback locations above contain spaces, so this is the usual case.
if ($sdkWsl -match '\s') {
  throw ("The SDK path contains a space: $sdk`n" +
         "The WSL build cannot use it (make would stop with Error 127 on the first compile).`n" +
         "Copy the SDK to a folder without spaces, e.g. C:\ps5sdk, and set PS5_PAYLOAD_SDK to it,`n" +
         "or build with tools/build-windows.sh instead.")
}

Write-Host "== Pre-release check ==" -ForegroundColor Cyan
Write-Host "Project: $projectRoot"
Write-Host "Version: $version"
Write-Host "Tag: $Tag"
Write-Host "SDK: $sdk"

# The toolchain is whatever the SDK's own llvm-config wrapper finds - the build
# runs through it - so ask the wrapper, in a call of its own. (Not as part of the
# build command below: wsl.exe hands that to a shell first, which would expand
# any $variable in it before bash ever sees it.)
if ($LlvmMajor -gt 0) {
  $llvmFound = (& wsl.exe bash -lc "'$sdkWsl/bin/prospero-llvm-config' --version" 2>$null | Select-Object -First 1)
  $llvmFoundMajor = if ($llvmFound) { ([string]$llvmFound -split '\.')[0].Trim() } else { "" }
  if ($llvmFoundMajor -ne "$LlvmMajor") {
    $seen = if ($llvmFound) { "LLVM $llvmFound" } else { "no LLVM" }
    throw ("The WSL build would run on $seen, but LLVM $LlvmMajor is expected: another major builds " +
           "an ELF with another layout than the releases have. Install LLVM $LlvmMajor in the WSL distro, " +
           "or pass -LlvmMajor <n> to build with the one it has on purpose (0 skips the check).")
  }
}

$buildCmd = "cd '$projectWsl' && export PS5_PAYLOAD_SDK='$sdkWsl' && make clean && make -j1 && make release"
& wsl.exe bash -lc $buildCmd
if ($LASTEXITCODE -ne 0) {
  throw "Build/release failed in WSL."
}

$releaseDir = Join-Path $projectRoot "PS5 Cooling and System Center v$version"
if (-not (Test-Path $releaseDir)) {
  throw "Release directory not found: $releaseDir"
}

$mainElf = Join-Path $releaseDir "PS5_Cooling_System_Center_v$version.elf"
$installerElf = Join-Path $releaseDir "cooling-center-launcher-installer_v$version.elf"
$liesmich = Join-Path $releaseDir "LIESMICH.txt"
$pkg = Get-ChildItem -Path $releaseDir -Filter "*.pkg" -File | Select-Object -First 1

foreach ($p in @($mainElf, $installerElf, $liesmich)) {
  if (-not (Test-Path $p)) {
    throw "Missing release asset: $p"
  }
}
if (-not $pkg) {
  throw "Missing release package (*.pkg) in $releaseDir"
}

Write-Host "Local release assets: OK" -ForegroundColor Green
Write-Host "- $(Split-Path $mainElf -Leaf)"
Write-Host "- $(Split-Path $installerElf -Leaf)"
Write-Host "- $($pkg.Name)"
Write-Host "- $(Split-Path $liesmich -Leaf)"

if (-not $SkipGitHub) {
  Assert-Command "gh"
  & gh auth status | Out-Null
  if ($LASTEXITCODE -ne 0) {
    throw "gh is not authenticated. Run 'gh auth login'."
  }

  $requiredAssetNames = @(
    (Split-Path $mainElf -Leaf),
    (Split-Path $installerElf -Leaf),
    $pkg.Name,
    (Split-Path $liesmich -Leaf)
  )

  $releaseExists = $true
  $releaseViewRaw = & gh release view $Tag --repo $Repo --json url,assets 2>$null
  if ($LASTEXITCODE -ne 0) {
    $releaseExists = $false
  }

  if ($CreateOrUpdateRelease) {
    if (-not $releaseExists) {
      & gh release create $Tag $mainElf $installerElf $pkg.FullName $liesmich `
        --repo $Repo --title "PS5 Cooling & System Center - Pro $Tag" `
        --notes "Automated pre-release upload for $Tag"
      if ($LASTEXITCODE -ne 0) {
        throw "gh release create failed."
      }
      $releaseViewRaw = & gh release view $Tag --repo $Repo --json url,assets
    }
    else {
      & gh release upload $Tag $mainElf $installerElf $pkg.FullName $liesmich `
        --repo $Repo --clobber
      if ($LASTEXITCODE -ne 0) {
        throw "gh release upload failed."
      }
      $releaseViewRaw = & gh release view $Tag --repo $Repo --json url,assets
    }
  }
  elseif (-not $releaseExists) {
    throw "Release $Tag not found in $Repo. Use -CreateOrUpdateRelease to publish assets."
  }

  $release = $releaseViewRaw | ConvertFrom-Json
  $names = @($release.assets | ForEach-Object { $_.name })
  $missing = @($requiredAssetNames | Where-Object { $_ -notin $names })
  if ($missing.Count -gt 0) {
    throw "Release asset verification failed. Missing: $($missing -join ', ')"
  }

  Write-Host "GitHub release assets: OK" -ForegroundColor Green
  Write-Host "URL: $($release.url)"
}

Write-Host "Pre-release check completed successfully." -ForegroundColor Green
