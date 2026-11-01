# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/flashlight.md, section "tests/shader_gles_test.py"
# ============================================================================
# Compiles the high precision shaders in src/ui/highp_shader.c with a real GLSL ES 1.00 compiler (Mesa's software OpenGL ES,
# through EGL, so no GPU and no window), renders a floor and a wall with them, and checks the pixels against the flashlight
# maths written out again in Python: the beam's pool, its edge, its fade with distance, and that with the beam off the new
# shader draws exactly what the plain one does. Skips (exit 0, says so) if Mesa's EGL and GLES2 libraries are not there.
# It cannot say how the shader behaves on a phone GPU: Mesa is a different compiler from the Mali's and more forgiving.
#   python3 tests/shader_gles_test.py        (needs numpy)
import ctypes as C, math, os, re, sys
import numpy as np

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
try:
    egl = C.CDLL("libEGL.so.1"); gl = C.CDLL("libGLESv2.so.2")
except OSError as e:
    print("SKIP: no EGL/GLES2 library here (%s)" % e); sys.exit(0)

failed = 0
def check(ok, what):
    global failed
    print("  %s: %s" % ("ok" if ok else "FAIL", what))
    if not ok: failed += 1

# ---- the shader sources, as they are in the C file ---------------------------------------------------------------
src = open(os.path.join(root, "src/ui/highp_shader.c")).read()
def grab(name):
    m = re.search(r'const char \*const %s =\s*((?:"[^\n]*"\s*)+);' % name, src)
    return ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))).replace('\\n', '\n')
SRC = {n: grab(n) for n in ("HIGHP_VS_ES2", "HIGHP_FS_ES2", "HIGHP_FLASH_VS_ES2", "HIGHP_FLASH_FS_ES2")}

# ---- an EGL context ----------------------------------------------------------------------------------------------
W, H = 800, 450
egl.eglGetProcAddress.restype = C.c_void_p; egl.eglGetProcAddress.argtypes = [C.c_char_p]
egl.eglGetDisplay.restype = C.c_void_p; egl.eglGetDisplay.argtypes = [C.c_void_p]
egl.eglInitialize.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int)]
egl.eglChooseConfig.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_void_p), C.c_int, C.POINTER(C.c_int)]
egl.eglCreateContext.restype = C.c_void_p; egl.eglCreateContext.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.POINTER(C.c_int)]
egl.eglCreatePbufferSurface.restype = C.c_void_p; egl.eglCreatePbufferSurface.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(C.c_int)]
egl.eglMakeCurrent.argtypes = [C.c_void_p] * 4
try:
    getp = C.CFUNCTYPE(C.c_void_p, C.c_uint, C.c_void_p, C.POINTER(C.c_int))(egl.eglGetProcAddress(b"eglGetPlatformDisplayEXT"))
    d = getp(0x31DD, None, None) or egl.eglGetDisplay(None)
    mj, mn = C.c_int(), C.c_int()
    assert egl.eglInitialize(d, C.byref(mj), C.byref(mn))
    egl.eglBindAPI(0x30A0)
    cfg = C.c_void_p(); n = C.c_int()
    assert egl.eglChooseConfig(d, (C.c_int * 13)(0x3033, 1, 0x3040, 4, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3038), C.byref(cfg), 1, C.byref(n))   # 8 bits a channel: the default can be 5-6-5 and n.value
    ctx = egl.eglCreateContext(d, cfg, None, (C.c_int * 3)(0x3098, 2, 0x3038))
    surf = egl.eglCreatePbufferSurface(d, cfg, (C.c_int * 5)(0x3057, W, 0x3056, H, 0x3038))
    assert egl.eglMakeCurrent(d, surf, surf, ctx)
except AssertionError:
    print("SKIP: could not make an OpenGL ES context here"); sys.exit(0)

for name, args in {"glUniform3f": [C.c_int, C.c_float, C.c_float, C.c_float], "glUniform4f": [C.c_int, C.c_float, C.c_float, C.c_float, C.c_float],
                   "glClearColor": [C.c_float] * 4}.items():
    getattr(gl, name).argtypes = args
