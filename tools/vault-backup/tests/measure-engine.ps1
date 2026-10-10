param([string]$Gcc = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
. (Join-Path $root 'config.ps1')
$Gcc = Resolve-VaultCompiler $Gcc
Push-Location $root
try {
    & $Gcc '-std=c11' '-O2' '-Wall' '-Wextra' '-Wno-misleading-indentation' '-D_WIN32_WINNT=0x0601' '-municode' '-static' 'tests/perf_test.c' 'src/crypto.c' 'src/backup.c' '-lbcrypt' '-lpsapi' '-ladvapi32' '-lshell32' '-o' 'bin/perf-test.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Performance test compilation failed.' }
    $fixture = Join-Path $env:LOCALAPPDATA ('VaultBackup\perf-test-' + [guid]::NewGuid().ToString('N'))
    & '.\bin\perf-test.exe' $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Performance test failed.' }
    # Delete only the newly created, empty fixture directory.
    if ((Get-ChildItem -LiteralPath $fixture -Force).Count -eq 0) { Remove-Item -LiteralPath $fixture }
} finally { Pop-Location }
