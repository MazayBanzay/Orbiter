# Titan surface for Orbiter 2024

Global Cassini-based tiles for the whole moon and a detailed landing site on a sea shore.
Atmosphere, haze and clouds are a separate work (Config/GC/Titan.atm.cfg, the AtmoFX D3D9Client
fork); the physical model (Config/Titan.cfg atmosphere keys, Titan.dll) is untouched.

## What is installed

| Where | What |
|---|---|
| `Textures/Titan/Surf, Mask` levels 1-9 | global albedo and sea mask (levels 8-9 poleward of 54 deg only) |
| `Textures/Titan/Elev` levels 4-8 | global height, seas flat at their shoreline level |
| `Textures/Titan/Surf, Mask, Elev` levels 10-18 | the landing site, up to ~1 m/px (Elev 0.5 m steps) |
| `Textures/D3D9Titan_A/B/C.dds` + `BODY Titan` in `Config/MicroTex.cfg` | micro textures (backup: `MicroTex.cfg.bak_titan`) |
| `Config/Titan.cfg` | TileFormat 2, MaxPatchResolution 21, ElevationResolution 0.5, Min/MaxElevation (backup: `Titan.cfg.bak_surface`) |

## Sources (data in "Orbiter 2016/Install/Titan_src")

- Cassini ISS global mosaic 4 km/px, PIA19658 (USGS): brightness.
- Birch et al. 2017 polar geomorphologic maps (CC BY 4.0): seas (Fm), filled lakes (Fl), empty basins (El).
- Miller et al. 2021 river networks.
- Cassini RADAR GTDR, model GTI (all altimetry and SARTopo through T77): height.
- Huygens DISR: the look of the ground at metre scale (rounded water-ice cobbles 5-15 cm on dark sand)
  and the red slope of the surface reflectance.

## Conventions

- Tiles hold surface **albedo** in sRGB with a slight red slope (`TINT` 1.12/1.0/0.84), not the
  orange light: dark sediment/dunes 0.055, ice-rich uplands 0.20, liquid 0.010. The orange comes
  from the atmosphere rendering. (The Orbiter 2016 set by Tantra/tools/gen_titan_tiles.py baked the
  orange light into the tiles.)
- Sea level: each hemisphere's seas share one level, the median GTI along their shorelines:
  -617 m in the north. (The 10th percentile inside the seas, -926 m, lay ~400 m below the shore
  terrain because GTI is interpolated across the seas.)
- Mask alpha 0 = liquid. Liquid reflections need the water shader variant: on the stock client the
  Mars shader has no water, so seas render matte and dark; the AtmoFX fork adds `AtmoFX_Water = 1`.

## Landing site: southern Kraken Mare, ~59.33 N 40.49 E

A peninsula in southern Kraken Mare whose eastern shore is cut by drowned river valleys (rias);
a mapped river reaches the head of one inlet here. Lower latitude than Ligeia (tiles less squeezed
east-west) and close to the mapped rivers.

- From data: shoreline, rivers, large-scale height, brightness.
- Synthesised below the data resolution (~0.3-4 km), from one continuous function of lon/lat so all
  levels agree: fractal coastline around the mapped one; dissected ice-bedrock uplands (ridged
  multifractal) with a gully network; valleys along the mapped rivers falling to the shore with a
  channel; cobble fans at the mouths; a dark wet margin; a flat liquid surface. Albedo follows the
  geology: bare ice bright on slopes and crests, organic sediment dark in gullies, valleys and the
  coastal lowland.
- Detail fades into the global height and colour 45-60 km from the centre.
- Coverage: levels 10-13 +-64 km, 14 +-40, 15 +-24, 16 +-12, 17 +-6, 18 +-2.6 km.
- Points: shore lon 40.55357 lat 59.33350 (level -617.25 m); flat ground 75 m from the water
  lon 40.55030 lat 59.33350 (-617.05 m).

Micro texture C (`tools/gen_microtex_titan_c.py`) replaces the Tantra one, whose two east-west dunes
per 2.6 km tile repeated as identical rows over the whole moon, including dune-free polar shores.
A and B are the Tantra ones (cobbles on dark sand; patchy cobble fields with rills).

## Rebuild

```
python tools/gen_titan_global.py "C:\Games\Orbiter-2024" "C:\Games\Orbiter 2016\Install\Titan_src"
python tools/gen_titan_site.py "C:\Games\Orbiter-2024" "C:\Games\Orbiter 2016\Install\Titan_src" 10
python tools/gen_microtex_titan_c.py "C:\Games\Orbiter-2024"
```

`gen_titan_site.py` caches its 25 m distance rasters in `tools/_titan_site_master_v3.npz` (regenerated
in ~15 s if missing). The site build takes ~1 h on 10 processes.

## Not real

There are no metre-scale data of Titan except the single Huygens view: everything finer than the
radar and ISS maps at the site is a plausible synthesis tied to them, not a map.
