迭代名称：latest-video-jaw-landmark-v3
分支基线：codex/jaw-contour-evaluation，v5 起点 f37d17a
输入：latest-video-neck-v5/ 的原图、当前版肤色 mask 与 v5 mask；共 43 帧
方法：MediaPipe Face Landmarker 面部外轮廓与当前版肤色 mask 求交；检测失败或下巴点在当前框外时保留 v5
candidate/：绿色面部外轮廓、黄色下巴点；mask-jaw/：160×120 二值 mask
comparison/：左侧 v5，右侧 v3；contact_*.png：逐帧总览；results.csv：状态、下巴位置、mask 像素数和检测耗时
结果：43/43 帧检测到脸，43/43 帧 mask 面积小于 v5。仅为这段视频上的离线参照，不代表跨场景准确率；插件仍使用 v5。
复现脚本：tools/jaw_landmark_probe.py；依赖 mediapipe==0.10.35、Pillow、NumPy
模型来源：https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/latest/face_landmarker.task
本轮模型 SHA-256：64184E229B263107BC2B804C6625DB1341FF2BB731874B0BCC2FE6544E0BC9FF
详见 doc/下颌轮廓去脖子实验.md
