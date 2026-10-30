# 人脸分割迭代结果

每次算法改动后，使用改动名作为唯一目录名运行 `tools/run_video_probe.ps1`。脚本拒绝覆盖已存在的目录，以便保留每轮结果。

每个目录保存同一段视频按 2 fps 抽取的全部帧，包括原画面、基线框、新下颌裁剪框、两种二值 mask、候选曲线、`results.csv` 和总览图。原始 MP4 不纳入 Git；结果图片使用压缩格式以控制仓库体积。

```powershell
$ff = 'C:\path\to\ffmpeg.exe'
& .\tools\run_video_probe.ps1 -Video '.\fig\WIN_20261029_21_03_35_Pro.mp4' -Iteration 'next-change-name' -Ffmpeg $ff
```

框的颜色：基线为青色，新裁剪为紫色。CSV 中的框坐标属于 `clrBmp_1d8` 坐标系；`jaw_us` 包含课程基线和新裁剪，不含视频解码、BMP 读取、YUV 准备和图片导出。

当前分支的 43 帧选样复测使用 `tools/run_jaw_probe.ps1`，结果位于 `latest-video-jaw/`。该目录的 `comparison/` 左右分别是课程基线与新方法，`candidate/` 中的黄色线是裁剪曲线；`jaw_found=0` 时保留本帧基线。

最新视频的当前版全量结果保存在 `latest-video-current/`；其中 `selection.csv` 记录逐帧入选与排除原因。去脖子实验使用 `tools/run_neck_probe.ps1`，仅重放入选帧，分别导出当前版与实验版的 mask、框和对照图。实验结论见 [最新视频去脖子实验](../../doc/最新视频去脖子实验.md)。

选定试用的 v4 结果位于 `latest-video-neck-v4/`，对应算法与预览提交 `0bb020b`。`candidate/` 中黄色曲线是下巴裁剪候选，`comparison/` 左右分别为改动前和 v4；目录内的 `results.csv` 与 `selection.csv` 可追溯逐帧数据和样本选择。

v5 实验预览位于 `latest-video-neck-v5/`，预览提交为 `a6d3435`。文件结构与 v4 相同；仅 001、002、026、029 四帧的裁剪行变化。预览提交当时未保存临时源码；当前分支已重新加入 v5 行宽突变修正，43 帧的裁剪框与像素数均与此目录的数据一致。Debug 插件已重新构建，摄像头效果仍待实测。
