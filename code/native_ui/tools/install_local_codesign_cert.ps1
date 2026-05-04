param(
    [string]$Subject = 'CN=ASEapp Surface Builder Local Code Signing'
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

$tempCert = Join-Path $env:TEMP 'aseapp_surface_builder_local_codesign.cer'
Export-Certificate -Cert $cert -FilePath $tempCert -Force | Out-Null
Import-Certificate -FilePath $tempCert -CertStoreLocation Cert:\CurrentUser\TrustedPublisher | Out-Null
Import-Certificate -FilePath $tempCert -CertStoreLocation Cert:\CurrentUser\Root | Out-Null
Remove-Item -LiteralPath $tempCert -Force -ErrorAction SilentlyContinue

Write-Host "Installed local code signing certificate:"
Write-Host "  Subject    : $($cert.Subject)"
Write-Host "  Thumbprint : $($cert.Thumbprint)"
Write-Host ""
Write-Host "Use this thumbprint explicitly if needed:"
Write-Host "`$env:ASEAPP_CODESIGN_THUMBPRINT='$($cert.Thumbprint)'"
