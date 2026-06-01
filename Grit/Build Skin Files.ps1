$ErrorActionPreference = "Stop"

$gritDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptFile = $MyInvocation.MyCommand.Path
$root = (Resolve-Path -LiteralPath (Join-Path $gritDir "..")).Path
$images = Join-Path $root "images"
$source = Join-Path $root "source"
$assetHeader = Join-Path $source "launcher_theme_assets.h"
Add-Type -AssemblyName System.Drawing
$topBarHeight = 19

function Path-Exists($path) {
    return Test-Path -LiteralPath $path
}

$allThemeSpecs = @(
    @{ Folder = "pale_blue"; Name = "Pale Blue"; Suffix = "PALE_BLUE"; Dark = $false },
    @{ Folder = "light_blue"; Name = "Light Blue"; Suffix = "LIGHT_BLUE"; Dark = $false },
    @{ Folder = "blue";      Name = "Blue";      Suffix = "BLUE";      Dark = $false },
    @{ Folder = "dark_blue"; Name = "Dark Blue"; Suffix = "DARK_BLUE"; Dark = $false },
    @{ Folder = "green";     Name = "Green";     Suffix = "GREEN";     Dark = $false },
    @{ Folder = "pale_green"; Name = "Pale Green"; Suffix = "PALE_GREEN"; Dark = $false },
    @{ Folder = "bright_green"; Name = "Bright Green"; Suffix = "BRIGHT_GREEN"; Dark = $false },
    @{ Folder = "lime";      Name = "Lime";      Suffix = "LIME";      Dark = $false },
    @{ Folder = "yellow";    Name = "Yellow";    Suffix = "YELLOW";    Dark = $false },
    @{ Folder = "red";       Name = "Red";       Suffix = "RED";       Dark = $false },
    @{ Folder = "orange";    Name = "Orange";    Suffix = "ORANGE";    Dark = $false },
    @{ Folder = "brown";     Name = "Brown";     Suffix = "BROWN";     Dark = $false },
    @{ Folder = "pink";      Name = "Pink";      Suffix = "PINK";      Dark = $false },
    @{ Folder = "pale_pink"; Name = "Pale Pink"; Suffix = "PALE_PINK"; Dark = $false },
    @{ Folder = "magenta";   Name = "Magenta";   Suffix = "MAGENTA";   Dark = $false },
    @{ Folder = "purple";    Name = "Purple";    Suffix = "PURPLE";    Dark = $false },
    @{ Folder = "dark";      Name = "Dark";      Suffix = "DARK";      Dark = $true  }
)
$fallbackDefault = $allThemeSpecs[0]
$blankSpec = @{ Folder = "blank"; Name = "Blank"; Suffix = "BLANK"; Dark = $false }
$customColourSpec = @{ Folder = "custom_colour"; Name = "Custom"; Suffix = "CUSTOM"; Dark = $false }
$customThemeSpec = @{ Folder = "custom_theme"; Name = "Custom"; Suffix = "CUSTOM_THEME"; Dark = $true }

function Has-ThemeTopFile($spec) {
    $dir = Join-Path $images $spec.Folder
    $name = $spec.Folder
    return (Path-Exists (Join-Path $dir "$name.bmp")) -or (Path-Exists (Join-Path $dir "$name.h"))
}

function Has-ThemeColourEntry($folder) {
    $text = Get-Content -LiteralPath $scriptFile -Raw
    return $text -match ('"' + [regex]::Escape($folder) + '"\s*\{')
}

function Has-RequiredCustomColourFiles() {
    $dir = Join-Path $images $customColourSpec.Folder
    $top = Join-Path $dir "$($customColourSpec.Folder).bmp"
    if (!(Path-Exists $top)) { return $false }
    foreach ($name in @("icon_gba", "icon_folder")) {
        if (!(Path-Exists (Join-Path $dir "$name.bmp"))) { return $false }
    }
    return Has-ThemeColourEntry $customColourSpec.Folder
}

function Has-RequiredCustomThemeFiles() {
    $dir = Join-Path $images $customThemeSpec.Folder
    foreach ($name in @("SD_LIST", "SD_HORIZONTAL", "SD_VERTICAL", "SET", "START", "HELP", "MENU")) {
        if (!(Path-Exists (Join-Path $dir "$name.bmp"))) { return $false }
    }
    return $true
}

function Has-RequiredThemeFiles($spec) {
    $dir = Join-Path $images $spec.Folder
    if(!$spec.Dark) {
        return Has-ThemeTopFile $spec
    }
    foreach ($name in @("SD_LIST", "SD_HORIZONTAL", "SD_VERTICAL", "SET", "START", "HELP")) {
        if (!(Path-Exists (Join-Path $dir "$name.bmp")) -and !(Path-Exists (Join-Path $dir "$name.h"))) { return $false }
    }
    return $true
}

