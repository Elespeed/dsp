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

## 打开与编译

1. 安装 Visual Studio 2022 的“使用 C++ 的桌面开发”工作负载，以及 C++ MFC、Windows SDK；
2. 打开 `VFWCAPTURE.sln`；
3. 选择 `Debug | Win32`，生成解决方案；
4. 以 `VFWCAPTURE` 作为启动项目运行。

> 仓库只保存可复现的源代码和工程文件，不保存 `Debug/`、`.vs/`、`ipch/` 等本机生成内容。
