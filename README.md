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

### Debug One Time Frame

For ID-based auto-discovery runs, use `--frame N` to process only one XCAT
time step instead of the full sequence. This is useful when debugging anatomy
hierarchy, atlas, report, or ParaView coloring issues.

```bash
./xcat2vtk \
  --id 260602 \
  --input ../data \
  --output ../outputs/vtk_output_debug \
  --dims 750 750 750 \
  --frame 1 \
  --anatomy-hierarchy \
  --anatomy-atlas \
  --anatomy-report
```

Use the frame number from the discovered filenames, for example
`260602_1_head.raw` or `260602_act_1.bin` means `--frame 1`. The output still
uses the usual `time_001/scene.vtm` folder and a one-frame `.pvd`, so the result
opens in ParaView like a normal run. If you pass explicit `--surface`,
`--activity`, or `--attenuation` paths without `--id`, the run is already a
single-frame conversion.

Keep the anatomy flags in this example when debugging the hierarchy. These are
opt-in features: `--anatomy-hierarchy` requests the nested anatomical layout,
`--anatomy-atlas` applies and updates the reusable atlas, and
`--anatomy-report` writes the review CSV. Only the atlas and report paths are
defaulted when those flags are present.

By default, surface blocks are stabilized across all discovered time frames before
the `.vtm` and `.pvd` files are written:

- all surface labels are scanned first
- labels are sorted alphabetically
- every time frame uses the same surface block order
- labels missing from a time frame are kept as named empty blocks

This keeps ParaView rendering order consistent across animation frames. Use
`--no-stable-surface-order` to disable this behavior.

## Anatomical Hierarchy Output

Large whole-body XCAT surface sets can contain thousands of anatomical surface
blocks. A flat multiblock list is difficult to navigate in ParaView, especially
when selecting entire systems such as cardiovascular, respiratory,
musculoskeletal, or nervous anatomy.

Use `--anatomy-hierarchy` to write surfaces into a nested anatomical
`vtkMultiBlockDataSet` while preserving every source surface as a leaf. Activity
and attenuation volumes are grouped under `00_Fields`, and anatomical surfaces
are placed under standardized top-level groups such as:

```text
01_Cardiovascular
02_Nervous
03_Respiratory
04_Digestive
05_Urinary
09_Musculoskeletal
10_Lymphatic_and_Immune
99_Unclassified
```

Example:

```bash
./xcat2vtk \
  --id 260602 \
  --input ../data \
  --output ../vtk_output \
  --dims 750 750 750 \
  --anatomy-hierarchy \
  --anatomy-report
```

Useful options:

- `--anatomy-hierarchy` enables nested anatomical surface blocks. Without it,
  the converter writes the legacy flat block layout.
- `--anatomy-config PATH` overlays custom aliases and rules.
- `--frame N` limits ID-based auto-discovery to one time step for debugging.
- `--anatomy-report [PATH]` writes one CSV row per source surface block.
  Reports are not written unless this flag is present. Without a path, it
  writes `anatomy_report_<id>.csv` under `--output`, or `anatomy_report.csv`
  when no `--id` is supplied.
- `--anatomy-overrides PATH` reads an edited anatomy report CSV and applies
  batch corrections before writing the hierarchy. `--anatomy-report-input PATH`
  is accepted as an alias.
- `--anatomy-atlas [PATH]` reads corrected rows from a reusable override atlas
  and appends any newly unclassified labels as `needs_review` rows. The atlas is
  not used or updated unless this flag is present. Without a path, it uses
  `config/anatomy_atlas.csv`.
- `--strict-anatomy` fails the run if any surface remains unclassified.
- `--flat-blocks` explicitly requests the legacy flat multiblock layout.

The flat layout remains the default for compatibility.

Each anatomical leaf receives VTK field-data metadata including:

```text
OriginalBlockName
OriginalBlockIndex
NormalizedAnatomyName
CanonicalAnatomyName
CanonicalAnatomyIdentifier
AnatomicalSystem
AnatomicalSubsystem
AnatomicalRegion
StructureType
Laterality
PieceNumber
TemporalPhase
ClassificationConfidence
ClassificationRule
SourceFile
```

