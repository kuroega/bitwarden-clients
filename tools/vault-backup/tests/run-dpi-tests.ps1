param([string]$Gcc = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
. (Join-Path $root 'config.ps1')
$Gcc = Resolve-VaultCompiler $Gcc
Push-Location $root
try {
    & (Join-Path (Split-Path $Gcc) 'windres.exe') 'src/app.rc' '-O' 'coff' '-o' 'bin/app-res.o'
    if ($LASTEXITCODE -ne 0) { throw 'DPI test resource compilation failed.' }
    & $Gcc '-specs=src/no-default-manifest.specs' '-std=c11' '-O2' '-Wall' '-Wextra' '-Wno-misleading-indentation' '-D_WIN32_WINNT=0x0A00' '-municode' '-static' '-finput-charset=UTF-8' '-fexec-charset=UTF-8' 'tests/dpi_theme_test.c' 'src/backup.c' 'src/crypto.c' 'src/recover.c' 'bin/app-res.o' '-lbcrypt' '-lcomctl32' '-lshell32' '-lole32' '-ladvapi32' '-lcomdlg32' '-luuid' '-luxtheme' '-lgdi32' '-o' 'bin/dpi-theme-test.exe'
    if ($LASTEXITCODE -ne 0) { throw 'DPI test compilation failed.' }
    & '.\bin\dpi-theme-test.exe'
    if ($LASTEXITCODE -ne 0) { throw 'DPI layout test failed.' }
} finally { Pop-Location }
