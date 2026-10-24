# h5-runner.ps1 - job bridge between Claude and this PC for the Halo 5 / WinDurango work.
#
# Start once from a normal PowerShell window (it finds Visual Studio itself):
#     powershell -ExecutionPolicy Bypass -File C:\Games\Halo5-Guardians-Dev\tools\h5-runner.ps1
#
# It watches Dev\queue\ for *.json job files and runs them one at a time. Jobs can only pick from a
# fixed set of actions (configure, build, stage, run, stop, selftest). Nothing in a job file is ever
# executed as a command: env var names/values, script names, ids and targets are all checked against
# strict patterns, and every path is built here, not taken from the job.
# Results go to Dev\results\<id>\ (status.json + logs). Press Ctrl+C to stop the runner.

$ErrorActionPreference = 'Stop'
$Dev      = Split-Path -Parent $PSScriptRoot
$Src      = Join-Path $Dev 'src'
$Build    = Join-Path $Dev 'build'
$Root     = Join-Path $Dev 'RootFolder'
$Queue    = Join-Path $Dev 'queue'
$Results  = Join-Path $Dev 'results'
$CdbDir   = Join-Path $Dev 'tools\cdb'
$Profile  = Join-Path $Dev 'profile'
$TempDir  = Join-Path $Dev 'temp'
$Vanilla  = 'C:\Games\Halo5-Guardians-Vanilla\RootFolder\game_Release.exe'

$VsRoot   = 'C:\Program Files\Microsoft Visual Studio\18\Community'
$Vcvars   = Join-Path $VsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$CMake    = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$Ninja    = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$Cdb      = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
$VcpkgTc  = 'C:/vcpkg/scripts/buildsystems/vcpkg.cmake'
$VcpkgDir = 'C:/wd/.cmake/Release/vcpkg_installed'

# DLLs the shim build produces and that 'stage' may copy into RootFolder (with matching .pdb).
$StageWhitelist = @('d3d11_x','kernelx','winrt_x','etwplus','mmdevapi','WinDurango.Common','WinDurango.Implementation.WinRT')
# CMake target names. 'shim' = the two DLLs that change almost every iteration. 'all' fails (Kinect SDK missing).
$TargetWhitelist = @('WinDurango.D3D11X','WinDurango.KernelX','WinDurango.WinRT','WinDurango.etwplus','WinDurango.MMDevAPI',
                     'WinDurango.Common','WinDurango.Implementation.WinRT')
$TargetSets = @{ 'shim' = @('WinDurango.D3D11X','WinDurango.KernelX') }
$ScriptStamp = (Get-Item -LiteralPath $PSCommandPath).LastWriteTimeUtc
$GameProcs = @('game_Release','cdb')

foreach ($d in $Queue,$Results,$TempDir) { New-Item -ItemType Directory -Force -Path $d | Out-Null }

function Write-Json($path, $obj) { $obj | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $path -Encoding UTF8 }
function Heartbeat($state, $job) {
    Write-Json (Join-Path $Results 'runner.json') ([ordered]@{ state=$state; job=$job; pid=$PID; time=(Get-Date).ToString('o') })
}
function Fail($msg) { throw [System.Exception]::new($msg) }

function Run-Cmd($label, $cmdline, $logPath, $timeoutSec) {
    # Runs "<vcvars> && <cmdline>" in cmd.exe; cmdline is always built by this script.
    $bat = Join-Path $TempDir ("h5-" + $label + ".cmd")
    Set-Content -LiteralPath $bat -Encoding ASCII -Value @("@echo off", "call `"$Vcvars`" >nul", "if errorlevel 1 exit /b 90", $cmdline, "exit /b %errorlevel%")
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'cmd.exe'; $psi.Arguments = "/d /c `"$bat`""
    $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $er = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($timeoutSec * 1000)) { & taskkill /T /F /PID $p.Id | Out-Null; Fail "$label timed out after $timeoutSec s" }
    $p.WaitForExit()
    Set-Content -LiteralPath $logPath -Value ($o.Result + "`r`n" + $er.Result)
    return $p.ExitCode
}

function Stop-Game {
    foreach ($n in $GameProcs) { Get-Process -Name $n -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue }
}

function Job-Selftest($out) {
    $checks = [ordered]@{}
    foreach ($kv in @{vcvars=$Vcvars; cmake=$CMake; ninja=$Ninja; cdb=$Cdb; src=$Src; root=$Root; vanilla=$Vanilla}.GetEnumerator()) {
        $checks[$kv.Key] = Test-Path -LiteralPath $kv.Value
    }
    $checks['vcpkg_toolchain'] = Test-Path -LiteralPath $VcpkgTc
    $checks['vcpkg_installed'] = Test-Path -LiteralPath $VcpkgDir
    $checks['exe_matches_vanilla'] = ((Get-FileHash (Join-Path $Root 'game_Release.exe')).Hash -eq (Get-FileHash $Vanilla).Hash)
    $checks['ps_version'] = $PSVersionTable.PSVersion.ToString()
    Write-Json (Join-Path $out 'selftest.json') $checks
    return 0
}

function Job-Configure($out) {
    $deps = ($Dev -replace '\\','/') + '/deps'
    $cmd = "`"$CMake`" -S `"$Src`" -B `"$Build`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$($Ninja -replace '\\','/')`" " +
           "-DCMAKE_SHARED_LINKER_FLAGS=`"/DEBUG /OPT:REF /OPT:ICF`" -DCMAKE_BUILD_TYPE=Release " +
           "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded -DCMAKE_POLICY_DEFAULT_CMP0141=NEW " +
           "-DCMAKE_TOOLCHAIN_FILE=$VcpkgTc -DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_INSTALLED_DIR=$VcpkgDir " +
           "-DFETCHCONTENT_SOURCE_DIR_IMGUI=`"$deps/imgui`" -DFETCHCONTENT_SOURCE_DIR_JSON=`"$deps/json`" " +
           "-DFETCHCONTENT_SOURCE_DIR_SPDLOG=`"$deps/spdlog`" -DFETCHCONTENT_FULLY_DISCONNECTED=ON"
    return Run-Cmd 'configure' $cmd (Join-Path $out 'configure.log') 1800
}

function Job-Build($out, $job) {
    $req = if ($job.target) { [string]$job.target } else { 'shim' }
    $targets = if ($TargetSets.ContainsKey($req)) { $TargetSets[$req] } else { @($req) }
    foreach ($t in $targets) { if ($TargetWhitelist -notcontains $t) { Fail "target '$t' not allowed" } }
    $cmd = "`"$CMake`" --build `"$Build`" --parallel --target " + ($targets -join ' ')
    $rc = Run-Cmd 'build' $cmd (Join-Path $out 'build.log') 3600
    # Keep just the error lines handy.
    Select-String -Path (Join-Path $out 'build.log') -Pattern ': (fatal )?error ' -ErrorAction SilentlyContinue |
        Select-Object -First 200 | ForEach-Object { $_.Line } | Set-Content (Join-Path $out 'errors.txt')
    return $rc
}

