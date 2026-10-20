# Mesh authoring format (preparation stage)

`tools/chronicle_mesh.py` prepares triangle geometry for the forthcoming mesh
replacement path. **The game validates this format but cannot activate mesh packages yet.** Do not add
it to a manifest's `files` mapping: that mapping still replaces compatible native
assets. No new mesh runtime capability is advertised by this tool, and it does
not install packages, edit game assets, enable mods, launch the game, or fetch
external resources. Runtime package selection and native construction attachment remain in the
coverage ledger; staged declarations and conflict checks are available below.

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
bounded to 256 visuals and one million source/replacement points each. Native morph/color/material updates and GPU publication are integrated for
internal bindings. Package activation, native construction attachment and retail
replacement-rendering tests remain required before mesh replacement is advertised.
No custom skeleton, motion, item, weapon or cutscene registry is introduced.


## Detached visual preparation

`PrepareMeshVisual` builds CPU-side replacement geometry with the source visual's
material/texture state. Authored triangles are grouped by source strip, preserving
winding and order within each strip. Missing material strips, invalid typed
geometry and immediate per-frame visuals are rejected. The result has no GPU
handle and does not change the source visual.

A retained nearest-source-vertex map supplies vertex colors and morph-only
position deltas. `RefreshMeshVisual` instead accepts a complete replacement skin
pose when one is available; callers must combine any additional native morph
residuals before supplying that pose. Replacement normals and UVs stay as
exported, matching the native remake path's treatment of built normals/UVs.
Material refresh preserves replacement index ranges. It validates all updates
before publishing and allocates nothing, so malformed updates cannot leave a
partially refreshed visual.

These helpers remain internal preparation. The game does not yet select a mesh package or attach replacements during
native visual construction. Internal registration can now upload a separate GPU
mesh, and native draw/remake paths resolve and refresh it while retaining the
original as fallback. Native animation and synthetic GPU tests are not retail
replacement-model validation. Package activation and retail visual evidence are still required. Replacement-aware frame bounds are now integrated as described below.


## Internal GPU publication

`AttachReplacementVisual` requires a registered source visual, native model rest
points and the flattened-render-vertex to native-vertex index mapping. That exact
mapping keeps color/morph correspondence and inherited bone influences aligned,
including duplicate source positions. It stages geometry, a GPU mesh and registry
storage before replacing a committed binding. Failed attachments retain the old
replacement. The original visual and GPU mesh remain available for fallback.

The draw adapter selects the replacement only while it is healthy. Native remake
refreshes colors/materials and either source displacement or a skin pose plus the
residual between the latest native source positions and recorded native skin
output. This prevents double-applying skinning and retains later morph changes.
Invalid updates stop selecting the replacement until rebuilt; visual retirement
releases its GPU handle and skin binding. Empty registries leave native drawing
and updates in place.

Limits are 256 replacement visuals, one million replacement vertices, one million
flattened source points, and three million triangle indices across committed
replacements, in addition to the per-asset and skin-registry limits. These bound
framework resources; they do not change native array capacities. This internal
registration API is not yet callable by packages or a new public host operation.


## Replacement-aware culling

Frame culling uses the original native corners unless a healthy internal mesh
replacement exists for that visual's native model. Active replacements expand a
temporary bounding box to include the original corners and replacement geometry;
no native frame corner or model data is written. The same temporary corners feed
normal and shadow culling.

Bounds are cached after successful publication and refresh. Culling includes both
the previous and current published poses, and all registered buffers sharing the
model identity, because MDT drawing selects its double buffer after frame culling.
This is conservative: it can draw extra geometry, but avoids rejecting an extended
replacement using the smaller original bound. Existing remake-pending visibility
behavior is unchanged. Failed replacements cease contributing bounds; native
fallback remains available. With no registered replacement, the helper returns
before querying the visual's model identity or calculating bounds.

This does not enable mesh packages. Native construction attachment, target discovery and runtime activation remain
pending; staged declarations and conflict diagnostics are described below.


## Staged package declarations

Mesh package activation is **not yet available**. Authors can validate, pack,
install and inspect conflicts for this declaration now, but enabling a package
with meshes blocks the whole package with an explicit diagnostic. Its scripts,
plugins and other overrides do not become active. Packages remain disabled by
default. This staging gate will be removed only with native construction
attachment and retail validation; no mesh capability is advertised yet.

Add an optional `meshes` array to the normal `mod.json`:

```json
"meshes": [
  {
    "texture": "hero_body",
    "native_vertices": 120,
    "source": "models/hero.mesh.json"
  }
]
```

`texture` is a case-insensitive native texture name, 1-31 ASCII letters, digits,
dots, underscores or hyphens. `native_vertices`, when supplied, is an integer
from 1 to 100000 referring to the original model's vertex count, not the exported
replacement count. Omit it to target every count for that name. This is target
metadata, not a change to native capacities. These targets reserve existing model
replacement only; they do not allocate custom item, weapon or character IDs.

`source` is a contained relative package path to the schema-1 mesh JSON described
above. Geometry is validated during discovery even for disabled packages. Limits
are 16 declarations, 16 MiB per file, and 64 MiB total declared mesh bytes per
package; repeated references count toward the total. Unknown declaration fields,
escaping paths, invalid geometry and overlapping targets within a package are
rejected.

Two enabled packages conflict when their normalized texture names match and
vertex counts match, or either declaration omits its count. Both packages receive
the target and other owner's ID in their shared mod-manager diagnostics. Every
collision is reported, including three-way collisions; blocked dependencies are
propagated. Distinct exact vertex counts do not conflict. Disabled packages do
not claim targets. This is a conservative declaration check; native target
resolution and ambiguity checks still need runtime integration.

`chronicle_mod.py validate`, `pack`, `install` and `check-set` understand mesh
files. Packing includes only declared files and existing permitted metadata;
installation does not enable a package. `check-set` also reports the temporary
activation gate, so an offline-valid draft cannot be mistaken for a runnable mod.
