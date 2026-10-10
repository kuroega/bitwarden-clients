param([string]$Gcc = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
. (Join-Path $root 'config.ps1')
$Gcc = Resolve-VaultCompiler $Gcc
Push-Location $root
try {
    & $Gcc '-specs=src/no-default-manifest.specs' '-std=c11' '-O2' '-Wall' '-Wextra' '-Wno-misleading-indentation' '-D_WIN32_WINNT=0x0A00' '-municode' '-static' '-finput-charset=UTF-8' '-fexec-charset=UTF-8' 'tests/recover_gui_test.c' 'src/backup.c' 'src/crypto.c' 'bin/app-res.o' '-lbcrypt' '-lcomctl32' '-lshell32' '-lole32' '-ladvapi32' '-lcomdlg32' '-luuid' '-luxtheme' '-lgdi32' '-o' 'bin/recover-gui-test.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Recovery test compilation failed.' }
    & '.\bin\recover-gui-test.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Recovery GUI test failed.' }
} finally { Pop-Location }
