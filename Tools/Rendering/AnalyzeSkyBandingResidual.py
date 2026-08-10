#!/usr/bin/env python3
"""Generate repeatable sky-banding residual evidence from an ABTS screenshot."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def gaussian_blur_float(image: np.ndarray, sigma: float) -> np.ndarray:
    """Separable reflect-padded Gaussian blur without an 8-bit round trip."""
    if sigma <= 0.0:
        return image.copy()
    radius = max(1, int(np.ceil(3.0 * sigma)))
    coordinates = np.arange(-radius, radius + 1, dtype=np.float32)
    kernel = np.exp(-(coordinates * coordinates) / (2.0 * sigma * sigma))
    kernel /= np.sum(kernel)

    horizontal = np.pad(image, ((0, 0), (radius, radius)), mode="reflect")
    horizontal = np.apply_along_axis(
        lambda row: np.convolve(row, kernel, mode="valid"),
        axis=1,
        arr=horizontal,
    )
    vertical = np.pad(horizontal, ((radius, radius), (0, 0)), mode="reflect")
    return np.apply_along_axis(
        lambda column: np.convolve(column, kernel, mode="valid"),
        axis=0,
        arr=vertical,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--crop-width", type=int, default=1450)
    parser.add_argument("--crop-height", type=int, default=520)
    parser.add_argument("--sigma", type=float, default=32.0)
    parser.add_argument("--gain", type=float, default=16.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    image = Image.open(args.image).convert("RGB")
    crop = image.crop(
        (
            0,
            0,
            min(args.crop_width, image.width),
            min(args.crop_height, image.height),
        )
    )
    rgb = np.asarray(crop, dtype=np.float32) / 255.0
    luminance = (
        0.2126 * rgb[:, :, 0]
        + 0.7152 * rgb[:, :, 1]
        + 0.0722 * rgb[:, :, 2]
    )
    low_frequency = gaussian_blur_float(luminance, args.sigma)
    residual = luminance - low_frequency
    residual_visual = np.uint8(
        np.clip(128.0 + residual * 255.0 * args.gain, 0.0, 255.0)
    )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    stem = args.image.stem
    crop.save(args.output_dir / f"{stem}_SkyCrop.png")
    Image.fromarray(
        np.uint8(np.clip(low_frequency * 255.0 + 0.5, 0.0, 255.0)),
        "L",
    ).save(
        args.output_dir / f"{stem}_LowFrequency.png"
    )
    residual_path = args.output_dir / f"{stem}_HighFrequencyResidual.png"
    Image.fromarray(residual_visual, "L").save(residual_path)

    sky = np.asarray(crop)[: min(360, crop.height), : min(900, crop.width)]
    flat_x = np.mean(np.all(sky[:, 1:] == sky[:, :-1], axis=2)) * 100.0
    flat_y = np.mean(np.all(sky[1:] == sky[:-1], axis=2)) * 100.0
    unique_rgb = np.unique(sky.reshape(-1, 3), axis=0).shape[0]
    print(f"image={args.image}")
    print(f"residual={residual_path}")
    print(f"flatX={flat_x:.3f}% flatY={flat_y:.3f}% uniqueRGB={unique_rgb}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
