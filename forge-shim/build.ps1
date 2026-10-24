$ErrorActionPreference='Stop'
$base=$PSScriptRoot
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231'
$sdk='C:\Program Files (x86)\Windows Kits\10'
$version='10.0.26100.0'
$env:INCLUDE="$vc\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\shared;$sdk\Include\$version\um;$sdk\Include\$version\winrt;$sdk\Include\$version\cppwinrt"
$env:LIB="$vc\lib\x64;$sdk\Lib\$version\ucrt\x64;$sdk\Lib\$version\um\x64"
$compiler="$vc\bin\Hostx64\x64\cl.exe"
New-Item -ItemType Directory -Force "$base\bin" | Out-Null
Push-Location "$base\bin"
try {
 $shimSrc = Get-ChildItem "$base\src\*.cpp" | Where-Object { $_.Name -notin @('Launcher.cpp','ForgeLauncher.cpp','HostSmoke.cpp','Halo5Campaign.cpp','Bootstrap.cpp') } | ForEach-Object { $_.FullName }
 $shimSrc += @('buffer.c','hook.c','trampoline.c','hde\hde64.c') | ForEach-Object { "$base\third_party\minhook\src\$_" }
 & $compiler /nologo /std:c++20 /EHsc /MT /W4 /O2 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /LD $shimSrc /link /OUT:ForgeDesktopShim.dll windowsapp.lib user32.lib ole32.lib runtimeobject.lib dxgi.lib msxml6.lib
 if($LASTEXITCODE){throw 'Shim build failed'}
 & $compiler /nologo /std:c++20 /EHsc /MT /W4 /O2 /DUNICODE /D_UNICODE "$base\src\ForgeLauncher.cpp" /link /OUT:ForgeLauncher.exe
 if($LASTEXITCODE){throw 'Play launcher build failed'}
 & $compiler /nologo /std:c++20 /EHsc /MT /W4 /O2 /DUNICODE /D_UNICODE "$base\src\Halo5Campaign.cpp" /link /SUBSYSTEM:WINDOWS /OUT:Osiris.exe user32.lib
 if($LASTEXITCODE){throw 'Player launcher build failed'}
 & $compiler /nologo /std:c++20 /EHsc /MT /W4 /O2 /DUNICODE /D_UNICODE /LD "$base\src\Bootstrap.cpp" /link /OUT:Memory_Release_UWP.dll user32.lib
 if($LASTEXITCODE){throw 'In-process loader build failed'}
 & $compiler /nologo /std:c++20 /EHsc /MT /W4 /DUNICODE /D_UNICODE "$base\src\Launcher.cpp" /link /OUT:ForgeDebugLauncher.exe
 if($LASTEXITCODE){throw 'Debug launcher build failed'}
} finally {Pop-Location}
