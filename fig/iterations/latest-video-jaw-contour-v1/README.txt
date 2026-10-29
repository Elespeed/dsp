迭代名称：latest-video-jaw-contour-v1
分支基线：codex/jaw-contour-evaluation，起点 f37d17a
输入：latest-video-neck-v4/ 的候选线与当前 mask，latest-video-neck-v5/ 的实验 mask；共 43 帧
方法：灰度边缘引导的逐列自由曲线，限制相邻列变化；对照图左侧为 v5，右侧为本轮
candidate/：黄色下颌候选线；mask-jaw/：本轮二值 mask；comparison/：逐帧对照；contact_*.png：总览
结论：多帧曲线跟随衣领形成锯齿或 W 形，未接入插件。详见 doc/下颌轮廓去脖子实验.md
