param(
    [string]$Subject = 'CN=ASEapp Surface Builder Local Code Signing',
    [string]$PublicCertPath = ''
)

$ErrorActionPreference = 'Stop'

$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
    Where-Object { $_.Subject -eq $Subject -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $cert) {
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $Subject `
        -CertStoreLocation Cert:\CurrentUser\My `
        -KeyUsage DigitalSignature `
        -KeyLength 3072 `
        -HashAlgorithm SHA256 `
        -NotAfter (Get-Date).AddYears(5)
}

if ([string]::IsNullOrWhiteSpace($PublicCertPath)) {
    $nativeUiDir = Split-Path -Parent $PSScriptRoot
    $PublicCertPath = Join-Path $nativeUiDir 'certs\ASEappSurfaceBuilderLocalCodeSigning.cer'
}

$publicCertFullPath = [System.IO.Path]::GetFullPath($PublicCertPath)
$publicCertDir = Split-Path -Parent $publicCertFullPath
New-Item -ItemType Directory -Path $publicCertDir -Force | Out-Null
Export-Certificate -Cert $cert -FilePath $publicCertFullPath -Force | Out-Null
Import-Certificate -FilePath $publicCertFullPath -CertStoreLocation Cert:\CurrentUser\TrustedPublisher | Out-Null
Import-Certificate -FilePath $publicCertFullPath -CertStoreLocation Cert:\CurrentUser\Root | Out-Null

Write-Host "Installed local code signing certificate:"
Write-Host "  Subject    : $($cert.Subject)"
Write-Host "  Thumbprint : $($cert.Thumbprint)"
Write-Host "  Public cert: $publicCertFullPath"
Write-Host ""
Write-Host "Use this thumbprint explicitly if needed:"
Write-Host "`$env:ASEAPP_CODESIGN_THUMBPRINT='$($cert.Thumbprint)'"
