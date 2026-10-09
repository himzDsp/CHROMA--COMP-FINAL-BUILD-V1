CHROMA COMP PRESET FILES (JSON)
===============================

All presets are plain-text JSON files in one folder:

  Windows       : C:\Users\<you>\Chroma-Comp\Presets\
  macOS / Linux : ~/Chroma-Comp/Presets/

  Factory\      read-only presets. "Init" is built into the plugin and also written here as Init.json
                (the built-in values always win, so editing Init.json changes nothing). Add your own .json files
                here to make factory presets: reopen the plugin window and they show under "Factory".
  Favourites\   a copy of every preset you starred (the star button). Kept in step with the original; delete the
                copy or press the star again to remove a favourite.
  User\         the presets you save with SAVE. DEL deletes the file.

The file name is the preset name. If the home folder cannot be written, the plugin falls back to
%APPDATA%\HimzDSP\Chroma Comp\Presets (macOS: ~/Library/Application Support/HimzDSP/Chroma Comp/Presets).

Presets saved by older versions (.ccpreset files in %APPDATA%\HimzDSP\Chroma Comp\Presets and "Factory Presets",
plus Favourites.txt) are converted to JSON once, the first time this version runs. The old files stay where they are.

How to make a factory preset
----------------------------
1. Set up the sound in the plugin, press SAVE and name it (it is written to User\).
2. Move that .json file from User\ into Factory\.
3. Reopen the plugin window: it shows under "Factory", is read-only (SAVE will not overwrite it, DEL is greyed out).

File format
-----------
{
  "format": "ChromaCompPreset",
  "version": 1,
  "name": "My Preset",
  "parameters": {
    "threshold": -18.0,
    "ratio": 3.0,
    "knee": 6.0,
    ...
  }
}

Parameter ids and value ranges
------------------------------
  threshold      -60 .. 0      dB
  ratio          1 .. 20       :1
  knee           0 .. 30       dB (0 = hard knee, higher = softer)
  attack         0.1 .. 200    ms
  release        5 .. 1000     ms
  makeup         0 .. 24       dB
  mix            0 .. 100      %
  lookahead      0 or 1        lookahead on / off
  lookaheadTime  0 .. 10       ms (default 1.5)
  hold           0 .. 500      ms (0 = off)
  range          0 .. 120      dB
  autoRelease    0 or 1
  adaptiveOn     0 or 1        adaptive engine on / off
  adapt          0 .. 100      %
  feel           0..3          0 Tight, 1 Subtle, 2 Balance, 3 Glue
  intensity      0..2          0 Low, 1 Mid, 2 High
  transients     0 or 1        0 Preserve, 1 Unpreserved

Ids you leave out fall back to the Init values (no compression), except adaptiveOn (falls back to 1 = on), so list
every parameter you care about. Input / Output gain are not part of presets.
