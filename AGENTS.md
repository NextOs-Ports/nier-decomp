# NieR project instructions

- Work only on the original 2010 Xbox 360 NieR. NextOS ARM64/GLES2/Mali-450 is the initial port target.
- Use one agent per terminal. Do not start additional agents or background missions.
- Reuse compatible components from the existing ports according to docs/REUSE.md. Preserve the source projects and their approved builds.
- Game inputs, extracted assets, keys and generated commercial code stay outside version control. Review the actual staged files before every public push.
- No AI attribution or coauthor trailers in commits. Preserve configured Git hooks.
- Decompilation percentage is computed by tools/progress.py from a complete hash-identified inventory. Never count generated recompilation as verified decompilation or invent a denominator.
- Every completed work block updates progress/functions.json or progress/milestones.json as applicable, evidence, CHANGELOG.md and the generated dashboard in the same commit. Run `python3 tools/progress.py --snapshot`, the tracker tests and `--check` before committing.
- Record executable hash and externally measured original timing before performance claims. Presume 60 Hz logic until external evidence establishes otherwise. Measure game speed separately from presented FPS.
- A new game or renderer is not functional until real gameplay, image, audio and controls have been demonstrated. Report scene and validation scope.
- For any authorized device test, impose a process-specific mechanical cutoff: quick tests at most 60 seconds and long tests at most 300 seconds after the first game frame. Preserve saves and installations. Never interrupt the owner's normal play session.
- Keep future handoff notes in docs/HANDOFF.md. A setup milestone does not mean game code has been recovered.
