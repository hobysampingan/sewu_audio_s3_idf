import re
import os

filepath = r"c:\Users\NEWLIFE\Documents\Arduino\audiodecoder\UAS\sewu_audio_s3\sewu_audio_s3_idf\components\sewu_ui\sewu_ui.c"

with open(filepath, "r", encoding="utf-8") as f:
    content = f.read()

# 1. Add include
if '#include "sewu_gfx.h"' not in content:
    content = content.replace('#include "sewu_app_state.h"', '#include "sewu_app_state.h"\n#include "sewu_gfx.h"')

# 2. Update inv_c macro
content = content.replace('#define inv_c(r, g, b) rgb565(255-(r), 255-(g), 255-(b))', '#define inv_c(r, g, b) gfx_color565(255-(r), 255-(g), 255-(b))')

# 3. Remove FONT 5x7
font_start = "/* ================================================================\n   FONT 5x7"
runtime_start = "/* ================================================================\n   RUNTIME STATE"
if font_start in content and runtime_start in content:
    start_idx = content.find(font_start)
    end_idx = content.find(runtime_start)
    content = content[:start_idx] + content[end_idx:]

# 4. Remove DRAWING PRIMITIVES
draw_start = "/* ================================================================\n   DRAWING PRIMITIVES"
vu_start = "/* ================================================================\n   VERTICAL VU METER"
if draw_start in content and vu_start in content:
    start_idx = content.find(draw_start)
    end_idx = content.find(vu_start)
    content = content[:start_idx] + content[end_idx:]

# 5. Remove s_fill_buf and s_glyph_buf
content = re.sub(r"static uint16_t s_fill_buf\[.*?\];\n", "", content)
content = re.sub(r"static uint16_t s_glyph_buf\[.*?\];\n", "", content)
content = re.sub(r"static uint16_t rgb565\(.*?\).*?}\n", "", content, flags=re.DOTALL)

# 6. Replace function calls
# Only replace calls that are exactly matched
content = re.sub(r"\bfill_rect\(", "gfx_fill_rect(", content)
content = re.sub(r"\bdraw_rect\(", "gfx_draw_rect(", content)
content = re.sub(r"\bdraw_text\(", "gfx_draw_text(", content)
content = re.sub(r"\bdraw_text2x\(", "gfx_draw_text2x(", content)
content = re.sub(r"\bdraw_textf2x\(", "gfx_printf2x(", content)
content = re.sub(r"\bdraw_textf\(", "gfx_printf(", content)

# 7. Add gfx_init to tft_init
if "gfx_init(s_panel" not in content:
    content = content.replace("s_tft_ready = true;\n    return true;", "s_tft_ready = true;\n    gfx_init(s_panel, GFX_ROT_0);\n    return true;")

with open(filepath, "w", encoding="utf-8") as f:
    f.write(content)

print("sewu_ui.c refactored successfully.")
