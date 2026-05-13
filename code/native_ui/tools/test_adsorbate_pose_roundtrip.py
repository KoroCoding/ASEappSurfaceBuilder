#!/usr/bin/env python3
"""ASE extended-XYZ round-trip test for adsorbate pose editing.

The fixture is a small Cu slab with methanol as the real-molecule adsorbate.
The test intentionally performs the same pose operations expected from the GUI:
rigid translation, pivot rotation, and C-O bond-length adjustment, then writes
and rereads extxyz through ASE.
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import numpy as np


def _install_scipy_import_shim_for_ase_io() -> None:
    """Avoid unrelated SciPy DLL imports while still exercising ASE extxyz I/O.

    Some Windows Application Control environments block SciPy's compiled
    optimize extensions.  ASE's top-level ``ase.io`` import can touch those
    modules indirectly through trajectory/spacegroup helpers even though this
    round-trip test only needs extended XYZ read/write.  A minimal shim keeps
    the test focused on ASE's extxyz path without modifying the environment.
    """
    import types

    scipy = sys.modules.setdefault("scipy", types.ModuleType("scipy"))
    integrate = sys.modules.setdefault("scipy.integrate", types.ModuleType("scipy.integrate"))
    spatial = sys.modules.setdefault("scipy.spatial", types.ModuleType("scipy.spatial"))
    if not hasattr(integrate, "trapezoid"):
        integrate.trapezoid = np.trapezoid if hasattr(np, "trapezoid") else np.trapz
    scipy.integrate = integrate
    scipy.spatial = spatial


def _distance(positions: np.ndarray, i: int, j: int) -> float:
    return float(np.linalg.norm(positions[j] - positions[i]))


def _rotate_about_axis(points: np.ndarray, pivot: np.ndarray, axis: np.ndarray, degrees: float) -> np.ndarray:
    axis = axis / np.linalg.norm(axis)
    theta = math.radians(degrees)
    rel = points - pivot
    cos_t = math.cos(theta)
    sin_t = math.sin(theta)
    return pivot + rel * cos_t + np.cross(axis, rel) * sin_t + axis * np.dot(rel, axis)[:, None] * (1.0 - cos_t)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "assets" / "samples" / "adsorbate_pose_methanol_on_cu.extxyz",
        help="Input extxyz fixture written in ASEapp's export layout.",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=Path.cwd() / "build" / "adsorbate_pose_ase_roundtrip",
        help="Directory for generated round-trip artifacts.",
    )
    args = parser.parse_args()

    _install_scipy_import_shim_for_ase_io()
    try:
        from ase.io import read, write
    except Exception as exc:  # pragma: no cover - environment guard
        print(f"ASE import failed: {exc}", file=sys.stderr)
        return 2

    args.work_dir.mkdir(parents=True, exist_ok=True)
    atoms = read(str(args.input), format="extxyz")
    assert len(atoms) == 10, len(atoms)
    assert atoms.get_chemical_symbols() == ["Cu", "Cu", "Cu", "Cu", "C", "O", "H", "H", "H", "H"]
    assert np.allclose(atoms.cell.array, np.diag([8.0, 8.0, 15.0]), atol=1e-8)
    assert atoms.pbc.all()

    if "atom_id" not in atoms.arrays:
        raise AssertionError("atom_id property was not read by ASE")
    if "tag" not in atoms.arrays:
        raise AssertionError("tag property was not read by ASE")
    assert int(atoms.arrays["atom_id"][-1]) == 10
    assert str(atoms.arrays["tag"][-1]) == "methanol-Ho"

    adsorbate = np.array([4, 5, 6, 7, 8, 9], dtype=int)
    positions0 = atoms.get_positions()
    co0 = _distance(positions0, 4, 5)
    ch0 = _distance(positions0, 4, 6)
    oh0 = _distance(positions0, 5, 9)

    positions = positions0.copy()
    positions[adsorbate] += np.array([1.25, -0.50, 0.75])
    assert np.allclose(positions[:4], positions0[:4], atol=1e-10)
    assert np.isclose(_distance(positions, 4, 5), co0, atol=1e-10)
    assert np.isclose(_distance(positions, 4, 6), ch0, atol=1e-10)

    pivot = positions[4].copy()
    positions[adsorbate] = _rotate_about_axis(positions[adsorbate], pivot, np.array([0.0, 0.0, 1.0]), 90.0)
    assert np.allclose(positions[4], pivot, atol=1e-10)
    assert np.isclose(_distance(positions, 4, 5), co0, atol=1e-10)
    assert np.isclose(_distance(positions, 4, 6), ch0, atol=1e-10)

    co_before = _distance(positions, 4, 5)
    target_co = co_before + 0.25
    direction = (positions[5] - positions[4]) / co_before
    positions[[5, 9]] += direction * (target_co - co_before)
    assert np.isclose(_distance(positions, 4, 5), target_co, atol=1e-10)
    assert np.isclose(_distance(positions, 5, 9), oh0, atol=1e-10)

    atoms.set_positions(positions)
    out_path = args.work_dir / "ase_roundtrip_pose.extxyz"
    write(str(out_path), atoms, format="extxyz")
    reread = read(str(out_path), format="extxyz")

    assert len(reread) == len(atoms)
    assert reread.get_chemical_symbols() == atoms.get_chemical_symbols()
    assert np.allclose(reread.cell.array, atoms.cell.array, atol=1e-8)
    assert reread.pbc.all()
    assert np.allclose(reread.get_positions(), positions, atol=1e-8)
    assert np.isclose(_distance(reread.get_positions(), 4, 5), target_co, atol=1e-8)
    assert np.isclose(_distance(reread.get_positions(), 5, 9), oh0, atol=1e-8)
    assert int(reread.arrays["atom_id"][-1]) == 10
    assert str(reread.arrays["tag"][-1]) == "methanol-Ho"

    print(f"PASS ASE extxyz round-trip: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
