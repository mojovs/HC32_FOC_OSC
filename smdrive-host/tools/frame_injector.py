#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""按《原工程》协议造流：生成 .bin 原始流，用于上位机回放/压测与自动化验证。

帧格式（见上位机逆向报告 §3/§4）：
    55 55 <CMD> [payload] AA AA
    CMD=0x02: 12B 序列号（3×u32 BE）             -> 17 字节
    CMD=0x04: 1710B 波形/数据 payload            -> 1715 字节

用法示例：
    python frame_injector.py --out test.bin --frames 1000
    python frame_injector.py --out noisy.bin --frames 1000 --corrupt 5 --noise 3
"""
from __future__ import annotations

import argparse
import math
import random
import struct

HEADER = b"\x55\x55"
FOOTER = b"\xaa\xaa"
RSP_AUTH = 0x02
RSP_DATA = 0x04

WAVE_FLOAT_N, WAVE_INT_N, WAVE_SAMPLES = 6, 4, 40
DATA_FLOAT_N, DATA_INT_N = 10, 10
INT_WAVE_TYPE = 0x06


def auth_frame(serial_words=(0x11223344, 0x55667788, 0x99AABBCC)) -> bytes:
    return HEADER + bytes([RSP_AUTH]) + struct.pack(">III", *serial_words) + FOOTER


def data_frame(n: int, rate_hz: float = 10000.0, seed: int = 0) -> bytes:
    """第 n 帧（每通道 40 点，全局样本号 = n*40 .. n*40+39）。"""
    body = bytearray()
    base = n * WAVE_SAMPLES

    for c in range(WAVE_FLOAT_N):
        body += bytes([c])                                  # 波形序号
        for s in range(WAVE_SAMPLES):
            t = (base + s) / rate_hz
            freq = 5.0 * (c + 1)
            body += struct.pack(">f", math.sin(2 * math.pi * freq * t) * (c + 1))

    for c in range(WAVE_INT_N):
        body += bytes([INT_WAVE_TYPE])                      # 类型 0x06 = int32
        for s in range(WAVE_SAMPLES):
            v = (base + s) * (c + 1) + seed
            body += struct.pack(">I", v & 0xFFFFFFFF)

    for i in range(DATA_FLOAT_N):
        body += bytes([i])
        body += struct.pack(">f", float(n) * 0.001 + i)

    for i in range(DATA_INT_N):
        body += bytes([i])
        body += struct.pack(">I", (n * 3 + i) & 0xFFFFFFFF)

    assert len(body) == 1710, len(body)
    return HEADER + bytes([RSP_DATA]) + bytes(body) + FOOTER


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="输出 .bin 路径")
    ap.add_argument("--frames", type=int, default=1000, help="数据帧数量（不含认证帧）")
    ap.add_argument("--rate", type=float, default=10000.0, help="采样率 Hz（仅影响波形内容）")
    ap.add_argument("--corrupt", type=int, default=0, help="故意损坏尾部的帧数（验证丢帧计数）")
    ap.add_argument("--noise", type=int, default=0, help="每帧前插入的随机噪声字节数")
    ap.add_argument("--seed", type=int, default=1234)
    args = ap.parse_args()

    rnd = random.Random(args.seed)
    corrupt_at = set(rnd.sample(range(args.frames), min(args.corrupt, args.frames))) if args.corrupt else set()

    with open(args.out, "wb") as fh:
        fh.write(auth_frame())
        for n in range(args.frames):
            f = bytearray(data_frame(n, args.rate, args.seed % 97))
            if n in corrupt_at:
                f[-1] = 0x00                                 # 破坏帧尾
            if args.noise:
                fh.write(bytes(rnd.randrange(1, 256) for _ in range(args.noise)))
            fh.write(f)

    size = 17 + args.frames * 1715 + args.noise * args.frames
    print(f"写出 {args.out}: 1 认证帧 + {args.frames} 数据帧（{args.corrupt} 帧破损）≈ {size} 字节")
    print(f"预期上位机: 解析 {args.frames - args.corrupt} 帧数据, 丢帧 {args.corrupt}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
