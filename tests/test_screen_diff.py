"""Tests for tools/screen_diff.py — Hatari-screenshot vs offline-render diffing.

The load-bearing part is `q565`.  A Hatari screenshot is NOT 8:8:8 truth: the
SDL surface is RGB565, so every colour comes back bit-replicated.  Comparing a
faithful render against a raw 8-bit reference therefore shows ~1% RMSE and
"142056 pixels differ" — which reads exactly like a palette bug and is not one.

These tests pin the expansion so nobody re-derives it (or re-chases it).
"""
import os

import pytest

from screen_diff import q565, read_ppm, quantize_ppm, write_ppm


def test_q565_matches_the_values_observed_on_screen():
	"""Real pairs measured off a Hatari shot of BIGPIC set 6 (WALKING FOREST)."""
	# offline 8-bit truth  ->  what Hatari's framebuffer hands back
	assert q565(0x00, 0x83, 0x1F) == (0x00, 0x82, 0x18)
	assert q565(0x00, 0x8B, 0x47) == (0x00, 0x8A, 0x42)
	assert q565(0x00, 0xA3, 0x87) == (0x00, 0xA2, 0x84)


def test_q565_is_bit_replication_not_truncation():
	"""0x1F -> 0x18, NOT 0x00.  Truncating instead of replicating is the trap."""
	assert q565(0xFF, 0xFF, 0xFF) == (0xFF, 0xFF, 0xFF)   # white stays white
	assert q565(0x00, 0x00, 0x00) == (0x00, 0x00, 0x00)
	assert q565(0x1F, 0x1F, 0x1F) == (0x18, 0x1C, 0x18)


def test_q565_is_idempotent():
	"""Quantizing an already-quantized colour must not drift it further."""
	for c in ((0x83, 0x1F, 0x47), (0x12, 0x34, 0x56), (0xFE, 0xDC, 0xBA)):
		once = q565(*c)
		assert q565(*once) == once


def test_q565_green_keeps_more_precision_than_red_and_blue():
	"""565: green has 6 bits, red/blue 5.  Green resolves steps they can't."""
	assert q565(0, 0x04, 0) != q565(0, 0x00, 0)     # 6-bit green sees this
	assert q565(0x04, 0, 0) == q565(0x00, 0, 0)     # 5-bit red does not


def test_ppm_round_trip(tmp_path):
	p = str(tmp_path / "a.ppm")
	pix = bytes(range(3 * 6))
	write_ppm(p, 3, 2, pix)
	assert read_ppm(p) == (3, 2, pix)


def test_quantize_ppm_writes_a_565_reference(tmp_path):
	src, dst = str(tmp_path / "s.ppm"), str(tmp_path / "d.ppm")
	write_ppm(src, 2, 1, bytes([0x00, 0x83, 0x1F, 0x00, 0x8B, 0x47]))
	assert quantize_ppm(src, dst) == (2, 1)
	assert read_ppm(dst)[2] == bytes([0x00, 0x82, 0x18, 0x00, 0x8A, 0x42])
