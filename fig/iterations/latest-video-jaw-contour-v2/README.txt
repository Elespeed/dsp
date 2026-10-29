迭代名称：latest-video-jaw-contour-v2
分支基线：codex/jaw-contour-evaluation，起点 f37d17a
输入：latest-video-neck-v4/ 的候选线与当前 mask，latest-video-neck-v5/ 的实验 mask；共 43 帧
方法：灰度边缘引导的平滑抛物下颌曲线；对照图左侧为 v5，右侧为本轮
candidate/：黄色下颌候选线；mask-jaw/：本轮二值 mask；comparison/：逐帧对照；contact_*.png：总览
结论：用户标注的第 001 帧真实下巴明显高于本轮黄色曲线；这条曲线误跟随领口，不能视为正确保留下巴。多帧有同类错误，未接入插件。详见 doc/下颌轮廓去脖子实验.md
