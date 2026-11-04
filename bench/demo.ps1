# Live demo of riptide on Windows; the PowerShell counterpart of bench/demo.sh.
#
#   powershell -ExecutionPolicy Bypass -File bench\demo.ps1 BUILD_DIR DATASET_DIR [-NoPause] [-NoOpen]
#
# DATASET_DIR must contain silesia_x5.tar.gz (built by bench/datasets.py, or copied from
# a machine that has it). The build is made first if BUILD_DIR has no riptide.exe; set
# VCPKG_ROOT so that CMake finds zlib. The script pauses before each step (press Enter);
# -NoPause (or NOPAUSE=1) runs straight through and -NoOpen (or NOOPEN=1) does not open
# the images.
#
# gzip is not part of Windows. The baseline is the gzip that comes with Git for Windows
# if there is one, otherwise Python's gzip module (zlib in both cases). Decompressed
# data never passes through a PowerShell pipeline, which would re-encode it as text.

param(
    [Parameter(Mandatory = $true)][string]$Build,
    [Parameter(Mandatory = $true)][string]$Data,
    [switch]$NoPause,
    [switch]$NoOpen
)

$Repo = Split-Path -Parent $PSScriptRoot
$File = Join-Path $Data 'silesia_x5.tar.gz'
$Out = Join-Path ([System.IO.Path]::GetTempPath()) 'riptide-demo'
if ($env:NOPAUSE) { $NoPause = $true }
if ($env:NOOPEN) { $NoOpen = $true }

function Bold([string]$Text) { Write-Host ''; Write-Host "== $Text" -ForegroundColor Cyan }
function Show([string]$Text) { Write-Host '> ' -ForegroundColor Green -NoNewline; Write-Host $Text }
function Warn([string]$Text) { Write-Host 'Warning: ' -ForegroundColor Yellow -NoNewline; Write-Host $Text }
function Pause-Demo { if (-not $NoPause) { [void](Read-Host '   [Enter]') } }

# Quotes one argument for a Windows command line (CommandLineToArgvW rules).
function Format-Argument([string]$Value) {
    if ($Value -and $Value -notmatch '[\s"]') { return $Value }
    return '"' + (($Value -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"'
}

# Runs a program and returns the SHA-256 of its standard output, read as raw bytes.
function Get-OutputHash([string]$Exe, [string[]]$Arguments) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Exe
    $info.Arguments = ($Arguments | ForEach-Object { Format-Argument $_ }) -join ' '
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $process = [System.Diagnostics.Process]::Start($info)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hash = $sha.ComputeHash($process.StandardOutput.BaseStream)
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "$Exe exited with code $($process.ExitCode)" }
    return ($hash | ForEach-Object { $_.ToString('x2') }) -join ''
}

function Find-Riptide {
    foreach ($config in 'Release', 'RelWithDebInfo', '.', 'Debug') {
        $path = Join-Path (Join-Path $Build $config) 'riptide.exe'
        if (Test-Path $path) { return (Resolve-Path $path).Path }
    }
    return $null
}

# Python that actually runs (the Microsoft Store placeholder only prints a hint).
function Find-Python {
    foreach ($candidate in 'py -3', 'python', 'python3') {
        $name, $prefix = $candidate -split ' '
        $prefix = @($prefix | Where-Object { $_ })
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if (-not $command) { continue }
        & $command.Source @prefix -c 'import sys' 2>$null
        if ($LASTEXITCODE -eq 0) { return @{ Exe = $command.Source; Prefix = $prefix } }
    }
    return $null
}

if (-not (Test-Path $File)) {
    Write-Host "No $File; copy it from a machine that has it, or build it with: python bench/datasets.py $Data"
    exit 1
}

$Riptide = Find-Riptide
if (-not $Riptide) {
    Write-Host 'Building riptide (first time only, a few minutes)...'
    $configure = @('-S', $Repo, '-B', $Build, '-DCMAKE_BUILD_TYPE=Release')
    if ($env:VCPKG_ROOT) {
        $configure += "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    }
    cmake @configure | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host 'cmake configure failed (is zlib installed? see README)'; exit 1 }
    cmake --build $Build --config Release --parallel | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host 'build failed'; exit 1 }
    $Riptide = Find-Riptide
}
$Config = Split-Path -Leaf (Split-Path -Parent $Riptide)
if ($Config -notin 'Release', 'RelWithDebInfo', 'Debug') { $Config = 'Release' }
New-Item -ItemType Directory -Force -Path $Out | Out-Null

