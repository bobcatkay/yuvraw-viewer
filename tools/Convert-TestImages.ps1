<#
.SYNOPSIS
Converts the three representative raw images in the supplied Test directory and
verifies every generated file with FFmpeg.

.DESCRIPTION
The script intentionally keeps all source-layout knowledge in one place:

* P010: 3840x2160, tightly packed, BT.2020 limited range. The frame that has a
  same-basename PNG is selected so generated files can be compared with a real
  reference image.
* Android format35/NV21: logical size 1440x1920 with a 1472-byte row stride.
  FFmpeg reads it as a 1472x1920 NV21 frame and crops the 32 padding columns.
* RGBA: the declared 3840x2880 canvas is preserved as one source variant. The
  alpha bounding box is only 1920x1440, so a second variant crops that valid
  upper-left region. This makes the producer-side zero-filled canvas visible
  without losing the useful image.

Outputs are raw single-frame files. Their names contain the logical WxH and the
YUVRaw format token. The script overwrites only its deterministic files in
OutputDirectory and refuses to write inside InputDirectory.

Each output is checked three ways:

1. Exact byte count for its pixel format.
2. FFmpeg rawvideo readback.
3. RGB PSNR. P010 outputs use the independently supplied PNG as their reference.
   Other sources use the directly generated RGB8 output from the same source
   variant, so their PSNR proves conversion-to-conversion consistency rather than
   independently proving the source pixel-format, crop, or color-matrix choice.

PSNR must meet a quality floor. Grayscale uses a lower floor because discarding
chroma is intentional; color outputs use a stricter floor that catches target-side
channel swaps, range mistakes, and visibly broken conversion. Inspect the generated
preview PNGs to validate source interpretation when no independent reference exists.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File .\tools\Convert-TestImages.ps1 `
  -InputDirectory '.\private-fixtures' `
  -OutputDirectory '.\artifacts\converted-fixtures'
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$InputDirectory,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$OutputDirectory,

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$FfmpegPath = 'ffmpeg'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Physical source layout.
$P010Width = 3840
$P010Height = 2160
$Nv21ActiveWidth = 1440
$Nv21StorageWidth = 1472
$Nv21Height = 1920
$Nv21StrideBytes = 1472
$RgbaCanvasWidth = 3840
$RgbaCanvasHeight = 2880
$RgbaActiveWidth = 1920
$RgbaActiveHeight = 1440
$RgbaBytesPerPixel = 4

# Raw frame-size ratios.
$Yuv420SizeNumerator = 3
$Yuv420SizeDenominator = 2
$TwoBytesPerPixel = 2
$ThreeBytesPerPixel = 3
$FourBytesPerPixel = 4
$OneBytePerPixel = 1

# Conversion and report settings.
$FrameCount = 1
$JsonSerializationDepth = 6
$ScaleFlags = 'bilinear'
$LimitedRange = 'limited'
$FullRange = 'full'
$Bt601Matrix = 'bt601'
$Bt709Matrix = 'bt709'
$Bt2020Matrix = 'bt2020'
$Yv12PlaneSwapFilter = 'shuffleplanes=0:2:1'
$PsnrPattern = 'average:(?<Average>(?:[0-9]+(?:\.[0-9]+)?)|inf)'
$MinimumColorPsnrDb = 30.0
$MinimumGrayscalePsnrDb = 15.0
$PathComparison = [System.StringComparison]::OrdinalIgnoreCase

function Convert-ToAbsolutePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return [System.IO.Path]::GetFullPath($Path)
}

function Test-IsSameOrChildPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CandidatePath,

        [Parameter(Mandatory = $true)]
        [string]$ParentPath
    )

    $normalizedParent = $ParentPath.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $parentPrefix = $normalizedParent + [System.IO.Path]::DirectorySeparatorChar

    return $CandidatePath.Equals($normalizedParent, $PathComparison) -or
        $CandidatePath.StartsWith($parentPrefix, $PathComparison)
}

