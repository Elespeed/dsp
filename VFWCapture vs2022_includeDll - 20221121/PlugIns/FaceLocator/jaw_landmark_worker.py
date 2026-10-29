"""通过标准输入接收 320×240 RGB 帧，返回脸部外轮廓的 160×120 坐标。"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import mediapipe as mp
import numpy as np


INPUT_WIDTH = 320
INPUT_HEIGHT = 240
MASK_WIDTH = 160
MASK_HEIGHT = 120
FRAME_BYTES = INPUT_WIDTH * INPUT_HEIGHT * 3
FACE_OVAL = (
    10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288,
    397, 365, 379, 378, 400, 377, 152, 148, 176, 149, 150, 136,
    172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109,
)
RESPONSE = struct.Struct("<B74h")


def read_frame() -> bytes | None:
    chunks = bytearray()
    while len(chunks) < FRAME_BYTES:
        part = sys.stdin.buffer.read(FRAME_BYTES - len(chunks))
        if not part:
            return None
        chunks.extend(part)
    return bytes(chunks)


def main() -> None:
    model_path = Path(sys.argv[1])
    options = mp.tasks.vision.FaceLandmarkerOptions(
        base_options=mp.tasks.BaseOptions(model_asset_path=str(model_path)),
        running_mode=mp.tasks.vision.RunningMode.IMAGE,
        num_faces=1,
    )
    with mp.tasks.vision.FaceLandmarker.create_from_options(options) as detector:
        sys.stdout.buffer.write(b"R")
        sys.stdout.buffer.flush()
        while True:
            frame = read_frame()
            if frame is None:
                return
            image = np.frombuffer(frame, dtype=np.uint8).reshape(
                (INPUT_HEIGHT, INPUT_WIDTH, 3)
            ).copy()
            result = detector.detect(mp.Image(image_format=mp.ImageFormat.SRGB, data=image))
            if not result.face_landmarks:
                answer = RESPONSE.pack(0, *([0] * 74))
            else:
                points = result.face_landmarks[0]
                indices = (152,) + FACE_OVAL
                coordinates = [
                    value
                    for index in indices
                    for value in (
                        int(points[index].x * MASK_WIDTH),
                        int(points[index].y * MASK_HEIGHT),
                    )
                ]
                answer = RESPONSE.pack(1, *coordinates)
            sys.stdout.buffer.write(answer)
            sys.stdout.buffer.flush()


if __name__ == "__main__":
    main()
