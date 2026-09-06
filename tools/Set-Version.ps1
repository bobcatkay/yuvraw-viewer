[CmdletBinding(SupportsShouldProcess)]
param([Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version)
$ErrorActionPreference = 'Stop'
$maximumResourceVersionComponent = 65535
$components = $Version.Split('.')
if (@($components | Where-Object { [uint64]$_ -gt $maximumResourceVersionComponent }).Count) {
    throw "Windows version components must not exceed $maximumResourceVersionComponent."
}
$headerPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'src/Core/FAppVersion.h'
$text = [IO.File]::ReadAllText($headerPath)
$names = @('MAJOR','MINOR','PATCH')
for ($index = 0; $index -lt $names.Count; $index++) {
    $pattern = '(?m)^(#define\s+YUVRAW_VERSION_' + $names[$index] + '\s+)\d+\s*$'
    if (-not [regex]::IsMatch($text, $pattern)) { throw "Missing version component: $($names[$index])" }
    $value = [int]$components[$index]
    $text = [regex]::Replace($text, $pattern, { param($match) $match.Groups[1].Value + $value })
}
if ($PSCmdlet.ShouldProcess($headerPath, "Set version to $Version")) {
    [IO.File]::WriteAllText($headerPath, $text, (New-Object Text.UTF8Encoding($false)))
    Write-Host "[Version] Set $Version. Review and commit this change before tagging."
}
