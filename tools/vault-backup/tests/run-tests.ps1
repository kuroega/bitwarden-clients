param([string]$Gcc = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
. (Join-Path $root 'config.ps1')
$Gcc = Resolve-VaultCompiler $Gcc
& (Join-Path $root 'build.ps1') -Gcc $Gcc
& (Join-Path $PSScriptRoot 'run-dpi-tests.ps1') -Gcc $Gcc
& (Join-Path $PSScriptRoot 'run-recover-tests.ps1') -Gcc $Gcc
& (Join-Path $PSScriptRoot 'run-folder-tests.ps1') -Gcc $Gcc
Push-Location $root
try {
    & $Gcc '-std=c11' '-O2' '-Wall' '-Wextra' '-Wno-misleading-indentation' '-D_WIN32_WINNT=0x0601' '-municode' '-static' '-finput-charset=UTF-8' 'tests/engine_test.c' 'src/backup.c' 'src/crypto.c' '-lbcrypt' '-ladvapi32' '-lshell32' '-o' 'bin/engine-test.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
    $testRoot = Join-Path $env:LOCALAPPDATA ('VaultBackup\engine-tests-' + [guid]::NewGuid().ToString('N'))
    & '.\bin\vault-backup-cli.exe' --selftest $testRoot
    if ($LASTEXITCODE -ne 0) { throw 'Encryption self-test failed.' }
    & '.\bin\engine-test.exe' $testRoot
    if ($LASTEXITCODE -ne 0) { throw 'Failure-path checks failed.' }
    & $Gcc '-std=c11' '-O2' '-Wall' '-Wextra' '-Wno-misleading-indentation' '-D_WIN32_WINNT=0x0601' '-municode' '-static' 'tests/server_config_test.c' 'src/crypto.c' '-lbcrypt' '-ladvapi32' '-lshell32' '-o' 'bin/server-config-test.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Server configuration test compilation failed.' }
    & '.\bin\server-config-test.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Server configuration checks failed.' }
    # Only remove the known empty fixture directory; leave unexpected files for inspection.
    if ((Get-ChildItem -LiteralPath $testRoot -Force).Count -eq 0) { Remove-Item -LiteralPath $testRoot }
} finally { Pop-Location }
