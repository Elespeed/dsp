# 人脸分割迭代结果

每次算法改动后，使用改动名作为唯一目录名运行 `tools/run_video_probe.ps1`。脚本拒绝覆盖已存在的目录，以便保留每轮结果。

每个目录保存同一段视频按 2 fps 抽取的全部帧，包括原画面、基线框、精修框、两种二值 mask、`results.csv` 和四张总览图。原始 MP4 不纳入 Git；结果图片使用压缩格式以控制仓库体积。

```powershell
$ff = 'C:\path\to\ffmpeg.exe'
& .\tools\run_video_probe.ps1 -Video '.\fig\WIN_20261029_21_03_35_Pro.mp4' -Iteration 'next-change-name' -Ffmpeg $ff
```

框的颜色：基线为青色，精修为紫色。CSV 中的框坐标属于 `clrBmp_1d8` 坐标系；测时仅覆盖分割算法，不含视频解码、BMP 读取和图片导出。