function Has-RequiredBaseFiles() {
    $dir = Join-Path $images $blankSpec.Folder
    foreach ($name in @("SD_LIST", "SD_HORIZONTAL", "SD_VERTICAL", "SET", "START", "HELP")) {
        if (!(Path-Exists (Join-Path $dir "$name.bmp")) -and !(Path-Exists (Join-Path $dir "$name.h"))) { return $false }
    }
    return $true
}

if(!(Has-RequiredBaseFiles)) {
    Write-Host "Created/using images\blank for shared full-screen base backgrounds."
    throw "Add generic full-screen base BMPs to images\blank: SD_LIST.bmp, SD_HORIZONTAL.bmp, SD_VERTICAL.bmp, SET.bmp, START.bmp, HELP.bmp"
}

function Save-CompatibleBmp($srcPath, $dstPath, $topStripOnly = $false) {
    $srcImage = [System.Drawing.Image]::FromFile($srcPath)
    try {
        $dstWidth = if ($topStripOnly) { 240 } else { $srcImage.Width }
        $dstHeight = if ($topStripOnly) { $topBarHeight } else { $srcImage.Height }
        $normalised = New-Object System.Drawing.Bitmap $dstWidth, $dstHeight, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($normalised)
            try {
                if ($topStripOnly) {
                    $graphics.DrawImage($srcImage,
                        (New-Object System.Drawing.Rectangle 0, 0, 240, $topBarHeight),
                        (New-Object System.Drawing.Rectangle 0, 0, 240, $topBarHeight),
                        [System.Drawing.GraphicsUnit]::Pixel)
                }
                else {
                    $graphics.DrawImage($srcImage, 0, 0, $srcImage.Width, $srcImage.Height)
                }
            }
            finally {
                $graphics.Dispose()
            }
            $normalised.Save($dstPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
        }
        finally {
            $normalised.Dispose()
        }
    }
    finally {
        $srcImage.Dispose()
    }
}

function Convert-ThemeFolder($spec) {
    $dir = Join-Path $images $spec.Folder
    if (!(Path-Exists $dir)) { return }

    $bmps = Get-ChildItem -LiteralPath $dir -Filter "*.bmp" -File
    if ($bmps.Count -eq 0) { return }

    foreach ($bmp in $bmps) {
        $sourceName = [IO.Path]::GetFileNameWithoutExtension($bmp.Name)
        $input = $sourceName
        if ($input -eq "NOR") { continue }
        if ($input -eq "RECENTLY") { continue }
        if (!$spec.Dark -and $spec.Folder -ne "blank" -and @("SD_LIST", "SD_HORIZONTAL", "SD_VERTICAL", "SET", "START", "HELP").Contains($input)) { continue }
        $topStripOnly = (!$spec.Dark -and $spec.Folder -ne "blank" -and $input -eq $spec.Folder)
        $tempBmp = Join-Path $gritDir $bmp.Name
        Write-Host "Input: $($spec.Folder)\$($bmp.Name)"
        Push-Location -LiteralPath $gritDir
        try {
            Save-CompatibleBmp $bmp.FullName $tempBmp $topStripOnly
            $gritInput = $tempBmp
            & (Join-Path $gritDir "grit.exe") $gritInput -gu8 -gb -gB16 -ftc -s "gImage_${input}_$($spec.Suffix)___"
            if ($LASTEXITCODE -ne 0) {
                & (Join-Path $gritDir "grit.exe") $bmp.FullName -gu8 -gb -gB16 -ftc -s "gImage_${input}_$($spec.Suffix)___"
                if ($LASTEXITCODE -ne 0) {
                    throw "grit failed for $($bmp.FullName)"
                }
            }
            $cFile = Join-Path $gritDir "$sourceName.c"
            if (!(Path-Exists $cFile)) {
                throw "grit did not produce $cFile for $($bmp.FullName)"
            }
            $hFile = Join-Path $dir "$input.h"
            $text = Get-Content -LiteralPath $cFile -Raw
            $text = $text.Replace("___Bitmap", "").Replace("___", "")
            Set-Content -LiteralPath $hFile -Value $text -NoNewline -Encoding ASCII
            Remove-Item -LiteralPath $cFile -Force
            $localH = Join-Path $gritDir "$input.h"
            if (Path-Exists $localH) { Remove-Item -LiteralPath $localH -Force }
            if (Path-Exists $tempBmp) { Remove-Item -LiteralPath $tempBmp -Force }
        }
        finally {
            if (Path-Exists $tempBmp) { Remove-Item -LiteralPath $tempBmp -Force }
            Pop-Location
        }
    }
}