gl.glCreateShader.restype = C.c_uint; gl.glCreateProgram.restype = C.c_uint; gl.glGetString.restype = C.c_char_p
gl.glGetAttribLocation.argtypes = [C.c_uint, C.c_char_p]; gl.glGetUniformLocation.argtypes = [C.c_uint, C.c_char_p]
print("GL: %s, %s" % (gl.glGetString(0x1F01).decode(), gl.glGetString(0x8B8C).decode()))

def compile_shader(kind, text):
    s = gl.glCreateShader(kind); b = C.c_char_p(text.encode()); gl.glShaderSource(s, 1, C.byref(b), None); gl.glCompileShader(s)
    ok = C.c_int(); gl.glGetShaderiv(s, 0x8B81, C.byref(ok)); ln = C.c_int(); gl.glGetShaderiv(s, 0x8B84, C.byref(ln))
    log = C.create_string_buffer(max(ln.value, 1)); gl.glGetShaderInfoLog(s, ln.value, None, log)
    return s, bool(ok.value), log.value.decode(errors="replace").strip()
def program(vs, fs):
    v = compile_shader(0x8B31, SRC[vs]); f = compile_shader(0x8B30, SRC[fs])
    if not (v[1] and f[1]): return None, v[2] + f[2]
    p = gl.glCreateProgram(); gl.glAttachShader(p, v[0]); gl.glAttachShader(p, f[0]); gl.glLinkProgram(p)
    ok = C.c_int(); gl.glGetProgramiv(p, 0x8B82, C.byref(ok))
    return (p if ok.value else None), ""

print("Compile")
plain, err1 = program("HIGHP_VS_ES2", "HIGHP_FS_ES2"); check(plain is not None, "the plain high precision shader compiles and links  " + err1)
flash, err2 = program("HIGHP_FLASH_VS_ES2", "HIGHP_FLASH_FS_ES2"); check(flash is not None, "the flashlight shader compiles and links  " + err2)
if flash is None or plain is None: sys.exit(1)
for nm in (b"vertexPosition", b"vertexTexCoord", b"vertexColor"): check(gl.glGetAttribLocation(flash, nm) >= 0 and gl.glGetAttribLocation(plain, nm) >= 0, "attribute %s is there in both (raylib's batches bind by name)" % nm.decode())
for nm in (b"mvp", b"texture0", b"colDiffuse"): check(gl.glGetUniformLocation(flash, nm) >= 0, "uniform %s is there (raylib sets it by name)" % nm.decode())
for nm in (b"flashPos", b"flashDir", b"flashParams", b"flashTint"): check(gl.glGetUniformLocation(flash, nm) >= 0, "uniform %s is there (highp_shader.c looks it up)" % nm.decode())
bad, _, log = compile_shader(0x8B30, "#version 100\nprecision mediump float;\nvoid main(){ gl_FragColor = vec4(nope); }")
check(not _ and "nope" in log, "(and a deliberately broken shader is refused, so this check can fail)")

# ---- a scene: a floor and a wall, seen from 1.6 m up -------------------------------------------------------------
FOV, EYE, UP = 60.0, np.array([0.0, 1.6, 0.0]), np.array([0.0, 1.0, 0.0])
def lookat(eye, fwd):
    f = fwd / np.linalg.norm(fwd); s = np.cross(f, UP); s /= np.linalg.norm(s); u = np.cross(s, f)
    V = np.eye(4); V[0, :3], V[1, :3], V[2, :3] = s, u, -f
    V[:3, 3] = -V[:3, :3] @ eye
    return V, f, s, u
t = math.tan(math.radians(FOV) / 2); asp = W / H; zn, zf = 0.25, 60.0
P = np.zeros((4, 4)); P[0, 0] = 1 / (t * asp); P[1, 1] = 1 / t; P[2, 2] = -(zf + zn) / (zf - zn); P[2, 3] = -2 * zf * zn / (zf - zn); P[3, 2] = -1
V, FWD, S, U = lookat(EYE, np.array([0.0, 0.0, 1.0]))
MVP = (P @ V).astype(np.float32)

def quad(corners, rgba):
    out = []
    for i in (0, 1, 2, 0, 2, 3): out += list(corners[i]) + [0.0, 0.0] + [x / 255.0 for x in rgba]
    return np.array(out, np.float32)
