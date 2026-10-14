param(
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [Parameter(Mandatory = $true)][string]$Output,
    [string]$CertificateThumbprint,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)
$ErrorActionPreference = 'Stop'
$packagePath = (Resolve-Path $PackageDirectory).Path
$assetsPath = Join-Path $packagePath 'Assets'
New-Item -ItemType Directory -Force -Path $assetsPath | Out-Null
Add-Type -AssemblyName System.Drawing
$source = [System.Drawing.Image]::FromFile((Join-Path $PSScriptRoot 'assets\app-icon.png'))
try {
    foreach ($asset in @(@('StoreLogo', 50), @('Square44x44Logo', 44), @('Square150x150Logo', 150))) {
        $bitmap = New-Object System.Drawing.Bitmap($asset[1], $asset[1])
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.DrawImage($source, 0, 0, $asset[1], $asset[1])
            $bitmap.Save((Join-Path $assetsPath "$($asset[0]).png"), [System.Drawing.Imaging.ImageFormat]::Png)
        } finally { $graphics.Dispose(); $bitmap.Dispose() }
    }
} finally { $source.Dispose() }
$sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$makeAppx = Get-ChildItem "$sdkBin\*\x64\makeappx.exe" | Sort-Object FullName -Descending | Select-Object -First 1
if (!$makeAppx) { throw 'Install the Windows 10/11 SDK (MakeAppx.exe).' }
& $makeAppx.FullName pack /d $packagePath /p $Output /o
if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed: $LASTEXITCODE" }
if ($CertificateThumbprint) {
    $signTool = Join-Path $makeAppx.Directory.FullName 'signtool.exe'
    if (!(Test-Path $signTool)) { throw 'The Windows SDK SignTool is missing.' }
    & $signTool sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $Output
    if ($LASTEXITCODE -ne 0) { throw "MSIX signing failed: $LASTEXITCODE" }
    & $signTool verify /pa /all /v $Output
    if ($LASTEXITCODE -ne 0) { throw "MSIX signature verification failed: $LASTEXITCODE" }
} else {
    Write-Warning 'Unsigned preview MSIX. It cannot be installed or publicly distributed without an appropriate trusted publisher signature.'
}
Get-FileHash -Algorithm SHA256 $Output | Format-List
