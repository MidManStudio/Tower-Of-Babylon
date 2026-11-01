# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/flashlight.md, section "tools/render_dump_gles.py"
# ============================================================================
# Draws the real dungeon geometry (quads written by tests/renderer_headless.c with FLASH_DUMP) through the real shaders of
# src/ui/highp_shader.c, with Mesa's software OpenGL ES (no GPU, no window): the flashlight off, then on, side by side, as
# the phone would draw them (800 x 450, 60 degree field of view, near 0.25, far 60). For looking at, not for pass or fail.
#   FLASH_DUMP=/tmp/fl ./renderer_headless
#   python3 tools/render_dump_gles.py /tmp/fl /tmp/flashlight.png        (needs numpy and Pillow)
import ctypes as C, math, os, re, sys
import numpy as np
from PIL import Image, ImageDraw

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
base, out = sys.argv[1], sys.argv[2]
os.environ.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
egl = C.CDLL("libEGL.so.1"); gl = C.CDLL("libGLESv2.so.2")
W, H = 800, 450

src = open(os.path.join(root, "src/ui/highp_shader.c")).read()
def grab(name):
    m = re.search(r'const char \*const %s =\s*((?:"[^\n]*"\s*)+);' % name, src)
    return ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))).replace('\\n', '\n')
cfg = open(os.path.join(root, "assets/config/flashlight.mdix")).read()
def num(key): return float(re.search(r"%s\s*=\s*([0-9.]+)f" % key, cfg).group(1))
INNER, OUTER, RANGE, STRENGTH = num("beam_inner_deg"), num("beam_outer_deg"), num("range"), num("strength")
TINT = int(re.search(r"tint\s*=\s*0x([0-9A-Fa-f]+)", cfg).group(1), 16)
tint = np.array([(TINT >> 16) & 255, (TINT >> 8) & 255, TINT & 255]) / 255.0

egl.eglGetProcAddress.restype = C.c_void_p; egl.eglGetProcAddress.argtypes = [C.c_char_p]
egl.eglGetDisplay.restype = C.c_void_p; egl.eglGetDisplay.argtypes = [C.c_void_p]
egl.eglInitialize.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int)]
egl.eglChooseConfig.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_void_p), C.c_int, C.POINTER(C.c_int)]
egl.eglCreateContext.restype = C.c_void_p; egl.eglCreateContext.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.POINTER(C.c_int)]
egl.eglCreatePbufferSurface.restype = C.c_void_p; egl.eglCreatePbufferSurface.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(C.c_int)]
egl.eglMakeCurrent.argtypes = [C.c_void_p] * 4
getp = C.CFUNCTYPE(C.c_void_p, C.c_uint, C.c_void_p, C.POINTER(C.c_int))(egl.eglGetProcAddress(b"eglGetPlatformDisplayEXT"))
d = getp(0x31DD, None, None) or egl.eglGetDisplay(None)
mj, mn = C.c_int(), C.c_int(); assert egl.eglInitialize(d, C.byref(mj), C.byref(mn)); egl.eglBindAPI(0x30A0)
cfgp = C.c_void_p(); n = C.c_int()
assert egl.eglChooseConfig(d, (C.c_int * 15)(0x3033, 1, 0x3040, 4, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3025, 24, 0x3038), C.byref(cfgp), 1, C.byref(n)) and n.value
ctx = egl.eglCreateContext(d, cfgp, None, (C.c_int * 3)(0x3098, 2, 0x3038))
surf = egl.eglCreatePbufferSurface(d, cfgp, (C.c_int * 5)(0x3057, W, 0x3056, H, 0x3038))
assert egl.eglMakeCurrent(d, surf, surf, ctx)
for nm, a in {"glClearDepthf": [C.c_float], "glUniform3f": [C.c_int] + [C.c_float] * 3, "glUniform4f": [C.c_int] + [C.c_float] * 4, "glClearColor": [C.c_float] * 4}.items(): getattr(gl, nm).argtypes = a
gl.glCreateShader.restype = C.c_uint; gl.glCreateProgram.restype = C.c_uint
gl.glGetAttribLocation.argtypes = [C.c_uint, C.c_char_p]; gl.glGetUniformLocation.argtypes = [C.c_uint, C.c_char_p]

def program(vs, fs):
    ids = []
    for kind, text in ((0x8B31, grab(vs)), (0x8B30, grab(fs))):
        s = gl.glCreateShader(kind); b = C.c_char_p(text.encode()); gl.glShaderSource(s, 1, C.byref(b), None); gl.glCompileShader(s)
        ok = C.c_int(); gl.glGetShaderiv(s, 0x8B81, C.byref(ok)); assert ok.value, "shader did not compile"
        ids.append(s)
    p = gl.glCreateProgram(); [gl.glAttachShader(p, s) for s in ids]; gl.glLinkProgram(p)
    return p
plain = program("HIGHP_VS_ES2", "HIGHP_FS_ES2"); flash = program("HIGHP_FLASH_VS_ES2", "HIGHP_FLASH_FS_ES2")

