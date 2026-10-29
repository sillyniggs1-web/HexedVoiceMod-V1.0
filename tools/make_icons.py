# Generates the 23 new VoiceFX avatar icons (256x256 RGBA, circular, same look as the existing v_*.png).
import sys, math, numpy as np
from PIL import Image, ImageDraw
S = 4; N = 256 * S
def C(r, g, b, a=255): return (int(r), int(g), int(b), a)
class D:
    def __init__(s): s.im = Image.new('RGBA', (N, N), (0, 0, 0, 0)); s.d = ImageDraw.Draw(s.im)
    def k(s, v): return v * S
    def rect(s, x0, y0, x1, y1, f, r=0, o=None, w=0):
        k = s.k
        if r: s.d.rounded_rectangle([k(x0), k(y0), k(x1), k(y1)], radius=k(r), fill=f, outline=o, width=int(round(k(w))))
        else: s.d.rectangle([k(x0), k(y0), k(x1), k(y1)], fill=f, outline=o, width=int(round(k(w))))
    def ell(s, cx, cy, rx, ry, f, o=None, w=0): k = s.k; s.d.ellipse([k(cx - rx), k(cy - ry), k(cx + rx), k(cy + ry)], fill=f, outline=o, width=int(round(k(w))))
    def poly(s, pts, f, o=None): s.d.polygon([(s.k(x), s.k(y)) for x, y in pts], fill=f, outline=o)
    def line(s, pts, f, w=4): s.d.line([(s.k(x), s.k(y)) for x, y in pts], fill=f, width=int(round(s.k(w))), joint='curve')
    def arc(s, cx, cy, rx, ry, a0, a1, f, w=4): k = s.k; s.d.arc([k(cx - rx), k(cy - ry), k(cx + rx), k(cy + ry)], a0, a1, fill=f, width=k(w))
    def pie(s, cx, cy, rx, ry, a0, a1, f): k = s.k; s.d.pieslice([k(cx - rx), k(cy - ry), k(cx + rx), k(cy + ry)], a0, a1, fill=f)

def finish(art, base, name, outdir):
    # radial gradient disc + rim (like the existing icons), artwork on top, clipped to the circle
    yy, xx = np.mgrid[0:N, 0:N]; cx = cy = N / 2
    r = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / (N / 2)
    t = np.clip(r, 0, 1) ** 1.4
    b = np.array(base, dtype=float)
    light = b + (255 - b) * 0.38; dark = b * 0.36
    rgb = light[None, None, :] * (1 - t[..., None]) + dark[None, None, :] * t[..., None]
    bg = np.dstack([rgb, np.full((N, N), 255.)]).clip(0, 255).astype(np.uint8)
    im = Image.fromarray(bg, 'RGBA'); im.alpha_composite(art.im)
    mask = Image.new('L', (N, N), 0); ImageDraw.Draw(mask).ellipse([4 * S, 4 * S, N - 4 * S, N - 4 * S], fill=255)
    out = Image.new('RGBA', (N, N), (0, 0, 0, 0)); out.paste(im, (0, 0), mask)
    ImageDraw.Draw(out).ellipse([4 * S, 4 * S, N - 4 * S, N - 4 * S], outline=(88, 94, 104, 255), width=2 * S)
    out.resize((256, 256), Image.LANCZOS).save(f'{outdir}/{name}.png')

WHITE = C(240, 244, 248); DARK = C(18, 20, 26); SKIN = C(226, 170, 120); CY = C(40, 220, 245); RED = C(230, 60, 50); GRAY = C(150, 158, 170); LG = C(205, 212, 222)
def face(d, cx=128, cy=116, rw=44, rh=54, skin=SKIN):
    d.ell(cx, cy, rw, rh, skin)
def eyes(d, cx=128, cy=108, gap=20, r=5, col=DARK):
    d.ell(cx - gap, cy, r, r + 1, col); d.ell(cx + gap, cy, r, r + 1, col)
def shoulders(d, col, y=214):
    d.pie(128, y + 60, 82, 60, 180, 360, col)

