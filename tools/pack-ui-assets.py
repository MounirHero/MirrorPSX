"""Rebuild the UI atlas from the checked-in artwork and original input PNGs."""
from pathlib import Path
from PIL import Image, ImageOps, ImageFilter
import cairosvg
import io

root = Path(__file__).resolve().parents[1]
assets = root / "assets/ui"
names = ["cross", "circle", "square", "triangle", "l1", "r1", "dpad", "controller"]
ui_names = ["monitor-up", "settings", "wifi", "pause", "play", "circle-help", "x", "maximize"]
atlas = Image.new("RGBA", (1024, 824))
background = Image.open(assets / "backdrop.png").convert("RGBA").resize((960, 540), Image.Resampling.LANCZOS)
atlas.paste(background.filter(ImageFilter.GaussianBlur(28)), (0, 0))
for i, name in enumerate(names):
    icon = Image.open(assets / "prompts" / f"{name}.png").convert("RGBA")
    icon = ImageOps.contain(icon.crop(icon.getbbox()), (112, 112), Image.Resampling.LANCZOS)
    atlas.paste(icon, (i * 128 + (128 - icon.width) // 2, 552 + (128 - icon.height) // 2))
for i, name in enumerate(ui_names):
    source = (assets / "icons" / f"{name}.svg").read_text().replace('stroke="currentColor"', 'stroke="#ffffff"')
    png = cairosvg.svg2png(bytestring=source.encode(), output_width=104, output_height=104)
    icon = Image.open(io.BytesIO(png)).convert("RGBA")
    atlas.paste(icon, (i * 128 + 12, 680 + 12))
atlas.save(assets / "atlas.png")
(assets / "atlas.rgba").write_bytes(atlas.tobytes())
print("UI atlas: 1024 x 824, RGBA8")
