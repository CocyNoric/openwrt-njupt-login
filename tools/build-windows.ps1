param([string]$CurlSource = '', [string]$Python = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$envLines = & cmd.exe /d /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
# A parent process can contain both PATH and Path. Retain the developer PATH, then set it last.
$developerPath = ($envLines | Where-Object { $_ -cmatch '^PATH=' } | Select-Object -First 1).Substring(5)
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$' -and $matches[1] -ine 'Path') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$env:Path = $developerPath
$env:VSLANG = '1033'
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
function Run-CMake { & $cmake @args; if ($LASTEXITCODE -ne 0) { throw "CMake failed ($LASTEXITCODE)" } }
if ($CurlSource) {
    Run-CMake -S $CurlSource -B "$root/.deps/curl-build-native" -G Ninja `
        -DBUILD_CURL_EXE=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_TESTING=OFF `
        -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF `
        -DCURL_ZSTD=OFF -DUSE_LIBIDN2=OFF -DCURL_USE_LIBPSL=OFF -DUSE_NGHTTP2=OFF `
        -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON -DCMAKE_BUILD_TYPE=Release
    Run-CMake --build "$root/.deps/curl-build-native" --parallel 8
    Run-CMake --install "$root/.deps/curl-build-native" --prefix "$root/.deps/curl-install"
}
$options = @()
if ($Python) { $options += "-DPython3_EXECUTABLE=$Python" }
Run-CMake -S $root -B "$root/build" -G Ninja -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_PREFIX_PATH=$root/.deps/curl-install" @options
Run-CMake --build "$root/build" --parallel 8
& $ctest --test-dir "$root/build" --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
