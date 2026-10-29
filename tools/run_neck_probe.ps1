param(
    [string]$Video = 'fig/最新测试.mp4',
    [string]$Selection = 'fig/iterations/latest-video-current/selection.csv',
    [ValidatePattern('^[a-z0-9][a-z0-9-]*$')][string]$Iteration = 'latest-video-neck',
    [string]$Ffmpeg = 'ffmpeg',
    [string]$OutputRoot = 'fig/iterations'
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$videoPath = (Resolve-Path -LiteralPath $Video).Path
$selectionPath = (Resolve-Path -LiteralPath $Selection).Path
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
$selected = @(Import-Csv -LiteralPath $selectionPath | Where-Object status -eq 'include' | ForEach-Object frame)
if ($selected.Count -eq 0 -or $selected.Count -ne (@($selected | Select-Object -Unique)).Count) {
    throw '入选帧列表为空或包含重复编号'
}

$tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd([IO.Path]::DirectorySeparatorChar)
$tempDir = Join-Path $tempRoot ("dsp-neck-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempDir | Out-Null
try {
    $frames = Join-Path $tempDir 'frames'
    $chosen = Join-Path $tempDir 'chosen'
    $probe = Join-Path $tempDir 'probe'
    $pairs = Join-Path $tempDir 'pairs'
    New-Item -ItemType Directory -Path $frames,$chosen,$probe,$pairs | Out-Null
    $include = Join-Path $repo 'VFWCapture vs2022_includeDll - 20221121/PlugIns/FaceLocator'
    $source = Join-Path $repo 'tools/face_probe_bmp.cpp'
    $probeExe = Join-Path $tempDir 'face_probe_bmp.exe'
    & g++ -std=c++17 -O2 -I $include $source -o $probeExe
    if ($LASTEXITCODE -ne 0) { throw '离线工具编译失败' }

    # 沿用当前版视频实验的抽样与缩放参数，保证逐帧可比。
    $filter = 'fps=2,scale=640:360:flags=bicubic,pad=640:480:0:60:black'
    & $ffmpegPath -hide_banner -loglevel error -i $videoPath -vf $filter -c:v bmp -pix_fmt bgr24 (Join-Path $frames 'frame_%03d.bmp')
    if ($LASTEXITCODE -ne 0) { throw '视频抽帧失败' }
    foreach ($number in $selected) {
        $name = "frame_$number"
        $sourceFrame = Join-Path $frames "$name.bmp"
        if (-not (Test-Path -LiteralPath $sourceFrame)) { throw "入选帧不存在：$name" }
        Copy-Item -LiteralPath $sourceFrame -Destination (Join-Path $chosen "$name.bmp")
    }
    & $probeExe --batch $chosen $probe
    if ($LASTEXITCODE -ne 0) { throw '人脸分割批量测试失败' }

    New-Item -ItemType Directory -Path $outputDir | Out-Null
    foreach ($dir in @('original','current','neck','mask-current','mask-neck','comparison')) {
        New-Item -ItemType Directory -Path (Join-Path $outputDir $dir) | Out-Null
    }
    for ($i = 0; $i -lt $selected.Count; ++$i) {
        $name = "frame_$($selected[$i])"
        $items = @(
            @((Join-Path $chosen "$name.bmp"), (Join-Path $outputDir "original/$name.jpg")),
            @((Join-Path $probe "${name}_refined.bmp"), (Join-Path $outputDir "current/$name.jpg")),
            @((Join-Path $probe "${name}_neck.bmp"), (Join-Path $outputDir "neck/$name.jpg")),
            @((Join-Path $probe "${name}_refined.pgm"), (Join-Path $outputDir "mask-current/$name.png")),
            @((Join-Path $probe "${name}_neck.pgm"), (Join-Path $outputDir "mask-neck/$name.png"))
        )
        foreach ($item in $items) {
            & $ffmpegPath -hide_banner -loglevel error -i $item[0] -frames:v 1 $item[1]
            if ($LASTEXITCODE -ne 0) { throw "图片转换失败：$name" }
        }
        & $ffmpegPath -hide_banner -loglevel error -i (Join-Path $probe "${name}_refined.bmp") -i (Join-Path $probe "${name}_neck.bmp") -filter_complex '[0:v]scale=320:240[a];[1:v]scale=320:240[b];[a][b]hstack=inputs=2' -frames:v 1 (Join-Path $outputDir "comparison/$name.jpg")
        if ($LASTEXITCODE -ne 0) { throw "对照图生成失败：$name" }
        Copy-Item -LiteralPath (Join-Path $outputDir "comparison/$name.jpg") -Destination (Join-Path $pairs ('pair_{0:D3}.jpg' -f ($i + 1)))
    }
    & $ffmpegPath -hide_banner -loglevel error -framerate 2 -i (Join-Path $pairs 'pair_%03d.jpg') -vf 'tile=3x3' -fps_mode vfr (Join-Path $outputDir 'contact_%02d.png')
    if ($LASTEXITCODE -ne 0) { throw '总览图生成失败' }
    Copy-Item -LiteralPath (Join-Path $probe 'results.csv') -Destination (Join-Path $outputDir 'results.csv')
    Copy-Item -LiteralPath $selectionPath -Destination (Join-Path $outputDir 'selection.csv')
    @(
        "迭代名称：$Iteration",
        "源视频：$videoPath",
        "入选帧数：$($selected.Count)",
        "抽帧：每秒 2 帧，640x360 后上下补边至 640x480",
        '对照图左侧为当前版，右侧为去脖子实验版',
        "代码基线：$(git -C $repo rev-parse --short HEAD)；实验函数为当前工作树版本"
    ) | Set-Content -LiteralPath (Join-Path $outputDir 'README.txt') -Encoding UTF8
    foreach ($name in @('README.txt','results.csv','selection.csv')) {
        $path = Join-Path $outputDir $name
        $contents = [IO.File]::ReadAllText($path)
        [IO.File]::WriteAllText($path, $contents.Replace("`r`n", "`n"),
                                (New-Object System.Text.UTF8Encoding($false)))
    }
    Write-Output "结果已保存：$outputDir"
} finally {
    # 仅清理本次创建且仍位于系统临时目录内的文件。
    $resolved = [IO.Path]::GetFullPath($tempDir)
    if ($resolved.StartsWith($tempRoot + [IO.Path]::DirectorySeparatorChar,
                             [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolved)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
