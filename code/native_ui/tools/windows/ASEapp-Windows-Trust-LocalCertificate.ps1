param(
    [string]$CertificatePath = (Join-Path $PSScriptRoot 'ASEappSurfaceBuilderLocalCodeSigning.cer'),
    [string]$TargetPath = $PSScriptRoot,
    [switch]$AssumeYes
)

$ErrorActionPreference = 'Stop'

function Resolve-RequiredPath([string]$path, [string]$label) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "$label was not found: $path"
    }
    return (Resolve-Path -LiteralPath $path).Path
}

$certPath = Resolve-RequiredPath $CertificatePath 'Public certificate'
$targetPath = Resolve-RequiredPath $TargetPath 'Target path'

Write-Host 'ASEapp Surface Builder - local certificate trust helper'
Write-Host ''
Write-Host 'This script will:'
Write-Host "  1. Trust the bundled self-signed code-signing certificate for the current Windows user."
Write-Host "  2. Add the certificate to CurrentUser\\Root and CurrentUser\\TrustedPublisher."
Write-Host "  3. Remove the downloaded-from-Internet mark from ASEapp files under:"
Write-Host "     $targetPath"
Write-Host ''
Write-Host 'Only continue if you received these files from a source you trust.'
Write-Host 'This does not make ASEapp a Microsoft/CA-trusted public release.'
Write-Host ''

if (-not $AssumeYes) {
    $answer = Read-Host 'Continue? [y/N]'
    if ($answer -notmatch '^(y|Y|yes|YES)$') {
        Write-Host 'Cancelled.'
        exit 1
    }
}

$rootResult = Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\CurrentUser\Root
$publisherResult = Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\CurrentUser\TrustedPublisher

$extensionsToUnblock = @('.exe', '.dll', '.ps1', '.zip')
Get-ChildItem -LiteralPath $targetPath -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $extensionsToUnblock -contains $_.Extension.ToLowerInvariant() } |
    ForEach-Object {
        try {
            Unblock-File -LiteralPath $_.FullName -ErrorAction Stop
        }
        catch {
            Write-Warning "Could not unblock $($_.FullName): $($_.Exception.Message)"
        }
    }

Write-Host ''
Write-Host 'Installed certificate:'
Write-Host "  Root            : $($rootResult.Thumbprint)"
Write-Host "  TrustedPublisher: $($publisherResult.Thumbprint)"
Write-Host ''

$signedExecutables = Get-ChildItem -LiteralPath $targetPath -Recurse -File -Filter 'ASEapp*.exe' -ErrorAction SilentlyContinue |
    ForEach-Object {
        $signature = Get-AuthenticodeSignature -LiteralPath $_.FullName
        [pscustomobject]@{
            File = $_.FullName.Substring($targetPath.Length).TrimStart('\')
            Status = $signature.Status
            Signer = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { '' }
            Thumbprint = if ($signature.SignerCertificate) { $signature.SignerCertificate.Thumbprint } else { '' }
        }
    }

if ($signedExecutables) {
    $signedExecutables | Format-Table -AutoSize
}
else {
    Write-Warning 'No ASEapp*.exe files were found under the target path.'
}

Write-Host ''
Write-Host 'Done. If Windows still blocks the app, the PC may be governed by SmartScreen, Smart App Control, or organization policy.'