function ConvertTo-NativeProcessArgument {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Value
    )

    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') {
        return $Value
    }

    # ProcessStartInfo.Arguments receives one Windows command line. Follow the
    # CommandLineToArgvW backslash/quote rules so paths with spaces remain safe.
    $builder = New-Object System.Text.StringBuilder
    $backslash = [char]'\'
    $doubleQuote = [char]'"'
    $backslashCount = 0
    [void]$builder.Append($doubleQuote)

    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq $backslash) {
            ++$backslashCount
            continue
        }

        if ($character -eq $doubleQuote) {
            [void]$builder.Append($backslash, $backslashCount * 2 + 1)
            [void]$builder.Append($doubleQuote)
            $backslashCount = 0
            continue
        }

        if ($backslashCount -gt 0) {
            [void]$builder.Append($backslash, $backslashCount)
            $backslashCount = 0
        }

        [void]$builder.Append($character)
    }

    if ($backslashCount -gt 0) {
        [void]$builder.Append($backslash, $backslashCount * 2)
    }

    [void]$builder.Append($doubleQuote)
    return $builder.ToString()
}

function Invoke-Ffmpeg {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    # Windows PowerShell wraps native stderr as PowerShell errors. With the
    # script-wide Stop preference, the first normal FFmpeg log can truncate the
    # PSNR output. Redirect both pipes and use the native process exit code.
    [System.Diagnostics.ProcessStartInfo]$processStartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $processStartInfo.FileName = $script:FfmpegExecutable
    $processStartInfo.Arguments = ($Arguments |
        ForEach-Object { ConvertTo-NativeProcessArgument -Value $_ }) -join ' '
    $processStartInfo.UseShellExecute = $false
    $processStartInfo.CreateNoWindow = $true
    $processStartInfo.RedirectStandardOutput = $true
    $processStartInfo.RedirectStandardError = $true

    [System.Diagnostics.Process]$process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $processStartInfo
    $standardOutput = ''
    $standardError = ''
    $exitCode = -1

    try {
        if (-not $process.Start()) {
            throw "Failed to start FFmpeg: $script:FfmpegExecutable"
        }

        # Read both pipes concurrently so verbose FFmpeg output cannot deadlock.
        $standardOutputTask = $process.StandardOutput.ReadToEndAsync()
        $standardErrorTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $standardOutput = $standardOutputTask.Result
        $standardError = $standardErrorTask.Result
        $exitCode = $process.ExitCode
    }
    finally {
        $process.Dispose()
    }

    $text = ($standardOutput.TrimEnd(), $standardError.TrimEnd() |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) -join [Environment]::NewLine

    return [PSCustomObject]@{
        ExitCode = $exitCode
        Text = $text
    }
}

function Find-P010SourceWithReference {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Directory
    )

    $candidates = @(Get-ChildItem -LiteralPath $Directory -File |
        Where-Object { $_.Name -match '(?i)3840x2160.*p010le\.yuv$' } |
        Sort-Object Name)

    foreach ($candidate in $candidates) {
        $referencePath = [System.IO.Path]::ChangeExtension($candidate.FullName, '.png')

        if (Test-Path -LiteralPath $referencePath -PathType Leaf) {
            return [PSCustomObject]@{
                SourcePath = $candidate.FullName
                ReferencePath = $referencePath
            }
        }
    }

    throw 'No 3840x2160 p010le YUV file with a same-basename PNG reference was found.'
}

function Find-RepresentativeFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Directory,

        [Parameter(Mandatory = $true)]
        [string]$NamePattern,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    $matches = @(Get-ChildItem -LiteralPath $Directory -File |
        Where-Object { $_.Name -match $NamePattern } |
        Sort-Object Name)

    if ($matches.Count -eq 0) {
        throw "No $Description source matched pattern: $NamePattern"
    }

    if ($matches.Count -gt 1) {
        Write-Warning "Multiple $Description files matched; deterministically using '$($matches[0].Name)'."
    }

    return $matches[0].FullName
}

function Assert-InputFileSize {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [long]$ExpectedBytes,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    $actualBytes = (Get-Item -LiteralPath $Path).Length

    if ($actualBytes -ne $ExpectedBytes) {
        throw "$Description byte count mismatch. Expected $ExpectedBytes, found ${actualBytes}: $Path"
    }
}

