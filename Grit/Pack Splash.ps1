$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$header = Join-Path $root "images/SPLASH.h"
$target = Join-Path $root "source/launcher_splash_lz.h"
$tool = "C:\devkitPro\tools\bin\gbalzss.exe"
if (!(Test-Path -LiteralPath $tool)) {
    $command = Get-Command gbalzss.exe -ErrorAction SilentlyContinue
    if (!$command) { throw "Install devkitPro tools (gbalzss) to build the splash image." }
    $tool = $command.Source
}
$array = [regex]::Match([IO.File]::ReadAllText($header), 'gImage_splash\s*\[76800\][^=]*=\s*\{(.*?)\}', 'Singleline')
if (!$array.Success) { throw "SPLASH.h must contain a 240 x 160 16-bit splash image." }
[byte[]]$bytes = @([regex]::Matches($array.Groups[1].Value, '0x([0-9a-fA-F]{2})') | ForEach-Object { [Convert]::ToByte($_.Groups[1].Value, 16) })
if ($bytes.Length -ne 76800) { throw "Incorrect splash image size." }
$raw = Join-Path $PSScriptRoot "splash-build.raw"
$packed = Join-Path $PSScriptRoot "splash-build.lz"
$roundtrip = Join-Path $PSScriptRoot "splash-build-check.raw"
try {
    [IO.File]::WriteAllBytes($raw, $bytes)
    & $tool --vram e $raw $packed
    if ($LASTEXITCODE -ne 0) { throw "Splash compression failed." }
    & $tool d $packed $roundtrip
    if ($LASTEXITCODE -ne 0) { throw "Splash verification failed." }
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($roundtrip)) -ne [Convert]::ToBase64String($bytes)) {
        throw "Compressed splash did not round-trip exactly."
    }
    $data = [IO.File]::ReadAllBytes($packed)
    $lines = for ($i = 0; $i -lt $data.Length; $i += 24) {
        ($data[$i..([Math]::Min($i + 23, $data.Length - 1))] -join ',')
    }
    $prefix = "/* Generated losslessly from images/SPLASH.h by Grit/Pack Splash.ps1. */`nstatic const u8 launcher_splash_lz[] __attribute__((aligned(4))) = {`n"
    $suffix = @'

};
static void Launcher_DrawPackedSplash(void)
{
    register const void *source asm("r0") = launcher_splash_lz;
    register void *destination asm("r1") = (void*)0x06000000;
    REG_DISPCNT |= 0x0080;
    asm volatile("swi 0x12" : "+r"(source), "+r"(destination) : : "r2", "r3", "cc", "memory");
}
'@
    [IO.File]::WriteAllText($target, $prefix + ($lines -join ",`n") + $suffix + "`n", [Text.Encoding]::ASCII)
    Write-Host "Packed splash: $($data.Length) bytes; verified lossless."
}
finally {
    foreach ($file in @($raw, $packed, $roundtrip)) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
    }
}
