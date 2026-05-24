# ASEapp Surface Builder v1.3.3 Release Notes

Target artifacts: `ASEappSurfaceBuilder-1.3.3-Windows.exe` and `ASEappSurfaceBuilder-1.3.3-Windows.zip`.

## Summary

v1.3.3 is a patch release for vacuum-layer placement bugs, out-of-cell placement handling, supercell-state preservation, and large-structure interaction performance.

## Changes

- Add vacuum layers incrementally so adding top/bottom vacuum no longer removes the previously added opposite side.
- Mark out-of-cell placement previews in red and ask before auto-expanding the vacuum region to place them.
- Keep the current supercell transform label through vacuum, atom placement, precursor placement, and termination edits.
- Make Ctrl+drag range selection follow the visible rectangle exactly instead of selecting atoms just outside it.
- Add a screen-space atom selection index for faster overlap picking and range selection.
- Add off-screen culling, reduced duplicate projection work, and bucketed fast depth ordering for large interactive scenes.
- Preserve the two-color bond gradient while using lighter atom fills only in large/active fast-render paths.
- Persist startup display defaults for cell, bonds, outside-cell images, axes, labels, perspective, depth cue, and atom scale.
- Make right-side tool sections collapsible and persist their expanded/collapsed state.
- Embed the default app settings JSON in the executable resources and save user changes to a dedicated app config JSON, not to VASP/CIF/XYZ structure files.
- Add a Settings menu/dialog for startup display and right-panel defaults, and expose the main file, edit, structure, placement, pose, view, tools, and help operations from the menu bar.
- Prevent the GUI self-test from overwriting user startup display defaults.

## Verification

- Release build
- CTest
- GUI self-test
- `git diff --check`
- Windows ZIP/package generation
- Windows extracted-package launch smoke test