def telephone(d):
    d.rect(62, 142, 194, 208, C(34, 38, 46), 18)
    d.ell(128, 176, 26, 26, LG); d.ell(128, 176, 8, 8, C(34, 38, 46))
    for a in range(0, 300, 40): d.ell(128 + 17 * math.cos(math.radians(a - 40)), 176 + 17 * math.sin(math.radians(a - 40)), 3.2, 3.2, C(34, 38, 46))
    d.rect(50, 92, 206, 126, C(235, 70, 60), 17); d.rect(50, 78, 84, 134, C(235, 70, 60), 12); d.rect(172, 78, 206, 134, C(235, 70, 60), 12)
    d.rect(88, 126, 168, 142, C(34, 38, 46), 4)
    for r in (22, 34): d.arc(128, 70, r, r, 200, 340, WHITE, 4)
def radio(d):
    d.line([(170, 72), (214, 30)], LG, 4); d.ell(214, 30, 7, 7, RED)
    d.rect(44, 76, 212, 190, C(190, 110, 50), 16); d.rect(52, 84, 204, 182, C(214, 140, 66), 12)
    d.ell(100, 134, 40, 40, C(60, 36, 20)); 
    for r in (34, 24, 14): d.ell(100, 134, r, r, None, C(214, 140, 66), 3)
    d.rect(152, 96, 196, 120, C(250, 235, 190), 4); d.line([(172, 98), (172, 118)], RED, 3)
    d.ell(162, 150, 10, 10, C(60, 36, 20)); d.ell(188, 150, 10, 10, C(60, 36, 20)); d.line([(152, 172), (196, 172)], C(60, 36, 20), 3)
def megaphone(d):
    d.poly([(70, 106), (176, 56), (176, 178), (70, 146)], C(235, 70, 60)); d.poly([(70, 106), (110, 90), (110, 160), (70, 146)], C(255, 150, 120))
    d.rect(52, 104, 82, 148, C(40, 44, 52), 8); d.rect(84, 148, 116, 196, C(40, 44, 52), 8)
    d.ell(176, 117, 12, 62, C(255, 210, 120))
    for r in (30, 48): d.arc(176, 117, r, r + 14, -60, 60, WHITE, 5)
def walkie(d):
    d.line([(104, 70), (104, 30)], GRAY, 7); d.ell(104, 28, 7, 7, RED)
    d.rect(70, 66, 160, 226, C(52, 60, 50), 16); d.rect(78, 74, 152, 130, C(120, 190, 120), 6)
    d.line([(88, 90), (140, 90)], C(40, 90, 50), 4); d.line([(88, 104), (126, 104)], C(40, 90, 50), 4)
    for i in range(5): d.line([(88, 148 + i * 12), (142, 148 + i * 12)], C(30, 36, 30), 5)
    d.rect(160, 100, 174, 150, C(235, 70, 60), 5)
    for r in (22, 36): d.arc(196, 110, r, r, -50, 50, WHITE, 4)
def muffled(d):
    face(d, 128, 108, 46, 56); eyes(d, 128, 98, 20, 5)
    d.line([(100, 80), (116, 84)], DARK, 4); d.line([(156, 80), (140, 84)], DARK, 4)
    d.rect(76, 118, 180, 186, C(120, 110, 150), 22); d.rect(76, 118, 180, 138, C(145, 135, 175), 12)
    for x in (100, 128, 156): d.line([(x, 142), (x - 4, 178)], C(90, 80, 120), 4)
    d.line([(190, 70), (214, 62)], WHITE, 4); d.line([(194, 88), (222, 88)], WHITE, 4); d.line([(190, 106), (214, 114)], WHITE, 4)
def underwater(d):
    d.rect(52, 96, 204, 170, C(30, 36, 50), 34); d.rect(64, 106, 192, 160, C(90, 200, 240), 26); d.rect(70, 110, 120, 128, C(200, 240, 255), 9)
    d.line([(128, 106), (128, 160)], C(30, 36, 50), 8); d.line([(52, 132), (24, 124)], C(30, 36, 50), 8); d.line([(204, 132), (232, 124)], C(30, 36, 50), 8)
    d.rect(186, 40, 198, 100, C(250, 180, 40), 6)
    for (x, y, r) in [(80, 70, 9), (100, 48, 6), (62, 46, 5), (150, 196, 8), (176, 210, 5), (120, 218, 6)]: d.ell(x, y, r, r, None, WHITE, 3)
    d.line([(20, 232), (50, 224), (80, 232), (110, 224), (140, 232), (170, 224), (200, 232), (236, 224)], C(180, 230, 255), 4)
