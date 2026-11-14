#Requires -Version 5.1
#
# ChromiumTap build pipeline.
#
#   ChromiumTap.dll - Decryption and credential extraction engine
#   ChromiumTap.exe - GUI and orchestration CLI tool
#
# Flags enforced: /W4 /WX (WarningLevel=Level4, TreatWarningAsError=true)
# Toolchain: Automatically detects the latest Visual Studio instance & MSVC toolset.

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64', 'Win32')]
    [string]$Platform      = 'x64',

    [string]$Timestamp     = '2030-01-01 00:00:00',

    [switch]$Rebuild,
    [switch]$Clean,
    [switch]$KeepObj,
    [switch]$KeepDll,
    [switch]$KeepSymbols,
    [switch]$IncludeLanguageFiles,
    [switch]$UsePrebuiltModule,
    [switch]$CleanResources,

    # Component switches (omit all to build both)
    [switch]$Dll,
    [switch]$Exe
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$ProjectRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$BinDir      = Join-Path $ProjectRoot 'bin'
$ObjDir      = Join-Path $ProjectRoot 'obj'
$WallpaperBmp = Join-Path $ProjectRoot 'resources\wallpaper.bmp'
$ModuleStage  = Join-Path $ObjDir 'embedded-module-stage'

# --- Console formatting helpers -----------------------------------------------
function Write-Banner([string]$Text)    { Write-Host "`n=== $Text ===" -ForegroundColor Cyan }
function Write-Info([string]$Message)    { Write-Host "    [*] $Message" -ForegroundColor Cyan }
function Write-Step([string]$Message)    { Write-Host "    [-] $Message" -ForegroundColor DarkGray }
function Write-Success([string]$Message) { Write-Host "    [+] $Message" -ForegroundColor Green }
function Write-Failure([string]$Message) { Write-Host "`n[!] ERROR: $Message" -ForegroundColor Red }

# --- Timestamps ---------------------------------------------------------------
function Parse-FixedTimestamp([string]$Value) {
    $styles = [System.Globalization.DateTimeStyles]::AllowWhiteSpaces -bor
              [System.Globalization.DateTimeStyles]::AssumeLocal
    try {
        return [datetime]::Parse($Value, [System.Globalization.CultureInfo]::InvariantCulture, $styles)
    }
    catch {
        throw "Invalid -Timestamp '$Value'. Example: 2030-01-01 00:00:00"
    }
}

function Set-FixedFileTimestamp([string[]]$Paths, [datetime]$Value) {
    foreach ($path in $Paths) {
        if (Test-Path -LiteralPath $path) {
            $item = Get-Item -LiteralPath $path
            $item.CreationTime   = $Value
            $item.LastWriteTime  = $Value
            $item.LastAccessTime = $Value
        }
    }
}

