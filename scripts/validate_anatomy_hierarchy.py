#!/usr/bin/env python3
"""Validate xcat2vtk anatomical hierarchy VTM files."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


REQUIRED_METADATA = [
    "OriginalBlockName",
    "OriginalBlockIndex",
    "CanonicalAnatomyName",
    "CanonicalAnatomyIdentifier",
    "AnatomicalSystem",
    "AnatomicalSubsystem",
    "AnatomicalRegion",
    "StructureType",
    "Laterality",
    "PieceNumber",
    "TemporalPhase",
    "ClassificationConfidence",
    "ClassificationRule",
]


def metadata_name(parent, index):
    info = parent.GetMetaData(index)
    if info is None:
        return ""
    key = parent.NAME()
    if not info.Has(key):
        return ""
    return info.Get(key) or ""


def field_string(obj, name):
    field_data = obj.GetFieldData()
    if field_data is None:
        return ""
    array = field_data.GetAbstractArray(name)
    if array is None or array.GetNumberOfValues() == 0:
        return ""
    return str(array.GetValue(0))


def walk_multiblock(block, path, leaves, errors, unclassified):
    for index in range(block.GetNumberOfBlocks()):
        name = metadata_name(block, index)
        child = block.GetBlock(index)
        child_path = path + [name or f"<unnamed:{index}>"]
        if not name:
            errors.append("/".join(child_path) + ": missing block name")
        if child is None:
            errors.append("/".join(child_path) + ": empty block")
            continue
        if child.IsA("vtkMultiBlockDataSet"):
            if child.GetNumberOfBlocks() == 0:
                errors.append("/".join(child_path) + ": empty group")
            walk_multiblock(child, child_path, leaves, errors, unclassified)
            continue

        leaves.append((child_path, child))
        for key in REQUIRED_METADATA:
            if field_string(child, key) == "":
                errors.append("/".join(child_path) + f": missing metadata {key}")
        if field_string(child, "AnatomicalSystem") == "unclassified":
            unclassified.append("/".join(child_path))


def main(argv):
    parser = argparse.ArgumentParser(
        description="Validate xcat2vtk anatomical hierarchy metadata."
    )
    parser.add_argument("vtm", type=Path, help="Path to a hierarchical .vtm file")
    parser.add_argument(
        "--expected-leaves",
        type=int,
        help="Expected number of leaf datasets from the source flat scene",
    )
    args = parser.parse_args(argv)

    try:
        import vtk  # type: ignore
    except ImportError:
        print("ERROR: Python VTK bindings are required for validation.", file=sys.stderr)
        return 2

    if not args.vtm.exists():
        print(f"ERROR: file does not exist: {args.vtm}", file=sys.stderr)
        return 2

    reader = vtk.vtkXMLMultiBlockDataReader()
    reader.SetFileName(str(args.vtm))
    reader.Update()
    root = reader.GetOutput()
    if root is None or not root.IsA("vtkMultiBlockDataSet"):
        print(f"ERROR: not a readable vtkMultiBlockDataSet: {args.vtm}", file=sys.stderr)
        return 1

    leaves = []
    errors = []
    unclassified = []
    walk_multiblock(root, [], leaves, errors, unclassified)

    canonical_ids = {}
    source_indices = {}
    for path, obj in leaves:
        path_text = "/".join(path)
        canonical_id = field_string(obj, "CanonicalAnatomyIdentifier")
        if canonical_id:
            canonical_ids.setdefault(canonical_id, []).append(path_text)
        source_index = field_string(obj, "OriginalBlockIndex")
        if source_index:
            source_indices.setdefault(source_index, []).append(path_text)

    for canonical_id, paths in canonical_ids.items():
        if len(paths) > 1:
            errors.append(f"duplicate canonical id {canonical_id}: {paths}")

    for source_index, paths in source_indices.items():
        if len(paths) > 1:
            errors.append(f"duplicate source block assignment {source_index}: {paths}")

    if args.expected_leaves is not None and len(leaves) != args.expected_leaves:
        errors.append(
            f"leaf count mismatch: expected {args.expected_leaves}, found {len(leaves)}"
        )

    print("Anatomy hierarchy validation")
    print("----------------------------")
    print(f"File: {args.vtm}")
    print(f"Leaves: {len(leaves)}")
    print(f"Unclassified leaves: {len(unclassified)}")
    if unclassified:
        for path in unclassified:
            print(f"  unclassified: {path}")

    if errors:
        print("\nERRORS:")
        for error in errors:
            print(f"  {error}")
        return 1

    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