function New-TargetSpec {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Format,

        [Parameter(Mandatory = $true)]
        [string]$FileToken,

        [Parameter(Mandatory = $true)]
        [string]$PixelFormat,

        [Parameter(Mandatory = $true)]
        [string]$Extension,

        [Parameter(Mandatory = $true)]
        [long]$SizeNumerator,

        [Parameter(Mandatory = $true)]
        [long]$SizeDenominator,

        [Parameter(Mandatory = $true)]
        [string]$OutputRange,

        [Parameter()]
        [string]$WriteFilter = '',

        [Parameter()]
        [string]$ReadFilter = ''
    )

    return [PSCustomObject]@{
        Format = $Format
        FileToken = $FileToken
        PixelFormat = $PixelFormat
        Extension = $Extension
        SizeNumerator = $SizeNumerator
        SizeDenominator = $SizeDenominator
        OutputRange = $OutputRange
        WriteFilter = $WriteFilter
        ReadFilter = $ReadFilter
    }
}

function Get-ExpectedOutputBytes {
    param(
        [Parameter(Mandatory = $true)]
        [int]$Width,

        [Parameter(Mandatory = $true)]
        [int]$Height,

        [Parameter(Mandatory = $true)]
        [PSCustomObject]$Target
    )

    $pixelCount = [long]$Width * [long]$Height
    $scaledBytes = $pixelCount * [long]$Target.SizeNumerator

    if (($scaledBytes % [long]$Target.SizeDenominator) -ne 0) {
        throw "Dimensions ${Width}x${Height} cannot be represented exactly as $($Target.Format)."
    }

    return [long]($scaledBytes / [long]$Target.SizeDenominator)
}

function Get-OutputPath {
    param(
        [Parameter(Mandatory = $true)]
        [PSCustomObject]$Source,

        [Parameter(Mandatory = $true)]
        [PSCustomObject]$Target
    )

    $fileName = '{0}_{1}x{2}_{3}.{4}' -f
        $Source.Id,
        $Source.OutputWidth,
        $Source.OutputHeight,
        $Target.FileToken,
        $Target.Extension

    return Join-Path $script:ResolvedOutputDirectory $fileName
}

function Get-ScaleFilter {
    param(
        [Parameter(Mandatory = $true)]
        [PSCustomObject]$Source,

        [Parameter(Mandatory = $true)]
        [string]$InputRange,

        [Parameter(Mandatory = $true)]
        [string]$OutputRange
    )

    return 'scale=in_color_matrix={0}:out_color_matrix={0}:in_range={1}:out_range={2}:flags={3}' -f
        $Source.Matrix,
        $InputRange,
        $OutputRange,
        $script:ScaleFlags
}

function Convert-SourceToTarget {
    param(
        [Parameter(Mandatory = $true)]
        [PSCustomObject]$Source,

        [Parameter(Mandatory = $true)]
        [PSCustomObject]$Target,

        [Parameter(Mandatory = $true)]
        [string]$OutputPath
    )

    $filterParts = New-Object System.Collections.Generic.List[string]

    if (-not [string]::IsNullOrWhiteSpace($Source.SourceFilter)) {
        $filterParts.Add($Source.SourceFilter)
    }

    # swscale 6.0 has a same-format high-bit-depth/semiplanar corner case where
    # P010->P010 or NV21->NV21 can zero the chroma plane. Cropping alone does
    # not require color conversion, so keep identical format/range paths direct.
    $needsColorConversion =
        $Source.InputPixelFormat -ne $Target.PixelFormat -or
        $Source.InputRange -ne $Target.OutputRange

    if ($needsColorConversion) {
        $filterParts.Add(
            (Get-ScaleFilter -Source $Source -InputRange $Source.InputRange -OutputRange $Target.OutputRange))
    }

    $filterParts.Add("format=$($Target.PixelFormat)")

    if (-not [string]::IsNullOrWhiteSpace($Target.WriteFilter)) {
        $filterParts.Add($Target.WriteFilter)
    }

    $filterChain = $filterParts -join ','
    $arguments = @(
        '-hide_banner',
        '-loglevel', 'error',
        '-y',
        '-f', 'rawvideo',
        '-pixel_format', $Source.InputPixelFormat,
        '-video_size', "$($Source.InputWidth)x$($Source.InputHeight)",
        '-i', $Source.Path,
        '-vf', $filterChain,
        '-frames:v', $script:FrameCount.ToString(),
        '-an',
        '-sn',
        '-dn',
        '-pix_fmt', $Target.PixelFormat,
        '-f', 'rawvideo',
        $OutputPath
    )

    return Invoke-Ffmpeg -Arguments $arguments
}

