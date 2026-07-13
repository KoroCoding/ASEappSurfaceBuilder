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
import sys
import traceback
from pathlib import Path
from typing import Any


class _TeeLog:
    """Mirror ASE optimizer output to both the worker stdout and a log file."""

    def __init__(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self._file = path.open("w", encoding="utf-8", buffering=1)

    def write(self, text: str) -> int:
        self._file.write(text)
        sys.stdout.write(text)
        sys.stdout.flush()
        return len(text)

    def flush(self) -> None:
        self._file.flush()
        sys.stdout.flush()

    def close(self) -> None:
        self._file.close()


def _force_norms(forces: Any) -> list[float]:
    return [math.sqrt(sum(float(component) ** 2 for component in row)) for row in forces]


def _max_force(atoms: Any) -> float | None:
    forces = atoms.get_forces()
    norms = _force_norms(forces)
    return max(norms) if norms else None


def _potential_energy(atoms: Any) -> float | None:
    value = atoms.get_potential_energy()
    return float(value) if value is not None else None


def _write_optimized_structure(write: Any, path: Path, atoms: Any) -> None:
    """Write requested VASP results as POSCAR data; use extXYZ for GUI temporaries."""
    suffix = path.suffix.lower()
    if suffix in {".vasp", ".poscar", ".contcar"} or path.name.upper() in {"POSCAR", "CONTCAR"}:
        write(str(path), atoms, format="vasp", direct=True, vasp5=True, sort=False)
    else:
        write(str(path), atoms, format="extxyz")


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
    optimizer_log: _TeeLog | None = None
    summary: dict[str, Any] = {
        "ok": False,
        "model": str(args.model),
        "task": args.task,
        "requested_device": args.device,
        "python_executable": sys.executable,
        "fmax_target_ev_per_ang": args.fmax,
        "max_steps": args.steps,
    }

    try:
        print(f"[UMA] Python: {sys.executable}", flush=True)
        print("[UMA] Importing ASE, PyTorch, and fairchem...", flush=True)
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
        print(f"[UMA] Reading input: {args.input}", flush=True)
        atoms = read(str(args.input))
        constraint_masks = _load_constraint_masks(args.constraints, args.fix_selective)
        if constraint_masks:
            atoms.set_constraint([FixScaled(index, mask=mask) for index, mask in constraint_masks])
        elif not args.fix_selective:
            atoms.set_constraint()
        summary["fixed_atom_count"] = len(constraint_masks)

        print(f"[UMA] Loading checkpoint on {device}: {args.model}", flush=True)
        calculator = FAIRChemCalculator.from_model_checkpoint(
            str(args.model),
            task_name=args.task,
            device=device,
        )
        atoms.calc = calculator

        summary["initial_energy_ev"] = _potential_energy(atoms)
        summary["initial_fmax_ev_per_ang"] = _max_force(atoms)
        print(
            f"[UMA] Initial energy={summary['initial_energy_ev']:.10g} eV, "
            f"fmax={summary['initial_fmax_ev_per_ang']:.10g} eV/Ang",
            flush=True,
        )

        log_path = args.summary.with_name("relax_fire.log")
        optimizer_log = _TeeLog(log_path)
        optimizer = FIRE(atoms, logfile=optimizer_log)
        converged = optimizer.run(fmax=args.fmax, steps=args.steps)
        optimizer_log.close()
        optimizer_log = None

        summary["optimizer_steps"] = int(optimizer.get_number_of_steps())
        summary["converged"] = bool(converged)
        summary["final_energy_ev"] = _potential_energy(atoms)
        summary["final_fmax_ev_per_ang"] = _max_force(atoms)
        summary["log"] = str(log_path)

        args.output.parent.mkdir(parents=True, exist_ok=True)
        _write_optimized_structure(write, args.output, atoms)
        summary["output"] = str(args.output)
        summary["ok"] = True
        print(
            f"[UMA] Finished: converged={summary['converged']}, "
            f"steps={summary['optimizer_steps']}, "
            f"final_fmax={summary['final_fmax_ev_per_ang']:.10g} eV/Ang",
            flush=True,
        )
        _write_summary(args.summary, summary)
        print(json.dumps(summary, ensure_ascii=False), flush=True)
        return 0
    except Exception as exc:  # pragma: no cover - exercised by GUI failure path
        if optimizer_log is not None:
            optimizer_log.close()
        if isinstance(exc, ModuleNotFoundError) and exc.name == "fairchem":
            summary["error"] = (
                f"fairchem is not installed in {sys.executable}. "
                "Set Python command to 'auto' or select the Python executable "
                "from an environment containing fairchem-core."
            )
        else:
            summary["error"] = str(exc)
        summary["traceback"] = traceback.format_exc()
        _write_summary(args.summary, summary)
        print(json.dumps(summary, ensure_ascii=False), flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
