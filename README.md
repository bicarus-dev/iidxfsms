# iidxfsms

Replaces IIDX FAST/SLOW with signed millisecond offsets: blue `-8.3 ms` for early hits and red `+8.3 ms` for late hits. Separate scratch indicators use pale blue/red to distinguish them from keys.

> [!NOTE]
> This feature has been integrated into [2dxtra](https://github.com/aixxe/2dxtra) - use that instead.

Complete misses show `miss` instead of SLOW in milliseconds mode.

## Usage

1. Place the generated DLL and INI together beside Spice.
1. Add `-k iidxfsms.dll` to your usual Spice launch command.
1. Enable FAST/SLOW in the game's display options.

It supports in-game options for changing where the FAST/SLOW display is located, and also the split scratch timing view. There may be some overlap depending on the configuration but it should be minor.

Requires a recent Spice2x version supporting the Spice SDK.

In [iidxfsms.ini](iidxfsms.ini), set `mode=ms` for timing offsets or `mode=default` for the original display.

Set `show_pgreat=1` to show timing even for PGREAT. If 0.0 ms nothing is shown.

Restart the game after changing settings.

## Supported game versions
See https://github.com/bicarus-dev/iidxfsms/blob/main/src/versions/registry.h

## Build

Requires Docker for Linux containers. The first build creates the project's MinGW image and downloads pinned MinHook and SimpleIni sources.

```bat
build_docker.bat
```

See [PORTING.md](PORTING.md) for adding DLL versions and [THIRD_PARTY.md](THIRD_PARTY.md) for dependencies and redistribution.
