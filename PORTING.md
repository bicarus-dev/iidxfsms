# Adding DLL Support

## Identify the DLL

Use an unmodified x64 game DLL. Form its signature as `LDJ-<PE timestamp in hex>_<entry-point RVA in hex>`, matching iidxfreq's format.

## Create the Profile

1. Use an existing header in [src/versions](src/versions) as a template. Name the new header after the DLL signature and update its profile symbol and `pe_identifier`.
2. Locate every hook, renderer helper, option getter, color object, and press/release return site in the new DLL. Store module-relative RVAs; do not apply a fixed address delta from another profile.
3. Populate every named hook and helper field defined in [src/versions/profile.h](src/versions/profile.h), checking its use in [src/game_hooks.cpp](src/game_hooks.cpp). Copy the expected entry bytes from the new DLL itself.
4. Verify the calling conventions, candidate and display layouts, judgment codes, font structures, and sprite methods against the assumptions in the hook code. If these differ, adapt the implementation before registering the DLL; changing addresses alone is insufficient.
5. Include the new header in [src/versions/registry.h](src/versions/registry.h) and add its profile to `profiles`.

## Verify Support

Build with Docker:

```bat
build_docker.bat
```

- Check the signature and every entry guard against the original DLL. Do not weaken guards to make an incompatible build load.
- Confirm the plugin selects the new profile and enables its hooks in-game.
- Check early/late timing, complete misses, rounded-zero suppression, and PGREAT display with the option on and off.
- Check both players, SP/DP, combined and split scratch displays, visibility and position options, chords, charge notes, and scene transitions.
- Confirm text remains legible and scoring, judgment windows, and score saving are unchanged. Check coexistence with other loaded plugins.

Record which checks were completed and any remaining limitations when submitting the profile. A successful build or matching entry bytes alone does not establish gameplay compatibility.