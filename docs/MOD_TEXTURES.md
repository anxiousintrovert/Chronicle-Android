# Full-resolution texture packages

The `assets.textures.v1` capability replaces decoded existing textures with PNG images of independent dimensions. It does not resize native asset buffers or alter extracted files. The port preserves the original logical dimensions and gives upstream Kazarov the replacement pixel dimensions; existing UVs and atlas rectangles continue to address the original logical space.

Add this field to a normal schema-1 manifest (and keep `files: {}` when no raw assets are replaced):

```json
"textures": [
  {
    "name": "native_texture_name",
    "original_sha256": "<64 lowercase hexadecimal digits identifying the original decoded pixels>",
    "source": "textures/replacement.png"
  }
]
```

The digest placeholder above must be replaced before validation. Names are 1-31 ASCII letters, digits, dots, underscores or hyphens; matching folds ASCII case. The target is the name plus original content digest, not the name alone. Different textures with the same name can therefore have independent replacements. Identical original content with the same name is intentionally one target wherever loaded.

New packages remain disabled until explicitly enabled in the shared mod manager and restarted. Two enabled packages claiming the same target block both and their dependents. The manager/report explain texture conflicts. Rescanning updates the next-launch preview without changing active textures.

PNG sources must stay within the package. Limits are 256 declarations per package, 32 MiB encoded per image, 8192 pixels per side and 16 million decoded pixels. The runtime fully decodes images during validation; the offline author tool checks signature/dimensions, paths and declarations but does not fully decode pixel data. Pack/install includes declared PNGs. Runtime reloading verifies decoded source content against the activation snapshot; missing, invalid, changed or escaped sources fall back to the original path and log the reason. If replacement upload fails, the port releases the returned temporary image/palette handles and retries the intact original. If the original also cannot be uploaded, the ordinary renderer failure remains; fallback cannot repair global GPU/device failure.

The original identity is computed before localization/caption/ground processing. Replacement is applied after those operations, so an explicitly selected mod image wins. The SHA-256 input is ASCII `chronicle-rgba-v1:<width>:<height>:` followed by the original base level's row-major RGBA8 bytes. Indexed images expand their palette first, using the renderer's converted alpha. It is not a PNG-file hash. Use the discovery/export workflow below; do not guess the digest from a filename. Existing DC_TEXTURE_DUMP output may already contain localization or ground transformations and is not an authoritative original digest source.

Replacement uses one RGBA level and retains the original alpha-mode flag. It is a static image replacement: indexed palette cycling/recoloring is not reproduced by a single RGBA image. Animated or palette-driven targets, texture-copy paths and representative retail assets still require scenario validation. No arbitrary external quest/model formats or new engine content registries are implied.

This capability has synthetic native texture-entry, GPU rendering, conflict, changed-source fallback and disable/restart tests. Retail replacement rendering and broader palette/copy scenarios are still required before declaring the first release ready.

## Discover and export a target

Run an ordinary game session with an explicit new capture directory under an existing parent:

```powershell
.\darkcloud.exe --data "C:\path\to\data" --save "C:\path\to\test-save" --discover-textures "C:\path\to\new-captures"
```

Visit the scene containing the texture, then exit. This authoring option captures original base-level RGBA before localization or mod replacements, preserving alpha and expanding indexed palettes. Without the option, capture is dormant. Existing output directories and every PS2 path are rejected. Each `.texture` snapshot is exclusively and atomically published; duplicate name/content targets are skipped. Capture stops at 2048 targets or 256 MiB. Captures contain game assets and stay outside the repository.

Inspect a snapshot or export an editable PNG:

```powershell
python tools/chronicle_texture.py inspect "C:\path\to\new-captures\<capture>.texture"
python tools/chronicle_texture.py export "C:\path\to\new-captures\<capture>.texture" "C:\path\to\replacement.png"
```

Export verifies the stored content hash and exact pixel length, preserves RGBA/alpha, refuses overwrite, and prints the `name`, `original_sha256` and `source` declaration. Copy that declaration into the package's `textures` array and place the PNG at the declared package-relative source path. Edit/redraw the PNG at the intended resolution; keep the original target hash unchanged. Use `chronicle_mod.py check`, `pack` and `install` as for other packages, then explicitly enable and restart. PNG export uses only Python's standard library.

A `.texture` file has one UTF-8 JSON header line (schema 1, name, width, height, original_sha256, encoding rgba8), followed by exactly width*height*4 raw bytes. It is an authoring snapshot, not a replacement image format accepted in a package.