function Test-RawReadback {
    param(
        [Parameter(Mandatory = $true)]
        [PSCustomObject]$Target,

        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [int]$Width,

        [Parameter(Mandatory = $true)]
        [int]$Height
    )

    $arguments = @(
        '-hide_banner',
        '-loglevel', 'error',
        '-f', 'rawvideo',
        '-pixel_format', $Target.PixelFormat,
        '-video_size', "${Width}x${Height}",
        '-i', $Path
    )

    if (-not [string]::IsNullOrWhiteSpace($Target.ReadFilter)) {
        $arguments += @('-vf', $Target.ReadFilter)
    }

    $arguments += @(
        '-frames:v', $script:FrameCount.ToString(),
        '-f', 'null',
        '-'
    )

    return Invoke-Ffmpeg -Arguments $arguments
}

function Measure-OutputPsnr {
    param(
        [Parameter(Mandatory = $true)]
        [PSCustomObject]$Source,

        [Parameter(Mandatory = $true)]
        [PSCustomObject]$Target,

        [Parameter(Mandatory = $true)]
        [string]$OutputPath,

        [Parameter(Mandatory = $true)]
        [string]$ReferencePath,

        [Parameter(Mandatory = $true)]
        [ValidateSet('Image', 'RawRgb8')]
        [string]$ReferenceKind
    )

    $arguments = @(
        '-hide_banner',
        '-loglevel', 'info',
        '-f', 'rawvideo',
        '-pixel_format', $Target.PixelFormat,
        '-video_size', "$($Source.OutputWidth)x$($Source.OutputHeight)",
        '-i', $OutputPath
    )

    if ($ReferenceKind -eq 'RawRgb8') {
        $arguments += @(
            '-f', 'rawvideo',
            '-pixel_format', 'rgb24',
            '-video_size', "$($Source.OutputWidth)x$($Source.OutputHeight)",
            '-i', $ReferencePath
        )
    }
    else {
        $arguments += @('-i', $ReferencePath)
    }

    $targetFilterParts = New-Object System.Collections.Generic.List[string]

    if (-not [string]::IsNullOrWhiteSpace($Target.ReadFilter)) {
        $targetFilterParts.Add($Target.ReadFilter)
    }

    $targetFilterParts.Add(
        (Get-ScaleFilter -Source $Source -InputRange $Target.OutputRange -OutputRange $script:FullRange))
    $targetFilterParts.Add('format=rgb24')

    $targetFilter = $targetFilterParts -join ','
    $filterComplex = '[0:v]{0}[converted];[1:v]format=rgb24[reference];[converted][reference]psnr' -f
        $targetFilter

    $arguments += @(
        '-filter_complex', $filterComplex,
        '-frames:v', $script:FrameCount.ToString(),
        '-f', 'null',
        '-'
    )

    $result = Invoke-Ffmpeg -Arguments $arguments
    $match = [regex]::Match($result.Text, $script:PsnrPattern)

    return [PSCustomObject]@{
        ExitCode = $result.ExitCode
        Average = if ($match.Success) { $match.Groups['Average'].Value } else { '' }
        Text = $result.Text
    }
}

$resolvedInput = Resolve-Path -LiteralPath $InputDirectory -ErrorAction Stop

if (-not (Test-Path -LiteralPath $resolvedInput.Path -PathType Container)) {
    throw "InputDirectory is not a directory: $InputDirectory"
}

$ResolvedInputDirectory = Convert-ToAbsolutePath -Path $resolvedInput.Path
$candidateOutputDirectory = Convert-ToAbsolutePath -Path $OutputDirectory