def load(path):
    out = []
    for line in open(path):
        v = line.split()
        if not v or v[0] != "Q": continue
        pts = [tuple(map(float, v[1 + 3 * i:4 + 3 * i])) for i in range(4)]
        cols = [tuple(int(x) for x in v[13 + 3 * i:16 + 3 * i]) + (255,) for i in range(4)]
        for i in (0, 1, 2, 0, 2, 3): out += list(pts[i]) + [0.0, 0.0] + [c / 255.0 for c in cols[i]]
    return np.array(out, np.float32)

eye = np.array(list(map(float, open(base + "_meta.txt").read().split()[:3])))
fwd = np.array(list(map(float, open(base + "_meta.txt").read().split()[3:6]))); fwd /= np.linalg.norm(fwd)
up0 = np.array([0.0, 1.0, 0.0]); right = np.cross(fwd, up0); right /= np.linalg.norm(right); up = np.cross(right, fwd)
V = np.eye(4); V[0, :3], V[1, :3], V[2, :3] = right, up, -fwd; V[:3, 3] = -V[:3, :3] @ eye
t = math.tan(math.radians(60) / 2); asp = W / H; zn, zf = 0.25, 60.0
P = np.zeros((4, 4)); P[0, 0] = 1 / (t * asp); P[1, 1] = 1 / t; P[2, 2] = -(zf + zn) / (zf - zn); P[2, 3] = -2 * zf * zn / (zf - zn); P[3, 2] = -1
MVP = (P @ V).astype(np.float32)
torch = eye + fwd * 0.10 + right * 0.12 - up * 0.08            # the same hand-held offset as main.c

gl.glGenTextures(1, C.byref(tex := C.c_uint())); gl.glBindTexture(0x0DE1, tex)
gl.glTexImage2D(0x0DE1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, (C.c_ubyte * 4)(255, 255, 255, 255))
gl.glTexParameteri(0x0DE1, 0x2801, 0x2600); gl.glTexParameteri(0x0DE1, 0x2800, 0x2600)

def render(prog, verts, beam):
    buf = C.c_uint(); gl.glGenBuffers(1, C.byref(buf)); gl.glBindBuffer(0x8892, buf)
    gl.glBufferData(0x8892, verts.nbytes, verts.ctypes.data_as(C.c_void_p), 0x88E4)
    gl.glViewport(0, 0, W, H); gl.glClearColor(0, 0, 0, 1); gl.glClearDepthf(1.0); gl.glClear(0x4100)
    gl.glEnable(0x0B71); gl.glEnable(0x0B44)                    # depth test and back face culling, as raylib has them
    gl.glUseProgram(prog)
    for name, size, off in ((b"vertexPosition", 3, 0), (b"vertexTexCoord", 2, 12), (b"vertexColor", 4, 20)):
        loc = gl.glGetAttribLocation(prog, name); gl.glEnableVertexAttribArray(loc); gl.glVertexAttribPointer(loc, size, 0x1406, 0, 36, C.c_void_p(off))
    gl.glUniformMatrix4fv(gl.glGetUniformLocation(prog, b"mvp"), 1, 0, np.ascontiguousarray(MVP.T).ctypes.data_as(C.c_void_p))
    gl.glUniform4f(gl.glGetUniformLocation(prog, b"colDiffuse"), 1, 1, 1, 1)
    gl.glActiveTexture(0x84C0); gl.glBindTexture(0x0DE1, tex); gl.glUniform1i(gl.glGetUniformLocation(prog, b"texture0"), 0)
    if beam:
        gl.glUniform3f(gl.glGetUniformLocation(prog, b"flashPos"), *torch); gl.glUniform3f(gl.glGetUniformLocation(prog, b"flashDir"), *fwd)
        gl.glUniform4f(gl.glGetUniformLocation(prog, b"flashParams"), math.cos(math.radians(INNER)), math.cos(math.radians(OUTER)), 1.0 / RANGE, STRENGTH)
        gl.glUniform3f(gl.glGetUniformLocation(prog, b"flashTint"), *tint)
    gl.glDrawArrays(0x0004, 0, len(verts) // 9)
    px = (C.c_ubyte * (W * H * 4))(); gl.glReadPixels(0, 0, W, H, 0x1908, 0x1401, px)
    return Image.fromarray(np.frombuffer(px, np.uint8).reshape(H, W, 4)[::-1, :, :3].copy())

off_img = render(plain, load(base + "_off.txt"), False)          # no flashlight: the dark surfaces are not even drawn
on_img = render(flash, load(base + "_on.txt"), True)             # flashlight on: the dark surfaces in the beam are drawn and lit
sheet = Image.new("RGB", (W, H * 2 + 4), (60, 60, 60))
sheet.paste(off_img, (0, 0)); sheet.paste(on_img, (0, H + 4))
dr = ImageDraw.Draw(sheet); dr.text((8, 6), "flashlight off", fill=(255, 255, 0)); dr.text((8, H + 10), "flashlight on", fill=(255, 255, 0))
sheet.save(out); print("wrote", out)