function Convert-SplashImage() {
    $png = Join-Path $images "SPLASH.png"
    if (!(Path-Exists $png)) { return }

    $tempBmp = Join-Path $gritDir "SPLASH.bmp"
    Write-Host "Input: SPLASH.png"
    Push-Location -LiteralPath $gritDir
    try {
        Save-CompatibleBmp $png $tempBmp $false
        & (Join-Path $gritDir "grit.exe") $tempBmp -gu8 -gb -gB16 -ftc -s "gImage_splash___"
        if ($LASTEXITCODE -ne 0) {
            throw "grit failed for $png"
        }
        $cFile = Join-Path $gritDir "SPLASH.c"
        if (!(Path-Exists $cFile)) {
            throw "grit did not produce $cFile for $png"
        }
        $hFile = Join-Path $images "SPLASH.h"
        $text = Get-Content -LiteralPath $cFile -Raw
        $text = $text.Replace("___Bitmap", "").Replace("___", "")
        Set-Content -LiteralPath $hFile -Value $text -NoNewline -Encoding ASCII
        Remove-Item -LiteralPath $cFile -Force
        $localH = Join-Path $gritDir "SPLASH.h"
        if (Path-Exists $localH) { Remove-Item -LiteralPath $localH -Force }
        if (Path-Exists $tempBmp) { Remove-Item -LiteralPath $tempBmp -Force }
    }
    finally {
        if (Path-Exists $tempBmp) { Remove-Item -LiteralPath $tempBmp -Force }
        Pop-Location
    }
}

