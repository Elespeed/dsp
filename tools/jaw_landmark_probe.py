"""用面部关键点离线检查下颌轮廓，输出与 v5 的逐帧 mask 对照。"""

from __future__ import annotations

import argparse
import csv
import time
from pathlib import Path

import mediapipe as mp
import numpy as np
from PIL import Image, ImageDraw


# MediaPipe 面部椭圆的顺时针点序；第 152 点是下巴尖端。
FACE_OVAL = [
    10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288,
    397, 365, 379, 378, 400, 377, 152, 148, 176, 149, 150, 136,
    172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109,
]


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def overlay_mask(original: Image.Image, mask: np.ndarray, title: str) -> Image.Image:
    expanded = Image.fromarray(mask).resize(original.size, Image.Resampling.NEAREST)
    rgb = np.asarray(original).copy()
    active = np.asarray(expanded) != 0
    rgb[active] = (rgb[active] * 0.58 + np.array([230, 30, 30]) * 0.42).astype(np.uint8)
    result = Image.fromarray(rgb)
    ImageDraw.Draw(result).text((12, 68), title, fill="yellow")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--iteration", default="latest-video-jaw-landmark-v3")
    args = parser.parse_args()
    root = args.root.resolve()
    source = root / "fig/iterations/latest-video-neck-v5"
    output = root / "fig/iterations" / args.iteration
    if not args.model.is_file():
        raise SystemExit(f"模型文件不存在：{args.model}")
    if output.exists():
        raise SystemExit(f"迭代目录已存在，不覆盖：{output}")
    for part in ("mask-jaw", "candidate", "comparison"):
        (output / part).mkdir(parents=True, exist_ok=True)

    options = mp.tasks.vision.FaceLandmarkerOptions(
        base_options=mp.tasks.BaseOptions(model_asset_path=str(args.model.resolve())),
        running_mode=mp.tasks.vision.RunningMode.IMAGE,
        num_faces=1,
    )
    records: list[dict[str, str | int | float]] = []
    previews: list[Image.Image] = []
    with mp.tasks.vision.FaceLandmarker.create_from_options(options) as detector:
        for row in load_rows(source / "results.csv"):
            name = Path(row["file"]).stem
            original_path = source / "original" / f"{name}.jpg"
            original = Image.open(original_path).convert("RGB")
            current = np.asarray(Image.open(source / "mask-current" / f"{name}.png").convert("L"))
            old = np.asarray(Image.open(source / "mask-neck" / f"{name}.png").convert("L"))

            started = time.perf_counter()
            detected = detector.detect(mp.Image.create_from_file(str(original_path)))
            elapsed_ms = (time.perf_counter() - started) * 1000
            chin_x: float | str = ""
            chin_y: float | str = ""
            status = "no-face"
            mask = old.copy()
            candidate = original.copy()
            if detected.face_landmarks:
                landmarks = detected.face_landmarks[0]
                chin_x = landmarks[152].x * original.width
                chin_y = landmarks[152].y * original.height
                box_left = int(row["refined_left"]) * 2
                box_right = box_left + int(row["refined_width"]) * 2
                box_top = int(row["refined_top"]) * 4
                box_bottom = box_top + int(row["refined_height"]) * 4
                if box_left <= chin_x <= box_right and box_top <= chin_y <= box_bottom:
                    # 把原图关键点映射到 160×120 的课程二值 mask，保留脸部椭圆内的肤色。
                    jaw = [(int(landmarks[i].x * original.width),
                            int(landmarks[i].y * original.height)) for i in FACE_OVAL]
                    polygon = [(int(x * current.shape[1] / original.width),
                                int(y * current.shape[0] / original.height)) for x, y in jaw]
                    oval = Image.new("L", (current.shape[1], current.shape[0]))
                    ImageDraw.Draw(oval).polygon(polygon, fill=255)
                    mask = np.where(np.asarray(oval) != 0, current, 0).astype(np.uint8)
                    pen = ImageDraw.Draw(candidate)
                    pen.line(jaw + [jaw[0]], fill=(0, 255, 0), width=3)
                    pen.ellipse((chin_x - 4, chin_y - 4, chin_x + 4, chin_y + 4),
                                fill=(255, 255, 0))
                    status = "face-oval"
                else:
                    status = "chin-outside-current-box"

            Image.fromarray(mask).save(output / "mask-jaw" / f"{name}.png")
            candidate.save(output / "candidate" / f"{name}.jpg", quality=90)
            before = overlay_mask(original, old, "v5 mask")
            after = overlay_mask(candidate, mask, "jaw landmark mask")
            pair = Image.new("RGB", (640, 240), "black")
            pair.paste(before.resize((320, 240)), (0, 0))
            pair.paste(after.resize((320, 240)), (320, 0))
            pair.save(output / "comparison" / f"{name}.jpg", quality=88)
            previews.append(pair)
            records.append({
                "frame": name,
                "status": status,
                "chin_x": f"{chin_x:.1f}" if isinstance(chin_x, float) else "",
                "chin_y": f"{chin_y:.1f}" if isinstance(chin_y, float) else "",
                "v5_pixels": int(np.count_nonzero(old)),
                "jaw_pixels": int(np.count_nonzero(mask)),
                "detect_ms": f"{elapsed_ms:.1f}",
            })

    with (output / "results.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(records)
    for start in range(0, len(previews), 9):
        page = Image.new("RGB", (1920, 720), "black")
        for index, preview in enumerate(previews[start : start + 9]):
            page.paste(preview, ((index % 3) * 640, (index // 3) * 240))
        page.save(output / f"contact_{start // 9 + 1:02}.png")
    print(f"processed={len(records)} detected={sum(r['status'] == 'face-oval' for r in records)} output={output}")


if __name__ == "__main__":
    main()
