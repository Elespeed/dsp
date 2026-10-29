param(
    [string]$Python = 'python'
)

$ErrorActionPreference = 'Stop'
$project = Join-Path (Split-Path $PSScriptRoot -Parent) 'VFWCapture vs2022_includeDll - 20221121'
$plugin = Join-Path $project 'Debug/Plugin'
$environment = Join-Path $plugin '.venv'
$interpreter = Join-Path $environment 'Scripts/python.exe'
$model = Join-Path $plugin 'face_landmarker.task'
$expectedHash = '64184E229B263107BC2B804C6625DB1341FF2BB731874B0BCC2FE6544E0BC9FF'
$modelUrl = 'https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/latest/face_landmarker.task'

New-Item -ItemType Directory -Force -Path $plugin | Out-Null
if (-not (Test-Path -LiteralPath $interpreter)) {
    & $Python -m venv $environment
    if ($LASTEXITCODE -ne 0) { throw 'Python 虚拟环境创建失败。' }
}
& $interpreter -m pip install 'mediapipe==0.10.35'
if ($LASTEXITCODE -ne 0) { throw 'MediaPipe 安装失败。' }

# 模型只放在本机 Debug 目录；校验后才交给 VS 插件使用。
$valid = (Test-Path -LiteralPath $model) -and
    ((Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash -eq $expectedHash)
if (-not $valid) {
    $download = Join-Path $plugin 'face_landmarker.task.download'
    Invoke-WebRequest -Uri $modelUrl -OutFile $download
    if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $expectedHash) {
        throw '下载的面部模型校验失败。'
    }
    Move-Item -LiteralPath $download -Destination $model -Force
}
Write-Host "Debug 面部关键点环境已就绪：$plugin"
