#!/usr/bin/env python3
"""Generates the Tiled (.tmj) maps in assets/ui/ that the client uses as UI backgrounds.

Run once to (re)create the defaults; afterwards edit the .tmj files in Tiled -- the client re-reads them at start-up.
The UI sheets are nine-slice tile sheets with transparent separator cells:
  * 7x7 sheets (WoodTable, Banner, Ribbons): corners are 2x2 tiles, edges/centre are single tiles at row/col 3.
  * 5x5 sheets (Papers, Buttons, Bars):      corners/edges/centre are the tiles at rows/cols 0, 2 and 4.
Custom map properties read by the client: `scale` (draw scale of 64px tiles) and `padding` (inner content margin in px).
"""
import json, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "ui")
UI = "../UI Elements/UI Elements/"
SHEETS = {
    "WoodTable": (UI + "Wood Table/WoodTable.png", 448, 448),
    "RegularPaper": (UI + "Papers/RegularPaper.png", 320, 320),
    "BigRibbons": (UI + "Ribbons/BigRibbons.png", 448, 640),
    "BigBlueButton": (UI + "Buttons/BigBlueButton_Regular.png", 320, 320),
    "BigBlueButtonPressed": (UI + "Buttons/BigBlueButton_Pressed.png", 320, 320),
    "BigRedButton": (UI + "Buttons/BigRedButton_Regular.png", 320, 320),
    "BigRedButtonPressed": (UI + "Buttons/BigRedButton_Pressed.png", 320, 320),
}


def axis7(n, band):  # source indices along one axis for a 7-cell nine-slice (band: which edge pair)
    return [0, 1] + [3] * (n - 4) + [5, 6]


def axis5(n):
    return [0] + [2] * (n - 2) + [4]


def write(name, sheet, w, h, data, scale, padding):
    path, iw, ih = SHEETS[sheet]
    cols = iw // 64
    m = {
        "compressionlevel": -1, "height": h, "width": w, "infinite": False, "orientation": "orthogonal", "renderorder": "right-down",
        "tiledversion": "1.10.2", "version": "1.10", "type": "map", "tilewidth": 64, "tileheight": 64, "nextlayerid": 2, "nextobjectid": 1,
        "layers": [{"id": 1, "name": "Background", "type": "tilelayer", "visible": True, "opacity": 1, "x": 0, "y": 0, "width": w, "height": h, "data": data}],
        "tilesets": [{"firstgid": 1, "name": sheet, "image": path, "imagewidth": iw, "imageheight": ih, "tilewidth": 64, "tileheight": 64,
                      "columns": cols, "tilecount": cols * (ih // 64), "margin": 0, "spacing": 0}],
        "properties": [{"name": "scale", "type": "float", "value": scale}, {"name": "padding", "type": "int", "value": padding}],
    }
    with open(os.path.join(OUT, name + ".tmj"), "w") as f:
        json.dump(m, f, indent=1)


def panel7(name, sheet, w, h, scale, padding, row_offset=0):
    cols = SHEETS[sheet][1] // 64
    xs, ys = axis7(w, 0), axis7(h, 0)
    data = [ys[r] * cols + xs[c] + 1 + row_offset * cols for r in range(h) for c in range(w)]
    write(name, sheet, w, h, data, scale, padding)


def panel5(name, sheet, w, h, scale, padding):
    cols = SHEETS[sheet][1] // 64
    xs, ys = axis5(w), axis5(h)
    data = [ys[r] * cols + xs[c] + 1 for r in range(h) for c in range(w)]
    write(name, sheet, w, h, data, scale, padding)


os.makedirs(OUT, exist_ok=True)
panel7("hud_resources", "BigRibbons", 12, 2, 0.5, 14)              # blue ribbon (rows 0-1 of the sheet)
panel7("title_ribbon", "BigRibbons", 9, 2, 0.5, 10)
panel7("build_menu", "WoodTable", 9, 14, 0.5, 26)
panel7("minimap_frame", "WoodTable", 10, 10, 0.4, 20)
panel5("context_panel", "RegularPaper", 9, 12, 0.5, 18)
panel5("roster_panel", "RegularPaper", 7, 8, 0.5, 14)
panel5("button_blue", "BigBlueButton", 4, 2, 0.3, 6)
panel5("button_blue_pressed", "BigBlueButtonPressed", 4, 2, 0.3, 6)
panel5("button_red", "BigRedButton", 4, 2, 0.3, 6)
panel5("button_red_pressed", "BigRedButtonPressed", 4, 2, 0.3, 6)
print("wrote", sorted(os.listdir(OUT)))
