param(
    [Parameter(Mandatory = $true)][string]$Video,
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-z0-9][a-z0-9-]*$')][string]$Iteration,
    [string]$Ffmpeg = 'ffmpeg',
    [string]$ProbeExe = '',
    [string]$OutputRoot = 'fig/iterations'
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$videoPath = (Resolve-Path -LiteralPath $Video).Path
$outputBase = if ([IO.Path]::IsPathRooted($OutputRoot)) {
    [IO.Path]::GetFullPath($OutputRoot)
} else {
    [IO.Path]::GetFullPath((Join-Path $repo $OutputRoot))
}
$outputDir = Join-Path $outputBase $Iteration
if (Test-Path -LiteralPath $outputDir) {
    throw "迭代目录已存在，不覆盖历史结果：$outputDir"
}
$ffmpegPath = if (Test-Path -LiteralPath $Ffmpeg) {
    (Resolve-Path -LiteralPath $Ffmpeg).Path
} else {
    (Get-Command $Ffmpeg -ErrorAction Stop).Source
}

# 临时帧放在系统临时目录；最终目录只保存便于回溯的压缩结果。
$tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd([IO.Path]::DirectorySeparatorChar)
$tempDir = [IO.Path]::GetFullPath((Join-Path $tempRoot ("dsp-face-" + [guid]::NewGuid().ToString('N'))))
if (-not $tempDir.StartsWith($tempRoot + [IO.Path]::DirectorySeparatorChar,
                             [StringComparison]::OrdinalIgnoreCase)) {
    throw '临时目录路径检查失败'
}
New-Item -ItemType Directory -Path $tempDir | Out-Null
try {
    $frames = Join-Path $tempDir 'frames'
    $probe = Join-Path $tempDir 'probe'
    New-Item -ItemType Directory -Path $frames, $probe | Out-Null
    if (-not $ProbeExe) {
        $ProbeExe = Join-Path $tempDir 'face_probe_bmp.exe'
        $include = Join-Path $repo 'VFWCapture vs2022_includeDll - 20221121/PlugIns/FaceLocator'
        $source = Join-Path $repo 'tools/face_probe_bmp.cpp'
        & g++ -std=c++17 -O2 -I $include $source -o $ProbeExe
        if ($LASTEXITCODE -ne 0) { throw '离线工具编译失败' }
    }
    $probePath = (Resolve-Path -LiteralPath $ProbeExe).Path
    $filter = 'fps=2,scale=640:360:flags=bicubic,pad=640:480:0:60:black'
    & $ffmpegPath -hide_banner -loglevel error -i $videoPath -vf $filter -c:v bmp -pix_fmt bgr24 (Join-Path $frames 'frame_%03d.bmp')
    if ($LASTEXITCODE -ne 0) { throw '视频抽帧失败' }
    & $probePath --batch $frames $probe
    if ($LASTEXITCODE -ne 0) { throw '人脸检测批量测试失败' }

    New-Item -ItemType Directory -Path $outputDir | Out-Null
    foreach ($name in @('original', 'baseline', 'jaw', 'candidate', 'mask-baseline', 'mask-jaw')) {
        New-Item -ItemType Directory -Path (Join-Path $outputDir $name) | Out-Null
    }
    $jobs = @(
        @((Join-Path $frames 'frame_%03d.bmp'), (Join-Path $outputDir 'original/frame_%03d.jpg'), 'jpg'),
        @((Join-Path $probe 'frame_%03d_baseline.bmp'), (Join-Path $outputDir 'baseline/frame_%03d.jpg'), 'jpg'),
        @((Join-Path $probe 'frame_%03d_jaw.bmp'), (Join-Path $outputDir 'jaw/frame_%03d.jpg'), 'jpg'),
        @((Join-Path $probe 'frame_%03d_candidate.bmp'), (Join-Path $outputDir 'candidate/frame_%03d.jpg'), 'jpg'),
        @((Join-Path $probe 'frame_%03d_baseline.pgm'), (Join-Path $outputDir 'mask-baseline/frame_%03d.png'), 'png'),
        @((Join-Path $probe 'frame_%03d_jaw.pgm'), (Join-Path $outputDir 'mask-jaw/frame_%03d.png'), 'png')
    )
    foreach ($job in $jobs) {
        $extra = if ($job[2] -eq 'jpg') { @('-q:v', '4') } else { @() }
        & $ffmpegPath -hide_banner -loglevel error -framerate 2 -i $job[0] @extra $job[1]
        if ($LASTEXITCODE -ne 0) { throw "结果图片转换失败：$($job[1])" }
    }
    & $ffmpegPath -hide_banner -loglevel error -framerate 2 -i (Join-Path $outputDir 'jaw/frame_%03d.jpg') -vf 'scale=320:240,tile=4x4' -fps_mode vfr (Join-Path $outputDir 'contact_%02d.png')
    if ($LASTEXITCODE -ne 0) { throw '总览图生成失败' }
    Copy-Item -LiteralPath (Join-Path $probe 'results.csv') -Destination (Join-Path $outputDir 'results.csv')
    $manifest = @(
        "迭代名称：$Iteration",
        "源视频：$videoPath",
        "抽帧：每秒 2 帧，等比例缩放为 640x360，上下各补黑边 60 像素",
        "结果：原帧、基线框、新下颌裁剪框、候选曲线、两种 mask、CSV 和总览图",
        "代码版本：$(git -C $repo rev-parse --short HEAD)",
        "坐标：CSV 中的框使用 clrBmp_1d8 坐标系"
    )
    $manifest | Set-Content -LiteralPath (Join-Path $outputDir 'README.txt') -Encoding UTF8
    foreach ($path in @((Join-Path $outputDir 'README.txt'), (Join-Path $outputDir 'results.csv'))) {
        $contents = [IO.File]::ReadAllText($path)
        $crlf = [string][char]13 + [char]10
        [IO.File]::WriteAllText($path, $contents.Replace($crlf, [string][char]10),
                                (New-Object System.Text.UTF8Encoding($false)))
    }
    Write-Output "结果已保存：$outputDir"
} finally {
    # 删除前再次确认路径在系统临时目录内。
    $resolved = [IO.Path]::GetFullPath($tempDir)
    if ($resolved.StartsWith($tempRoot + [IO.Path]::DirectorySeparatorChar,
                             [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolved)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