if (Test-IsSameOrChildPath -CandidatePath $candidateOutputDirectory -ParentPath $ResolvedInputDirectory) {
    throw 'OutputDirectory must not be InputDirectory or one of its child directories.'
}

if (-not (Test-Path -LiteralPath $candidateOutputDirectory)) {
    New-Item -ItemType Directory -Path $candidateOutputDirectory -Force | Out-Null
}

$resolvedOutput = Resolve-Path -LiteralPath $candidateOutputDirectory -ErrorAction Stop
$ResolvedOutputDirectory = Convert-ToAbsolutePath -Path $resolvedOutput.Path

$ffmpegCommand = Get-Command $FfmpegPath -ErrorAction Stop
$FfmpegExecutable = $ffmpegCommand.Source

if ([string]::IsNullOrWhiteSpace($FfmpegExecutable)) {
    $FfmpegExecutable = $ffmpegCommand.Path
}

$versionResult = Invoke-Ffmpeg -Arguments @('-hide_banner', '-version')

if ($versionResult.ExitCode -ne 0) {
    throw "FFmpeg could not be executed: $($versionResult.Text)"
}

$ffmpegVersion = ($versionResult.Text -split '\r?\n' | Select-Object -First 1)

Write-Host "Input : $ResolvedInputDirectory"
Write-Host "Output: $ResolvedOutputDirectory"
Write-Host "FFmpeg: $ffmpegVersion"

$p010Selection = Find-P010SourceWithReference -Directory $ResolvedInputDirectory
$nv21Path = Find-RepresentativeFile `
    -Directory $ResolvedInputDirectory `
    -NamePattern '(?i)^texture_video_format35_1472x1920\.yuv$' `
    -Description 'NV21 format35'
$rgbaPath = Find-RepresentativeFile `
    -Directory $ResolvedInputDirectory `
    -NamePattern '(?i)3840x2880.*rgba.*\.yuv$' `
    -Description 'RGBA'

$p010ExpectedBytes = [long]$P010Width * $P010Height * $ThreeBytesPerPixel
$nv21ExpectedBytes = [long]$Nv21StrideBytes * $Nv21Height *
    $Yuv420SizeNumerator / $Yuv420SizeDenominator
$rgbaExpectedBytes = [long]$RgbaCanvasWidth * $RgbaCanvasHeight * $RgbaBytesPerPixel

Assert-InputFileSize `
    -Path $p010Selection.SourcePath `
    -ExpectedBytes $p010ExpectedBytes `
    -Description 'P010 source'
Assert-InputFileSize `
    -Path $nv21Path `
    -ExpectedBytes $nv21ExpectedBytes `
    -Description 'NV21 source'