# --- Toolchain Discovery ------------------------------------------------------
function Find-LatestVisualStudio {
    $candidates = @()

    # 1. Query vswhere
    $vswhereCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    )
    $vswhereCmd = Get-Command 'vswhere.exe' -CommandType Application -ErrorAction SilentlyContinue
    if ($vswhereCmd) { $vswhereCandidates += $vswhereCmd.Source }

    $vswhere = $vswhereCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1

    if ($vswhere) {
        $json = & $vswhere -all -products * -prerelease -sort -format json -utf8 2>$null
        if ($LASTEXITCODE -eq 0 -and $json) {
            try {
                $installs = ($json -join "`n") | ConvertFrom-Json
                foreach ($inst in $installs) {
                    if ($inst.installationPath) { $candidates += $inst.installationPath }
                }
            } catch {}
        }
    }

    # 2. Filesystem search across standard roots
    $searchRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
    foreach ($root in $searchRoots) {
        $vsRoot = Join-Path $root 'Microsoft Visual Studio'
        if (Test-Path -LiteralPath $vsRoot) {
            $yearDirs = Get-ChildItem -LiteralPath $vsRoot -Directory -ErrorAction SilentlyContinue |
                        Sort-Object {
                            $clean = ($_.Name -replace '[^\d.]', '')
                            if ($clean) { if ($clean -notmatch '\.') { "$clean.0" } else { $clean } } else { '0.0' }
                        } -Descending
            foreach ($yd in $yearDirs) {
                foreach ($ed in Get-ChildItem -LiteralPath $yd.FullName -Directory -ErrorAction SilentlyContinue) {
                    $candidates += $ed.FullName
                }
            }
        }
    }

    # 3. Filter candidates: must contain MSBuild and MSVC C++ toolchain (avoids SSMS etc.)
    foreach ($vsPath in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        $msvcRoot = Join-Path $vsPath 'VC\Tools\MSVC'
        if (-not (Test-Path -LiteralPath $msvcRoot)) { continue }

        $bestMsvc = Get-ChildItem -LiteralPath $msvcRoot -Directory -ErrorAction SilentlyContinue |
                    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe') } |
                    Sort-Object {
                        $v = $null
                        if ([version]::TryParse($_.Name, [ref]$v)) { $v } else { [version]'0.0' }
                    } -Descending |
                    Select-Object -First 1

        if (-not $bestMsvc) { continue }

        $msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\amd64\MSBuild.exe'
        if (-not (Test-Path -LiteralPath $msbuild)) {
            $msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
        }
        if (-not (Test-Path -LiteralPath $msbuild)) {
            $msbuild = Join-Path $vsPath 'MSBuild\15.0\Bin\MSBuild.exe'
        }
        if (-not (Test-Path -LiteralPath $msbuild)) { continue }

        $dumpbin = Join-Path $bestMsvc.FullName 'bin\Hostx64\x64\dumpbin.exe'

        return [pscustomobject]@{
            VsPath     = $vsPath
            MSBuild    = $msbuild
            MsvcName   = $bestMsvc.Name
            MsvcPath   = $bestMsvc.FullName
            Dumpbin    = $(if (Test-Path -LiteralPath $dumpbin) { $dumpbin } else { $null })
        }
    }

    throw 'No Visual Studio with MSBuild and Desktop C++ x64 toolset was found.'
}

function Find-LatestWindowsSdk {
    $roots = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'),
        (Join-Path $env:ProgramFiles 'Windows Kits\10')
    ) | Where-Object { Test-Path -LiteralPath $_ }

    foreach ($sdkRoot in $roots) {
        $inc = Join-Path $sdkRoot 'Include'
        if (-not (Test-Path -LiteralPath $inc)) { continue }

        $latest = Get-ChildItem -LiteralPath $inc -Directory -ErrorAction SilentlyContinue |
                  Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
                  Sort-Object { [version]$_.Name } -Descending |
                  Select-Object -First 1

        if ($latest) {
            return [pscustomobject]@{
                Version = $latest.Name
                Root    = $sdkRoot
            }
        }
    }

    return $null
}

# --- Ensure Required Headers & Assets -----------------------------------------
function Ensure-ProjectPrerequisites {
    # If resource.h or HelpSystem.h or ICON are missing in standalone kvc_pass, borrow from sibling kvc
    $siblingKvc = Join-Path (Split-Path $ProjectRoot -Parent) 'kvc\kvc'

    $resourceH = Join-Path $ProjectRoot 'resources\resource.h'
    if (-not (Test-Path -LiteralPath $resourceH)) {
        $sourceH = Join-Path $siblingKvc 'resource.h'
        if (Test-Path -LiteralPath $sourceH) {
            Copy-Item -LiteralPath $sourceH -Destination $resourceH -Force
            Write-Step 'Restored resources\resource.h from kvc'
        }
    }

    $helpH = Join-Path $ProjectRoot 'app\HelpSystem.h'
    if (-not (Test-Path -LiteralPath $helpH)) {
        $sourceH = Join-Path $siblingKvc 'HelpSystem.h'
        if (Test-Path -LiteralPath $sourceH) {
            Copy-Item -LiteralPath $sourceH -Destination $helpH -Force
            Write-Step 'Restored app\HelpSystem.h from kvc'
        }
    }

}

