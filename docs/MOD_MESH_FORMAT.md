# Mesh replacement and authoring

Enabled packages can replace existing native model geometry through
`assets.mesh_replace.v1`. Declare replacements in `meshes`, not the manifest
`files` mapping. Packages are disabled by default; enable them through the shared
Options > Mods menu and restart. Mesh-only packages need no Lua or native plugin.
`tools/chronicle_mesh.py` prepares geometry without installing or enabling mods,
changing game assets, launching the game, or fetching external resources.

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
character, model, or texture registry ID. The runtime adapter checks
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
Native visual construction connects through the package bridge described below.

The format stores geometry only. Texture pixels, external material definitions,
vertex colors, skin weights, animation, attachments and transforms are not
imported. GLB/DAE import remains follow-up work. Importing an OBJ
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
native material mapping.


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

The runtime uses this adapter for enabled mesh declarations. Native
`MotionProc2` notifications now drive attached bindings, and visual rebuild,
arena retirement and transient sweeping detach them. Bindings reject changed rest
pointers/counts and invalid influences until explicitly rebuilt. Registration is
bounded to 256 visuals and one million source/replacement points each. Native morph/color/material updates and GPU publication are integrated for
enabled package bindings.
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

These helpers feed native construction through the package bridge. Registration
uploads a separate GPU mesh, and native draw/remake paths resolve and refresh it
while retaining the original as fallback. Replacement-aware frame bounds are
integrated as described below.


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
framework resources; they do not change native array capacities. Packages use declarative targets rather than calling the internal registration
API directly; no new public host operation is needed.


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

## Package declarations

Packages remain disabled by default. Enable a validated package in Options > Mods
and restart to load its geometry. Add `assets.mesh_replace.v1` to
`requires_capabilities` so older hosts reject an unsupported package explicitly.

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
resolution additionally rejects ambiguous matches across different textures on
the same model, retains the native visual, and reports the owners involved.

`chronicle_mod.py validate`, `pack`, `install` and `check-set` understand mesh
files. Packing includes only declared files and existing permitted metadata;
installation does not enable a package. `check-set` checks declared conflicts;
offline validation cannot establish a match to an actual model or skeleton.


## Internal decoded catalog

`MeshCatalog` stages an immutable geometry snapshot per active owner. Loading
rechecks path containment and file size, decodes all declarations and publishes
the owner snapshot only after every asset succeeds. Failed loads preserve the
previous snapshot; startup integration retires failed owners before native
construction. Inactive packages perform no filesystem work. Removing an owner's
last declaration removes its cached definitions on successful reload.

The catalog caps all owners together at 256 definitions, 64 MiB encoded source,
one million vertices and one million triangles. Replacing an owner excludes its
previous snapshot from the new budget. These are framework cache limits and do
not enlarge native model arrays.

Selection uses case-insensitive names from all strips of a native visual and its
native vertex count. Repeated occurrences of one texture match a declaration only
once. Different declarations matching different textures on the same model are
ambiguous, even if manifest-level target checks could not detect the overlap.
Callers must require `Match::Unique()` before attachment; the two retained matches
provide owner/target information for diagnostics. Never choose a winner by package
or strip order. An ambiguous match must keep the original visual.

Selected definitions are shared immutable snapshots. Retiring an owner or clearing
the catalog removes it from future selection without invalidating a selection
already in use. Selection performs no filesystem access, geometry copying or GPU
allocation. The catalog does not itself attach visuals. The package bridge below handles
attachment.


## Native package bridge and diagnostics

The runtime owns a `MeshPackages` catalog. Eligible packages are decoded at
startup, before callbacks, and mesh-only owners can participate without Lua or a
native plugin. Packages that fail startup are retired before native construction.

Native MDT construction captures the flattened-to-native vertex correspondence
only while a catalog is active. After normal visual construction/upload, the
bridge selects a unique target and attaches a separate replacement mesh. Source
MDT data remains unchanged. With an empty catalog, no correspondence entries are
allocated and no package attachment runs. Unmatched models retain their original
visual without errors. Unsupported mappings, missing material strips and ambiguous
matches keep the original visual and produce owner-prefixed diagnostics.

