# Mesh authoring format (preparation stage)

`tools/chronicle_mesh.py` prepares triangle geometry for the forthcoming mesh
replacement path. **The current game does not load this format yet.** Do not add
it to a manifest's `files` mapping: that mapping still replaces compatible native
assets. No new mesh runtime capability is advertised by this tool, and it does
not install packages, edit game assets, enable mods, launch the game, or fetch
external resources. Runtime selection, conflict checks and renderer integration
remain in the coverage ledger.

## Commands

Use Python 3 with its standard library:

```powershell
python tools/chronicle_mesh.py obj path/to/triangulated.obj path/to/model.mesh.json
python tools/chronicle_mesh.py inspect path/to/model.mesh.json
```

Conversion prints the output path, vertex/triangle counts, material strip IDs and
axis-aligned bounds. Output publication is exclusive: an existing destination is
never overwritten, and an unsuccessful publication leaves no partial destination.
The parent directory must already exist and support hard links. Paths containing
a `ps2` directory are rejected. Input is read-only. This Python-only tool does not
compile native code; subsequent native integration uses Chronicle's Clang build.

## Geometry schema 1

A UTF-8 JSON object contains exactly three fields:

```json
{
  "schema_version": 1,
  "vertices": [
    [0, 0, 0, 0, 0, 1, 0, 0],
    [1, 0, 0, 0, 0, 1, 1, 0],
    [0, 1, 0, 0, 0, 1, 0, 1]
  ],
  "triangles": [[0, 0, 1, 2]]
}
```

Each vertex is `[x,y,z,nx,ny,nz,u,v]` in source model coordinates. Each triangle is
`[strip,a,b,c]`, where the three zero-based vertex indices retain source winding.
`strip` is a native material-strip selector from 0 to 255, not an item, weapon,
character, model, or texture registry ID. An eventual runtime adapter must check
that the chosen source visual actually has every referenced strip; this offline
tool cannot prove that match. The format does not prescribe contributor-owned
custom content IDs or skeleton registries.

Limits:

- Source and serialized output: 16 MiB each.
- Vertices: 3..100000; triangles: 1..200000.
- All eight vertex components must be finite numbers with absolute value at most
  1000000. Booleans are not numbers or indices.
- Normals must be unit length within 0.001 after conversion to renderer float32 precision.
- Triangle indices must be distinct and in bounds. Squared cross-product length
  must exceed 1e-20 after float32 conversion, rejecting zero-area, extremely small
  and precision-collapsed triangles.
- Unknown fields and duplicate JSON keys are rejected.

The native `DecodeMeshAsset` decoder enforces the same limits before returning
detached float32 geometry. It rejects nested objects and excessive JSON nesting,
and performs no graphics allocation, native-state write, RNG call or file read.
It is not yet connected to package activation or visual construction.

The format stores geometry only. Texture pixels, external material definitions,
vertex colors, skin weights, animation, attachments and transforms are not
imported. GLB/DAE and runtime replacement remain follow-up work. Importing an OBJ
successfully does not establish that it fits a particular retail skeleton,
material layout, allocation budget or model.

## OBJ input contract

Export triangulated OBJ geometry. Supported records are `v x y z`, `vt u v`,
`vn x y z`, triangle `f` records, and `usemtl stripN`. Faces accept positive
one-based and negative relative indices, including `v`, `v/vt`, `v//vn`, and
`v/vt/vn` forms. Indices must refer to records already read. Non-triangle faces,
points/lines and unknown record types are rejected rather than silently dropped.

Explicit normals are normalized. Missing normals use the face's flat normal;
missing UVs become `[0,0]`. No smoothing or coordinate-system conversion is
performed. Identical complete vertices are deduplicated; differing UVs or normals
remain separate. Face and winding order are preserved. Material defaults to
`strip0`; named materials must be `strip0` through `strip255`.

Object/group names, smoothing declarations and material-library declarations are
ignored. Material libraries are never opened, so absolute/relative paths in an
`mtllib` line cannot make the converter read another file. This is deliberate:
use explicit normals for smooth surfaces and numbered material strips for the
future native material mapping.


## Native skin-binding preparation

`MeshSkinBinding` prepares a correspondence from each replacement point to the
nearest undeformed source point in model coordinates. Equal-distance ties choose
the lowest source index. A balanced spatial tree accelerates matching; a binding
allows at most 100000 points on each side and 25000000 search-node visits total.
Pathological inputs fail preparation rather than spending unbounded time there.

Each replacement vertex then inherits that source vertex's native bone influences
in native order. The calculation uses the replacement's own rest position and
the same arithmetic as the unchanged `gameutil_motion.cpp` skin helper, including
bind translation, accumulated weighted motion and the inverse owner transform.
Pose reset and bone application allocate no memory. Invalid/nonfinite weights,
matrices or overflowing results reject an influence without partially changing
its affected vertices. Preparation does not write source points or consume RNG.

This is an internal adapter, not an exposed package feature yet. Native
`MotionProc2` notifications now drive attached bindings, and visual rebuild,
arena retirement and transient sweeping detach them. Bindings reject changed rest
pointers/counts and invalid influences until explicitly rebuilt. Registration is
bounded to 256 visuals and one million source/replacement points each. Native
morph/color/material updates, replacement visual publication, package conflict
resolution and retail replacement-rendering tests remain required before package
mesh replacement is advertised.
No custom skeleton, motion, item, weapon or cutscene registry is introduced.