function Job-Stage($out, $job) {
    $bin = Join-Path $Build 'bin'
    $names = if ($job.dlls) { @($job.dlls) } else { $StageWhitelist }
    $prev = Join-Path $out 'prev'; New-Item -ItemType Directory -Force -Path $prev | Out-Null
    $manifest = @()
    foreach ($n in $names) {
        if ($StageWhitelist -notcontains $n) { Fail "dll '$n' not allowed" }
        foreach ($ext in '.dll','.pdb') {
            $srcFile = Join-Path $bin ($n + $ext)
            if (-not (Test-Path -LiteralPath $srcFile)) { if ($ext -eq '.dll') { Fail "missing $srcFile" } else { continue } }
            $dst = Join-Path $Root ($n + $ext)
            if (Test-Path -LiteralPath $dst) { Copy-Item -LiteralPath $dst -Destination $prev -Force }
            Copy-Item -LiteralPath $srcFile -Destination $dst -Force
            $manifest += [ordered]@{ file = $n + $ext; sha256 = (Get-FileHash $dst).Hash }
        }
    }
    Write-Json (Join-Path $out 'staged.json') $manifest
    return 0
}

function Job-Run($out, $job) {
    $script = [string]$job.script
    if ($script -notmatch '^[a-z0-9-]{1,40}$') { Fail 'bad script name' }
    $cf = Join-Path $CdbDir ($script + '.cdb')
    if (-not (Test-Path -LiteralPath $cf)) { Fail "no such cdb script: $script" }
    $timeout = [int]$job.timeout; if ($timeout -lt 10 -or $timeout -gt 3600) { $timeout = 600 }
    if ((Get-FileHash (Join-Path $Root 'game_Release.exe')).Hash -ne (Get-FileHash $Vanilla).Hash) { Fail 'game_Release.exe differs from Vanilla; refusing to run' }
    Stop-Game

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Cdb
    $log = Join-Path $out 'cdb.log'
    $psi.Arguments = "-G -cf `"$cf`" -logo `"$log`" `"$(Join-Path $Root 'game_Release.exe')`""
    $psi.WorkingDirectory = $Root
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $e = $psi.EnvironmentVariables
    $e['LOCALAPPDATA'] = Join-Path $Profile 'Local'
    $e['APPDATA'] = Join-Path $Profile 'Roaming'
    $e['TEMP'] = $TempDir; $e['TMP'] = $TempDir
    $e['HALO5_NATIVE_HOST'] = '1'; $e['HALO5_D3D_DEBUG'] = '1'; $e['_NT_SYMBOL_PATH'] = ''
    if ($job.frames) { $fd = Join-Path $out 'frames'; New-Item -ItemType Directory -Force $fd | Out-Null; $e['HALO5_FRAME_CAPTURE_DIR'] = $fd }
    if ($job.render_trace) { $rd = Join-Path $out 'render-trace'; New-Item -ItemType Directory -Force $rd | Out-Null; $e['HALO5_RENDER_TRACE_DIR'] = $rd }
    if ($job.env) {
        foreach ($p in $job.env.PSObject.Properties) {
            if ($p.Name -notmatch '^HALO5_[A-Z0-9_]{1,40}$') { Fail "env name '$($p.Name)' not allowed" }
            if ($p.Name -match '_DIR$') { Fail 'directory env vars are set by the runner, not the job' }
            $v = [string]$p.Value
            if ($v -notmatch '^[A-Za-z0-9_.-]{0,240}$') { Fail "env value for $($p.Name) not allowed" }
            $e[$p.Name] = $v
        }
    }
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.StandardInput.Close()
    $stdoutTask = $p.StandardOutput.ReadToEndAsync(); $stderrTask = $p.StandardError.ReadToEndAsync()
    $timedOut = -not $p.WaitForExit($timeout * 1000)
    if ($timedOut) { Stop-Game; $p.WaitForExit(15000) | Out-Null }
    Set-Content (Join-Path $out 'cdb-console.txt') ($stdoutTask.Result + $stderrTask.Result)
    $wdLog = Get-ChildItem (Join-Path $Profile 'Local\Halo5Standalone\WinDurango') -Filter 'windurango_log_*' -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime | Select-Object -Last 1
    if ($wdLog) { Copy-Item $wdLog.FullName $out }
    # Quick digests so results are readable without paging the whole log.
    if (Test-Path $log) {
        Select-String -Path $log -Pattern '^\[H5STUB\]' | ForEach-Object { $_.Line } | Sort-Object -Unique | Set-Content (Join-Path $out 'stubs.txt')
        Select-String -Path $log -Pattern '^=== ' | ForEach-Object { $_.Line } | Set-Content (Join-Path $out 'markers.txt')
    }
    Write-Json (Join-Path $out 'run.json') ([ordered]@{ timed_out=$timedOut; exit=$(if ($timedOut) { $null } else { $p.ExitCode }); timeout=$timeout })
    return 0
}