Assert-InputFileSize `
    -Path $rgbaPath `
    -ExpectedBytes $rgbaExpectedBytes `
    -Description 'RGBA source'

$referenceReadback = Invoke-Ffmpeg -Arguments @(
    '-hide_banner',
    '-loglevel', 'error',
    '-i', $p010Selection.ReferencePath,
    '-frames:v', $FrameCount.ToString(),
    '-f', 'null',
    '-'
)

if ($referenceReadback.ExitCode -ne 0) {
    throw "The P010 PNG reference cannot be decoded: $($referenceReadback.Text)"
}

$sourceVariants = @(
    [PSCustomObject]@{
        Id = 'source01_reference'
        Description = 'P010 frame with same-basename PNG reference'
        Strategy = 'Tightly packed 3840x2160 P010; preserve full frame'
        Path = $p010Selection.SourcePath
        InputPixelFormat = 'p010le'
        InputWidth = $P010Width
        InputHeight = $P010Height
        OutputWidth = $P010Width
        OutputHeight = $P010Height
        SourceFilter = ''
        Matrix = $Bt2020Matrix
        InputRange = $LimitedRange
        ReferencePath = $p010Selection.ReferencePath
        ReferenceKind = 'Image'
    },
    [PSCustomObject]@{
        Id = 'source02_stride_removed'
        Description = 'Android format35 NV21 with row padding'
        Strategy = 'Read 1472x1920, crop to 1440x1920 to remove 32 padding columns'
        Path = $nv21Path
        InputPixelFormat = 'nv21'
        InputWidth = $Nv21StorageWidth
        InputHeight = $Nv21Height
        OutputWidth = $Nv21ActiveWidth
        OutputHeight = $Nv21Height
        SourceFilter = "crop=${Nv21ActiveWidth}:${Nv21Height}:0:0"
        Matrix = $Bt601Matrix
        InputRange = $LimitedRange
        ReferencePath = ''
        ReferenceKind = 'RawRgb8'
    },
    [PSCustomObject]@{
        Id = 'source03_full_canvas'
        Description = 'RGBA declared canvas'
        Strategy = 'Preserve the complete 3840x2880 canvas, including zero-filled right and lower areas'
        Path = $rgbaPath
        InputPixelFormat = 'rgba'
        InputWidth = $RgbaCanvasWidth
        InputHeight = $RgbaCanvasHeight
        OutputWidth = $RgbaCanvasWidth
        OutputHeight = $RgbaCanvasHeight
        SourceFilter = ''
        Matrix = $Bt709Matrix
        InputRange = $FullRange
        ReferencePath = ''
        ReferenceKind = 'RawRgb8'
    },
    [PSCustomObject]@{
        Id = 'source03_active_crop'
        Description = 'RGBA active upper-left region'
        Strategy = 'Crop the known non-zero alpha bounding box at 1920x1440+0+0'
        Path = $rgbaPath
        InputPixelFormat = 'rgba'
        InputWidth = $RgbaCanvasWidth
        InputHeight = $RgbaCanvasHeight
        OutputWidth = $RgbaActiveWidth
        OutputHeight = $RgbaActiveHeight
        SourceFilter = "crop=${RgbaActiveWidth}:${RgbaActiveHeight}:0:0"
        Matrix = $Bt709Matrix
        InputRange = $FullRange
        ReferencePath = ''
        ReferenceKind = 'RawRgb8'
    }
)

# RGB8 is first because it is the generated PSNR reference for sources without a supplied PNG.
$targetSpecs = @(
    (New-TargetSpec -Format 'RGB8' -FileToken 'RGB8' -PixelFormat 'rgb24' -Extension 'raw' `
        -SizeNumerator $ThreeBytesPerPixel -SizeDenominator $OneBytePerPixel -OutputRange $FullRange),
    (New-TargetSpec -Format 'NV12' -FileToken 'NV12' -PixelFormat 'nv12' -Extension 'yuv' `
        -SizeNumerator $Yuv420SizeNumerator -SizeDenominator $Yuv420SizeDenominator -OutputRange $LimitedRange),
    (New-TargetSpec -Format 'NV21' -FileToken 'NV21' -PixelFormat 'nv21' -Extension 'yuv' `
        -SizeNumerator $Yuv420SizeNumerator -SizeDenominator $Yuv420SizeDenominator -OutputRange $LimitedRange),
    (New-TargetSpec -Format 'I420' -FileToken 'I420_YUV420P' -PixelFormat 'yuv420p' -Extension 'yuv' `
        -SizeNumerator $Yuv420SizeNumerator -SizeDenominator $Yuv420SizeDenominator -OutputRange $LimitedRange),
    (New-TargetSpec -Format 'YV12' -FileToken 'YV12' -PixelFormat 'yuv420p' -Extension 'yuv' `
        -SizeNumerator $Yuv420SizeNumerator -SizeDenominator $Yuv420SizeDenominator -OutputRange $LimitedRange `
        -WriteFilter $Yv12PlaneSwapFilter -ReadFilter $Yv12PlaneSwapFilter),
    (New-TargetSpec -Format 'YUV422P' -FileToken 'YUV422P' -PixelFormat 'yuv422p' -Extension 'yuv' `
        -SizeNumerator $TwoBytesPerPixel -SizeDenominator $OneBytePerPixel -OutputRange $LimitedRange),
    (New-TargetSpec -Format 'YUV444P' -FileToken 'YUV444P' -PixelFormat 'yuv444p' -Extension 'yuv' `
        -SizeNumerator $ThreeBytesPerPixel -SizeDenominator $OneBytePerPixel -OutputRange $LimitedRange),
    (New-TargetSpec -Format 'NV16' -FileToken 'NV16' -PixelFormat 'nv16' -Extension 'yuv' `
        -SizeNumerator $TwoBytesPerPixel -SizeDenominator $OneBytePerPixel -OutputRange $LimitedRange),
    (New-TargetSpec -Format 'YUY2' -FileToken 'YUY2' -PixelFormat 'yuyv422' -Extension 'yuv' `
        -SizeNumerator $TwoBytesPerPixel -SizeDenominator $OneBytePerPixel -OutputRange $LimitedRange),
    (New-TargetSpec -Format 'UYVY' -FileToken 'UYVY' -PixelFormat 'uyvy422' -Extension 'yuv' `
        -SizeNumerator $TwoBytesPerPixel -SizeDenominator $OneBytePerPixel -OutputRange $LimitedRange),
    (New-TargetSpec -Format 'P010' -FileToken 'P010' -PixelFormat 'p010le' -Extension 'yuv' `
        -SizeNumerator $ThreeBytesPerPixel -SizeDenominator $OneBytePerPixel -OutputRange $LimitedRange),
    (New-TargetSpec -Format 'RGBA8' -FileToken 'RGBA8' -PixelFormat 'rgba' -Extension 'raw' `
        -SizeNumerator $FourBytesPerPixel -SizeDenominator $OneBytePerPixel -OutputRange $FullRange),
    (New-TargetSpec -Format 'Gray8' -FileToken 'Gray8' -PixelFormat 'gray' -Extension 'raw' `
        -SizeNumerator $OneBytePerPixel -SizeDenominator $OneBytePerPixel -OutputRange $FullRange),
    (New-TargetSpec -Format 'Gray16' -FileToken 'Gray16' -PixelFormat 'gray16le' -Extension 'raw' `
        -SizeNumerator $TwoBytesPerPixel -SizeDenominator $OneBytePerPixel -OutputRange $FullRange)
)