def studio(d):
    d.rect(94, 40, 162, 134, C(180, 188, 200), 34)
    for y in range(58, 124, 11): d.line([(98, y), (158, y)], C(90, 98, 112), 3)
    d.arc(128, 112, 54, 56, 10, 170, DARK, 8); d.line([(128, 168), (128, 204)], DARK, 8); d.rect(94, 204, 162, 214, DARK, 5)
    d.ell(128, 178, 26, 26, None, C(240, 244, 248, 200), 4)
def host(d):
    face(d, 128, 120, 42, 52); eyes(d, 128, 114, 18, 4.5)
    d.arc(128, 116, 68, 74, 180, 360, DARK, 12); d.rect(52, 106, 74, 150, C(235, 70, 60), 9); d.rect(182, 106, 204, 150, C(235, 70, 60), 9)
    d.line([(60, 150), (86, 168), (112, 160)], GRAY, 4); d.ell(116, 158, 9, 9, DARK)
    d.line([(112, 150), (126, 156)], C(150, 70, 60), 4); d.line([(110, 150), (126, 150)], C(150, 50, 40), 5)
    shoulders(d, C(40, 44, 56), 200)
def echo(d):
    d.ell(70, 128, 18, 18, WHITE); d.rect(62, 118, 78, 138, C(30, 70, 140), 6)
    for i, (r, a) in enumerate([(38, 255), (66, 200), (94, 140), (122, 80)]): d.arc(70, 128, r, r, -52, 52, C(255, 255, 255, a), 8 - i)
def cave(d):
    d.rect(0, 150, 256, 256, C(60, 50, 42)); d.pie(128, 196, 96, 120, 180, 360, C(14, 12, 14))
    d.poly([(24, 256), (24, 120), (60, 70), (128, 46), (196, 70), (232, 120), (232, 256), (200, 256), (200, 130), (172, 96), (128, 80), (84, 96), (56, 130), (56, 256)], C(96, 82, 70))
    for x, h in [(84, 34), (112, 54), (140, 40), (168, 28)]: d.poly([(x - 9, 88), (x + 9, 88), (x, 88 + h)], C(130, 114, 98))
    for x, h in [(96, 26), (150, 36)]: d.poly([(x - 10, 214), (x + 10, 214), (x, 214 - h)], C(110, 96, 84))
    d.ell(128, 168, 10, 10, C(255, 210, 90)); d.arc(128, 168, 26, 26, 0, 360, C(255, 210, 90, 120), 3)
def stadium(d):
    d.ell(128, 150, 108, 66, C(60, 70, 62)); d.ell(128, 150, 90, 52, C(90, 100, 92)); d.ell(128, 150, 66, 36, C(60, 160, 80))
    d.line([(128, 116), (128, 184)], C(255, 255, 255, 160), 2); d.ell(128, 150, 14, 9, None, C(255, 255, 255, 170), 2)
    for x in (34, 222):
        d.line([(x, 150), (x, 78)], GRAY, 5); d.rect(x - 14, 62, x + 14, 78, C(255, 240, 160), 4)
    d.poly([(34, 66), (4, 24), (64, 24)], C(255, 240, 160, 70)); d.poly([(222, 66), (192, 24), (252, 24)], C(255, 240, 160, 70))
def chorus(d):
    for (cx, cy, r, col, sc) in [(70, 132, 28, C(120, 190, 255), 0.85), (186, 132, 28, C(255, 160, 200), 0.85), (128, 112, 36, C(255, 220, 120), 1.0)]:
        d.ell(cx, cy, r * sc, r * sc * 1.1, col); d.pie(cx, cy + 90 * sc, 56 * sc, 52 * sc, 180, 360, col)
    d.ell(128, 160, 10, 14, DARK); d.ell(70, 176, 7, 10, DARK); d.ell(186, 176, 7, 10, DARK)
    for (x, y) in [(40, 60), (208, 56)]: d.ell(x, y + 16, 8, 6, WHITE); d.line([(x + 7, y + 14), (x + 7, y - 10), (x + 18, y - 4)], WHITE, 3)
