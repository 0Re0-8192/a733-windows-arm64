param(
    [Parameter(Mandatory)][string]$CertificatePath,
    [Parameter(Mandatory)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
if (-not ('A733CertificateBlob' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class A733CertificateBlob {
    [DllImport("crypt32.dll", SetLastError=true)]
    public static extern bool CertSerializeCertificateStoreElement(IntPtr context, uint flags, byte[] blob, ref uint length);
    [DllImport("crypt32.dll", SetLastError=true)]
    public static extern IntPtr CertOpenStore(IntPtr provider, uint encoding, IntPtr cryptProvider, uint flags, IntPtr parameter);
    [DllImport("crypt32.dll", SetLastError=true)]
    public static extern bool CertAddSerializedElementToStore(IntPtr store, byte[] blob, uint length, uint disposition, uint flags, uint types, out uint type, out IntPtr context);
    [DllImport("crypt32.dll")]
    public static extern bool CertFreeCertificateContext(IntPtr context);
    [DllImport("crypt32.dll")]
    public static extern bool CertCloseStore(IntPtr store, uint flags);
}
'@
}
$cert = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
    (Resolve-Path -LiteralPath $CertificatePath).Path)
$store = [IntPtr]::Zero
$restored = [IntPtr]::Zero
try {
    [uint32]$size = 0
    if (-not [A733CertificateBlob]::CertSerializeCertificateStoreElement($cert.Handle, 0, $null, [ref]$size)) {
        throw 'Could not determine serialized certificate size'
    }
    $blob = New-Object byte[] $size
    if (-not [A733CertificateBlob]::CertSerializeCertificateStoreElement($cert.Handle, 0, $blob, [ref]$size)) {
        throw 'Could not serialize certificate store element'
    }
    # Validate the registry Blob format by importing it into a memory store.
    # The host's persistent certificate stores are never opened here.
    $store = [A733CertificateBlob]::CertOpenStore([IntPtr]2, 0, [IntPtr]::Zero, 0, [IntPtr]::Zero)
    if ($store -eq [IntPtr]::Zero) { throw 'Could not open memory certificate store' }
    [uint32]$kind = 0
    if (-not [A733CertificateBlob]::CertAddSerializedElementToStore($store, $blob, $size, 4, 0, 2, [ref]$kind, [ref]$restored)) {
        throw 'Serialized certificate did not round-trip through CryptoAPI'
    }
    $roundTrip = [Security.Cryptography.X509Certificates.X509Certificate2]::new($restored)
    try {
        if ($roundTrip.Thumbprint -ne $cert.Thumbprint) { throw 'Certificate round-trip thumbprint mismatch' }
    } finally { $roundTrip.Dispose() }
    [IO.File]::WriteAllBytes([IO.Path]::GetFullPath($OutputPath), $blob)
    [pscustomobject]@{
        Thumbprint = $cert.Thumbprint
        SerializedBytes = $size
        BlobPath = [IO.Path]::GetFullPath($OutputPath)
        BlobSha256 = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash
        CryptoApiRoundTrip = $true
    }
} finally {
    if ($restored -ne [IntPtr]::Zero) { $null = [A733CertificateBlob]::CertFreeCertificateContext($restored) }
    if ($store -ne [IntPtr]::Zero) { $null = [A733CertificateBlob]::CertCloseStore($store, 0) }
    $cert.Dispose()
}
