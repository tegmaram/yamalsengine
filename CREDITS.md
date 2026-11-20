# Credits

Assets are **not** bundled yet. When textures/models/audio land, they come from these
sources (all CC0 unless noted) — keep this file in sync.

```
Textures, HDRIs and models: Poly Haven — CC0 1.0 (https://polyhaven.com/license)
Textures:                   ambientCG — CC0 1.0 (https://docs.ambientcg.com/license/)
Textures:                   cgbookcase — CC0 1.0 (https://www.cgbookcase.com/textures)
Models:                     Kenney — CC0 1.0 (https://kenney.nl)
Models:                     Quaternius — Quaternius Asset License v1.0 (https://quaternius.com)
Audio:                      Sonniss GameAudioGDC — royalty-free, no attribution
Fonts:                      SIL Open Font License (ship OFL.txt with the font)
```

## Notes

- **Poly Haven**: best API (`api.polyhaven.com`), CC0, 8K max, tiles: `concrete_floor_01`,
  `stone_wall_02`, weathered wood/brick. HDRIs are useful as ground-truth references for
  the procedural sky.
- **ambientCG**: ~2000 CC0 materials, direct downloads
  (`https://ambientcg.com/get?file=Concrete049_4K-JPG.zip`), documented JSON API.
- **cgbookcase**: best grime/rust/stain/wet maps for horror interiors.
- **FreePBR**: ⚠ NOT CC0 — non-commercial only, no redistribution. Avoid unless purchased.
- **ShareTextures**: CC0-based but forbids redistribution as a collection and hotlinking.
- **Freesound**: per-file licenses; filter to CC0, log every CC-BY file.
- **Sketchfab**: CC0 filter only, verify per model.

## Texture pipeline (when we get there)

Prototype: ship 1K–2K PNG/JPG, decoded with DirectXTex or stb_image.
Later: `toktx` → KTX2 with BC7 (albedo), BC5 (normals, reconstruct Z in shader),
never sRGB-tag normal maps, generate mips offline.