def jet(d):
    d.poly([(128, 30), (146, 96), (230, 150), (230, 170), (146, 146), (140, 206), (172, 226), (172, 238), (128, 228), (84, 238), (84, 226), (116, 206), (110, 146), (26, 170), (26, 150), (110, 96)], LG)
    d.poly([(128, 30), (140, 80), (116, 80)], RED); d.ell(128, 98, 7, 16, C(60, 150, 220))
    for x in (64, 192): d.line([(x, 190), (x, 250)], C(255, 255, 255, 120), 5)
def drunk(d):
    d.rect(76, 82, 164, 202, C(250, 190, 50), 10); d.rect(84, 94, 100, 190, C(255, 225, 130), 6)
    d.arc(176, 140, 30, 34, -90, 90, C(250, 190, 50), 12)
    for (x, y, r) in [(86, 78, 18), (112, 68, 22), (142, 76, 20), (160, 86, 14)]: d.ell(x, y, r, r, WHITE)
    for (x, y) in [(112, 130), (132, 160), (122, 182)]: d.ell(x, y, 5, 5, C(255, 240, 180))
    for (x, y) in [(36, 56), (214, 60), (42, 168)]:
        d.line([(x - 10, y), (x + 10, y)], WHITE, 4); d.line([(x, y - 10), (x, y + 10)], WHITE, 4)
    d.arc(40, 128, 16, 16, 0, 270, WHITE, 4)
def overlord(d):
    d.pie(128, 128, 78, 78, 180, 360, C(110, 116, 130)); d.rect(50, 126, 206, 190, C(110, 116, 130), 8)
    d.rect(44, 118, 212, 134, C(70, 76, 90), 6); d.rect(62, 146, 194, 172, DARK, 12)
    d.ell(128, 159, 18, 11, RED); d.ell(128, 159, 8, 8, C(255, 200, 190)); d.line([(128, 60), (128, 34)], GRAY, 5); d.ell(128, 30, 8, 8, RED)
    for x in range(70, 190, 16): d.line([(x, 182), (x, 188)], C(60, 66, 80), 3)
    shoulders(d, C(50, 54, 66), 214)
def cyborg(d):
    face(d, 128, 116, 46, 58)
    d.pie(128, 116, 46, 58, 270, 450, C(150, 160, 175)); d.line([(128, 58), (128, 174)], DARK, 4)
    d.ell(104, 104, 6, 7, DARK); d.ell(156, 104, 14, 14, DARK); d.ell(156, 104, 9, 9, CY); d.ell(153, 101, 3, 3, WHITE)
    d.line([(138, 142), (176, 142)], C(90, 98, 112), 5); d.line([(138, 154), (172, 154)], C(90, 98, 112), 4)
    d.line([(110, 144), (120, 148)], DARK, 4); d.ell(176, 76, 4, 4, RED); d.line([(150, 80), (178, 90), (178, 130)], C(90, 98, 112), 3)
    shoulders(d, C(48, 52, 64), 206)
def arcade(d):
    pix = ["..XXXXXX..", ".XXXXXXXX.", "XXXXXXXXXX", "XX..XX..XX", "XXXXXXXXXX", "XXXXXXXXXX", "XX.XXXX.XX", "XXX....XXX", ".XXXXXXXX.", "..XXXXXX.."]
    u = 14; ox, oy = 128 - 5 * u, 126 - 5 * u
    for r, row in enumerate(pix):
        for c, ch in enumerate(row):
            if ch == 'X': d.rect(ox + c * u, oy + r * u, ox + (c + 1) * u, oy + (r + 1) * u, C(255, 220, 60) if r < 8 else C(255, 170, 40))
    for (r, c) in [(3, 2), (3, 3), (3, 6), (3, 7)]: d.rect(ox + c * u, oy + r * u, ox + (c + 1) * u, oy + (r + 1) * u, DARK)
    for (r, c) in [(6, 2), (6, 7), (7, 3), (7, 4), (7, 5), (7, 6)]: d.rect(ox + c * u, oy + r * u, ox + (c + 1) * u, oy + (r + 1) * u, DARK)
    for (x, y) in [(36, 40), (216, 60), (40, 210), (214, 206)]: d.rect(x - 6, y - 6, x + 6, y + 6, WHITE)
