param([switch]$Run,[string]$MsysRoot="C:\msys64",[string]$BuildDirectory="build/desktop")
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$env:PATH="$MsysRoot\mingw64\bin;$env:PATH"
$env:PKG_CONFIG_PATH="$MsysRoot\mingw64\lib\pkgconfig"
Push-Location $root
try {
    & "$MsysRoot\mingw64\bin\cmake.exe" -S . -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER="$MsysRoot/mingw64/bin/gcc.exe" -DCMAKE_CXX_COMPILER="$MsysRoot/mingw64/bin/g++.exe" "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
    if($LASTEXITCODE -ne 0){throw "Desktop configuration failed"}
    & "$MsysRoot\mingw64\bin\cmake.exe" --build $BuildDirectory -j 6
    if($LASTEXITCODE -ne 0){throw "Desktop build failed"}
    python tools/copy-runtime.py "$BuildDirectory/MirrorPSX.exe" "$MsysRoot/mingw64"
    if($LASTEXITCODE -ne 0){throw "Runtime bundling failed"}
    & "$MsysRoot\mingw64\bin\ctest.exe" --test-dir $BuildDirectory --output-on-failure
    if($LASTEXITCODE -ne 0){throw "Tests failed"}
    if($Run){$resolvedBuild=(Resolve-Path -LiteralPath $BuildDirectory).Path; Start-Process -FilePath "$resolvedBuild/MirrorPSX.exe" -WorkingDirectory $root -WindowStyle Hidden -RedirectStandardOutput "$resolvedBuild/stdout.log" -RedirectStandardError "$resolvedBuild/stderr.log"}
} finally {Pop-Location}