# Forge jobs: build Dev\forge-shim (forgebuild) and run Dev\Forge through ForgeLauncher (forge).
$ForgeShimDir = Join-Path $Dev 'forge-shim'
$ForgeDir  = Join-Path $Dev 'Forge'
$ForgeHash = '82396C46A8811DD0D7493DB42CB22EC8938FB0CC952A6DF249D243FC0CE4CD6E'
function Job-ForgeBuild($out, $job) {
    if (-not $job.keep_game) { Get-Process -Name halo5forge,ForgeLauncher,ForgeStandalone,h5sololauncher.Worker -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 2 }
    $log = Join-Path $out 'build.log'
    $p = Start-Process powershell -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $ForgeShimDir 'build.ps1') -NoNewWindow -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $out 'build-err.log')
    $null = $p.Handle   # caches the handle so ExitCode is available
    if (-not $p.WaitForExit(900000)) { $p.Kill(); Fail 'forge build timed out' }   # not -Wait: it also waits for mspdbsrv
    Select-String -Path $log,(Join-Path $out 'build-err.log') -Pattern 'error|failed' -ErrorAction SilentlyContinue | Select-Object -First 80 | ForEach-Object { $_.Line } | Set-Content (Join-Path $out 'errors.txt')
    return $p.ExitCode
}
if (-not ('WinCap' -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System; using System.Drawing; using System.Runtime.InteropServices;
public static class WinCap {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  struct RECT { public int L, T, R, B; }
  public static IntPtr Find(uint pid) { IntPtr best = IntPtr.Zero; int area = 0;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); RECT r;
      if (p == pid && IsWindowVisible(h) && GetClientRect(h, out r) && (r.R - r.L) * (r.B - r.T) > area) { area = (r.R - r.L) * (r.B - r.T); best = h; } return true; }, IntPtr.Zero);
    return best; }
  public static bool Save(uint pid, string path) { IntPtr h = Find(pid); if (h == IntPtr.Zero) return false; RECT r; GetClientRect(h, out r);
    int w = Math.Max(1, r.R - r.L), ht = Math.Max(1, r.B - r.T);
    using (var bmp = new Bitmap(w, ht)) { using (var g = Graphics.FromImage(bmp)) { IntPtr dc = g.GetHdc(); PrintWindow(h, dc, 3); g.ReleaseHdc(dc); }
      bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png); } return true; }
}
"@
}
function Take-Shot($path) {
    try {
        $g = Get-Process -Name halo5forge -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $g -or -not [WinCap]::Save([uint32]$g.Id, $path)) { Add-Content ($path + '.err.txt') 'no game window' }
    } catch { Add-Content ($path + '.err.txt') $_.Exception.Message }
}
function Job-Forge($out, $job) {
    $ForgeDir = if ([string]$job.root -eq 'ForgeCampaign') { Join-Path $Dev 'ForgeCampaign' } else { $script:ForgeDir }
    $exe = Join-Path $ForgeShimDir 'bin\ForgeLauncher.exe'; $shim = Join-Path $ForgeShimDir 'bin\ForgeDesktopShim.dll'
    foreach ($f in $exe,$shim,(Join-Path $ForgeDir 'halo5forge.exe')) { if (-not (Test-Path -LiteralPath $f)) { Fail "missing $f" } }
    if ((Get-FileHash (Join-Path $ForgeDir 'halo5forge.exe')).Hash -ne $ForgeHash) { Fail 'halo5forge.exe hash mismatch; refusing to run' }
    Get-Process -Name halo5forge,ForgeLauncher,ForgeStandalone -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    # Real account: the Halo Waypoint token (prod.xsts.halowaypoint.com) lives only ~4 h. An expired one makes
    # spartan-token fail (401) -> "error authorizing user", no lobby, no campaign start. Refresh it first when it has
    # less than 90 minutes left (xbl-probe uses the saved refresh token; no sign-in needed).
    if ($job.env -and $job.env.FORGE_REAL_TOKENS) {
        $tok = Join-Path $Dev 'forge-profile\xbl\shim-tokens.txt'; $left = -1
        if (Test-Path $tok) { $line = Get-Content $tok | Where-Object { $_ -like 'https://prod.xsts.halowaypoint.com/*' } | Select-Object -First 1
            if ($line) { try { $left = ([datetime]::Parse(($line -split "`t")[1]).ToUniversalTime() - [datetime]::UtcNow).TotalMinutes } catch { } } }
        if ($left -lt 90) {
            $xo = Join-Path $out 'xbl'; New-Item -ItemType Directory -Force $xo | Out-Null
            $p0 = Start-Process powershell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $Dev 'tools\xbl-probe.ps1'),'-Out',$xo,'-Profile',(Join-Path $Dev 'forge-profile')) -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $xo 'stdout.log') -RedirectStandardError (Join-Path $xo 'stderr.log')
            $null = $p0.Handle; if (-not $p0.WaitForExit(120000)) { $p0.Kill() }
            Add-Content (Join-Path $out 'tokens.txt') ("halowaypoint token had {0:N0} min left; refreshed (exit {1})" -f $left, $p0.ExitCode)
        }
    }
    $prof = Join-Path $Dev 'forge-profile'; New-Item -ItemType Directory -Force (Join-Path $prof 'local'),(Join-Path $prof 'temp') | Out-Null
    $ls = Join-Path $prof 'local\ForgeStandalone\LocalState'
    if (-not $job.keep_state -and (Test-Path $ls)) { Move-Item $ls (Join-Path $out 'prev-LocalState') }   # saved prefs block the next boot's sign-in
    $shots = Join-Path $out 'shots'; New-Item -ItemType Directory -Force $shots | Out-Null
    $secs = [int]$job.seconds; if ($secs -lt 20 -or $secs -gt 3600) { $secs = 150 }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe; $psi.Arguments = "`"$ForgeDir`" `"$shim`" --log `"$(Join-Path $out 'game-debug.log')`" --seconds $secs"; $psi.WorkingDirectory = $ForgeDir
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.CreateNoWindow = $true
    $e = $psi.EnvironmentVariables
    $e['LOCALAPPDATA'] = Join-Path $prof 'local'; $e['TEMP'] = Join-Path $prof 'temp'; $e['TMP'] = $e['TEMP']
    if ($job.env) { foreach ($p in $job.env.PSObject.Properties) { if ($p.Name -notmatch '^FORGE_[A-Z0-9_]{1,40}$') { Fail "env $($p.Name) not allowed" }; $v=[string]$p.Value; if ($v -notmatch '^[A-Za-z0-9_.-]{0,240}$') { Fail 'bad env value' }; $e[$p.Name]=$v } }
    Add-Type -AssemblyName System.Drawing,System.Windows.Forms
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $er = $p.StandardError.ReadToEndAsync()
    $interval = [int]$job.shot_every; if ($interval -lt 5 -or $interval -gt 120) { $interval = 20 }
    $start = Get-Date; $n = 0
    while (-not $p.WaitForExit($interval * 1000)) {
        $n++; Take-Shot (Join-Path $shots ('shot-{0:D3}-{1}s.png' -f $n, [int]((Get-Date)-$start).TotalSeconds))
        if (((Get-Date)-$start).TotalSeconds -gt ($secs + 60)) { break }
        if ($job.peek) { $pk = Join-Path $Dev 'tools\h5peek\bin\h5peek.exe'; if (Test-Path $pk) { Add-Content (Join-Path $out 'peek.log') ("--- {0}s" -f [int]((Get-Date)-$start).TotalSeconds); & $pk *>> (Join-Path $out 'peek.log') } }
    }
    if (-not $p.HasExited) { & taskkill /T /F /PID $p.Id | Out-Null }
    Get-Process -Name halo5forge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Set-Content (Join-Path $out 'launcher.log') ($o.Result + "`r`n" + $er.Result)
    return 0
}