def rocker(d):
    d.poly([(192, 40), (214, 62), (136, 140), (118, 122)], C(120, 70, 40)); d.rect(184, 28, 216, 56, C(40, 44, 52), 5)
    d.ell(94, 168, 52, 46, C(220, 40, 40)); d.ell(120, 130, 34, 30, C(220, 40, 40)); d.ell(94, 168, 26, 24, None, C(250, 200, 200), 4)
    d.ell(94, 168, 10, 10, DARK); d.rect(74, 190, 124, 200, C(235, 235, 240), 4)
    d.line([(70, 140), (200, 50)], C(235, 235, 240), 2)
    d.poly([(204, 168), (222, 140), (216, 176), (236, 170), (210, 216), (214, 186)], C(255, 210, 60))
def whisper(d):
    face(d, 128, 116, 46, 58); d.line([(98, 100), (114, 104)], DARK, 4); d.line([(142, 104), (158, 100)], DARK, 4)
    d.arc(106, 92, 12, 6, 200, 340, DARK, 3); d.arc(150, 92, 12, 6, 200, 340, DARK, 3)
    d.line([(114, 146), (142, 146)], C(180, 80, 70), 6)
    d.rect(117, 134, 139, 226, C(240, 190, 140), 11, C(190, 130, 90), 2); d.ell(128, 140, 11, 9, C(240, 190, 140))
    for r in (16, 28): d.arc(186, 150, r, r, -60, 60, WHITE, 4)
    shoulders(d, C(70, 80, 110), 208)
def vocoder(d):
    hs = [30, 62, 96, 74, 120, 88, 52, 100, 66, 36]
    for i, h in enumerate(hs):
        x = 36 + i * 19; t = i / 9.0; col = C(40 + 140 * t, 220 - 90 * t, 245)
        d.rect(x, 190 - h, x + 13, 190, col, 4); d.rect(x, 190 + 6, x + 13, 190 + 6 + h * 0.35, C(col[0], col[1], col[2], 110), 4)
    pts = [(30 + i * 4, 70 + 26 * math.sin(i / 3.0)) for i in range(55)]; d.line(pts, WHITE, 4)
def ghost(d):
    d.pie(128, 120, 58, 62, 180, 360, WHITE); d.rect(70, 118, 186, 200, WHITE)
    for i in range(4): d.ell(86 + i * 28, 200, 14, 16, WHITE)
    for i in range(3): d.ell(100 + i * 28, 205, 14, 12, None) if False else None
    d.ell(106, 118, 9, 13, DARK); d.ell(150, 118, 9, 13, DARK); d.ell(128, 154, 12, 16, DARK)
    d.ell(70, 94, 7, 7, C(255, 255, 255, 120)); d.ell(196, 80, 5, 5, C(255, 255, 255, 120))
    for (x, y) in [(36, 70), (220, 150)]: d.arc(x, y, 14, 14, 20, 300, C(255, 255, 255, 150), 3)
