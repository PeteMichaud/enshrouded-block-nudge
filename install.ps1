<#
.SYNOPSIS
Copies BlockNudge from this checkout into the Enshrouded game folder.

.DESCRIPTION
Finds the Steam install of Enshrouded, then copies dist\dinput8.dll and, if the game folder
has none yet, BlockNudge.ini next to enshrouded.exe. Run it through install.cmd.

.PARAMETER GameDir
The folder that holds enshrouded.exe. Only needed if it is not found through Steam.

.PARAMETER Uninstall
Removes BlockNudge's dinput8.dll and BlockNudge.ini from the game folder. Logs are kept.

.PARAMETER ResetIni
Overwrites the game folder's BlockNudge.ini with the default one from this checkout.

.PARAMETER Force
Replaces a dinput8.dll in the game folder even if it belongs to another mod.
#>
[CmdletBinding()]
param(
    [string]$GameDir,
    [switch]$Uninstall,
    [switch]$ResetIni,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$SteamAppId = '1203620'   # Enshrouded
$Repo = $PSScriptRoot

function Find-GameDir {
    $steamRoots = @()
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
        $props = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if (-not $props) { continue }
        foreach ($name in 'SteamPath', 'InstallPath') {
            $value = $props.PSObject.Properties[$name]
            if ($value -and $value.Value) { $steamRoots += ($value.Value -replace '/', '\') }
        }
    }

    # Each Steam library is listed in the main one's libraryfolders.vdf.
    $libraries = @()
    foreach ($root in $steamRoots) {
        $libraries += $root
        $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $vdf)) { continue }
        $text = Get-Content -LiteralPath $vdf -Raw
        foreach ($match in [regex]::Matches($text, '"path"\s+"([^"]+)"')) {
            $libraries += ($match.Groups[1].Value -replace '\\\\', '\')
        }
    }

    foreach ($library in ($libraries | Select-Object -Unique)) {
        $manifest = Join-Path $library "steamapps\appmanifest_$SteamAppId.acf"
        if (-not (Test-Path -LiteralPath $manifest)) { continue }
        $match = [regex]::Match((Get-Content -LiteralPath $manifest -Raw), '"installdir"\s+"([^"]+)"')
        if (-not $match.Success) { continue }
        $dir = Join-Path $library (Join-Path 'steamapps\common' $match.Groups[1].Value)
        if (Test-Path -LiteralPath (Join-Path $dir 'enshrouded.exe')) { return $dir }
    }
    return $null
}

# BlockNudge's DLL carries its name in its log messages; another mod's dinput8.dll does not.
function Test-IsBlockNudge([string]$Path) {
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Path))
    return $text.Contains('BlockNudge')
}

if (-not $GameDir) { $GameDir = Find-GameDir }
if (-not $GameDir) {
    throw "Enshrouded was not found through Steam. Run again with the folder that holds enshrouded.exe: install.cmd -GameDir ""D:\Games\Enshrouded"""
}
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'enshrouded.exe'))) {
    throw "There is no enshrouded.exe in $GameDir"
}
Write-Host "Game folder: $GameDir"

if (Get-Process -Name 'enshrouded' -ErrorAction SilentlyContinue) {
    throw 'Enshrouded is running. Close the game and run this again.'
}

$targetDll = Join-Path $GameDir 'dinput8.dll'
$targetIni = Join-Path $GameDir 'BlockNudge.ini'
$dllExists = Test-Path -LiteralPath $targetDll

if ($dllExists -and -not $Force -and -not (Test-IsBlockNudge $targetDll)) {
    throw @"
$targetDll belongs to another mod, so it was left alone.
Either load BlockNudge through that mod's loader (copy dist\dinput8.dll there under another
name, such as BlockNudge.dll), or run again with -Force to replace the other mod's DLL.
"@
}

if ($Uninstall) {
    if ($dllExists) {
        Remove-Item -LiteralPath $targetDll
        Write-Host 'Removed dinput8.dll'
    } else {
        Write-Host 'dinput8.dll was not installed'
    }
    if (Test-Path -LiteralPath $targetIni) {
        Remove-Item -LiteralPath $targetIni
        Write-Host 'Removed BlockNudge.ini'
    }
    Write-Host 'BlockNudge is uninstalled. The game is vanilla again.'
    return
}

$sourceDll = Join-Path $Repo 'dist\dinput8.dll'
$sourceIni = Join-Path $Repo 'BlockNudge.ini'
if (-not (Test-Path -LiteralPath $sourceDll)) {
    throw "$sourceDll is missing. Pull the latest commit, or build it (see README.md)."
}

Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force
$hash = (Get-FileHash -LiteralPath $targetDll -Algorithm SHA256).Hash.Substring(0, 12)
Write-Host "Copied dinput8.dll (sha256 $hash...)"

if ($ResetIni -or -not (Test-Path -LiteralPath $targetIni)) {
    Copy-Item -LiteralPath $sourceIni -Destination $targetIni -Force
    Write-Host 'Copied BlockNudge.ini'
} else {
    Write-Host 'Kept the BlockNudge.ini already in the game folder (-ResetIni replaces it)'
}

Write-Host ''
Write-Host 'BlockNudge is installed. Start the game, then follow TESTING.md.'
Write-Host "The log will be at: $(Join-Path $GameDir 'BlockNudge.log')"