# dotnet job: {"type":"dotnet","action":"info"|"install"|"build"} - local SDK in Dev\dotnet (no admin).
$DotnetDir = Join-Path $Dev 'dotnet'
function Job-Dotnet($out, $job) {
    $log = Join-Path $out 'dotnet.log'
    switch ([string]$job.action) {
        'info' {
            $lines = @("system: " + ((Get-Command dotnet -ErrorAction SilentlyContinue).Source))
            try { $lines += (& dotnet --list-sdks 2>&1) } catch { $lines += "system dotnet: none" }
            if (Test-Path (Join-Path $DotnetDir 'dotnet.exe')) { $lines += "local:"; $lines += (& (Join-Path $DotnetDir 'dotnet.exe') --list-sdks 2>&1) }
            Set-Content $log $lines; return 0 }
        'install' {
            $script = Join-Path $out 'dotnet-install.ps1'
            Invoke-WebRequest -UseBasicParsing 'https://dot.net/v1/dotnet-install.ps1' -OutFile $script
            & powershell -NoProfile -ExecutionPolicy Bypass -File $script -Channel 10.0 -InstallDir $DotnetDir *> $log
            & (Join-Path $DotnetDir 'dotnet.exe') --list-sdks *>> $log; return $LASTEXITCODE }
        'build' {
            $sln = Join-Path $Dev 'halo5onpc\h5sololauncher.slnx'
            $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'; $env:DOTNET_NOLOGO = '1'
            $p = Start-Process (Join-Path $DotnetDir 'dotnet.exe') -ArgumentList 'build',"`"$sln`"",'-c','Release' -NoNewWindow -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $out 'dotnet-err.log')
            $null = $p.Handle; if (-not $p.WaitForExit(1800000)) { $p.Kill(); Fail 'dotnet build timed out' }
            Select-String -Path $log -Pattern ' error ' | Select-Object -First 60 | ForEach-Object { $_.Line } | Set-Content (Join-Path $out 'errors.txt')
            return $p.ExitCode }
        'native' {
            $srcDir = (Join-Path $Dev 'halo5onpc\src\h5sololauncher.ForgeReader') -replace '\\','/'
            $bld = (Join-Path $Dev 'halo5onpc\artifacts\forge-reader-build') -replace '\\','/'
            $cmd = "`"$CMake`" -S `"$srcDir`" -B `"$bld`" -G `"Ninja Multi-Config`" -DCMAKE_MAKE_PROGRAM=`"$($Ninja -replace '\\','/')`" -DBUILD_TESTING=OFF -DCMAKE_CXX_STANDARD=20 && `"$CMake`" --build `"$bld`" --config Release --parallel"
            $rc = Run-Cmd 'native' $cmd $log 3600
            Select-String -Path $log -Pattern 'error|FAILED' | Select-Object -First 60 | ForEach-Object { $_.Line } | Set-Content (Join-Path $out 'errors.txt')
            return $rc }
        default { Fail "unknown dotnet action" }
    }
}

# xblprobe job: {"type":"xblprobe","client_id":"<azure app id or empty for live mode>"} - real-account token probe
function Job-XblProbe($out, $job) {
    $id = [string]$job.client_id
    if ($id -and $id -notmatch '^[0-9a-fA-F-]{16,36}$') { Fail 'bad client_id' }
    $script = Join-Path $Dev 'tools\xbl-probe.ps1'
    $args2 = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$script,'-Out',$out,'-Profile',(Join-Path $Dev 'forge-profile'))
    if ($id) { $args2 += @('-ClientId',$id) }
    $p = Start-Process powershell -ArgumentList $args2 -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $out 'stdout.log') -RedirectStandardError (Join-Path $out 'stderr.log')
    $null = $p.Handle
    if (-not $p.WaitForExit(1200000)) { $p.Kill(); Fail 'probe timed out' }
    return $p.ExitCode
}

