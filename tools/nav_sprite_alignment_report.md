# Navigation Sprite Alignment Report

Source: `resources/nav_animation/nav_sheet.png`

- Source size: `1774×887`
- Fixed frame canvas: `180×150`
- Frames per group: `8`
- Output groups: `1440×150`
- Runtime scaling: one fixed `36×32` icon viewport
- Runtime content analysis / tight crop / per-frame scaling: disabled

## Fixed crop grid

X crop starts: `47, 256, 464, 673, 884, 1094, 1303, 1513`

Y source windows and fixed-canvas placement:

| Group | Source Y | Source H | Canvas Y | Anchor definition |
| --- | ---: | ---: | ---: | --- |
| dashboard | 22 | 150 | 0 | dashboard outer frame |
| motor_debug | 168 | 150 | 0 | motor outer ring center |
| firmware | 317 | 150 | 0 | firmware chip body and pins |
| manipulator | 459 | 147 | 4 | manipulator base |
| vision | 586 | 140 | 4 | vision camera body |
| settings | 715 | 150 | 0 | settings center hole |

The manipulator and vision source windows are deliberately shorter fixed windows
because the supplied rows overlap vertically. Their final canvases remain exactly
`180×150`, with fixed transparent padding at `canvasY=4`; this removes neighboring
row pixels without tight-cropping or re-centering the animated content.

## Integer translation overrides

All frames currently use the explicit manual override `(dx, dy) = (0, 0)`.
The overrides are kept in `nav_sprite_offsets.json`; future corrections must remain
integer translations only and must not add scaling, rotation, tight cropping, or
automatic re-centering.

| Group | F0 | F1 | F2 | F3 | F4 | F5 | F6 | F7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| dashboard | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) |
| motor_debug | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) |
| firmware | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) |
| manipulator | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) |
| vision | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) |
| settings | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) | (0,0) |
