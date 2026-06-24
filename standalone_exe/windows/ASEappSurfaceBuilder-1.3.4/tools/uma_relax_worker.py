#!/usr/bin/env python
"""Run a UMA-backed ASE geometry relaxation for ASEapp.

This script is intentionally a separate process so the Qt UI can keep the
heavy PyTorch/fairchem runtime outside the C++ executable and report failures
without crashing the app.
"""

from __future__ import annotations

import argparse
import json
import math
import traceback
from pathlib import Path
from typing import Any


def _force_norms(forces: Any) -> list[float]:
    return [math.sqrt(sum(float(component) ** 2 for component in row)) for row in forces]


def _max_force(atoms: Any) -> float | None:
    forces = atoms.get_forces()
    norms = _force_norms(forces)
    return max(norms) if norms else None


def _potential_energy(atoms: Any) -> float | None:
    value = atoms.get_potential_energy()
    return float(value) if value is not None else None


def _resolve_device(device: str) -> str:
    normalized = (device or "cuda").strip().lower()
    if normalized != "auto":
        return normalized
    try:
        import torch

        return "cuda" if torch.cuda.is_available() else "cpu"
    except Exception:
        return "cpu"


def _load_constraint_masks(path: Path | None, enabled: bool) -> list[tuple[int, tuple[bool, bool, bool]]]:
    if not enabled or path is None or not path.exists():
        return []
    payload = json.loads(path.read_text(encoding="utf-8"))
    masks: list[tuple[int, tuple[bool, bool, bool]]] = []
    movable_axes = payload.get("movableAxes")
    if isinstance(movable_axes, list):
        for index, axes in enumerate(movable_axes):
            if not isinstance(axes, list) or len(axes) < 3:
                continue
            fixed_mask = tuple(not bool(axes[axis]) for axis in range(3))
            if any(fixed_mask):
                masks.append((index, fixed_mask))
        return masks
    return [(int(index), (True, True, True)) for index in payload.get("fixAtomIndices", [])]


def _write_summary(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Relax an ASE structure with UMA/fairchem.")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--summary", required=True, type=Path)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--task", default="oc20")
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--fmax", default=0.05, type=float)
    parser.add_argument("--steps", default=500, type=int)
    parser.add_argument("--constraints", type=Path)
    parser.add_argument("--fix-selective", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    summary: dict[str, Any] = {
        "ok": False,
        "model": str(args.model),
        "task": args.task,
        "requested_device": args.device,
        "fmax_target_ev_per_ang": args.fmax,
        "max_steps": args.steps,
    }

    try:
        from ase.constraints import FixScaled
        from ase.io import read, write
        from ase.optimize import FIRE
        from fairchem.core.calculate.ase_calculator import FAIRChemCalculator

        if not args.model.exists():
            raise FileNotFoundError(f"UMA checkpoint was not found: {args.model}")
        if not args.input.exists():
            raise FileNotFoundError(f"Input structure was not found: {args.input}")

        device = _resolve_device(args.device)
        summary["device"] = device
        atoms = read(str(args.input))
        constraint_masks = _load_constraint_masks(args.constraints, args.fix_selective)
        if constraint_masks:
            atoms.set_constraint([FixScaled(index, mask=mask) for index, mask in constraint_masks])
        summary["fixed_atom_count"] = len(constraint_masks)

        calculator = FAIRChemCalculator.from_model_checkpoint(
            str(args.model),
            task_name=args.task,
            device=device,
        )
        atoms.calc = calculator

        summary["initial_energy_ev"] = _potential_energy(atoms)
        summary["initial_fmax_ev_per_ang"] = _max_force(atoms)

        log_path = args.summary.with_name("relax_fire.log")
        optimizer = FIRE(atoms, logfile=str(log_path))
        converged = optimizer.run(fmax=args.fmax, steps=args.steps)

        summary["optimizer_steps"] = int(optimizer.get_number_of_steps())
        summary["converged"] = bool(converged)
        summary["final_energy_ev"] = _potential_energy(atoms)
        summary["final_fmax_ev_per_ang"] = _max_force(atoms)
        summary["log"] = str(log_path)

        args.output.parent.mkdir(parents=True, exist_ok=True)
        write(str(args.output), atoms, format="extxyz")
        summary["output"] = str(args.output)
        summary["ok"] = True
        _write_summary(args.summary, summary)
        print(json.dumps(summary, ensure_ascii=False), flush=True)
        return 0
    except Exception as exc:  # pragma: no cover - exercised by GUI failure path
        summary["error"] = str(exc)
        summary["traceback"] = traceback.format_exc()
        _write_summary(args.summary, summary)
        print(json.dumps(summary, ensure_ascii=False), flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
