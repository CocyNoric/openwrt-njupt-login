param([string]$Python = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$root = $root.Replace('\', '/')
$deps = "$root/.deps"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -property installationPath
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$env:Path = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;" + $env:Path
$env:ZIG_GLOBAL_CACHE_DIR = "$deps/zig-cache"
$env:ZIG_LOCAL_CACHE_DIR = "$deps/zig-local"
$zig = "$deps/zig-x86_64-windows-0.15.2/zig.exe"
if (-not (Test-Path -LiteralPath $zig)) { throw 'Extract the verified official Zig 0.15.2 Windows archive into .deps first' }
$tlsSource = "$deps/mbedtls-3.6.7"
$curlSource = "$deps/curl-8.22.0"
foreach ($source in @($tlsSource, $curlSource)) {
    if (-not (Test-Path -LiteralPath "$source/CMakeLists.txt")) { throw "Missing source: $source" }
}
$common = @('-G', 'Ninja', "-DCMAKE_TOOLCHAIN_FILE=$root/cmake/zig-aarch64.cmake",
    "-DZIG_EXE=$zig", '-DCMAKE_BUILD_TYPE=MinSizeRel', '-DCMAKE_INSTALL_LIBDIR=lib')
function Run-CMake { & $cmake @args; if ($LASTEXITCODE -ne 0) { throw "CMake failed ($LASTEXITCODE)" } }
$pyOption = @()
if ($Python) { $pyOption += "-DPython3_EXECUTABLE=$Python" }
Run-CMake -S $tlsSource -B "$deps/mbedtls-arm-build" @common @pyOption `
    -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DGEN_FILES=OFF `
    -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON
Run-CMake --build "$deps/mbedtls-arm-build" --parallel 4
Run-CMake --install "$deps/mbedtls-arm-build" --prefix "$deps/arm-libs"
Run-CMake -S $curlSource -B "$deps/curl-arm-build" @common @pyOption `
    "-DCMAKE_PREFIX_PATH=$deps/arm-libs" -DBUILD_CURL_EXE=OFF -DBUILD_SHARED_LIBS=OFF `
    -DBUILD_STATIC_LIBS=ON -DBUILD_TESTING=OFF -DCURL_USE_MBEDTLS=ON -DCURL_USE_OPENSSL=OFF `
    -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_LIBIDN2=OFF `
    -DCURL_USE_LIBPSL=OFF -DUSE_NGHTTP2=OFF -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON `
    -DCURL_DISABLE_FTP=ON -DCURL_DISABLE_FILE=ON -DCURL_DISABLE_DICT=ON -DCURL_DISABLE_TELNET=ON `
    -DCURL_DISABLE_TFTP=ON -DCURL_DISABLE_POP3=ON -DCURL_DISABLE_IMAP=ON -DCURL_DISABLE_SMTP=ON `
    -DCURL_DISABLE_RTSP=ON -DCURL_DISABLE_GOPHER=ON -DCURL_DISABLE_MQTT=ON `
    '-DCURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt' '-DCURL_CA_PATH=none'
if (-not (Select-String -LiteralPath "$deps/curl-arm-build/lib/curl_config.h" `
    -SimpleMatch '#define CURL_CA_BUNDLE "/etc/ssl/certs/ca-certificates.crt"' -Quiet)) {
    throw 'Incorrect libcurl CA bundle path; refusing to build a router binary with broken HTTPS verification'
}
Run-CMake --build "$deps/curl-arm-build" --parallel 4
Run-CMake --install "$deps/curl-arm-build" --prefix "$deps/arm-libs"
Run-CMake -S $root -B "$root/build-aarch64" @common `
    "-DCMAKE_PREFIX_PATH=$deps/arm-libs" -DBUILD_TESTING=OFF
Run-CMake --build "$root/build-aarch64" --parallel 4
