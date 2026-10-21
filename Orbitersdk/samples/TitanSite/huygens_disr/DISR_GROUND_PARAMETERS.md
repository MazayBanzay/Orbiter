# Huygens landing site ground: parameters for the D3D11 client

Sources (downloaded 2026-10-19 from the NASA PDS Atmospheres Node, volume hpdisr_0001, EXTRAS):
- `POSTER_F.JPG` — DISR images taken after landing (Karkoschka processing), with a Moon footprint at the same scale.
- `POSTER_G.JPG` — Titan cobbles at actual size.
- `MOSAIC_1m.PNG` — the landing site mosaic, 1 m/px (Karkoschka).
Literature: Tomasko et al. 2005 (Nature 438); Keller et al. 2008 (PSS 56); Karkoschka & Schröder 2016 (Icarus);
Zarnecki et al. 2005 (Nature 438, penetrometer); ESA/NASA notes ("rounded cobbles 10-15 cm on a darker granular
surface"; radio echo "5-10 cm stones").

## What the surface images show (read off POSTER_F/G; to be refined with the full papers)

| Property | Value | Notes |
|---|---|---|
| Cobble shape | rounded, oblate ("flattened pebbles"), height/width ~0.35-0.5 | fluvial, worn edges |
| Cobble size | 5-15 cm near the camera, finer to the horizon; largest seen ~15-20 cm | radio: 5-10 cm typical |
| Cover | ~20-35 % of the area in cobble fields, patchy; cobbles visible to the horizon | single flood, ~1 m/s (Keller 2008) |
| Embedding | partly buried; a dark ring ("moat") at the base of each cobble | scour or a contact shadow: key to the 3D look |
| Soil | fine granular dark material, grain visible down to ~cm; damp, cohesive | penetrometer: thin crust over soft ground |
| Brightness | cobbles slightly brighter than the soil (water ice), tops brighter than sides | reflectivity varies ~1 % within 100 m |
| Light | soft, low contrast, no hard shadows | diffuse sky dominates |
| Albedo convention | dark sediment 0.055, ice 0.20 (TitanSite README) | the orange is the light, not the ground |

## How the client uses it
- Step 2 (materials): soil = fine granular albedo + height noise at cm scale, damp darker; cobble-field cover 20-35 %.
- Step 3 (3D rocks): oblate rounded meshes (h/w 0.35-0.5), 5-20 cm, size-frequency weighted to small ones,
  partly sunk (30-50 %), with a dark contact ring/AO at the base; brighter tops; density from the ground classes.
- Everything finer than the data is synthesis tied to these numbers.
