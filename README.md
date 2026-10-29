# DSP 人脸分割与定位

浙江大学《数字信号处理综合实验》课程工程。项目基于 Visual Studio 2022（C++/MFC）实现视频流中的肤色分割、人脸区域定位，并在课程框架的 `FaceLocator` 插件中迭代实现。

## 本次范围

- 基于 YUV 色度阈值的人脸肤色二值化；
- 形态学开闭运算和连通域分析；
- 输出人脸包围框及肤色像素统计；
- 尽量剔除脖子、肩膀等非面部肤色区域；
- 不实现眨眼检测、跟踪、疲劳判定与报警。

## 工程结构

- `VFWCapture vs2022_includeDll - 20221121/`：Visual Studio 2022 解决方案；
- `VFWCapture vs2022_includeDll - 20221121/PlugIns/FaceLocator/`：本项目主要实现位置；
- `doc/`：任务要求与实验资料。

## 开发环境

- Visual Studio Community 2026 18.11.1；
- MSVC 平台工具集 `v145`；
- Windows SDK `10.0.26100.0`；
- “使用 C++ 的桌面开发”工作负载及适用于 v145 的 C++ MFC 组件；
- 目标平台为 `Win32`，不使用 x64 配置。

## 打开、编译与运行

1. 打开 `VFWCAPTURE.sln`；
2. 选择 `Debug | Win32`，执行“生成解决方案”；
3. 将 `VFWCAPTURE` 设为启动项目；
4. 使用 `F5` 调试运行，或使用 `Ctrl+F5` 直接运行；
5. 程序启动后会从 `Debug/Plugin/` 加载插件，连接摄像头后再点击“开始”。

解决方案同时保留 `Release | Win32` 和 `Template | Win32` 配置，三个配置均统一使用 `v145` 和 Windows SDK `10.0.26100.0`；其中 `Template | Win32` 在解决方案层统一映射到经过验证的 Release 配置。

> 仓库保存源代码、工程文件和压缩后的迭代测试结果；不保存 `Debug/`、`.vs/`、`ipch/` 等本机生成内容。

## 离线图片与视频测试

单张无压缩 24 位 BMP 可用 `tools/face_probe_bmp.cpp` 检查；批量图片使用 `face_probe_bmp --batch 输入目录 输出目录`。工具导出基线和精修的 mask、画框图、像素统计与算法耗时。

视频使用 `tools/run_video_probe.ps1` 按每秒 2 帧抽样，结果保存在 `fig/iterations/改动名/`。每次改动使用新名字，已有目录不会被覆盖。首轮实拍复测及失败案例见 [参考算法借鉴与实拍视频复测](doc/参考算法借鉴与实拍视频复测.md)；运行命令见 [迭代结果说明](fig/iterations/README.md)。原始人像视频只在本机保留，不纳入 Git。

最新视频的 45 帧当前版复测及 43 帧去脖子实验见 [最新视频去脖子实验](doc/最新视频去脖子实验.md)。实验版发现误切下巴，因此插件仍使用当前精修算法。