FLOOR = [(-12, 0, 0.5), (12, 0, 0.5), (12, 0, 40), (-12, 0, 40)]
WALL = [(-8, 0, 9), (8, 0, 9), (8, 6, 9), (-8, 6, 9)]

tex = C.c_uint(); gl.glGenTextures(1, C.byref(tex)); gl.glBindTexture(0x0DE1, tex)
white = (C.c_ubyte * 4)(255, 255, 255, 255); gl.glTexImage2D(0x0DE1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, white)
gl.glTexParameteri(0x0DE1, 0x2801, 0x2600); gl.glTexParameteri(0x0DE1, 0x2800, 0x2600)

def render(prog, base_rgb, flashparams=None):
    verts = np.concatenate([quad(FLOOR, base_rgb + (255,)), quad(WALL, base_rgb + (255,))])
    buf = C.c_uint(); gl.glGenBuffers(1, C.byref(buf)); gl.glBindBuffer(0x8892, buf)
    gl.glBufferData(0x8892, verts.nbytes, verts.ctypes.data_as(C.c_void_p), 0x88E4)
    gl.glViewport(0, 0, W, H); gl.glClearColor(0, 0, 0, 1); gl.glClear(0x4000)
    gl.glUseProgram(prog)
    stride = 9 * 4
    for name, size, off in ((b"vertexPosition", 3, 0), (b"vertexTexCoord", 2, 12), (b"vertexColor", 4, 20)):
        loc = gl.glGetAttribLocation(prog, name); gl.glEnableVertexAttribArray(loc)
        gl.glVertexAttribPointer(loc, size, 0x1406, 0, stride, C.c_void_p(off))
    gl.glUniformMatrix4fv(gl.glGetUniformLocation(prog, b"mvp"), 1, 0, np.ascontiguousarray(MVP.T).ctypes.data_as(C.c_void_p))
    gl.glUniform4f(gl.glGetUniformLocation(prog, b"colDiffuse"), 1, 1, 1, 1)
    gl.glActiveTexture(0x84C0); gl.glBindTexture(0x0DE1, tex); gl.glUniform1i(gl.glGetUniformLocation(prog, b"texture0"), 0)
    if flashparams:
        pos, dirv, ci, co, rng, strength, tint = flashparams
        gl.glUniform3f(gl.glGetUniformLocation(prog, b"flashPos"), *pos); gl.glUniform3f(gl.glGetUniformLocation(prog, b"flashDir"), *dirv)
        gl.glUniform4f(gl.glGetUniformLocation(prog, b"flashParams"), ci, co, 1.0 / rng, strength)
        gl.glUniform3f(gl.glGetUniformLocation(prog, b"flashTint"), *tint)
    gl.glDrawArrays(0x0004, 0, 12)
    px = (C.c_ubyte * (W * H * 4))(); gl.glReadPixels(0, 0, W, H, 0x1908, 0x1401, px)
    return np.frombuffer(px, np.uint8).reshape(H, W, 4)[::-1, :, :3].astype(np.float64)   # top row first

# ---- the maths again, in Python ------------------------------------------------------------------------------------
def beam_at(point, pos, dirv, ci, co, rng, strength):
    v = point - pos; d = max(np.linalg.norm(v), 0.001)
    spot = min(max((np.dot(v, dirv) / d - co) / (ci - co), 0.0), 1.0); spot = spot * spot * (3 - 2 * spot)
    fall = min(max(1.0 - d / rng, 0.0), 1.0)
    return spot * fall * fall * strength
def world_at(px, py):
    nx = (px + 0.5) / W * 2 - 1; ny = 1 - (py + 0.5) / H * 2
    ray = FWD + nx * asp * t * S + ny * t * U
    best = None
    if ray[1] < -1e-6:
        th = -EYE[1] / ray[1]; p = EYE + ray * th
        if -12 <= p[0] <= 12 and 0.5 <= p[2] <= 40: best = (th, p)
    if ray[2] > 1e-6:
        tw = (9 - EYE[2]) / ray[2]; p = EYE + ray * tw
        if -8 <= p[0] <= 8 and 0 <= p[1] <= 6 and (best is None or tw < best[0]): best = (tw, p)
    return best[1] if best else None

