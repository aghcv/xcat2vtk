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