function Assert-LocalizationCatalogs {
    $englishPath = Join-Path $ProjectRoot 'resources\lang\en.rc'
    $polishPath  = Join-Path $ProjectRoot 'resources\lang\pl.rc'
    if (-not (Test-Path -LiteralPath $englishPath) -or
        -not (Test-Path -LiteralPath $polishPath)) {
        throw 'Localization catalogs en.rc and pl.rc are required.'
    }

    function Read-Catalog([string]$Path) {
        $entries = @{}
        foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
            if ($line -match '^\s*(IDS_[A-Z0-9_]+)\s+"(.*)"\s*$') {
                $key = $Matches[1]
                $value = $Matches[2]
                if ($entries.ContainsKey($key)) {
                    throw "Duplicate localization key '$key' in $Path"
                }
                $entries[$key] = ([regex]::Matches($value, '\{\}')).Count
            }
        }
        return $entries
    }

    $english = Read-Catalog $englishPath
    $polish  = Read-Catalog $polishPath
    $missing = @($english.Keys | Where-Object { -not $polish.ContainsKey($_) } | Sort-Object)
    $extra   = @($polish.Keys | Where-Object { -not $english.ContainsKey($_) } | Sort-Object)
    if ($missing.Count -gt 0) {
        throw "Missing Polish localization keys: $($missing -join ', ')"
    }
    if ($extra.Count -gt 0) {
        throw "Polish localization contains unknown keys: $($extra -join ', ')"
    }

    $placeholderMismatch = @($english.Keys | Where-Object {
        $english[$_] -ne $polish[$_]
    } | Sort-Object)
    if ($placeholderMismatch.Count -gt 0) {
        throw "Localization placeholder mismatch: $($placeholderMismatch -join ', ')"
    }

    Write-Step "Localization catalogs verified: $($english.Count) keys"
}

function Find-Implementer {
    param(
        [switch]$Optional
    )

    $projectsRoot = Split-Path $ProjectRoot -Parent
    $candidates = @(
        (Join-Path $projectsRoot 'kvc\Implementer\bin\implementer.exe'),
        (Join-Path $projectsRoot 'Implementer\bin\implementer.exe'),
        (Join-Path $projectsRoot 'implementer\bin\implementer.exe')
    )
    $command = Get-Command 'implementer.exe' -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty Source
    if ($command) {
        $candidates += $command
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            Write-Step "Using implementer: $candidate"
            return $candidate
        }
    }
    if ($Optional) {
        return $null
    }
    throw 'implementer.exe was not found. Build Implementer before ChromiumTap.exe.'
}

function Build-EmbeddedModule {
    param(
        [Parameter(Mandatory)][string]$DllPath,
        [Parameter(Mandatory)][string]$Implementer
    )

    if (-not (Test-Path -LiteralPath $DllPath -PathType Leaf)) {
        throw "ChromiumTap.dll is required for the embedded module: $DllPath"
    }

    if (Test-Path -LiteralPath $ModuleStage) {
        $resolvedStage = [IO.Path]::GetFullPath($ModuleStage)
        $resolvedObj = [IO.Path]::GetFullPath($ObjDir)
        if (-not $resolvedStage.StartsWith($resolvedObj, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean embedded module staging directory: $resolvedStage"
        }
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }

    New-Item -ItemType Directory -Path $ModuleStage -Force | Out-Null
    Copy-Item -LiteralPath $DllPath -Destination (Join-Path $ModuleStage 'ChromiumTap.dll') -Force

    Write-Info "Building embedded module image..."
    $packOutput = @(& $Implementer pack $ModuleStage $WallpaperBmp 'wallpaper.bmp' --strip-pe 2>&1)
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $WallpaperBmp -PathType Leaf)) {
        $packOutput | Write-Host
        throw "Failed to build resources\wallpaper.bmp"
    }

    $inspectOutput = @(& $Implementer inspect $WallpaperBmp 2>&1)
    if ($LASTEXITCODE -ne 0) {
        $inspectOutput | Write-Host
        throw "Embedded wallpaper.bmp verification failed."
    }
    Write-Success ("Built embedded module image -> resources\wallpaper.bmp ({0:N0} bytes)" -f (Get-Item -LiteralPath $WallpaperBmp).Length)

}

