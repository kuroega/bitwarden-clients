param()
$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'bin\vault-backup.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Run build.ps1 to build the GUI first.' }
$exe = (Resolve-Path -LiteralPath $exe).ProviderPath
$icon = Join-Path $PSScriptRoot 'assets\vault-backup-heian.ico'
if (-not (Test-Path -LiteralPath $icon)) { throw 'Application icon was not found.' }
$icon = (Resolve-Path -LiteralPath $icon).ProviderPath
$desktop = [Environment]::GetFolderPath('Desktop')
$link = Join-Path $desktop 'Bitwarden Vault Backup & Recover.lnk'
$oldLinks = @('Vault Backup.lnk', 'Vault Backup & Recover.lnk')
$shell = New-Object -ComObject WScript.Shell
try {
    $shortcut = $shell.CreateShortcut($link)
    $shortcut.TargetPath = $exe
    $shortcut.WorkingDirectory = Split-Path $exe
    $shortcut.IconLocation = "$icon,0"
    $shortcut.Description = 'Bitwarden Vault Backup & Recover: local encrypted backups, recovery and history management'
    $shortcut.WindowStyle = 1
    $shortcut.Save()
    $check = $shell.CreateShortcut($link)
    if ($check.TargetPath -ne $exe -or $check.IconLocation -ne "$icon,0") { throw 'Shortcut verification failed.' }
    # Retire only the old shortcut that belongs to this installation.
    foreach ($oldName in $oldLinks) {
        $oldLink = Join-Path $desktop $oldName
        if (Test-Path -LiteralPath $oldLink) {
            $oldShortcut = $shell.CreateShortcut($oldLink)
            if ($oldShortcut.TargetPath -eq $exe) { Remove-Item -LiteralPath $oldLink }
        }
    }
    Write-Output "Desktop shortcut created: $link"
} finally {
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
}