def oldman(d):
    face(d, 128, 112, 44, 54); d.ell(128, 66, 34, 14, None)
    d.ell(88, 100, 10, 18, C(235, 235, 240)); d.ell(168, 100, 10, 18, C(235, 235, 240))
    d.rect(96, 88, 118, 94, C(235, 235, 240), 3); d.rect(138, 88, 160, 94, C(235, 235, 240), 3)
    d.ell(106, 104, 6, 6, DARK); d.ell(150, 104, 6, 6, DARK); d.ell(106, 104, 14, 14, None, C(80, 60, 40), 3); d.ell(150, 104, 14, 14, None, C(80, 60, 40), 3); d.line([(120, 104), (136, 104)], C(80, 60, 40), 3)
    d.pie(128, 134, 50, 52, 0, 180, C(235, 235, 240)); d.ell(128, 138, 26, 11, C(250, 250, 252)); d.ell(128, 130, 8, 6, C(226, 150, 110))
    d.rect(76, 52, 180, 66, C(70, 80, 100), 6); d.pie(128, 66, 52, 30, 180, 360, C(70, 80, 100))
    d.line([(96, 76), (112, 74)], C(200, 200, 205), 4)
    shoulders(d, C(120, 80, 60), 216)
def villain(d):
    d.pie(128, 130, 82, 100, 180, 360, C(26, 20, 34)); d.rect(46, 130, 210, 230, C(26, 20, 34))
    d.ell(128, 136, 48, 56, C(8, 6, 12))
    d.poly([(96, 118), (120, 128), (96, 134)], RED); d.poly([(160, 118), (136, 128), (160, 134)], RED)
    d.poly([(92, 112), (122, 124)], RED); d.line([(90, 108), (120, 120)], C(40, 20, 40), 4); d.line([(166, 108), (136, 120)], C(40, 20, 40), 4)
    d.line([(112, 166), (144, 166)], C(70, 40, 80), 4)
    d.poly([(40, 230), (60, 170), (96, 190), (128, 256)], C(90, 30, 100)); d.poly([(216, 230), (196, 170), (160, 190), (128, 256)], C(90, 30, 100))

ICONS = {  # name: (draw fn, base colour)
 'v_telephone': (telephone, (110, 125, 145)), 'v_radio': (radio, (215, 125, 55)), 'v_megaphone': (megaphone, (220, 70, 60)),
 'v_walkie': (walkie, (70, 130, 70)), 'v_muffled': (muffled, (110, 90, 140)), 'v_underwater': (underwater, (30, 110, 200)),
 'v_studio': (studio, (110, 112, 160)), 'v_host': (host, (190, 50, 100)), 'v_echo': (echo, (50, 110, 210)),
 'v_cave': (cave, (110, 90, 70)), 'v_stadium': (stadium, (50, 150, 100)), 'v_chorus': (chorus, (130, 80, 200)),
 'v_jet': (jet, (60, 170, 205)), 'v_drunk': (drunk, (200, 140, 40)), 'v_overlord': (overlord, (130, 130, 160)),
 'v_cyborg': (cyborg, (50, 160, 175)), 'v_arcade': (arcade, (200, 60, 150)), 'v_rocker': (rocker, (180, 40, 40)),
 'v_whisper': (whisper, (120, 120, 160)), 'v_vocoder': (vocoder, (40, 90, 170)), 'v_ghost': (ghost, (95, 110, 150)),
 'v_oldman': (oldman, (150, 120, 85)), 'v_villain': (villain, (110, 40, 120)),
}

# ---------------------------------------------------------------- v8: Auto-Tune / chord icons
def star(d, cx, cy, r, col):
    q = r * 0.25
    d.poly([(cx, cy - r), (cx + q, cy - q), (cx + r, cy), (cx + q, cy + q), (cx, cy + r), (cx - q, cy + q), (cx - r, cy), (cx - q, cy - q)], col)
def note(d, x, y, col, s=1.0, stem=True):
    d.ell(x, y, 12 * s, 8.5 * s, col)
    if stem: d.line([(x + 10.5 * s, y - 1 * s), (x + 10.5 * s, y - 46 * s)], col, 4.2 * s)
def autotune(d):
    for i in range(5): d.line([(26, 78 + i * 24), (230, 78 + i * 24)], C(255, 255, 255, 55), 2)
    pts = [(30 + i * 3, 126 + 30 * math.sin(i * 0.55) * max(0.0, 1 - i / 32.0)) for i in range(34)]
    d.line(pts, C(40, 220, 245), 6); d.line([(128, 126), (214, 126)], WHITE, 8)
    d.ell(214, 126, 15, 11, WHITE); d.line([(226, 124), (226, 78)], WHITE, 5)
    d.ell(128, 126, 7, 7, C(40, 220, 245)); d.arc(128, 126, 18, 18, 0, 360, C(40, 220, 245, 120), 3)
