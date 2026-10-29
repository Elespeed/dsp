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
2. 选择 `Debug | Win32`，执行“生成解决方案”；`VFWCAPTURE` 已设置依赖 `FaceLocator`，后续修改分割源码再按 F5 时也会构建该插件；
3. 将 `VFWCAPTURE` 设为启动项目；
4. 使用 `F5` 调试运行，或使用 `Ctrl+F5` 直接运行；
5. 程序启动后会从 `Debug/Plugin/PB_FaceLocator.dll` 加载人脸定位插件，连接摄像头后再点击“开始”。若 VS 在解决方案打开期间提示文件已从外部修改，先重新加载解决方案，使构建依赖生效。重新构建插件前应先停止正在运行的程序，之后重新启动才能加载新 DLL。

解决方案同时保留 `Release | Win32` 和 `Template | Win32` 配置，三个配置均统一使用 `v145` 和 Windows SDK `10.0.26100.0`；其中 `Template | Win32` 在解决方案层统一映射到经过验证的 Release 配置。

> 仓库保存源代码、工程文件和压缩后的迭代测试结果；不保存 `Debug/`、`.vs/`、`ipch/` 等本机生成内容。

## 离线图片与视频测试

单张无压缩 24 位 BMP 可用 `tools/face_probe_bmp.cpp` 检查；批量图片使用 `face_probe_bmp --batch 输入目录 输出目录`。工具导出基线和精修的 mask、画框图、像素统计与算法耗时。

视频使用 `tools/run_video_probe.ps1` 按每秒 2 帧抽样，结果保存在 `fig/iterations/改动名/`。每次改动使用新名字，已有目录不会被覆盖。首轮实拍复测及失败案例见 [参考算法借鉴与实拍视频复测](doc/参考算法借鉴与实拍视频复测.md)；运行命令见 [迭代结果说明](fig/iterations/README.md)。原始人像视频只在本机保留，不纳入 Git。

最新视频的 45 帧当前版复测及 43 帧去脖子实验见 [最新视频去脖子实验](doc/最新视频去脖子实验.md)。v1/v2 的纯几何裁剪曾误切下巴；**v4 黄色下巴弧线试用版**保存在分支 `codex/neck-removal-evaluation` 的提交 **`0bb020b`**，可作为回退检查点。当前分支已在 v4 基础上恢复 v5 行宽突变修正；仍需用真实摄像头验证不同光线和姿态。

### v4 预览文件

`fig/iterations/latest-video-current/refined/` 是改动前的框图，保留作对照。`fig/iterations/latest-video-neck-v4/` 中各文件的含义如下：

| 路径 | 内容 |
| --- | --- |
| `original/`、`current/`、`neck/` | 同一入选帧的原图、改动前框图、v4 框图 |
| `mask-current/`、`mask-neck/` | 改动前与 v4 的二值肤色区域 |
| `candidate/` | 原图上黄色下巴候选弧线；没有可靠候选时不画线 |
| `comparison/`、`contact_*.png` | 单帧左右对照及整批总览；左为改动前，右为 v4 |
| `results.csv`、`selection.csv`、`README.txt` | 框和像素统计、入选理由、该轮参数与来源说明 |

Release 配置的插件构建产物位于 `PlugIns/FaceLocator/Release/FaceLocator.dll`；若用 Release 程序试验，需将新构建的文件复制为 `Release/Plugin/PB_FaceLocator.dll`。本机此前的 Release DLL 是 v4，不能代表当前 v5 源码；生成的 DLL 不纳入 Git。

### v5 实验预览

v5 的 43 帧预览和逐帧数据另存于 `fig/iterations/latest-video-neck-v5/`，预览提交为 **`a6d3435`**。目录结构与 v4 相同；其中 `candidate/` 是黄色下巴候选弧线，`comparison/` 和 `contact_*.png` 是改动前与 v5 的对照。v5 在 v4 上加入行宽突变修正，裁剪行仅在 001、002、026、029 四帧变化。预览提交当时未保存临时源码，现已重新加入 `FaceSegmentation.h`；用最新视频的 43 个入选帧核对，裁剪框、像素数与预览数据全部一致。`Debug | Win32` 插件已在本机重新构建，并通过插件接口测试。
