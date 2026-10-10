param([string]$Gcc = '')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
. (Join-Path $root 'config.ps1')
$Gcc = Resolve-VaultCompiler $Gcc
$out = Join-Path $root 'bin'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$windres = Join-Path (Split-Path $Gcc) 'windres.exe'
Push-Location $root
try {
    & $windres 'src/app.rc' '-O' 'coff' '-o' 'bin/app-res.o'
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    $common = @('-specs=src/no-default-manifest.specs','-std=c11','-O2','-Wall','-Wextra','-Wno-misleading-indentation','-D_WIN32_WINNT=0x0A00','-municode','-static','-static-libgcc','-finput-charset=UTF-8','-fexec-charset=UTF-8','src/main.c','src/backup.c','src/crypto.c','src/recover.c','bin/app-res.o','-lbcrypt','-lcomctl32','-lshell32','-lole32','-ladvapi32','-lcomdlg32','-luuid')
    & $Gcc @common '-luxtheme' '-mwindows' '-o' 'bin/vault-backup.exe'
    if ($LASTEXITCODE -ne 0) { throw 'GUI compilation failed.' }
    & $Gcc @common '-DVAULT_CLI' '-o' 'bin/vault-backup-cli.exe'
    if ($LASTEXITCODE -ne 0) { throw 'CLI compilation failed.' }
} finally { Pop-Location }
Write-Output "Build completed: $out"
