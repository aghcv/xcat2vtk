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