# --- Clean action -------------------------------------------------------------
if ($Clean) {
    Write-Banner 'Cleaning Build Outputs'
    if (Test-Path -LiteralPath $ObjDir) {
        Remove-Item -LiteralPath $ObjDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Step 'Removed obj\'
    }
    if (Test-Path -LiteralPath $BinDir) {
        Remove-Item -LiteralPath (Join-Path $BinDir 'ChromiumTap.*'), (Join-Path $BinDir 'ChromiumTap.pdb'), (Join-Path $BinDir 'kvc_crypt.*'), (Join-Path $BinDir 'kvc_pass.*') -Force -ErrorAction SilentlyContinue
        $stagedLanguage = Join-Path $BinDir 'language'
        if (Test-Path -LiteralPath $stagedLanguage) {
            Remove-Item -LiteralPath $stagedLanguage -Recurse -Force
        }
        Write-Step 'Cleaned bin\'
    }
    if ($CleanResources -and (Test-Path -LiteralPath $WallpaperBmp)) {
        Remove-Item -LiteralPath $WallpaperBmp -Force
        Write-Step 'Removed generated resources\wallpaper.bmp'
    }
    Write-Success 'Clean completed.'
    return
}

# --- Main Execution -----------------------------------------------------------
try {
    if ($UsePrebuiltModule -and $Dll) {
        throw '-UsePrebuiltModule cannot be combined with -Dll.'
    }

    $buildAllRequested = -not ($Dll -or $Exe -or $UsePrebuiltModule)
    $buildExeRequested = $buildAllRequested -or $Exe -or $UsePrebuiltModule
    $buildDllRequested = $buildAllRequested -or $Dll -or $Exe
    $implementer = $null
    if ($buildExeRequested -and -not $UsePrebuiltModule) {
        $implementer = Find-Implementer -Optional
    }

    $useBundledModule = $UsePrebuiltModule -or ($buildExeRequested -and -not $implementer)
    if ($useBundledModule -and $Dll) {
        throw 'Rebuilding ChromiumTap.dll requires implementer.exe.'
    }

    if ($useBundledModule) {
        if (-not (Test-Path -LiteralPath $WallpaperBmp -PathType Leaf)) {
            throw 'The bundled wallpaper.bmp resource is required when implementer.exe is unavailable.'
        }
    }

    $buildTitle = if ($useBundledModule) {
        "Building ChromiumTap.exe with bundled module ($Configuration | $Platform)"
    } else {
        "Building ChromiumTap.exe & ChromiumTap.dll ($Configuration | $Platform)"
    }
    Write-Banner $buildTitle

    Ensure-ProjectPrerequisites
    Assert-LocalizationCatalogs

    $fixedTimestamp = Parse-FixedTimestamp -Value $Timestamp
    $epoch = [DateTimeOffset]::new($fixedTimestamp).ToUnixTimeSeconds()
    $env:SOURCE_DATE_EPOCH = [string]$epoch

    # Discover Toolchain
    $toolchain = Find-LatestVisualStudio
    $sdk       = Find-LatestWindowsSdk

    Write-Info "Visual Studio : $($toolchain.VsPath)"
    Write-Info "MSBuild       : $($toolchain.MSBuild)"
    Write-Info "MSVC Toolset  : $($toolchain.MsvcName)"
    if ($sdk) { Write-Info "Windows SDK   : $($sdk.Version)" }
    Write-Info "Warnings Mode : /W4 /WX (Strict Zero Warnings)"
    Write-Info "Output Root   : $BinDir"

    # Ensure output directory exists
    if (-not (Test-Path -LiteralPath $BinDir)) {
        New-Item -ItemType Directory -Path $BinDir | Out-Null
    }

    # Decide targets to build
    $buildDll = -not $useBundledModule -and $buildDllRequested
    $buildExe = $buildExeRequested
    $projectsToBuild = @()

    if ($buildDll) {
        $projectsToBuild += @{
            Name       = 'ChromiumTap.dll'
            Vcxproj    = Join-Path $ProjectRoot 'ChromiumTapCore.vcxproj'
            OutputFile = Join-Path $BinDir 'ChromiumTap.dll'
        }
    }

    if ($buildExe) {
        if ($useBundledModule) {
            if ($UsePrebuiltModule) {
                Write-Info 'Embedded module: using bundled wallpaper.bmp (forced)'
            } else {
                Write-Info 'Embedded module: implementer.exe not found; using bundled wallpaper.bmp'
            }
        } elseif ($implementer) {
            Write-Info "Embedded module: rebuilding with $implementer"
        }
        $projectsToBuild += @{
            Name       = 'ChromiumTap.exe'
            Vcxproj    = Join-Path $ProjectRoot 'ChromiumTap.vcxproj'
            OutputFile = Join-Path $BinDir 'ChromiumTap.exe'
        }
    }

    $targetAction = if ($Rebuild) { 'Rebuild' } else { 'Build' }

    foreach ($p in $projectsToBuild) {
        Write-Banner "Compiling $($p.Name) ($targetAction)"

        $msbuildArgs = @(
            $p.Vcxproj,
            "/t:$targetAction",
            "/p:Configuration=$Configuration",
            "/p:Platform=$Platform",
            "/p:SolutionDir=$ProjectRoot\",
            "/p:OutDir=$BinDir\",
            "/p:IntDir=$ObjDir\$($p.Name)\$Configuration\$Platform\",
            "/p:WarningLevel=Level4",
            "/p:TreatWarningAsError=true",
            "/p:MASM_WarningLevel=3",
            "/p:MASM_TreatWarningsAsErrors=true",
            "/p:VCToolsVersion=$($toolchain.MsvcName)",
            "/p:SOURCE_DATE_EPOCH=$epoch",
            '/m',
            '/nologo',
            '/v:minimal',
            '/clp:Summary'
        )

        if ($sdk) {
            $msbuildArgs += "/p:WindowsTargetPlatformVersion=$($sdk.Version)"
        }

        & $toolchain.MSBuild @msbuildArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile $($p.Name) with exit code $LASTEXITCODE"
        }

        if (-not (Test-Path -LiteralPath $p.OutputFile)) {
            throw "Build finished but output file was not found: $($p.OutputFile)"
        }

        Write-Success "Built $($p.Name) -> $($p.OutputFile)"

        if ($p.Name -eq 'ChromiumTap.dll' -and $buildExe) {
            Build-EmbeddedModule -DllPath $p.OutputFile -Implementer $implementer
        }
    }

    if ($buildExe -and -not $Dll -and -not $KeepDll) {
        $stagedDll = Join-Path $BinDir 'ChromiumTap.dll'
        if (Test-Path -LiteralPath $stagedDll) {
            Remove-Item -LiteralPath $stagedDll -Force
            Write-Step 'Final layout: embedded DLL, only ChromiumTap.exe remains in bin\'
        }
    }

    if ($IncludeLanguageFiles) {
        # Optional external language packs. Built-in en/pl catalogs require no sidecar files.
        $srcLangDir = Join-Path $ProjectRoot 'resources\language'
        if (Test-Path -LiteralPath $srcLangDir) {
            $dstLangDir = Join-Path $BinDir 'language'
            if (-not (Test-Path -LiteralPath $dstLangDir)) {
                New-Item -ItemType Directory -Path $dstLangDir | Out-Null
            }
            Copy-Item -Path (Join-Path $srcLangDir '*.lng') -Destination $dstLangDir -Force
            Write-Step 'Copied resources\language\*.lng -> bin\language\'
        }
    }

    if (-not $KeepSymbols) {
        $pdbPath = Join-Path $BinDir 'ChromiumTap.pdb'
        if (Test-Path -LiteralPath $pdbPath) {
            Remove-Item -LiteralPath $pdbPath -Force
            Write-Step 'Removed ChromiumTap.pdb from final bin\'
        }
    }

    # Clean intermediate obj directory unless -KeepObj is passed
    if (-not $KeepObj -and (Test-Path -LiteralPath $ObjDir)) {
        Remove-Item -LiteralPath $ObjDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Step 'Cleaned obj\ directory.'
    }

    # Deterministic file timestamp stamping
    $outputItems = @($projectsToBuild | ForEach-Object {
        Get-Item -LiteralPath $_.OutputFile -ErrorAction SilentlyContinue
    } | Where-Object { $_ })
    Set-FixedFileTimestamp -Paths ($outputItems | Select-Object -ExpandProperty FullName) -Value $fixedTimestamp

    # Summary
    Write-Banner 'Build Succeeded (/W4 /WX)'
    $summary = foreach ($item in $outputItems) {
        [pscustomobject]@{
            Binary = $item.Name
            Size   = ('{0:N0} bytes' -f $item.Length)
            SHA256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.Substring(0, 16) + '...'
            Status = 'OK'
        }
    }
    $summary | Format-Table -AutoSize
}
catch {
    Write-Failure $_.Exception.Message
    exit 1
}