$results = @()

foreach ($source in $sourceVariants) {
    Write-Host ''
    Write-Host "[$($source.Id)] $($source.Strategy)"

    $generatedRgbReference = Get-OutputPath -Source $source -Target $targetSpecs[0]

    foreach ($target in $targetSpecs) {
        $outputPath = Get-OutputPath -Source $source -Target $target
        $expectedBytes = Get-ExpectedOutputBytes `
            -Width $source.OutputWidth `
            -Height $source.OutputHeight `
            -Target $target

        $actualBytes = [long]0
        $sizeValid = $false
        $readbackValid = $false
        $psnrAverage = ''
        $psnrValid = $false
        $errorText = ''

        Write-Host ("  {0,-8} -> {1}" -f $target.Format, [System.IO.Path]::GetFileName($outputPath))

        try {
            $conversion = Convert-SourceToTarget `
                -Source $source `
                -Target $target `
                -OutputPath $outputPath

            if ($conversion.ExitCode -ne 0) {
                throw "FFmpeg conversion failed: $($conversion.Text)"
            }

            $actualBytes = (Get-Item -LiteralPath $outputPath).Length
            $sizeValid = ($actualBytes -eq $expectedBytes)

            if (-not $sizeValid) {
                throw "Output byte count mismatch. Expected $expectedBytes, found $actualBytes."
            }

            $readback = Test-RawReadback `
                -Target $target `
                -Path $outputPath `
                -Width $source.OutputWidth `
                -Height $source.OutputHeight
            $readbackValid = ($readback.ExitCode -eq 0)

            if (-not $readbackValid) {
                throw "FFmpeg readback failed: $($readback.Text)"
            }

            $referencePath = $source.ReferencePath
            $referenceKind = $source.ReferenceKind

            if ($referenceKind -eq 'RawRgb8') {
                $referencePath = $generatedRgbReference
            }

            if (-not (Test-Path -LiteralPath $referencePath -PathType Leaf)) {
                throw "PSNR reference does not exist: $referencePath"
            }

            $psnr = Measure-OutputPsnr `
                -Source $source `
                -Target $target `
                -OutputPath $outputPath `
                -ReferencePath $referencePath `
                -ReferenceKind $referenceKind

            $psnrAverage = $psnr.Average
            $minimumPsnrDb = if ($target.Format.StartsWith(
                    'Gray',
                    [System.StringComparison]::OrdinalIgnoreCase)) {
                $MinimumGrayscalePsnrDb
            }
            else {
                $MinimumColorPsnrDb
            }

            if ($psnr.ExitCode -eq 0 -and
                -not [string]::IsNullOrWhiteSpace($psnrAverage)) {
                if ($psnrAverage.Equals('inf', [System.StringComparison]::OrdinalIgnoreCase)) {
                    $psnrValid = $true
                }
                else {
                    $parsedPsnr = [double]::Parse(
                        $psnrAverage,
                        [System.Globalization.CultureInfo]::InvariantCulture)
                    $psnrValid = ($parsedPsnr -ge $minimumPsnrDb)
                }
            }

            if (-not $psnrValid) {
                throw "PSNR verification failed or fell below ${minimumPsnrDb} dB: $($psnr.Text)"
            }
        }
        catch {
            $errorText = $_.Exception.Message
            Write-Warning "$($source.Id)/$($target.Format): $errorText"
        }

        $results += [PSCustomObject][ordered]@{
            SourceId = $source.Id
            SourcePath = $source.Path
            SourceStrategy = $source.Strategy
            TargetFormat = $target.Format
            Width = $source.OutputWidth
            Height = $source.OutputHeight
            OutputPath = $outputPath
            ExpectedBytes = $expectedBytes
            ActualBytes = $actualBytes
            SizeValid = $sizeValid
            ReadbackValid = $readbackValid
            PsnrReference = if ($source.ReferenceKind -eq 'Image') {
                $source.ReferencePath
            }
            else {
                $generatedRgbReference
            }
            PsnrAverageDb = $psnrAverage
            MinimumPsnrDb = if ($target.Format.StartsWith(
                    'Gray',
                    [System.StringComparison]::OrdinalIgnoreCase)) {
                $MinimumGrayscalePsnrDb
            }
            else {
                $MinimumColorPsnrDb
            }
            PsnrValid = $psnrValid
            Error = $errorText
        }
    }
}

$csvPath = Join-Path $ResolvedOutputDirectory 'conversion-summary.csv'
$jsonPath = Join-Path $ResolvedOutputDirectory 'conversion-summary.json'

$results | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

$sourceSummary = @($sourceVariants | ForEach-Object {
    [PSCustomObject][ordered]@{
        Id = $_.Id
        Description = $_.Description
        Strategy = $_.Strategy
        SourcePath = $_.Path
        InputPixelFormat = $_.InputPixelFormat
        InputSize = "$($_.InputWidth)x$($_.InputHeight)"
        OutputSize = "$($_.OutputWidth)x$($_.OutputHeight)"
        Matrix = $_.Matrix
        Range = $_.InputRange
        ReferencePath = $_.ReferencePath
    }
})

$summary = [PSCustomObject][ordered]@{
    GeneratedAtUtc = [DateTime]::UtcNow.ToString('o')
    Ffmpeg = $ffmpegVersion
    InputDirectory = $ResolvedInputDirectory
    OutputDirectory = $ResolvedOutputDirectory
    Sources = $sourceSummary
    Results = $results
}

$summary |
    ConvertTo-Json -Depth $JsonSerializationDepth |
    Out-File -LiteralPath $jsonPath -Encoding utf8

$failedResults = @($results | Where-Object {
    -not $_.SizeValid -or -not $_.ReadbackValid -or -not $_.PsnrValid
})

Write-Host ''
Write-Host "CSV summary : $csvPath"
Write-Host "JSON summary: $jsonPath"
Write-Host "Validated   : $($results.Count - $failedResults.Count) / $($results.Count)"

if ($failedResults.Count -gt 0) {
    throw "$($failedResults.Count) conversion(s) failed validation. See the generated summaries."
}

Write-Host 'All conversions passed byte-count, FFmpeg readback, and reference/consistency PSNR thresholds.'
