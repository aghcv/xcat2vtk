# xcat2vtk

`xcat2vtk` is a command-line converter for transforming XCAT phantom outputs into ParaView-ready VTK datasets.

The goal is to convert hybrid XCAT outputs — voxelized `.bin` activity/attenuation phantoms and labeled surface `.raw` triangle meshes — into a structured 4D visualization dataset using VTK formats.

## Motivation

XCAT can generate both:

1. **Voxelized phantom data**
   - Activity phantom: `*_act_*.bin`
   - Attenuation phantom: `*_atn_*.bin`

2. **Surface mesh data**
   - Labeled triangle surface meshes: `*.raw`

The voxelized files can be opened in ImageJ as raw 32-bit real volumes, but this does not preserve the labeled anatomical surface information.

This tool aims to create an efficient hybrid dataset that can be opened directly in ParaView, combining:

- 3D voxel volumes
- labeled anatomical surfaces
- time-frame organization
- metadata for reproducibility

## Target Output

For an XCAT case ID such as:

```text
260602
```

the expected output structure is:

```text
vtk_output/
  260602/
    manifest.json
    time_001/
      activity.vti
      attenuation.vti
      surfaces/
        head.vtp
        r_kidney.vtp
        l_kidney.vtp
      scene.vtm
    time_002/
      activity.vti
      attenuation.vti
      surfaces/
        head.vtp
        r_kidney.vtp
        l_kidney.vtp
      scene.vtm
    260602_scene.pvd
```

The final `.pvd` file can be opened in ParaView as a time series.

## Supported Input Files

Initial expected XCAT file patterns:

```text
<id>_act_<frame>.bin
<id>_atn_<frame>.bin
<id>_<frame>_<tissue>.raw
```

Example:

```text
260602_act_1.bin
260602_atn_1.bin
260602_1_head.raw
260602_1_r_kidney.raw
260602_1_l_kidney.raw
```

The tool should eventually also support `.raw` surface files that contain multiple labeled sections internally, for example:

```text
head
x1 y1 z1 x2 y2 z2 x3 y3 z3
x1 y1 z1 x2 y2 z2 x3 y3 z3

r_kidney
x1 y1 z1 x2 y2 z2 x3 y3 z3
...
```

## VTK Output Formats

| XCAT data type | Output format | VTK data object |
|---|---|---|
| Activity `.bin` | `.vti` | `vtkImageData` |
| Attenuation `.bin` | `.vti` | `vtkImageData` |
| Surface `.raw` | `.vtp` | `vtkPolyData` |
| One full time frame | `.vtm` | `vtkMultiBlockDataSet` |
| Full 4D sequence | `.pvd` | ParaView time-series collection |

## Planned Versions

### Version 0.1 — Single-frame conversion

Convert time frames into VTK multiblock scenes. You can either pass explicit paths or use auto-discovery with `--id` and `--input`. Auto-discovery includes all surface `.raw` files for the frame, and each `.raw` can contain multiple labeled sections. Output is written as per-frame folders plus a `.pvd` time-series file.

Example:

```bash
xcat2vtk \
  --id 260602 \
  --input ./data \
  --output ./outputs \
  --dims 750 750 750 \
  --spacing 1.0 1.0 1.0 \
  --origin 0.0 0.0 0.0
```

Main goal:

- Prove that `.bin` volumes can be converted to `.vti`
- Prove that `.raw` triangle surfaces can be converted to `.vtp`
- Prove that volumes and surfaces can be grouped into one `.vtm`

### Version 0.2 — ID-based automatic scanner

Automatically find all files beginning with a case ID.

Example:

```bash
xcat2vtk \
  --id 260602 \
  --input ./xcat_output \
  --output ./vtk_output \
  --dims 750 750 750
```

Main goal:

- Scan an input folder
- Detect activity, attenuation, and surface files
- Group files by time frame
- Support `--dry-run`

### Version 0.3 — 4D time-series export

Create one `.vtm` scene per time frame and a global `.pvd` file.

Main goal:

- Enable ParaView time-slider visualization
- Write organized output folders
- Support multiple time frames

### Version 0.4 — Metadata, labels, and manifest

Add metadata for reproducibility and better ParaView organization.

Main goal:

- Write `manifest.json`
- Attach names to multiblock entries
- Preserve tissue labels
- Add scalar names: `activity`, `attenuation`
- Add optional label map support

### Version 0.5 — Performance and packaging

Improve large-dataset performance and prepare binary distribution.

Main goal:

- Add compression options
- Add surface point deduplication
- Add better error handling
- Add tests
- Add GitHub Actions CI
- Add install/package instructions
- Prepare Linux/macOS binaries or container support

## Build Requirements

Recommended:

- C++17 or newer
- CMake 3.20+
- VTK
- Optional: CLI11 or another command-line parser

## Build

```bash
mkdir build
cd build
cmake ..
cmake --build . -j
```

## Run

```bash
./xcat2vtk --help
```


Example:

```bash
./xcat2vtk \
  --id 260602 \
  --input ../data \
  --output ../vtk_output \
  --dims 750 750 750 \
  --spacing 1.0 1.0 1.0
```

By default, surface blocks are stabilized across all discovered time frames before
the `.vtm` and `.pvd` files are written:

- all surface labels are scanned first
- labels are sorted alphabetically
- every time frame uses the same surface block order
- labels missing from a time frame are kept as named empty blocks

This keeps ParaView rendering order consistent across animation frames. Use
`--no-stable-surface-order` to disable this behavior.

## Development Philosophy

This project should develop incrementally.

Each version should:

1. Build successfully.
2. Include a small test dataset or synthetic test.
3. Produce output that can be opened in ParaView.
4. Avoid breaking previous functionality.
5. Keep the command-line interface simple.

## Notes on XCAT Binary Volumes

The `.bin` files are assumed to be:

- 32-bit real values
- little-endian
- regular Cartesian voxel grids
- dimensions provided by the user

For ImageJ, these are typically opened using:

```text
Image type: 32-bit Real
Width: X dimension
Height: Y dimension
Number of images: Z dimension
Offset: 0
Gap: 0
Little-endian byte order: checked
```

The same assumptions are used for the initial `.vti` conversion.

The converter writes volume samples as VTK cell data because the input array
represents voxel values. For `--dims 750 750 750`, the written image extent has
751 grid points along each axis and 750 voxel cells.

If surfaces are present and `--spacing` or `--origin` are not supplied, the
converter scans the volume data for non-background voxel cells and fits that
occupied voxel bounding box to the global transformed surface bounds. This keeps
zero/background margins outside the anatomical surface extent while aligning the
actual voxelized domain with the XCAT surface meshes.

The default background value is `0`, with exact comparison. You can adjust this
when needed:

- `--volume-background VALUE`
- `--volume-background-epsilon EPS`
- `--volume-fit-source auto|attenuation|activity|union`

`auto` uses attenuation files when available, because attenuation usually
represents the full body support, and falls back to activity files otherwise.
Explicit `--spacing` and `--origin` values always take priority. Use
`--no-fit-volume-to-surfaces` to keep the old default index-space placement.

## Notes on XCAT Surface Files

The `.raw` surface files are expected to contain triangle data in the format:

```text
x1 y1 z1 x2 y2 z2 x3 y3 z3
```

Each line or group of nine floating-point values represents one triangle.

The converter should create:

- VTK points
- VTK triangle cells
- `vtkPolyData`
- `.vtp` output

### Surface Alignment

Surface `.raw` files can be in a different coordinate system than the voxel volumes. Use these optional flags to align surfaces with the volume space:

- `--surface-scale SX SY SZ`
- `--surface-translate TX TY TZ`

If your surfaces are in voxel index space, set `--surface-scale` to match `--spacing` and `--surface-translate` to match `--origin`.

## Long-Term Direction

This repository may later support additional output formats:

- VTKHDF / HDF5 for scalable storage
- Zarr for cloud-friendly chunked arrays
- OpenVDB for sparse volumes
- NanoVDB for GPU visualization
- DICOM export for clinical-software compatibility
- OBJ/STL export for CAD or 3D-printing workflows

The primary ParaView-compatible workflow should remain:

```text
.bin + .raw
   ↓
.vti + .vtp
   ↓
.vtm per time frame
   ↓
.pvd full 4D sequence
```

## License

MIT.



## voxel sizing 

## Fine
| Phantom      | Voxel size |    X |    Y |    Z |
| ------------ | ---------: | ---: | ---: | ---: |
| Infant       |   0.020 cm | 1108 | 1108 | 2600 |
| 1 year       |   0.025 cm | 1313 | 1313 | 3080 |
| 5 years      |   0.030 cm | 1563 | 1563 | 3667 |
| 10 years     |   0.040 cm | 1481 | 1481 | 3475 |
| 15 years     |   0.045 cm | 1525 | 1525 | 3578 |
| Adult female |   0.050 cm | 1380 | 1380 | 3240 |
| Adult male   |   0.050 cm | 1500 | 1500 | 3520 |

## medium
| Phantom      | Height | Updated cubic voxel size | X voxels | Y voxels | Z slices |
| ------------ | -----: | -----------------------: | -------: | -------: | -------: |
| Infant       |  52 cm |       0.040 cm = 0.40 mm |      554 |      554 |     1300 |
| 1 year       |  77 cm |       0.050 cm = 0.50 mm |      657 |      657 |     1540 |
| 5 years      | 110 cm |       0.060 cm = 0.60 mm |      782 |      782 |     1834 |
| 10 years     | 139 cm |       0.080 cm = 0.80 mm |      741 |      741 |     1738 |
| 15 years     | 161 cm |       0.090 cm = 0.90 mm |      763 |      763 |     1789 |
| Adult female | 162 cm |       0.100 cm = 1.00 mm |      690 |      690 |     1620 |
| Adult male   | 176 cm |       0.100 cm = 1.00 mm |      750 |      750 |     1760 |
17
