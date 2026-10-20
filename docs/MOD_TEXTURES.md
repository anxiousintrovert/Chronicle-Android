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

PNG sources must stay within the package. Limits are 256 declarations per package, 32 MiB encoded per image, 8192 pixels per side and 16 million decoded pixels. The runtime fully decodes images during validation; the offline author tool checks signature/dimensions, paths and declarations but does not fully decode pixel data. Pack/install includes declared PNGs. Runtime reloading verifies decoded source content against the activation snapshot; missing, invalid, changed or escaped sources fall back to the original path and log the reason. GPU allocation failures are not yet covered by this fallback contract.

The original identity is computed before localization/caption/ground processing. Replacement is applied after those operations, so an explicitly selected mod image wins. The SHA-256 input is ASCII `chronicle-rgba-v1:<width>:<height>:` followed by the original base level's row-major RGBA8 bytes. Indexed images expand their palette first, using the renderer's converted alpha. It is not a PNG-file hash. Author-friendly target discovery/export remains outstanding; do not guess the digest from a filename. Existing DC_TEXTURE_DUMP output may already contain localization or ground transformations and is not an authoritative original digest source.

Replacement uses one RGBA level and retains the original alpha-mode flag. It is a static image replacement: indexed palette cycling/recoloring is not reproduced by a single RGBA image. Animated or palette-driven targets, texture-copy paths and representative retail assets still require scenario validation. No arbitrary external quest/model formats or new engine content registries are implied.

This capability has synthetic native texture-entry, GPU rendering, conflict, changed-source fallback and disable/restart tests. Retail capture/replacement and target-discovery tooling are still required before declaring the first release ready.