deg = math.radians
CI, CO, RNG, STR = math.cos(deg(14)), math.cos(deg(30)), 20.0, 0.8
TINT = np.array([0xFF, 0xE9, 0xB0]) / 255.0
DIR = np.array([0.0, -0.18, 1.0]); DIR /= np.linalg.norm(DIR)           # pointing a little down, so the floor shows the pool

print("The plain and the beam shader agree when the beam is off")
a = render(plain, (80, 100, 120))
b = render(flash, (80, 100, 120), (tuple(EYE), tuple(DIR), CI, CO, 1.0 / RNG, 0.0, tuple(TINT)))
check(np.abs(a - b).max() == 0, "strength 0 draws exactly the same pixels as the plain shader (max difference %d)" % np.abs(a - b).max())
check(a[225, 400].sum() > 0, "and the scene is not just black")

print("The beam, on black surfaces (what the lighting left dark)")
img = render(flash, (0, 0, 0), (tuple(EYE), tuple(DIR), CI, CO, RNG, STR, tuple(TINT)))
worst, lit, outside_bad, n = 0.0, 0, 0, 0
peak = 0.0
for py in range(4, H, 12):
    for px in range(4, W, 12):
        p = world_at(px, py)
        if p is None: continue
        e = beam_at(p, EYE, DIR, CI, CO, RNG, STR) * 0.85 * TINT * 255
        got = img[py, px]; n += 1
        if e.max() > 4: lit += 1
        peak = max(peak, e.max())
        worst = max(worst, np.abs(got - e).max())
        if e.max() == 0 and got.max() > 1.5: outside_bad += 1
print("    %d sampled pixels on the floor and wall, %d inside the beam, brightest expected %.0f / 255, worst error %.1f" % (n, lit, peak, worst))
check(lit > 100, "there is a pool of light (%d sampled pixels inside it)" % lit)
check(worst <= 3.0, "every sampled pixel is within 3/255 of the maths (worst %.1f): the GLSL does what flashlight.c does" % worst)
check(outside_bad == 0, "nothing is lit outside the cone")
cx = img[H // 2 + 60, W // 2]
check(cx[0] > cx[2] > 0, "the light is warm (red above blue at the middle of the pool: %s)" % cx.astype(int))
Image = None
try:
    from PIL import Image
except ImportError: pass
if Image is not None and os.environ.get("SHADER_TEST_PNG"):
    Image.fromarray(img.astype(np.uint8)).save(os.environ["SHADER_TEST_PNG"]); print("    wrote", os.environ["SHADER_TEST_PNG"])

print("The beam on a surface that is already lit")
img2 = render(flash, (60, 60, 60), (tuple(EYE), tuple(DIR), CI, CO, RNG, STR, tuple(TINT)))
worst2 = 0.0
for py in range(4, H, 20):
    for px in range(4, W, 20):
        p = world_at(px, py)
        if p is None: continue
        base = np.array([60.0] * 3); bm = beam_at(p, EYE, DIR, CI, CO, RNG, STR)
        peak = min(base.max() / 255.0, 1.0)
        e = np.minimum(base + bm * (0.85 * TINT * 255 * (1.0 - 0.55 * peak) + 0.35 * base), 255)
        worst2 = max(worst2, np.abs(img2[py, px] - e).max())
check(worst2 <= 3.0, "base + beam * (0.85 tint (1 - 0.55 peak) + 0.35 base): within 3/255 of the maths (worst %.1f)" % worst2)
check(img2[H // 2 + 60, W // 2].sum() > np.array([60, 60, 60]).sum() + 30, "a lit surface in the beam is brighter than one outside it")

print("Dimmer and narrower")
img3 = render(flash, (0, 0, 0), (tuple(EYE), tuple(DIR), CI, CO, RNG, 0.4, tuple(TINT)))
check(abs(img3[H // 2 + 60, W // 2].max() - 0.5 * img[H // 2 + 60, W // 2].max()) <= 2.0, "strength 0.4 gives half of what strength 0.8 does")

print("\n%s" % ("%d FAILED" % failed if failed else "all passed"))
sys.exit(1 if failed else 0)
