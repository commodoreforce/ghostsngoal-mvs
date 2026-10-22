# Original art reference packs

This directory is intentionally for **local, user-owned source art** and the large ZIP files are not committed to Git.

For the premium MVS opening-zone build, place these files here:

- `ambientazione.zip`
- `sprite.zip`

`./scripts/prepare-ngdevkit-project.sh` auto-detects them.

Current Phase 1 policy:

- `ambientazione.zip` is actively used for the high-quality Level 1 sky/city master art.
- `sprite.zip` is detected and reserved as the master source for the next actor-art refinement; current gameplay animation remains aligned to the web runtime frames until the master poses are normalized.
- the web game remains the source of truth for scale, timing, camera, gameplay and final composition.

Do not commit the large ZIP packs to the repository.