For surface leaves, `OriginalBlockName`, `NormalizedAnatomyName`,
`CanonicalAnatomyName`, and numeric `OriginalBlockIndex` are also repeated as
cell-data arrays so ParaView can expose them in the dataset array/coloring UI
and spreadsheet views.

The report can also drive batch corrections. A typical review loop is:

```bash
# First pass: generate the hierarchy and review table.
./xcat2vtk \
  --id 260602 \
  --input ../data \
  --output ../vtk_output_review \
  --dims 750 750 750 \
  --anatomy-hierarchy \
  --anatomy-report

# Edit/copy rows into corrected_anatomy_report.csv, then apply them.
./xcat2vtk \
  --id 260602 \
  --input ../data \
  --output ../vtk_output_corrected \
  --dims 750 750 750 \
  --anatomy-hierarchy \
  --anatomy-overrides ../vtk_output_review/corrected_anatomy_report.csv \
  --anatomy-report
```

The override CSV may be a full edited `anatomy_report.csv` or a smaller patch
table. Rows with `original_block_index` and `original_name` apply to that exact
source surface. Rows with only `original_name` apply to every matching source
surface label. Editable correction columns include `normalized_name`,
`canonical_name`, `system`, `subsystem`, `anatomical_region`, `structure_type`,
`laterality`, `piece_number`, `temporal_phase`, `hierarchy_path`,
`classification_confidence`, and `classification_rule`. If `hierarchy_path` is
omitted for a classified row, it is derived from `system` and `subsystem`.

For a longer-running review workflow, keep a single atlas CSV:

```bash
./xcat2vtk \
  --id 260602 \
  --input ../data \
  --output ../vtk_output_atlas \
  --dims 750 750 750 \
  --anatomy-hierarchy \
  --anatomy-atlas \
  --anatomy-report
```

By default the atlas lives at `config/anatomy_atlas.csv`; pass
`--anatomy-atlas path/to/atlas.csv` only when experimenting with a separate
review file. The atlas is also an override file. Rows marked
`review_action=corrected` are applied as batch corrections. Rows marked `needs_review`,
`needs_manual_review`, `pending`, `unchanged`, or similar are kept for review
but skipped by the override loader. When a new phantom introduces an
unclassified label that is not already in the atlas, `--anatomy-atlas` appends a
generic `original_name` row with example source-file/index evidence. Fill the
classification columns and change `review_action` to `corrected` to activate it
for future runs.

Numbered fragments such as `bronchi_17` remain separate datasets. They are
classified under a shared parent such as
`03_Respiratory/Tracheobronchial_Tree/Bronchi`, with the piece number preserved
in metadata and in the readable leaf name, for example `bronchus_017`.

Classification rules live in `config/anatomy_hierarchy.yml`. The file supports a
small dependency-free YAML subset:

```yaml
aliases:
  lkidney:
    canonical_name: kidney
    laterality: left

rules:
  - id: pulmonary_artery
    priority: 100
    pattern: ".*pulmonary.*arter.*"
    system: cardiovascular
    subsystem: pulmonary_circulation/arteries
    anatomical_region: thorax
    structure_type: artery
    hierarchy_path: 01_Cardiovascular/Pulmonary_Circulation/Arteries
```

Higher-priority rules run before generic fallbacks, so pulmonary arteries and
veins are placed under pulmonary circulation instead of systemic vessels. Any
label without a confident rule is preserved under `99_Unclassified` and printed
as a warning.

Validate a generated hierarchy with:

```bash
python3 scripts/validate_anatomy_hierarchy.py \
  ../vtk_output/260602/time_001/scene.vtm
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

### VTI grid subdivision for student projects

Use `--sample-vti-blocks N` to subdivide the entire non-background bounding
box of each activity or attenuation volume into a uniform 3-D grid of `.vti`
blocks. This lets you hand students a small, self-contained spatial region of
the phantom without revealing the full data.

```bash
./xcat2vtk \
  --activity ../data/260602_act_1.bin \
  --output ../vtk_output \
  --dims 750 750 750 \
  --sample-vti-blocks 64