def hardtune(d):
    d.line([(34, 168), (60, 150), (84, 176), (108, 144), (134, 160), (160, 118), (186, 140), (224, 96)], C(255, 120, 190, 110), 5)    # the wobbly original
    st = [(34, 190), (84, 190), (84, 152), (134, 152), (134, 112), (184, 112), (184, 74), (226, 74)]
    d.line(st, C(255, 255, 255, 80), 15); d.line(st, WHITE, 8)
    d.poly([(150, 168), (176, 168), (162, 196), (182, 196), (136, 244), (148, 206), (128, 206)], C(255, 220, 70))
def magicchords(d):
    for (x, y) in [(116, 168), (116, 128), (116, 88)]:
        d.ell(x, y, 22, 22, C(190, 140, 255, 90)); note(d, x, y, WHITE, 1.15, stem=False)
    d.line([(137, 170), (137, 56)], WHITE, 5)
    d.poly([(137, 56), (168, 70), (164, 84), (137, 74)], WHITE)
    for (x, y, r, col) in [(52, 70, 17, C(255, 225, 120)), (200, 150, 21, C(255, 225, 120)), (58, 190, 12, WHITE), (205, 70, 10, WHITE), (36, 130, 9, C(255, 225, 120))]: star(d, x, y, r, col)
def choir(d):
    for (cx, cy, r, rob, sc) in [(64, 138, 25, C(170, 40, 50), 0.82), (192, 138, 25, C(170, 40, 50), 0.82), (128, 118, 31, C(205, 60, 60), 1.0)]:
        d.pie(cx, cy + 92 * sc, 58 * sc, 62 * sc, 180, 360, rob)
        d.poly([(cx - 16 * sc, cy + 36 * sc), (cx + 16 * sc, cy + 36 * sc), (cx, cy + 62 * sc)], WHITE)
        d.ell(cx, cy, r * 0.95 * sc / 0.82 * 0.82, r * 1.12 * sc / 0.82 * 0.82, SKIN)
        d.ell(cx - 9 * sc, cy - 6 * sc, 3 * sc, 4 * sc, DARK); d.ell(cx + 9 * sc, cy - 6 * sc, 3 * sc, 4 * sc, DARK)
        d.ell(cx, cy + 12 * sc, 7 * sc, 9 * sc, DARK)
    d.rect(96, 196, 160, 232, C(245, 230, 190), 4); d.line([(128, 196), (128, 232)], C(150, 120, 80), 3)
    note(d, 40, 70, C(255, 225, 120), 0.8); note(d, 214, 66, C(255, 225, 120), 0.8)
def angel(d):
    d.poly([(104, 130), (44, 80), (24, 118), (50, 128), (30, 150), (62, 152), (50, 176), (104, 170)], WHITE)
    d.poly([(152, 130), (212, 80), (232, 118), (206, 128), (226, 150), (194, 152), (206, 176), (152, 170)], WHITE)
    d.ell(128, 128, 38, 46, SKIN); d.pie(128, 212, 60, 52, 180, 360, WHITE)
    d.arc(112, 124, 8, 4, 200, 340, DARK, 3); d.arc(144, 124, 8, 4, 200, 340, DARK, 3); d.ell(128, 150, 8, 10, DARK)
    d.ell(128, 70, 38, 11, None, C(255, 215, 80), 7)
    for (x, y, r) in [(40, 50, 11), (216, 44, 9), (200, 196, 8)]: star(d, x, y, r, C(255, 240, 160))
def powerchord(d):
    d.rect(52, 86, 204, 206, C(36, 38, 46), 12); d.rect(62, 126, 194, 196, C(92, 78, 60), 6)
    for y in range(132, 194, 8): d.line([(64, y), (192, y)], C(62, 52, 40), 2)
    for x in range(72, 190, 36): d.ell(x, 106, 8, 8, C(220, 220, 228)); d.line([(x, 106), (x + 3, 100)], DARK, 2)
    d.rect(70, 98, 80, 112, C(255, 255, 255, 0), 0)
    d.poly([(148, 42), (112, 128), (136, 128), (116, 214), (186, 106), (158, 106), (190, 42)], C(255, 215, 60), C(160, 90, 20))