Startup reset clears owned attachments. Callback failure or owner retirement
removes both catalog entries and that runtime's GPU replacements and skin bindings
before the next draw. Native visual retirement also releases attachments. A failed
replacement refresh selects the original visual and contributes a diagnostic.
The existing shared mod-manager menu displays these as module diagnostics, along
with script errors; no separate menu or plugin UI is introduced. Attachment
messages are deduplicated and bounded to 64, with at most 64 live refresh messages.

Native fixture tests cover construction, unchanged MDT bytes, cached geometry
after file removal, remake/morph updates, ambiguous two-texture models, material
mismatch, refresh failure, and an actual Lua callback failure retiring its mesh.
Author target discovery/export is available below. Retail validation scope is
described at the end of this document.


## Discovering real model targets

Start Chronicle with `--discover-meshes DIR`, where `DIR` does not exist and its
parent does. Use your own extracted data and a separate authoring save. For
example, with an existing `authoring` directory:

```powershell
.\build-mods\darkcloud.exe --data "C:\my-pal-data" --save ".\authoring\save" --discover-meshes ".\authoring\capture-01"
python tools/chronicle_mesh.py targets .\authoring\capture-01
```

Visit the scenes containing the models you want. Discovery observes native MDT
construction before the renderer releases its source triangle indices. It does
not enable mods or rewrite native model data. Without the flag the observer
returns immediately, without inspecting model geometry or accessing files.

Each unique geometry/target snapshot is an atomically published
`<sha256>.model.json`. Duplicate constructions produce no duplicate file. The
hash includes target metadata and exported geometry, not native addresses. A
capture stops after 256 unique snapshots or 64 MiB; each snapshot is at most
16 MiB. Existing output directories and all PS2 paths are rejected. Individual
unsupported models produce bounded, deduplicated stderr diagnostics while other
models can still be captured. The exit summary reports snapshot/error counts.

A snapshot contains exactly `schema_version: 1`, `native_vertices`, `textures`,
`mesh`, `fallback_normals`, and `dropped_degenerate_triangles`. `mesh` is the
schema-1 geometry above. Export normalizes native normals; zero-length normals
use `(0,0,1)` and are counted. Degenerate native triangles are omitted and counted.
Positions, UVs and strip indices remain references to the built visual. This is
an editable geometry reference, not a skeleton, animation, texture-image or
complete custom-model export. Native model data is unchanged by these adjustments.

List candidates with `targets`. It shows the original vertex count, all texture
names, manifest-compatible `targetable_textures`, geometry counts and export
adjustments. Models sharing a texture/count can still differ; inspect all matching
snapshots before declaring a broad replacement. A multi-texture model needs only
one matching declaration; declaring separate replacements for its different
textures makes runtime selection ambiguous.

Extract a ready-to-inspect framework mesh, or export an OBJ for a model editor:

```powershell
python tools/chronicle_mesh.py extract .\authoring\capture-01\HASH.model.json .\authoring\reference.mesh.json
python tools/chronicle_mesh.py export-obj .\authoring\capture-01\HASH.model.json .\authoring\reference.obj
python tools/chronicle_mesh.py obj .\authoring\edited.obj .\authoring\edited.mesh.json
```

Replace `HASH` with a listed snapshot name. Outputs must be new files outside PS2
folders. OBJ uses `usemtl strip0` through `strip255`; preserve those native strip
assignments when editing. OBJ import can deduplicate identical flattened vertices,
so use the snapshot's `native_vertices` for the manifest target, not the exported
OBJ vertex count. Export and import never install or enable a package.


## Validation and limitations

The repeatable `chronicle_mod_smoke.py --suite meshes` PAL scenario exercises
Toan's 403-vertex skinned body across both render buffers with doubled X positions.
It verifies changing animation, replacement selection, native position and RNG
preservation during rebuild, and cleanup/native restoration after disabling via
the shared manager lifecycle. Separate ordinary executable starts validate the
same package disabled and enabled. These are Windows llvm-mingw Clang/Vulkan
checks, not proof for every model or platform.

Replacements inherit existing skeletons, materials and animation. New skeletons,
animations, character registries, GLB/DAE imports and contributor-owned custom
item/weapon/cutscene frameworks are outside this feature. Discovery exports the
currently built geometry; inspect its pose before using it as a rest-shape authoring
reference. A malformed or unsupported runtime match falls back to native rendering
with a diagnostic in the shared manager.