```

The converter factorises **N** into `(nx, ny, nz)` to produce the most
cube-like blocks given the bounding-box proportions. For example:

| N | Grid | Notes |
|---|------|-------|
| 8 | 2×2×2 | perfect cube |
| 27 | 3×3×3 | perfect cube |
| 64 | 4×4×4 | perfect cube |
| 10 | 1×2×5 | best aspect ratio for equal extents |
| 100 | 4×5×5 | near-cube |

If **N** is prime the grid degenerates to `1×1×N`; use a nearby composite
number for better block shapes.

The sample files are written under:

```text
time_000/
  vti_samples/
    activity/
      activity_grid_000_000_000.vti
      activity_grid_001_000_000.vti
      ...
```

The filename encodes the zero-indexed grid cell `(ix, iy, iz)`. Each `.vti`
block includes a `xcat2vtk_sample_metadata` field-data array with:

- `grid_label`, `grid_strategy`, `grid_cell_ijk`, `grid_linear_index`
- `grid_divisions_ijk`, `grid_total_count`, `grid_requested_count`
- `source_path`, `source_scalar`, `frame`, `source_dims_ijk`
- `block_start_ijk`, `block_end_ijk`, `block_dims_ijk`
- `sample_bounds_kind`, `sample_bounds_min_ijk`, `sample_bounds_max_ijk`
- `spacing`, `block_physical_origin`, `block_physical_max`
- `non_background_fraction`, `non_background_voxels`, `voxel_count`
- `scalar_min`, `scalar_max`, `scalar_mean`
- `distinct_scalar_values_observed`, `distinct_scalar_values_capped`

`--subsample-vti-blocks N` is accepted as an alias.

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

## High
| Phantom      | Voxel size |    X |    Y |    Z |
| ------------ | ---------: | ---: | ---: | ---: |
| Infant       |   0.020 cm | 1108 | 1108 | 2600 |
| 1 year       |   0.025 cm | 1313 | 1313 | 3080 |
| 5 years      |   0.030 cm | 1563 | 1563 | 3667 |
| 10 years     |   0.040 cm | 1481 | 1481 | 3475 |
| 15 years     |   0.045 cm | 1525 | 1525 | 3578 |
| Adult female |   0.050 cm | 1380 | 1380 | 3240 |
| Adult male   |   0.050 cm | 1500 | 1500 | 3520 |

## Medium
| Phantom      | Height | Updated cubic voxel size | X voxels | Y voxels | Z slices | Z CORREC |
| ------------ | -----: | -----------------------: | -------: | -------: | -------: | -------: |
| Infant       |  52 cm |       0.040 cm = 0.40 mm |      554 |      554 |     1300 |     1400 |
| 1 year       |  77 cm |       0.050 cm = 0.50 mm |      657 |      657 |     1540 |     1640 |
| 5 years      | 110 cm |       0.060 cm = 0.60 mm |      782 |      782 |     1834 |     1934 |
| 10 years     | 139 cm |       0.080 cm = 0.80 mm |      741 |      741 |     1738 |     1838 |
| 15 years     | 161 cm |       0.090 cm = 0.90 mm |      763 |      763 |     1789 |     1889 |
| Adult female | 162 cm |       0.100 cm = 1.00 mm |      690 |      690 |     1620 |     1720 |
| Adult male   | 176 cm |       0.100 cm = 1.00 mm |      750 |      750 |     1760 |     1860 |

## Low
| Phantom      | Height | Updated cubic voxel size | X voxels | Y voxels | Z slices | Z CORREC |
| ------------ | -----: | -----------------------: | -------: | -------: | -------: | -------: |
| Infant       |  52 cm |       0.080 cm = 0.80 mm |      ??? |      ??? |      650 |      700 |
| 1 year       |  77 cm |       0.100 cm = 1.00 mm |      ??? |      ??? |      770 |      820 |
| 5 years      | 110 cm |       0.120 cm = 1.20 mm |      ??? |      ??? |      917 |      967 |
| 10 years     | 139 cm |       0.160 cm = 1.60 mm |      ??? |      ??? |      869 |      919 |
| 15 years     | 161 cm |       0.180 cm = 1.80 mm |      ??? |      ??? |      ??? |      ??? |
| Adult female | 162 cm |       0.200 cm = 2.00 mm |      345 |      345 |      810 |      860 |
| Adult male   | 176 cm |       0.200 cm = 2.00 mm |      375 |      375 |      880 |      930 |