def darkchords(d):
    for i in range(7):
        x = 36 + i * 26.5; on = i in (0, 2, 4)
        d.rect(x, 128, x + 25, 214, C(150, 100, 255) if on else C(225, 225, 235), 4, C(40, 30, 60), 2)
    for i in (0, 1, 3, 4, 5):
        x = 36 + i * 26.5 + 17; d.rect(x, 128, x + 17, 178, C(20, 16, 30), 3)
    d.ell(176, 66, 30, 30, C(245, 240, 215)); d.ell(190, 58, 27, 27, C(60, 36, 96))
    star(d, 100, 56, 12, WHITE); star(d, 64, 96, 8, C(190, 160, 255)); star(d, 224, 112, 8, WHITE)
def duet(d):
    for (cx, col, sg) in [(78, C(120, 170, 255), 1), (178, C(255, 150, 190), -1)]:
        d.pie(cx, cy := 214, 50, 48, 180, 360, col)
        d.ell(cx, 136, 33, 41, SKIN)
        d.arc(cx - 10, 126, 8, 5, 20, 160, DARK, 3); d.arc(cx + 10, 126, 8, 5, 20, 160, DARK, 3)
        d.ell(cx + sg * 5, 156, 7, 9, DARK)
    note(d, 118, 96, WHITE, 1.0); note(d, 146, 74, C(255, 225, 120), 0.85)
    star(d, 128, 44, 10, C(255, 225, 120))
def octaver(d):
    note(d, 90, 200, WHITE, 1.45); note(d, 168, 100, C(150, 215, 255), 0.85)
    d.line([(176, 130), (176, 190)], C(255, 225, 120), 6); d.poly([(176, 214), (160, 190), (192, 190)], C(255, 225, 120)); d.poly([(176, 106), (160, 130), (192, 130)], C(255, 225, 120))
    for y in (86, 104, 122): d.line([(36, y), (110, y)], C(255, 255, 255, 50), 2)
    star(d, 52, 52, 10, WHITE); star(d, 214, 190, 8, C(255, 225, 120))
def dreamy(d):
    d.ell(176, 62, 28, 28, C(250, 240, 200)); d.ell(190, 54, 25, 25, C(60, 56, 120))
    for (x, y, r) in [(60, 56, 11), (110, 36, 8), (214, 118, 8), (40, 120, 8)]: star(d, x, y, r, C(255, 250, 220))
    for (x, y, rx, ry) in [(84, 188, 40, 24), (128, 174, 46, 32), (176, 190, 40, 24), (128, 200, 78, 22)]: d.ell(x, y, rx, ry, C(245, 246, 255))
    note(d, 104, 150, C(150, 140, 230), 0.9); note(d, 156, 134, C(200, 150, 240), 0.8)

ICONS.update({
 'v_autotune': (autotune, (40, 130, 200)), 'v_hardtune': (hardtune, (200, 50, 130)), 'v_magicchords': (magicchords, (110, 60, 200)),
 'v_choir': (choir, (190, 130, 50)), 'v_angel': (angel, (130, 150, 210)), 'v_powerchord': (powerchord, (190, 50, 40)),
 'v_darkchords': (darkchords, (70, 40, 120)), 'v_duet': (duet, (230, 110, 80)), 'v_octaver': (octaver, (50, 120, 160)),
 'v_dreamy': (dreamy, (90, 80, 190)),
})
NEW8 = ['v_autotune', 'v_hardtune', 'v_magicchords', 'v_choir', 'v_angel', 'v_powerchord', 'v_darkchords', 'v_duet', 'v_octaver', 'v_dreamy']

if __name__ == '__main__':
    out = sys.argv[1]
    only = sys.argv[2:] 
    for name, (fn, base) in ICONS.items():
        if only and name not in only: continue
        d = D(); fn(d); finish(d, base, name, out)
    print('icons written to', out)
