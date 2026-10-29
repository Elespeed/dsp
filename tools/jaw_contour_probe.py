"""在已入选的视频帧上试验灰度边缘引导的下颌曲线，不修改插件。"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def face_interval(candidate_path: Path) -> tuple[int, int] | None:
    # 旧版黄色候选线记录了插件内部估计的面部左右范围。
    rgb = np.asarray(Image.open(candidate_path).convert("RGB"))
    yellow = (rgb[:, :, 0] > 170) & (rgb[:, :, 1] > 170) & (rgb[:, :, 2] < 110)
    _, xs = np.nonzero(yellow)
    if len(xs) < 24:
        return None
    return int(xs.min() // 4), int(xs.max() // 4)


def edge_strength(gray: np.ndarray, x: int, y: int) -> float:
    # 比较候选轮廓上下的亮度；下方较暗时形成较强的下颌阴影边缘。
    px, py = x * 4 + 2, y * 4
    if py < 7 or py + 7 >= gray.shape[0]:
        return 0.0
    above = gray[py - 6 : py - 1, px - 2 : px + 2].mean()
    below = gray[py + 2 : py + 7, px - 2 : px + 2].mean()
    return float(np.clip(above - below, 0, 24))


def trace_curve(
    original: Image.Image, mask: np.ndarray, left: int, right: int,
    cut_row: int, curve_rise: int,
) -> tuple[np.ndarray, float, float]:
    gray = np.asarray(original.convert("L").filter(ImageFilter.GaussianBlur(1.5)), dtype=float)
    xs = np.arange(left, right + 1)
    center = (left + right) / 2
    half = max(1.0, (right - left) / 2)
    prior = cut_row - curve_rise * ((xs - center) / half) ** 2
    states: list[np.ndarray] = []
    edges: list[np.ndarray] = []
    for x, expected in zip(xs, prior):
        low = max(1, int(round(expected)) - 5)
        high = min(mask.shape[0] - 1, int(round(expected)) + 5)
        choices = np.arange(low, high + 1)
        states.append(choices)
        edges.append(np.array([edge_strength(gray, int(x), int(y)) for y in choices]))

    costs = 0.35 * (states[0] - prior[0]) ** 2 - 0.6 * edges[0]
    parents: list[np.ndarray] = []
    for i in range(1, len(xs)):
        expected_step = prior[i] - prior[i - 1]
        step = states[i][:, None] - states[i - 1][None, :]
        transition = 1.3 * (step - expected_step) ** 2
        transition[np.abs(step) > 2] = 1e6
        options = costs[None, :] + transition
        parent = np.argmin(options, axis=1)
        costs = (options[np.arange(len(states[i])), parent] +
                 0.35 * (states[i] - prior[i]) ** 2 - 0.6 * edges[i])
        parents.append(parent)

    index = int(np.argmin(costs))
    path = np.empty(len(xs), dtype=int)
    path[-1] = states[-1][index]
    for i in range(len(xs) - 2, -1, -1):
        index = int(parents[i][index])
        path[i] = states[i][index]
    chosen_edge = float(np.mean([edge_strength(gray, int(x), int(y))
                                 for x, y in zip(xs, path)]))
    prior_edge = float(np.mean([edge_strength(gray, int(x), int(round(y)))
                                for x, y in zip(xs, prior)]))
    return path, chosen_edge, prior_edge


def trace_parabola(
    original: Image.Image, left: int, right: int, cut_row: int, curve_rise: int,
) -> tuple[np.ndarray, float, float]:
    # 固定为单峰下颌弧线，避免自由路径沿着衬衫领口形成锯齿或 W 形。
    gray = np.asarray(original.convert("L").filter(ImageFilter.GaussianBlur(1.5)), dtype=float)
    xs = np.arange(left, right + 1)
    center = (left + right) / 2
    half = max(1.0, (right - left) / 2)
    shape = ((xs - center) / half) ** 2
    prior = np.rint(cut_row - curve_rise * shape).astype(int)

    def strength(path: np.ndarray) -> float:
        return float(np.mean([edge_strength(gray, int(x), int(y))
                              for x, y in zip(xs, path)]))

    prior_edge = strength(prior)
    best = (prior_edge, prior, prior_edge)
    for center_row in range(cut_row - 5, cut_row + 3):
        for rise in range(max(2, curve_rise - 3), curve_rise + 5):
            path = np.rint(center_row - rise * shape).astype(int)
            raw = strength(path)
            score = raw - 0.8 * abs(center_row - cut_row) - 0.3 * abs(rise - curve_rise)
            if score > best[0]:
                best = (score, path, raw)
    return best[1], best[2], prior_edge


def refine_mask(mask: np.ndarray, left: int, right: int, path: np.ndarray) -> np.ndarray:
    result = mask.copy()
    result[:, :left] = 0
    result[:, right + 1 :] = 0
    for x, y in zip(range(left, right + 1), path):
        result[y:, x] = 0
    return result


def overlay_mask(image: Image.Image, mask: np.ndarray, title: str) -> Image.Image:
    up = Image.fromarray(mask).resize(image.size, Image.Resampling.NEAREST)
    pixels = np.asarray(image).copy()
    active = np.asarray(up) > 0
    pixels[active] = (pixels[active] * 0.58 + np.array([230, 30, 30]) * 0.42).astype(np.uint8)
    out = Image.fromarray(pixels)
    ImageDraw.Draw(out).text((12, 68), title, fill="yellow")
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--iteration", default="latest-video-jaw-contour-v1")
    parser.add_argument("--method", choices=("free", "parabola"), default="free")
    args = parser.parse_args()
    root = args.root.resolve()
    v4 = root / "fig/iterations/latest-video-neck-v4"
    v5 = root / "fig/iterations/latest-video-neck-v5"
    output = root / "fig/iterations" / args.iteration
    if output.exists():
        raise SystemExit(f"迭代目录已存在，不覆盖：{output}")
    for part in ("mask-jaw", "candidate", "comparison"):
        (output / part).mkdir(parents=True, exist_ok=True)

    records = []
    previews = []
    for record in load_rows(v4 / "results.csv"):
        name = Path(record["file"]).stem
        original = Image.open(v4 / "original" / f"{name}.jpg").convert("RGB")
        current = np.asarray(Image.open(v4 / "mask-current" / f"{name}.png").convert("L"))
        old = np.asarray(Image.open(v5 / "mask-neck" / f"{name}.png").convert("L"))
        bounds = face_interval(v4 / "candidate" / f"{name}.jpg")
        cut = int(record["neck_cut_row"])
        if bounds is None or cut < 0:
            result, path, reason = old.copy(), None, "no-v4-candidate"
            edge, prior_edge = 0.0, 0.0
        else:
            left, right = bounds
            if args.method == "free":
                path, edge, prior_edge = trace_curve(
                    original, current, left, right, cut, int(record["neck_curve_rise"]))
            else:
                path, edge, prior_edge = trace_parabola(
                    original, left, right, cut, int(record["neck_curve_rise"]))
            result = refine_mask(current, left, right, path)
            reason = f"{args.method}-edge-traced"

        Image.fromarray(result).save(output / "mask-jaw" / f"{name}.png")
        candidate = original.copy()
        if path is not None:
            xy = [((x * 4 + 2), (int(y) * 4)) for x, y in zip(range(left, right + 1), path)]
            ImageDraw.Draw(candidate).line(xy, fill=(255, 220, 0), width=3)
        candidate.save(output / "candidate" / f"{name}.jpg", quality=90)
        before = overlay_mask(original, old, "v5 mask")
        after = overlay_mask(candidate, result, "jaw edge mask")
        pair = Image.new("RGB", (640, 240), "black")
        pair.paste(before.resize((320, 240)), (0, 0))
        pair.paste(after.resize((320, 240)), (320, 0))
        pair.save(output / "comparison" / f"{name}.jpg", quality=88)
        previews.append(pair)
        records.append({
            "frame": name, "reason": reason,
            "v5_pixels": int(np.count_nonzero(old)),
            "jaw_pixels": int(np.count_nonzero(result)),
            "edge_score": f"{edge:.2f}", "prior_edge_score": f"{prior_edge:.2f}",
            "center_row": int(path[len(path) // 2]) if path is not None else "",
            "v4_cut_row": cut,
        })

    with (output / "results.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)
    for start in range(0, len(previews), 9):
        page = Image.new("RGB", (1920, 720), "black")
        for i, preview in enumerate(previews[start : start + 9]):
            page.paste(preview, ((i % 3) * 640, (i // 3) * 240))
        page.save(output / f"contact_{start // 9 + 1:02}.png")
    print(f"processed={len(records)} output={output}")


if __name__ == "__main__":
    main()
