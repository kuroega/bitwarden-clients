# Shared readers for the Git-ignored deployment configuration.
function Get-VaultLocalValue {
    param([string]$Section, [string]$Name)
    $configPath = Join-Path $PSScriptRoot 'server.local.ini'
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { return '' }
    $currentSection = ''
    foreach ($line in [IO.File]::ReadAllLines($configPath)) {
        $text = $line.Trim()
        if (-not $text -or $text.StartsWith(';') -or $text.StartsWith('#')) { continue }
        if ($text -match '^\[([^\]]+)\]$') { $currentSection = $Matches[1]; continue }
        if ($currentSection -ieq $Section -and $text -match '^([^=]+)=(.*)$' -and $Matches[1].Trim() -ieq $Name) {
            return $Matches[2].Trim()
        }
    }
    return ''
}

function Resolve-VaultCompiler {
    param([string]$Compiler = '')
    if (-not $Compiler) { $Compiler = Get-VaultLocalValue 'paths' 'gcc' }
    if (-not $Compiler) {
        $found = Get-Command gcc.exe -ErrorAction SilentlyContinue
        if ($found) { $Compiler = $found.Source }
    }
    if (-not $Compiler -or -not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
        throw 'GCC was not found. Set [paths] gcc in server.local.ini, add gcc.exe to PATH, or pass -Gcc.'
    }
    return $Compiler
}