# register job: {"type":"register","action":"prepare"|"register"|"status"|"unregister"}
# Builds Dev\ForgeRegistered as a hard-linked copy of Dev\Forge (no extra disk space, signature files left
# out) and registers it as a Developer Mode package so Windows treats it as Halo 5: Forge.
function Job-Register($out, $job) {
    $src = Join-Path $Dev 'Forge'; $dst = Join-Path $Dev 'ForgeRegistered'; $log = Join-Path $out 'register.log'
    switch ([string]$job.action) {
        'prepare' {
            $skip = @('AppxSignature.p7x','AppxBlockMap.xml','CodeIntegrity.cat')
            $n = 0
            Get-ChildItem -LiteralPath $src -Recurse -Force | ForEach-Object {
                $rel = $_.FullName.Substring($src.Length).TrimStart('\'); $target = Join-Path $dst $rel
                if ($_.PSIsContainer) { New-Item -ItemType Directory -Force $target | Out-Null }
                elseif ($skip -notcontains $_.Name -and -not (Test-Path -LiteralPath $target)) { New-Item -ItemType HardLink -Path $target -Target $_.FullName | Out-Null; $n++ }
            }
            Add-Content $log "linked $n files into $dst"
        }
        'register' {
            $m = Join-Path $dst 'AppxManifest.xml'; if (-not (Test-Path $m)) { Fail 'run prepare first' }
            try { Add-AppxPackage -Register $m -ErrorAction Stop; Add-Content $log 'registered' }
            catch { Add-Content $log ("register failed: " + $_.Exception.Message); Fail 'register failed (see register.log)' }
        }
        'unregister' { Get-AppxPackage Microsoft.Halo5Forge | Remove-AppxPackage; Add-Content $log 'removed' }
        default { }
    }
    $dev = try { (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' -ErrorAction Stop).AllowDevelopmentWithoutDevLicense } catch { 'unknown' }
    Add-Content $log "developer mode: $dev"
    @(Get-AppxPackage Microsoft.Halo5Forge) + @(Get-AppxPackage Microsoft.VCLibs.140.00) | Select-Object Name, Version, Architecture, PackageFamilyName, InstallLocation, IsDevelopmentMode | Format-List | Out-String | Add-Content $log
    return 0
}

# campaign job: {"type":"campaign","action":"assemble"} - builds Dev\ForgeCampaign: hard links of Dev\Forge plus the
# converted campaign files at their native paths (tools\campaign\assemble-plan.tsv: relative path <TAB> source file).
function Job-Campaign($out, $job) {
    $src = Join-Path $Dev 'Forge'; $dst = Join-Path $Dev 'ForgeCampaign'; $log = Join-Path $out 'campaign.log'
    if ([string]$job.action -ne 'assemble') { Fail 'unknown campaign action' }
    $n = 0
    Get-ChildItem -LiteralPath $src -Recurse -Force | ForEach-Object {
        $rel = $_.FullName.Substring($src.Length).TrimStart('\'); if ($rel -like 'campaign*') { return }
        $target = Join-Path $dst $rel
        if ($_.PSIsContainer) { New-Item -ItemType Directory -Force $target | Out-Null }
        elseif (-not (Test-Path -LiteralPath $target)) { New-Item -ItemType HardLink -Path $target -Target $_.FullName | Out-Null; $n++ }
    }
    Add-Content $log "base: linked $n Forge files"
    $added = 0; $replaced = 0
    foreach ($line in Get-Content (Join-Path $Dev 'tools\campaign\assemble-plan.tsv')) {
        if (-not $line) { continue }
        $rel, $from = $line -split "`t"; $target = Join-Path $dst $rel
        if (-not (Test-Path -LiteralPath $from)) { Fail "missing source $from" }
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force; $replaced++ }
        New-Item -ItemType HardLink -Path $target -Target $from | Out-Null; $added++
    }
    Add-Content $log "campaign: linked $added files ($replaced replaced Forge files)"
    return 0
}

# bake job: {"type":"bake"} - builds tools\h5bake and writes the campaign menu layer + mapinfo lists into Dev\ForgeCampaign
function Job-Bake($out, $job) {
    $proj = Join-Path $Dev 'tools\h5bake\h5bake.csproj'; $log = Join-Path $out 'bake.log'
    foreach ($v in 'H5BAKE_AUTOSOLO','H5BAKE_SOLO_GUARD','H5BAKE_OFFLINE_EDIT','H5BAKE_NO_MENU_IN_MISSIONS') { Remove-Item "env:$v" -ErrorAction SilentlyContinue }
    if ($job.env) { foreach ($pr in $job.env.PSObject.Properties) { if ($pr.Name -like 'H5BAKE_*') { Set-Item "env:$($pr.Name)" ([string]$pr.Value) } } }
    $env:DOTNET_ROOT = $DotnetDir
    & (Join-Path $DotnetDir 'dotnet.exe') build $proj -c Release -o (Join-Path $Dev 'tools\h5bake\bin') *> (Join-Path $out 'build.log')
    if ($LASTEXITCODE) { Fail 'h5bake build failed (build.log)' }
    & (Join-Path $DotnetDir 'dotnet.exe') build (Join-Path $Dev 'tools\h5peek\h5peek.csproj') -c Release -o (Join-Path $Dev 'tools\h5peek\bin') *> (Join-Path $out 'build-peek.log')
    $art = Get-ChildItem (Join-Path $Dev 'h5cache\game\menu-artwork') -Recurse -Filter 'h5solo-menu.module' | Select-Object -First 1
    if (-not $art) { Fail 'menu artwork module missing' }
    $artTarget = Join-Path $Dev 'ForgeCampaign\deploy\pc\levels\ui\mainmenu\mainmenu-rtx-12720-1.module'
    if (Test-Path -LiteralPath $artTarget) { Remove-Item -LiteralPath $artTarget -Force }
    New-Item -ItemType HardLink -Path $artTarget -Target $art.FullName | Out-Null
    & (Join-Path $Dev 'tools\h5bake\bin\h5bake.exe') (Join-Path $Dev 'h5cache') (Join-Path $Dev 'Forge') (Join-Path $Dev 'ForgeCampaign') *> $log
    return $LASTEXITCODE
}

# player job: {"type":"player","root":"ForgeCampaign"|"Package","seconds":N,"shot_every":N} - starts <root>\Osiris.exe
# exactly as a player would (no arguments, no FORGE_* environment; the folder's h5campaign\settings.ini applies).
function Job-Player($out, $job) {
    $root = switch ([string]$job.root) { 'Package' { Join-Path $Dev 'Package\Halo5Campaign' } default { Join-Path $Dev 'ForgeCampaign' } }
    $exe = Join-Path $root 'Osiris.exe'; if (-not (Test-Path -LiteralPath $exe)) { Fail "missing $exe" }
    Get-Process -Name halo5forge,ForgeLauncher,Halo5Campaign -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $shots = Join-Path $out 'shots'; New-Item -ItemType Directory -Force $shots | Out-Null
    $secs = [int]$job.seconds; if ($secs -lt 20 -or $secs -gt 3600) { $secs = 150 }
    $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = $exe; $psi.WorkingDirectory = $root; $psi.UseShellExecute = $false
    foreach ($k in @($psi.EnvironmentVariables.Keys)) { if ($k -like 'FORGE_*') { $psi.EnvironmentVariables.Remove($k) } }
    Add-Type -AssemblyName System.Drawing,System.Windows.Forms
    $p = [System.Diagnostics.Process]::Start($psi)
    $interval = [int]$job.shot_every; if ($interval -lt 5 -or $interval -gt 120) { $interval = 20 }
    $start = Get-Date; $n = 0
    while (-not $p.WaitForExit($interval * 1000)) {
        $n++; Take-Shot (Join-Path $shots ('shot-{0:D3}-{1}s.png' -f $n, [int]((Get-Date)-$start).TotalSeconds))
        if (((Get-Date)-$start).TotalSeconds -gt $secs) { break }
    }
    Get-Process -Name halo5forge,Halo5Campaign,Osiris -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    foreach ($f in 'game.log','launcher.log') { $src = Join-Path $root "h5campaign\logs\$f"; if (Test-Path $src) { Copy-Item $src (Join-Path $out $f) } }
    return 0
}

# package job: {"type":"package"} - builds the public release bundle in Dev\Release:
#   OsirisSetup.exe (self-contained, single file)  setup\worker (halo5onpc worker, self-contained)
#   setup\bake (h5bake, self-contained)  setup\runtime (Osiris.exe, ForgeDesktopShim.dll, ForgeLauncher.exe, Memory_Release_UWP.dll)
#   setup\data (audio recipes, settings template, readme)  licenses\   and a zip of all of it.
$ReleaseVersion = '1.0'   # Osiris release number (setup zip name)
function Job-Package($out, $job) {
    $rc = Job-ForgeBuild $out $job; if ($rc) { Fail 'shim/launcher build failed (build.log)' }
    $rel = Join-Path $Dev 'Release'; $stage = Join-Path $rel 'Osiris-Setup'
    if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    New-Item -ItemType Directory -Force (Join-Path $stage 'setup\runtime'),(Join-Path $stage 'setup\data'),(Join-Path $stage 'licenses') | Out-Null
    $env:DOTNET_ROOT = $DotnetDir; $dn = Join-Path $DotnetDir 'dotnet.exe'
    $pubs = @(
        @{ name='worker'; proj=(Join-Path $Dev 'halo5onpc\src\h5sololauncher.Worker\h5sololauncher.Worker.csproj'); dest=(Join-Path $stage 'setup\worker'); single=$false },
        @{ name='bake';   proj=(Join-Path $Dev 'tools\h5bake\h5bake.csproj'); dest=(Join-Path $stage 'setup\bake'); single=$false },
        @{ name='setup';  proj=(Join-Path $Dev 'tools\h5setup\h5setup.csproj'); dest=$stage; single=$true })
    foreach ($p in $pubs) {
        $args2 = @('publish', $p.proj, '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true', '-o', $p.dest, '-p:DebugType=none')
        if ($p.single) { $args2 += @('-p:PublishSingleFile=true', '-p:IncludeNativeLibrariesForSelfExtract=true') }
        & $dn @args2 *> (Join-Path $out "publish-$($p.name).log")
        if ($LASTEXITCODE) { Fail "publish $($p.name) failed (publish-$($p.name).log)" }
    }
    foreach ($f in 'Osiris.exe','ForgeDesktopShim.dll','ForgeLauncher.exe','Memory_Release_UWP.dll') { Copy-Item (Join-Path $ForgeShimDir "bin\$f") (Join-Path $stage 'setup\runtime') }
    Copy-Item (Join-Path $Dev 'tools\h5setup\data\*') (Join-Path $stage 'setup\data')
    Copy-Item (Join-Path $Dev 'tools\audio118\extra-recipes.json') (Join-Path $stage 'setup\data')
    Copy-Item 'C:\Program Files (x86)\Windows Kits\10\Redist\D3D\x64\d3dcompiler_47.dll' (Join-Path $stage 'setup\data')   # SDK redistributable
    Copy-Item (Join-Path $Dev 'tools\h5setup\data\README.txt') (Join-Path $stage 'README.txt')
    Copy-Item (Join-Path $ForgeShimDir 'LICENSE-WinDurango') (Join-Path $stage 'licenses\WinDurango.txt')
    Copy-Item (Join-Path $ForgeShimDir 'third_party\minhook\LICENSE.txt') (Join-Path $stage 'licenses\MinHook.txt') -ErrorAction SilentlyContinue
    $zip = Join-Path $rel "Osiris-Setup-$ReleaseVersion.zip"; if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
    Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum | ForEach-Object { Add-Content (Join-Path $out 'package.txt') ("{0} files, {1:N0} bytes; zip {2:N0} bytes" -f $_.Count, $_.Sum, (Get-Item $zip).Length) }
    return 0
}

# setup job: {"type":"setup","timeout":sec} - runs the release bundle's OsirisSetup.exe unattended on the dev copies
# (Dev\Forge, Dev\Guardians) into Dev\Package (game folder Dev\Package\Halo5Campaign), logging to setup.log.
function Job-Setup($out, $job) {
    $exe = Join-Path $Dev 'Release\Osiris-Setup\OsirisSetup.exe'; if (-not (Test-Path $exe)) { Fail 'release bundle not built (package job)' }
    Get-Process -Name halo5forge,ForgeLauncher,Halo5Campaign,h5sololauncher.Worker -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $log = Join-Path $out 'setup.log'; $pkg = Join-Path $Dev 'Package'; New-Item -ItemType Directory -Force $pkg | Out-Null
    $a = '--forge "' + (Join-Path $Dev 'Forge') + '" --guardians "' + (Join-Path $Dev 'Guardians') + '" --out "' + $pkg + '" --yes'
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'cmd.exe'; $psi.Arguments = '/d /s /c ""' + $exe + '" ' + $a + ' > "' + $log + '" 2>&1 < nul"'
    $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true; $psi.WorkingDirectory = Split-Path $exe
    foreach ($k in @($psi.EnvironmentVariables.Keys)) { if ($k -like 'FORGE_*' -or $k -like 'H5_*' -or $k -eq 'DOTNET_ROOT') { $psi.EnvironmentVariables.Remove($k) } }
    $p = [System.Diagnostics.Process]::Start($psi); $null = $p.Handle
    $timeout = [int]$job.timeout; if ($timeout -lt 60 -or $timeout -gt 43200) { $timeout = 21600 }
    $shots = Join-Path $out 'shots'; New-Item -ItemType Directory -Force $shots | Out-Null
    Add-Type -AssemblyName System.Drawing,System.Windows.Forms
    $start = Get-Date; $n = 0
    while (-not $p.WaitForExit(60000)) {
        if (Get-Process -Name halo5forge -ErrorAction SilentlyContinue) { $n++; Take-Shot (Join-Path $shots ('shot-{0:D3}-{1}s.png' -f $n, [int]((Get-Date)-$start).TotalSeconds)) }
        if (((Get-Date)-$start).TotalSeconds -ge $timeout) { & taskkill /T /F /PID $p.Id | Out-Null; Add-Content $log 'TIMEOUT'; return -1 }
    }
    return $p.ExitCode
}

# bakedisplay job: {"type":"bakedisplay"} - builds tools\h5bake and runs "h5bake --display Dev\Package\Halo5Campaign"
# (adds the display options settings script to an already-baked game folder), then copies the current shim/launcher in.
# bakedump job: {"type":"bakedump","module":"deploy\\any\\levels\\globals-rtx-1.module","filter":"main_menu"} - payloads of a
# Package\Halo5Campaign module into results\<id>\dump (read only).
function Job-BakeDump($out, $job) {
    $env:DOTNET_ROOT = $DotnetDir
    & (Join-Path $DotnetDir 'dotnet.exe') build (Join-Path $Dev 'tools\h5bake\h5bake.csproj') -c Release -o (Join-Path $Dev 'tools\h5bake\bin') *> (Join-Path $out 'build.log')
    if ($LASTEXITCODE) { Fail 'h5bake build failed (build.log)' }
    $rel = [string]$job.module; if ($rel -match '\.\.') { Fail 'bad module path' }
    $module = Join-Path (Join-Path $Dev 'Package\Halo5Campaign') $rel
    $a = @('--dump', $module, (Join-Path $out 'dump')); if ($job.filter) { $a += [string]$job.filter }
    & (Join-Path $Dev 'tools\h5bake\bin\h5bake.exe') @a *> (Join-Path $out 'bake.log')
    return $LASTEXITCODE
}

function Job-BakeDisplay($out, $job) {
    Remove-Item Env:H5BAKE_HUB_REFOCUS -ErrorAction SilentlyContinue
    if ($null -ne $job.hub_refocus) { $env:H5BAKE_HUB_REFOCUS = [string]$job.hub_refocus }
    Remove-Item Env:H5BAKE_HUB_OPEN -ErrorAction SilentlyContinue
    if ($null -ne $job.hub_open) { $env:H5BAKE_HUB_OPEN = [string]$job.hub_open }
    Get-Process -Name halo5forge,Halo5Campaign,Osiris -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 2
    $env:DOTNET_ROOT = $DotnetDir
    & (Join-Path $DotnetDir 'dotnet.exe') build (Join-Path $Dev 'tools\h5bake\h5bake.csproj') -c Release -o (Join-Path $Dev 'tools\h5bake\bin') *> (Join-Path $out 'build.log')
    if ($LASTEXITCODE) { Fail 'h5bake build failed (build.log)' }
    $game = Join-Path $Dev 'Package\Halo5Campaign'
    & (Join-Path $Dev 'tools\h5bake\bin\h5bake.exe') --display $game *> (Join-Path $out 'bake.log')
    if ($LASTEXITCODE) { return $LASTEXITCODE }
    Copy-Item (Join-Path $ForgeShimDir 'bin\Osiris.exe') $game -Force
    Copy-Item (Join-Path $ForgeShimDir 'bin\ForgeDesktopShim.dll') (Join-Path $game 'h5campaign') -Force
    # In-process loader: Forge's memory_release_uwp.dll becomes Memory_Release_UWP_343.dll (a rename never writes
    # through a hard link), the loader takes its name.
    $mem = Join-Path $game 'memory_release_uwp.dll'; $m343 = Join-Path $game 'Memory_Release_UWP_343.dll'
    if (-not (Test-Path -LiteralPath $m343)) { Rename-Item -LiteralPath $mem -NewName 'Memory_Release_UWP_343.dll' }
    elseif (Test-Path -LiteralPath $mem) { Remove-Item -LiteralPath $mem -Force }
    if ((Get-Item -LiteralPath $m343).Length -ne 15872) { Fail 'Memory_Release_UWP_343.dll is not Forge''s memory_release_uwp.dll' }
    Copy-Item (Join-Path $ForgeShimDir 'bin\Memory_Release_UWP.dll') $mem
    # Microsoft's HLSL compiler from this PC (Wine's can't build all of Forge's runtime shaders); used on the PS5.
    # The Windows SDK's redistributable copy only needs kernel32-level imports (the System32 one needs msvcp_win
    # exports Wine doesn't have); System32's is the fallback.
    $redist = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Redist' -Recurse -Filter d3dcompiler_47.dll -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1
    $compiler = if ($redist) { $redist.FullName } else { Join-Path $env:SystemRoot 'System32\D3DCompiler_47.dll' }
    Set-Content (Join-Path $out 'compiler.txt') $compiler
    Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Redist' -Recurse -Filter d3dcompiler_47.dll -ErrorAction SilentlyContinue | ForEach-Object { Add-Content (Join-Path $out 'compiler.txt') ("  " + $_.FullName + " " + $_.Length) }
    Copy-Item $compiler (Join-Path $game 'd3dcompiler_47.dll') -Force
    return 0
}

# h5worker job: {"type":"h5worker","action":"launch-check"|"launch"|"prepare"|"play","timeout":sec}
function Job-H5Worker($out, $job) {
    if ($job.attach) { Get-Process -Name h5sololauncher.Worker -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue } else {
    Get-Process -Name halo5forge,ForgeLauncher,ForgeStandalone,h5sololauncher.Worker -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 2 }
    $exe = Get-ChildItem (Join-Path $Dev 'halo5onpc\src\h5sololauncher.Worker\bin\Release') -Recurse -Filter 'h5sololauncher.Worker.exe' | Select-Object -First 1
    if (-not $exe) { Fail 'worker not built' }
    $pkg = 'Microsoft.Halo5Forge_1.194.6192.2_x64__8wekyb3d8bbwe'
    $src = Join-Path $Dev 'Guardians\RootFolder'; $cache = Join-Path $Dev 'h5cache'; $forge = Join-Path $Dev 'Forge'
    New-Item -ItemType Directory -Force $cache | Out-Null
    $argList = switch ([string]$job.action) {
        'launch-check' { @('--launch-check', $forge, $pkg) }
        'launch'       { @('--launch', $forge, $pkg) }
        'prepare'      { @('--prepare', $src, $cache, $forge, $pkg) }
        'play'         { @('--play', $src, $cache, $forge, $pkg) }
        default        { Fail 'unknown h5worker action' }
    }
    # Stream output to worker.log as it happens (cmd redirection), so stalls are visible while running.
    $log = Join-Path $out 'worker.log'
    $quoted = (($argList | ForEach-Object { '"' + $_ + '"' }) -join ' ')
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'cmd.exe'; $psi.Arguments = '/d /s /c ""' + $exe.FullName + '" ' + $quoted + ' > "' + $log + '" 2>&1"'
    $psi.WorkingDirectory = $exe.DirectoryName; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $e = $psi.EnvironmentVariables
    $e['DOTNET_ROOT'] = $DotnetDir
    $e['H5_STANDALONE_FORGE_ROOT'] = $forge
    $e['H5_STANDALONE_LAUNCHER'] = Join-Path $ForgeShimDir 'bin\ForgeLauncher.exe'
    $e['H5_STANDALONE_SHIM'] = Join-Path $ForgeShimDir 'bin\ForgeDesktopShim.dll'
    $e['H5_STANDALONE_PROFILE'] = Join-Path $Dev 'forge-profile'
    $e['FORGE_ENGAGE'] = '1'; $e['FORGE_WAM_SHAPE'] = 'full'; $e['FORGE_AUTOSTART'] = '1'
    if ($job.log) { $e['H5_STANDALONE_LOG'] = Join-Path $out 'game-debug.log' }
    if ($job.record) { $e['H5_RECORD_CALLS'] = Join-Path $out 'calls' }   # debug capture slows the game; opt in
    if ($job.env) { foreach ($pr in $job.env.PSObject.Properties) { if ($pr.Name -like 'FORGE_*' -or $pr.Name -like 'H5_*') { $e[$pr.Name] = [string]$pr.Value } } }
    $p = [System.Diagnostics.Process]::Start($psi); $null = $p.Handle
    $timeout = [int]$job.timeout; if ($timeout -lt 30 -or $timeout -gt 43200) { $timeout = 3600 }
    # Screenshot the game window every 60 s while the worker runs (shots\), so unattended runs can be inspected.
    $shots = Join-Path $out 'shots'; New-Item -ItemType Directory -Force $shots | Out-Null
    $start = Get-Date; $n = 0; $timedOut = $false
    while (-not $p.WaitForExit(60000)) {
        $n++; Take-Shot (Join-Path $shots ('shot-{0:D3}-{1}s.png' -f $n, [int]((Get-Date)-$start).TotalSeconds))
        if (((Get-Date)-$start).TotalSeconds -ge $timeout) { $timedOut = $true; break }
    }
    if ($timedOut) { & taskkill /T /F /PID $p.Id | Out-Null; Add-Content $log 'TIMEOUT' }
    # Leave Forge running after a play/launch job so the session can be used or inspected by hand.
    if ([string]$job.action -in @('play','launch') -and -not $timedOut) { return $p.ExitCode }
    Get-Process -Name halo5forge,ForgeLauncher,ForgeStandalone,h5sololauncher.Worker -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    if ($timedOut) { return -1 }
    return $p.ExitCode
}

Write-Host "h5-runner watching $Queue  (Ctrl+C to stop)"
Heartbeat 'idle' $null
$lastBeat = Get-Date
while ($true) {
    if ((Get-Item -LiteralPath $PSCommandPath).LastWriteTimeUtc -ne $ScriptStamp) {
        Write-Host "runner script changed on disk - reloading"
        & powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath
        exit
    }
    $next = Get-ChildItem -LiteralPath $Queue -Filter '*.json' -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -First 1
    if (-not $next) {
        if (((Get-Date) - $lastBeat).TotalSeconds -ge 15) { Heartbeat 'idle' $null; $lastBeat = Get-Date }
        Start-Sleep -Seconds 2; continue
    }
    $status = [ordered]@{ id=$null; type=$null; state='running'; started=(Get-Date).ToString('o'); finished=$null; exit=$null; error=$null }
    $out = $null
    try {
        $job = Get-Content -LiteralPath $next.FullName -Raw | ConvertFrom-Json
        if ([string]$job.id -notmatch '^[a-z0-9-]{1,48}$') { Fail 'bad job id' }
        $status.id = $job.id; $status.type = $job.type
        $out = Join-Path $Results $job.id
        if (Test-Path -LiteralPath $out) { Fail "results\$($job.id) already exists" }
        New-Item -ItemType Directory -Path $out | Out-Null
        Move-Item -LiteralPath $next.FullName -Destination (Join-Path $out 'job.json')
        Write-Json (Join-Path $out 'status.json') $status
        Heartbeat 'busy' $job.id
        Write-Host "[$(Get-Date -Format T)] $($job.id): $($job.type)"
        foreach ($r in @($job.requires)) {
            if (-not $r) { continue }
            if ([string]$r -notmatch '^[a-z0-9-]{1,48}$') { Fail 'bad requires id' }
            $rs = Join-Path $Results "$r\status.json"
            if (-not (Test-Path $rs) -or ((Get-Content $rs -Raw | ConvertFrom-Json).state -ne 'ok')) { Fail "skipped: required job $r did not succeed" }
        }
        $rc = switch ([string]$job.type) {
            'selftest'  { Job-Selftest $out }
            'configure' { Job-Configure $out }
            'build'     { Job-Build $out $job }
            'stage'     { Job-Stage $out $job }
            'run'       { Job-Run $out $job }
            'stop'      { Stop-Game; 0 }
            'forge'     { Job-Forge $out $job }
            'forgebuild' { Job-ForgeBuild $out $job }
            'dotnet'    { Job-Dotnet $out $job }
            'h5worker'  { Job-H5Worker $out $job }
            'xblprobe'  { Job-XblProbe $out $job }
            'register'  { Job-Register $out $job }
            'campaign'  { Job-Campaign $out $job }
            'bake'      { Job-Bake $out $job }
            'player'    { Job-Player $out $job }
            'package'   { Job-Package $out $job }
            'setup'     { Job-Setup $out $job }
            'bakedisplay' { Job-BakeDisplay $out $job }
            'bakedump'  { Job-BakeDump $out $job }
            default     { Fail "unknown job type '$($job.type)'" }
        }
        $status.exit = $rc
        $status.state = if ($rc -eq 0) { 'ok' } else { 'failed' }
    } catch {
        $status.state = 'error'; $status.error = $_.Exception.Message
        if (-not $out) {
            $out = Join-Path $Results ('bad-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
            New-Item -ItemType Directory -Force -Path $out | Out-Null
            if (Test-Path -LiteralPath $next.FullName) { Move-Item -LiteralPath $next.FullName -Destination (Join-Path $out 'job.json') -Force }
        }
    }
    $status.finished = (Get-Date).ToString('o')
    Write-Json (Join-Path $out 'status.json') $status
    Write-Host "    -> $($status.state) $($status.error)"
    Heartbeat 'idle' $null; $lastBeat = Get-Date
}