function Theme-Colours($spec) {
    switch ($spec.Folder) {
        "pale_blue" { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(10, 14, 17)"; SelectNor = "RGB(10, 14, 17)"; MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(10, 14, 17)"; TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "light_blue" { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(5, 19, 25)"; SelectNor = "RGB(5, 19, 25)"; MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(5, 19, 25)"; TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "blue"      { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(0, 11, 30)";  SelectNor = "RGB(0, 11, 30)";  MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(0, 11, 30)";  TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "dark_blue" { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(0, 0, 18)"; SelectNor = "RGB(0, 0, 18)"; MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(0, 0, 18)"; TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "green"     { return @{ Selected = "RGB(31, 31, 31)";  Text = "RGB(00, 00, 00)"; SelectSd = "RGB(0, 20, 7)";   SelectNor = "RGB(0, 20, 7)";   MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(0, 20, 7)";   TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "pale_green" { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(9, 24, 15)"; SelectNor = "RGB(9, 24, 15)"; MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(9, 24, 15)"; TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "bright_green" { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(0, 24, 0)"; SelectNor = "RGB(0, 24, 0)"; MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(0, 24, 0)"; TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "lime"      { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(18, 26, 0)"; SelectNor = "RGB(18, 26, 0)"; MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(18, 26, 0)"; TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "yellow"    { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(26, 24, 0)"; SelectNor = "RGB(26, 24, 0)"; MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(26, 24, 0)"; TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "red"       { return @{ Selected = "RGB(31, 31, 31)";  Text = "RGB(00, 00, 00)"; SelectSd = "RGB(31, 0, 2)";   SelectNor = "RGB(31, 0, 2)";   MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(31, 0, 2)";   TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "orange"    { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(31, 18, 0)";  SelectNor = "RGB(31, 18, 0)";  MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(31, 18, 0)";  TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "brown"     { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(23, 9, 0)"; SelectNor = "RGB(23, 9, 0)"; MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(23, 9, 0)"; TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "pink"      { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(31, 3, 20)";  SelectNor = "RGB(31, 3, 20)";  MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(31, 3, 20)";  TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "pale_pink" { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(26, 14, 26)"; SelectNor = "RGB(26, 14, 26)"; MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(26, 14, 26)"; TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "magenta"   { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(26, 0, 29)"; SelectNor = "RGB(26, 0, 29)"; MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(26, 0, 29)"; TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "purple"    { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(17, 0, 26)";  SelectNor = "RGB(17, 0, 26)";  MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(17, 0, 26)";  TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        "dark"      { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(31, 31, 31)"; SelectSd = "RGB(6, 11, 13)";  SelectNor = "RGB(6, 11, 13)";  MenuBtn = "RGB(00, 15, 22)"; BtnClean = "RGB(6, 11, 13)";  TitleFill = "RGB(4, 4, 4)";   TitleStripe = "RGB(2, 2, 2)";   BodyFill = "RGB(4, 4, 4)";   BodyStripe = "RGB(2, 2, 2)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
        default     { return @{ Selected = "RGB(31, 31, 31)"; Text = "RGB(00, 00, 00)"; SelectSd = "RGB(17, 0, 26)";  SelectNor = "RGB(17, 0, 26)";  MenuBtn = "RGB(23, 23, 23)"; BtnClean = "RGB(17, 0, 26)";  TitleFill = "RGB(31, 31, 31)"; TitleStripe = "RGB(29, 29, 29)"; BodyFill = "RGB(31, 31, 31)"; BodyStripe = "RGB(28, 28, 28)"; DarkTitleFill = "RGB(4, 4, 4)"; DarkTitleStripe = "RGB(2, 2, 2)"; DarkBodyFill = "RGB(4, 4, 4)"; DarkBodyStripe = "RGB(2, 2, 2)" } }
    }
}

function Theme-Entry($spec) {
    $suffix = $spec.Suffix
    $defaultSuffix = $fallbackDefault.Suffix
    $menu = if ($spec.Dark) { "gImage_MENU_DARK" } elseif (Path-Exists (Join-Path $images "blank\MENU.h")) { "gImage_MENU_BLANK" } elseif (Path-Exists (Join-Path $images "$($fallbackDefault.Folder)\MENU.h")) { "gImage_MENU_$defaultSuffix" } else { "gImage_MENU_DARK" }
    $iconGba = if (Path-Exists (Join-Path $images "$($spec.Folder)\icon_gba.h")) { "(const unsigned short*)gImage_icon_gba_$suffix" } else { "gImage_icon_gba_BASE" }
    $iconFolder = if (Path-Exists (Join-Path $images "$($spec.Folder)\icon_folder.h")) { "(const unsigned short*)gImage_icon_folder_$suffix" } else { "gImage_icon_folder_BASE" }
    $iconChip = if ($spec.Dark) { "(const unsigned short*)gImage_icon_chip_DARK" } elseif (Path-Exists (Join-Path $images "blank\icon_chip.h")) { "(const unsigned short*)gImage_icon_chip_BLANK" } elseif (Path-Exists (Join-Path $images "$($fallbackDefault.Folder)\icon_chip.h")) { "(const unsigned short*)gImage_icon_chip_$defaultSuffix" } else { "(const unsigned short*)gImage_icon_chip_DARK" }
    $c = Theme-Colours $spec
    if (-not $c.ContainsKey("DarkTitleFill")) { $c.DarkTitleFill = "RGB(4, 4, 4)" }
    if (-not $c.ContainsKey("DarkTitleStripe")) { $c.DarkTitleStripe = "RGB(2, 2, 2)" }
    if (-not $c.ContainsKey("DarkBodyFill")) { $c.DarkBodyFill = "RGB(4, 4, 4)" }
    if (-not $c.ContainsKey("DarkBodyStripe")) { $c.DarkBodyStripe = "RGB(2, 2, 2)" }
    if (-not $c.ContainsKey("BtnClean")) { $c.BtnClean = $c.SelectSd }
    $c.MenuBtn = $c.SelectSd
    if (-not $c.ContainsKey("TopbarText")) { $c.TopbarText = "RGB(31, 31, 31)" }
    if (-not $c.ContainsKey("Heart")) { $c.Heart = "LAUNCHER_COLOUR_AUTO" }
    if ($spec.Dark) {
        return "    { `"$($spec.Name)`", gImage_SET_$suffix, gImage_START_$suffix, gImage_HELP_$suffix, gImage_SD_LIST_$suffix, gImage_SD_HORIZONTAL_$suffix, gImage_SD_VERTICAL_$suffix, 0, 0, 0, 0, $menu, $iconGba, $iconFolder, $iconChip, $($c.Selected), $($c.Text), $($c.SelectSd), $($c.SelectNor), $($c.MenuBtn), $($c.BtnClean), $($c.TopbarText), $($c.Heart), $($c.TitleFill), $($c.TitleStripe), $($c.BodyFill), $($c.BodyStripe), $($c.DarkTitleFill), $($c.DarkTitleStripe), $($c.DarkBodyFill), $($c.DarkBodyStripe) }"
    }
    $top = "gImage_$($spec.Folder)_$suffix"
    "    { `"$($spec.Name)`", gImage_SET_BLANK, gImage_START_BLANK, gImage_HELP_BLANK, gImage_SD_LIST_BLANK, gImage_SD_HORIZONTAL_BLANK, gImage_SD_VERTICAL_BLANK, $top, $top, $top, $top, $menu, $iconGba, $iconFolder, $iconChip, $($c.Selected), $($c.Text), $($c.SelectSd), $($c.SelectNor), $($c.MenuBtn), $($c.BtnClean), $($c.TopbarText), $($c.Heart), $($c.TitleFill), $($c.TitleStripe), $($c.BodyFill), $($c.BodyStripe), $($c.DarkTitleFill), $($c.DarkTitleStripe), $($c.DarkBodyFill), $($c.DarkBodyStripe) }"
}

Convert-SplashImage

$customColourEnabled = Has-RequiredCustomColourFiles
$customThemeEnabled = Has-RequiredCustomThemeFiles
$themeSpecsToBuild = @($blankSpec) + $allThemeSpecs
if ($customColourEnabled) { $themeSpecsToBuild += $customColourSpec }
if ($customThemeEnabled) { $themeSpecsToBuild += $customThemeSpec }

foreach ($spec in $themeSpecsToBuild) {
    Convert-ThemeFolder $spec
}

$completeColourThemes = @($allThemeSpecs | Where-Object { $_.Folder -ne "dark" -and (Has-RequiredThemeFiles $_) })
if ($customColourEnabled -and (Has-RequiredThemeFiles $customColourSpec)) {
    $completeColourThemes += $customColourSpec
}
$activeThemes = @()
if ($completeColourThemes.Count -gt 0) {
    $activeThemes += $completeColourThemes
}
elseif (Has-RequiredThemeFiles $fallbackDefault) {
    $activeThemes += $fallbackDefault
}

$themeSpecsForIncludes = @($blankSpec) + @($allThemeSpecs)
if ($customColourEnabled) { $themeSpecsForIncludes += $customColourSpec }
if ($customThemeEnabled) { $themeSpecsForIncludes += $customThemeSpec }
$includeSpecs = $themeSpecsForIncludes | Where-Object {
    (Path-Exists (Join-Path $images "$($_.Folder)\SD_LIST.h")) -or
    (Path-Exists (Join-Path $images "$($_.Folder)\$($_.Folder).h")) -or
    (Path-Exists (Join-Path $images "$($_.Folder)\MENU.h")) -or
    (Path-Exists (Join-Path $images "$($_.Folder)\icon_chip.h")) -or
    (Path-Exists (Join-Path $images "$($_.Folder)\icon_gba.h")) -or
    (Path-Exists (Join-Path $images "$($_.Folder)\icon_folder.h"))
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#ifndef LAUNCHER_THEME_ASSETS_H")
$lines.Add("#define LAUNCHER_THEME_ASSETS_H")
$lines.Add("")
$lines.Add("#define gImage_icon_gba gImage_icon_gba_BASE")
$lines.Add('#include "icon_gba.h"')
$lines.Add("#undef gImage_icon_gba")
$lines.Add("#define gImage_icon_folder gImage_icon_folder_BASE")
$lines.Add('#include "icon_folder.h"')
$lines.Add("#undef gImage_icon_folder")
$lines.Add("")
foreach ($spec in $includeSpecs) {
    $imageNames = if ($spec.Folder -eq "blank" -or $spec.Dark) {
        @("SD_LIST", "SD_HORIZONTAL", "SD_VERTICAL", "SET", "START", "HELP", "MENU", "icon_chip", "icon_gba", "icon_folder")
    }
    else {
        @($spec.Folder, "MENU", "icon_chip", "icon_gba", "icon_folder")
    }
    foreach ($name in $imageNames) {
        if (Path-Exists (Join-Path $images "$($spec.Folder)\$name.h")) {
            $lines.Add("#define gImage_$name gImage_$name`_$($spec.Suffix)")
            $lines.Add("#include `"$($spec.Folder)/$name.h`"")
            $lines.Add("#undef gImage_$name")
        }
    }
}
$lines.Add("")
$customThemeMacro = if ($customThemeEnabled) { 1 } else { 0 }
$lines.Add("#define LAUNCHER_CUSTOM_THEME_ENABLED $customThemeMacro")
$lines.Add("#define LAUNCHER_THEME_COUNT $($activeThemes.Count)")
$lines.Add("#define LAUNCHER_THEME_TABLE_ENTRIES \")
$entries = @($activeThemes | ForEach-Object { Theme-Entry $_ })
for ($i = 0; $i -lt $entries.Count; $i++) {
    $suffix = if ($i -lt $entries.Count - 1) { ", \" } else { "" }
    $lines.Add($entries[$i] + $suffix)
}
$lines.Add("")
$lines.Add("#endif")
Set-Content -LiteralPath $assetHeader -Value ($lines -join "`r`n") -Encoding ASCII
Write-Host "Updated $assetHeader"


