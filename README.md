# Custom Camo for Geronimo

A [GML](https://github.com/RemArrow/geronimo-mod-loader) plugin that adds a camo to the kit station.
Every kit item that offers camos (plate carriers, belts, helmets and covers, shirts, pants, gloves,
the balaclava: 46 items) gets one more option at the end of its colour menu. Nothing else changes.

## How camo works in Geronimo

Read from the game's code:

- The whole outfit is one combined mesh with one material (`M_MasterMaterial_Arrays`). It samples
  `/Game/Characters/Common/Camo`, a texture array with one 2048x2048 slice per camo (23).
- `BP_GameInstance.Camos` lists the camo tiles in slice order.
- Each kit item (`/Game/Characters/Data/DA_*`, an `OutfitItem`) has a `Colors` map: option name ->
  `ColorOptionStruct` {tile texture, tint A/B/C, overall tint, pattern tiling}. The item's `_ID` mask
  splits it into red, green and blue areas for tints A, B and C. Camo options keep tint A white (the
  pattern as painted) and give B and C solid colours for trim and webbing; plain colours use the grey
  `T_MidColor_Tile` with tints.
- Picking an option at the station: the game finds its tile in `Camos`, and that index is the slice
  the material samples.

## What the plugin does

1. **A 24th slice.** `Paks\zzz_CustomCamo_P` is the game's camo array package rebuilt with the 23
   original slices byte for byte and the new camo appended as slice 23 (DXT5, all 12 mips). GML copies
   it to `Content\Paks\~GML` at launch; it overrides the game's package (same package ID).
2. **The camo list.** The camo, as a runtime texture built into the DLL (also the menu swatch), is
   appended to `BP_GameInstance.Camos`, so its index is 23. If the list doesn't have exactly 23
   entries (a game update), nothing is added.
3. **The options.** Each item's `Colors` map gets an entry copied from one of its existing camos
   (`CopyTintsFrom`, Flecktarn by default) with the tile swapped for ours. GML can only read maps, so
   the plugin inserts the element itself, and only after checking the map's layout (element count,
   allocation bits, every bucket chain). The element goes at the end of every bucket chain, so lookups
   find it whatever bucket the game's FName hash picks. This build's FName hash didn't match any
   textbook hash, so it isn't computed. The game never removes colour options, the one thing this
   would confuse.

## Install

Needs [GML 2.2.0 or newer](https://github.com/RemArrow/geronimo-mod-loader/releases). The easiest way is
[GML Manager](https://github.com/RemArrow/geronimo-mod-loader#gml-manager): Custom Camo is in its Browse tab. By hand: close
the game, download `CustomCamo-<version>.zip` from [Releases](https://github.com/RemArrow/geronimo-custom-camo/releases) and
extract it into `...GERONIMOGeronimoBinariesWin64`. That adds `GMLplugins$d`. To remove the
plugin, delete that folder.

## Build and install

```bat
rem put the camo picture (a seamless tile, any size) in Assets\camo.png / .jpg / .webp
build.bat install
```

`build.bat` scales the picture to the slice format (`Utils\UAssetParser camoprep`), extracts the
game's camo array with retoc, adds the slice (`camoarray add`), packs the container
(`Utils\retoc\retoc.exe to-zen`), and compiles the plugin. It needs the .NET 10 SDK, Visual Studio
2022 and the GML SDK. Install copies `CustomCamo.dll` and `Paks\` to
`...\Binaries\Win64\GML\plugins\CustomCamo\`. To remove it, delete that folder.

## Settings: `GML\config\com.remarrow.customcamo.cfg`

| Key | Default | |
|---|---|---|
| `[Camo] Name` | Custom Camo | the option's name in the menu (and in saved kits) |
| `[Camo] CopyTintsFrom` | Flecktarn | the existing camo whose trim tints and scale each item copies |
| `[Camo] Tiling` | 0 | pattern scale (the game's camos use 1); 0 = the copied option's |

## Testing

`tests\CustomCamoDev` (built, never installed) loads the team room, logs the JPC's options, puts the
JPC on the player in the custom camo, then Multicam and Flecktarn, through the game's gear manager, and
renders the player with its own scene capture (base colour) to PNGs.

Verified in game (2026-10-09):
- the camo list gets the camo as entry 23; all 46 camo items get the option, none refused;
- the JPC's menu list ends with the new option;
- the vest shows the new pattern (not Flecktarn, not the grey fallback), Multicam and Flecktarn still
  work;
- the rebuilt array's 23 game slices decode pixel-identical to the game's.

## Known limitations

- Multiplayer: everyone needs the plugin with the same `Name`; the host's game resolves the option.
  Verified in a two-player test (`Utils\MPDev`, 2026-10-13): a client put its plate carrier in the camo
  through its gear manager, and the host's copy of that player's gear (`ReplicatedGear`) has
  `DA_PlateCarrier_JPC=Custom Camo`, the option every machine with the plugin has as camo 23.
- A kit saved with the camo shows the item's default colour on a game without the plugin.
- Not yet checked: reloading a kit saved with the camo, and the swatch in the station's menu.
- Only one camo per build (slice 23). More would mean more slices and entries.