$Python = Find-Python
$Gzip = Get-Command gzip -ErrorAction SilentlyContinue
if ($Gzip) {
    $Gzip = $Gzip.Source
} elseif (Test-Path "$env:ProgramFiles\Git\usr\bin\gzip.exe") {
    $Gzip = "$env:ProgramFiles\Git\usr\bin\gzip.exe"
}
if ($Gzip) {
    $Reference = @{ Name = 'gzip'; Exe = $Gzip; Test = @('-t', $File); Cat = @('-dc', $File) }
} elseif ($Python) {
    $copy = 'import gzip, shutil, sys; shutil.copyfileobj(gzip.open(sys.argv[1]), {0}, 1 << 20)'
    $Reference = @{
        Name = 'Python gzip (zlib)'; Exe = $Python.Exe
        Test = $Python.Prefix + @('-c', ($copy -f "open(__import__('os').devnull, 'wb')"), $File)
        Cat  = $Python.Prefix + @('-c', ($copy -f 'sys.stdout.buffer'), $File)
    }
} else {
    $Reference = $null
    Warn 'neither gzip (Git for Windows) nor Python found; skipping the comparison with zlib.'
}

# riptide holds the whole 1 GB output in memory (about 3 GB at peak). On battery or
# with little free memory it runs much slower than measured.
if (Get-Command Get-CimInstance -ErrorAction SilentlyContinue) {
    $battery = Get-CimInstance Win32_Battery -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($battery -and $battery.BatteryStatus -ne 2) { Warn 'on battery; plug in the charger.' }
    $os = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
    if ($os -and $os.FreePhysicalMemory -lt 4MB) {
        Warn ('only {0:N1} GB of memory is free; close other apps first.' -f ($os.FreePhysicalMemory / 1MB))
    }
}

if ($Reference) { $referenceTest = $Reference.Test }
if ($Python) { $pythonPrefix = $Python.Prefix }

# Read the file once so that both tools start with it in memory.
& $Riptide -t $File
if ($Reference) { & $Reference.Exe @referenceTest }

Bold '1. The file: Silesia x5, a normal .gz file made by gzip'
Show 'Get-Item silesia_x5.tar.gz'
Write-Host ('{0:N0} MB  {1}' -f ((Get-Item $File).Length / 1MB), $File)
Pause-Demo

Bold '2. Same work as gzip, but faster (decompress and check, output thrown away)'
if ($Reference) {
    Show "$($Reference.Name): decompress and verify silesia_x5.tar.gz"
    $referenceTime = (Measure-Command { & $Reference.Exe @referenceTest }).TotalSeconds
    Write-Host ('   {0:N2} s' -f $referenceTime)
}
Show 'riptide -t silesia_x5.tar.gz'
$riptideTime = (Measure-Command { & $Riptide -t $File }).TotalSeconds
Write-Host ('   {0:N2} s' -f $riptideTime)
if ($Reference) { Write-Host ('   riptide is {0:N1}x faster' -f ($referenceTime / $riptideTime)) }
Pause-Demo

Bold '3. Byte for byte the same as gzip'
if ($Reference) {
    Show "$($Reference.Name): decompress silesia_x5.tar.gz | sha256"
    Write-Host (Get-OutputHash $Reference.Exe $Reference.Cat)
}
Show 'riptide silesia_x5.tar.gz | sha256'
Write-Host (Get-OutputHash $Riptide @($File))
Pause-Demo

Bold '4. Where the time goes: the six stages'
Show 'riptide -v -t silesia_x5.tar.gz'
& $Riptide -v -t $File
Pause-Demo

Bold '5. M3: the window step with small chunks (64 KiB, 5,205 chunks)'
foreach ($w in 'serial', 'blocked') {
    Show "riptide -v -t --chunk-size 64K --window $w silesia_x5.tar.gz"
    & $Riptide -v -t --repeat 3 --chunk-size 64K --window $w $File 2>&1 |
        ForEach-Object { "$_" } | Where-Object { $_ -match '^riptide:|window resolution  ' }
}
Write-Host '   (The GPU strategies use Metal and are only available on Apple silicon.)'
Pause-Demo

Bold '6. Timelines of every thread (our tracer)'
$canPlot = $false
if ($Python) {
    & $Python.Exe @pythonPrefix -c 'import PIL' 2>$null
    $canPlot = $LASTEXITCODE -eq 0
}
$images = @()
foreach ($w in 'serial', 'blocked') {
    $trace = Join-Path $Out "$w.json"
    Show "riptide -t --chunk-size 64K --window $w --trace $w.json silesia_x5.tar.gz"
    & $Riptide -t --repeat 3 --chunk-size 64K --window $w --trace $trace $File
    if ($canPlot) {
        $image = Join-Path $Out "$w.png"
        & $Python.Exe @pythonPrefix (Join-Path $Repo 'bench\plot_trace.py') $trace $image --title "window strategy: $w"
        $images += $image
    }
}
if ($canPlot) {
    Write-Host '   Opening the two timelines. The JSON files can also be dropped into ui.perfetto.dev.'
    if (-not $NoOpen) { $images | ForEach-Object { Invoke-Item $_ } }
} else {
    Write-Host '   No Python with Pillow (pip install pillow) to draw them; drop the JSON files into ui.perfetto.dev.'
}
Pause-Demo

Bold '7. Tests: all compared against zlib'
Show "ctest --test-dir build -C $Config"
ctest --test-dir $Build -C $Config 2>&1 | Select-Object -Last 4

Write-Host ''
Write-Host "Trace files are in $Out"
