/* abs_overlay.c -- the port's own pictures over the game's frame: the menu
 * focus ring (shaped like the button it is on), the hand cursor, the restart
 * bar, dialogs, the tiny planets' pages and their videos (abs_input.c,
 * abs_dialog.c, abs_extras.c, abs_video.c).
 *
 * Drawn with GLES 2 on the render thread just before each present, in the
 * engine's context. Everything the drawing changes is put back afterwards --
 * program, buffers, vertex attributes 0 and 1 (their whole pointer state:
 * the engine sets its pointers once and reuses them), texture unit 0,
 * blending, depth/stencil/scissor/cull, colour mask, viewport and
 * framebuffer -- so the engine's next frame starts from the state it left.
 * Text comes from abs_font.c, rendered once per string into an alpha
 * texture and kept in a small cache. MIT.
 */
#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "abs.h"
#include "util.h"

int abs_font_render_alpha(float px, const char *utf8, uint8_t *out, int w, int h, float x0, float baseline);
int abs_font_measure(float px, const char *utf8, int *w, int *left);

#if DCR_GL_MESA
#include <GLES2/gl2.h>

static GLuint g_prog;
static GLint u_screen, u_color, u_tex, u_tex_u, u_tex_v, u_use_tex;
static int g_failed, g_w, g_h, g_in;

static GLuint shader(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[256] = "";
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    debugPrintf("[overlay] shader: %s\n", log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

static int init(void) {
  if (g_prog)
    return 1;
  if (g_failed)
    return 0;
  static const char *vs = "attribute vec2 aPos;\n"
                          "attribute vec2 aUV;\n"
                          "varying vec2 vUV;\n"
                          "uniform vec2 uScreen;\n"
                          "void main() {\n"
                          "  vUV = aUV;\n"
                          "  gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);\n"
                          "}\n";
  /* uUseTex: 0 a colour, 1 an alpha texture (text) in that colour, 2 an RGBA
   * texture tinted by it, 3 video: Y, U, V in three textures (BT.601,
   * limited range) */
  static const char *fs = "precision mediump float;\n"
                          "varying vec2 vUV;\n"
                          "uniform vec4 uColor;\n"
                          "uniform sampler2D uTex;\n"
                          "uniform sampler2D uTexU;\n"
                          "uniform sampler2D uTexV;\n"
                          "uniform float uUseTex;\n"
                          "void main() {\n"
                          "  if (uUseTex > 2.5) {\n"
                          "    float y = 1.1643 * (texture2D(uTex, vUV).a - 0.0625);\n"
                          "    float u = texture2D(uTexU, vUV).a - 0.5;\n"
                          "    float v = texture2D(uTexV, vUV).a - 0.5;\n"
                          "    gl_FragColor = vec4(y + 1.5958 * v, y - 0.39173 * u - 0.8129 * v, y + 2.017 * u, 1.0);\n"
                          "  } else if (uUseTex > 1.5) gl_FragColor = texture2D(uTex, vUV) * uColor;\n"
                          "  else if (uUseTex > 0.5) gl_FragColor = vec4(uColor.rgb, uColor.a * texture2D(uTex, vUV).a);\n"
                          "  else gl_FragColor = uColor;\n"
                          "}\n";
  GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
  if (!v || !f) {
    g_failed = 1;
    return 0;
  }
  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glBindAttribLocation(p, 0, "aPos");
  glBindAttribLocation(p, 1, "aUV");
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    glDeleteProgram(p);
    g_failed = 1;
    debugPrintf("[overlay] the program did not link\n");
    return 0;
  }
  g_prog = p;
  u_screen = glGetUniformLocation(p, "uScreen");
  u_color = glGetUniformLocation(p, "uColor");
  u_tex = glGetUniformLocation(p, "uTex");
  u_tex_u = glGetUniformLocation(p, "uTexU");
  u_tex_v = glGetUniformLocation(p, "uTexV");
  u_use_tex = glGetUniformLocation(p, "uUseTex");
  return 1;
}

/* ------------------------------------------------------ the engine's state */
typedef struct {
  GLint enabled, size, type, norm, stride, buf;
  void *ptr;
} Attrib;

static struct {
  Attrib a[2];
  GLint prog, abuf, fb, active, tex0, tex1, tex2, vp[4], align;
  GLint bsrc_rgb, bdst_rgb, bsrc_a, bdst_a, beq_rgb, beq_a;
  GLboolean blend, depth, cull, scissor, stencil, mask[4];
} S;

static void attrib_save(GLuint i, Attrib *a) {
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a->enabled);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a->size);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a->type);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a->norm);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a->stride);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a->buf);
  glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a->ptr);
}

static void attrib_restore(GLuint i, const Attrib *a) {
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)a->buf);
  if (a->size > 0)
    glVertexAttribPointer(i, a->size, (GLenum)a->type, a->norm ? GL_TRUE : GL_FALSE, a->stride, a->ptr);
  if (a->enabled)
    glEnableVertexAttribArray(i);
  else
    glDisableVertexAttribArray(i);
}

int abs_ov_begin(int w, int h) {
  if (g_in || !init())
    return 0;
  g_in = 1;
  g_w = w, g_h = h;
  attrib_save(0, &S.a[0]);
  attrib_save(1, &S.a[1]);
  glGetIntegerv(GL_CURRENT_PROGRAM, &S.prog);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &S.abuf);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &S.fb);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &S.active);
  glActiveTexture(GL_TEXTURE2);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &S.tex2);
  glActiveTexture(GL_TEXTURE1);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &S.tex1);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &S.tex0);
  glGetIntegerv(GL_VIEWPORT, S.vp);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &S.align);
  glGetIntegerv(GL_BLEND_SRC_RGB, &S.bsrc_rgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &S.bdst_rgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &S.bsrc_a);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &S.bdst_a);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &S.beq_rgb);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &S.beq_a);
  glGetBooleanv(GL_COLOR_WRITEMASK, S.mask);
  S.blend = glIsEnabled(GL_BLEND);
  S.depth = glIsEnabled(GL_DEPTH_TEST);
  S.cull = glIsEnabled(GL_CULL_FACE);
  S.scissor = glIsEnabled(GL_SCISSOR_TEST);
  S.stencil = glIsEnabled(GL_STENCIL_TEST);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, w, h);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(g_prog);
  glUniform2f(u_screen, (GLfloat)w, (GLfloat)h);
  glUniform1i(u_tex, 0);
  glUniform1i(u_tex_u, 1);
  glUniform1i(u_tex_v, 2);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  return 1;
}

void abs_ov_end(void) {
  if (!g_in)
    return;
  g_in = 0;
  glViewport(S.vp[0], S.vp[1], S.vp[2], S.vp[3]);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)S.fb);
  attrib_restore(0, &S.a[0]);
  attrib_restore(1, &S.a[1]);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)S.abuf);
  glUseProgram((GLuint)S.prog);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, (GLuint)S.tex2);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, (GLuint)S.tex1);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, (GLuint)S.tex0);
  glActiveTexture((GLenum)S.active);
  glPixelStorei(GL_UNPACK_ALIGNMENT, S.align);
  glBlendEquationSeparate((GLenum)S.beq_rgb, (GLenum)S.beq_a);
  glBlendFuncSeparate((GLenum)S.bsrc_rgb, (GLenum)S.bdst_rgb, (GLenum)S.bsrc_a, (GLenum)S.bdst_a);
  glColorMask(S.mask[0], S.mask[1], S.mask[2], S.mask[3]);
  if (!S.blend) glDisable(GL_BLEND);
  if (S.depth) glEnable(GL_DEPTH_TEST);
  if (S.cull) glEnable(GL_CULL_FACE);
  if (S.scissor) glEnable(GL_SCISSOR_TEST);
  if (S.stencil) glEnable(GL_STENCIL_TEST);
}

/* ------------------------------------------------------------ shapes */
static void color(uint32_t rgba) {
  glUniform4f(u_color, (GLfloat)((rgba >> 24) & 255) / 255.0f, (GLfloat)((rgba >> 16) & 255) / 255.0f,
              (GLfloat)((rgba >> 8) & 255) / 255.0f, (GLfloat)(rgba & 255) / 255.0f);
}

void abs_ov_tri(const float *xy, uint32_t rgba) {
  if (!g_in)
    return;
  color(rgba);
  glUniform1f(u_use_tex, 0.0f);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, xy);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

void abs_ov_rect(float x, float y, float w, float h, uint32_t rgba) {
  if (!g_in || w <= 0 || h <= 0)
    return;
  const GLfloat q[] = {x, y, x + w, y, x, y + h, x + w, y + h};
  color(rgba);
  glUniform1f(u_use_tex, 0.0f);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void abs_ov_frame(float x, float y, float w, float h, float t, uint32_t rgba) {
  abs_ov_rect(x, y, w, t, rgba);
  abs_ov_rect(x, y + h - t, w, t, rgba);
  abs_ov_rect(x, y + t, t, h - 2 * t, rgba);
  abs_ov_rect(x + w - t, y + t, t, h - 2 * t, rgba);
}

/* The outline of a rounded rectangle (r > 0) or of the ellipse in the box
 * (r < 0), as n points going round, and each point's outward normal. */
#define RING_N 96
static int outline(float x, float y, float w, float h, float r, float *pt, float *nm) {
  const float cx = x + w * 0.5f, cy = y + h * 0.5f;
  if (r < 0) {
    const float rx = w * 0.5f, ry = h * 0.5f;
    for (int i = 0; i < RING_N; i++) {
      float a = (float)i / RING_N * 6.2831853f, c = cosf(a), s = sinf(a);
      pt[2 * i] = cx + rx * c, pt[2 * i + 1] = cy + ry * s;
      /* the ellipse's normal: (c / rx, s / ry), normalised */
      float nx = c * ry, ny = s * rx, l = sqrtf(nx * nx + ny * ny);
      nm[2 * i] = l > 0 ? nx / l : c, nm[2 * i + 1] = l > 0 ? ny / l : s;
    }
    return RING_N;
  }
  float m = (w < h ? w : h) * 0.5f;
  if (r > m)
    r = m;
  /* four quarter circles, RING_N / 4 points each */
  static const float corner[4][3] = {{1, 1, 0}, {-1, 1, 1}, {-1, -1, 2}, {1, -1, 3}};
  const int q = RING_N / 4;
  int n = 0;
  for (int k = 0; k < 4; k++) {
    float ccx = cx + corner[k][0] * (w * 0.5f - r), ccy = cy + corner[k][1] * (h * 0.5f - r);
    for (int i = 0; i < q; i++, n++) {
      float a = (corner[k][2] + (float)i / (q - 1)) * 1.5707963f, c = cosf(a), s = sinf(a);
      pt[2 * n] = ccx + r * c, pt[2 * n + 1] = ccy + r * s;
      nm[2 * n] = c, nm[2 * n + 1] = s;
    }
  }
  return n;
}

void abs_ov_ring(float x, float y, float w, float h, float r, float t, uint32_t rgba) {
  if (!g_in || w <= 0 || h <= 0 || t <= 0)
    return;
  float pt[2 * RING_N], nm[2 * RING_N];
  int n = outline(x, y, w, h, r, pt, nm);
  GLfloat strip[4 * (RING_N + 1)];
  for (int i = 0; i <= n; i++) {
    int k = i % n;
    strip[4 * i] = pt[2 * k], strip[4 * i + 1] = pt[2 * k + 1];
    strip[4 * i + 2] = pt[2 * k] + nm[2 * k] * t, strip[4 * i + 3] = pt[2 * k + 1] + nm[2 * k + 1] * t;
  }
  color(rgba);
  glUniform1f(u_use_tex, 0.0f);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, strip);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 2 * (n + 1));
}

void abs_ov_rrect(float x, float y, float w, float h, float r, uint32_t rgba) {
  if (!g_in || w <= 0 || h <= 0)
    return;
  float pt[2 * RING_N], nm[2 * RING_N];
  int n = outline(x, y, w, h, r, pt, nm);
  GLfloat fan[2 * (RING_N + 2)];
  fan[0] = x + w * 0.5f, fan[1] = y + h * 0.5f;
  for (int i = 0; i <= n; i++)
    fan[2 + 2 * i] = pt[2 * (i % n)], fan[3 + 2 * i] = pt[2 * (i % n) + 1];
  color(rgba);
  glUniform1f(u_use_tex, 0.0f);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, fan);
  glDrawArrays(GL_TRIANGLE_FAN, 0, n + 2);
}

/* ---------------------------------------------------------- pictures */
unsigned abs_ov_texture(const uint8_t *rgba, int w, int h) {
  if (!g_in || !rgba || w <= 0 || h <= 0)
    return 0;
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  return t;
}

void abs_ov_image(unsigned tex, float x, float y, float w, float h, uint32_t rgba) {
  if (!g_in || !tex)
    return;
  const GLfloat q[] = {x, y, x + w, y, x, y + h, x + w, y + h};
  static const GLfloat uv[] = {0, 0, 1, 0, 0, 1, 1, 1};
  glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
  color(rgba);
  glUniform1f(u_use_tex, 2.0f);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(1);
}

/* video frames: the three planes into three alpha textures, reused while the
 * size stays */
static GLuint g_yuv[3];
static int g_yuv_w, g_yuv_h;

static void plane(int i, const uint8_t *p, int stride, int w, int h) {
  glActiveTexture(GL_TEXTURE0 + i);
  glBindTexture(GL_TEXTURE_2D, g_yuv[i]);
  if (stride == w) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_ALPHA, GL_UNSIGNED_BYTE, p);
  } else {
    for (int y = 0; y < h; y++)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, w, 1, GL_ALPHA, GL_UNSIGNED_BYTE, p + (size_t)y * stride);
  }
}

void abs_ov_yuv(const uint8_t *const planes[3], const int strides[3], int w, int h, float x, float y,
                float dw, float dh) {
  if (!g_in || w <= 0 || h <= 0)
    return;
  if (!g_yuv[0] || g_yuv_w != w || g_yuv_h != h) {
    if (g_yuv[0])
      glDeleteTextures(3, g_yuv);
    glGenTextures(3, g_yuv);
    for (int i = 0; i < 3; i++) {
      int pw = i ? (w + 1) / 2 : w, ph = i ? (h + 1) / 2 : h;
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, g_yuv[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, pw, ph, 0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);
    }
    g_yuv_w = w, g_yuv_h = h;
  }
  if (planes) { /* NULL: the picture is the one already up */
    plane(2, planes[2], strides[2], (w + 1) / 2, (h + 1) / 2);
    plane(1, planes[1], strides[1], (w + 1) / 2, (h + 1) / 2);
    plane(0, planes[0], strides[0], w, h);
  } else {
    for (int i = 0; i < 3; i++) {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, g_yuv[i]);
    }
  }
  const GLfloat q[] = {x, y, x + dw, y, x, y + dh, x + dw, y + dh};
  static const GLfloat uv[] = {0, 0, 1, 0, 0, 1, 1, 1};
  glUniform1f(u_use_tex, 3.0f);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(1);
}

/* -------------------------------------------------------------- text */
#define NTEXT 24
typedef struct {
  char s[192];
  float px;
  GLuint tex;
  int w, h, left;
  u64 used;
} Text;
static Text g_text[NTEXT];

static Text *text_get(float px, const char *utf8) {
  Text *slot = NULL;
  u64 oldest = ~0ull;
  u64 now = armGetSystemTick();
  for (int i = 0; i < NTEXT; i++) {
    Text *t = &g_text[i];
    if (t->tex && t->px == px && !strncmp(t->s, utf8, sizeof t->s - 1)) {
      t->used = now;
      return t;
    }
    if (t->used < oldest)
      oldest = t->used, slot = t;
  }
  int w = 0, left = 0;
  if (abs_font_measure(px, utf8, &w, &left) != 0)
    return NULL;
  const int pad = 2;
  int tw = w + 2 * pad, th = (int)(px * 1.3f) + 2 * pad;
  if (tw <= 0 || th <= 0 || tw > 4096)
    return NULL;
  uint8_t *a = malloc((size_t)tw * th);
  if (!a)
    return NULL;
  abs_font_render_alpha(px, utf8, a, tw, th, (float)(pad - left), px * 1.0f + pad);
  if (slot->tex)
    glDeleteTextures(1, &slot->tex);
  glGenTextures(1, &slot->tex);
  glBindTexture(GL_TEXTURE_2D, slot->tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, tw, th, 0, GL_ALPHA, GL_UNSIGNED_BYTE, a);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  free(a);
  snprintf(slot->s, sizeof slot->s, "%s", utf8);
  slot->px = px;
  slot->w = tw, slot->h = th, slot->left = left - pad;
  slot->used = now;
  return slot;
}

/* utf8 with its em box's top-left at (x, y) */
void abs_ov_text(float x, float y, float px, uint32_t rgba, const char *utf8) {
  if (!g_in || !utf8 || !*utf8 || !abs_font_ready())
    return;
  Text *t = text_get(px, utf8);
  if (!t)
    return;
  const float x0 = x + (float)t->left, y0 = y - px * 0.08f;
  const GLfloat q[] = {x0, y0, x0 + t->w, y0, x0, y0 + t->h, x0 + t->w, y0 + t->h};
  static const GLfloat uv[] = {0, 0, 1, 0, 0, 1, 1, 1};
  glBindTexture(GL_TEXTURE_2D, t->tex);
  color(rgba);
  glUniform1f(u_use_tex, 1.0f);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(1);
}

#else /* the null renderer */
int abs_ov_begin(int w, int h) { return 0; }
void abs_ov_end(void) {}
void abs_ov_tri(const float *xy, uint32_t rgba) {}
void abs_ov_rect(float x, float y, float w, float h, uint32_t rgba) {}
void abs_ov_frame(float x, float y, float w, float h, float t, uint32_t rgba) {}
void abs_ov_ring(float x, float y, float w, float h, float r, float t, uint32_t rgba) {}
void abs_ov_rrect(float x, float y, float w, float h, float r, uint32_t rgba) {}
unsigned abs_ov_texture(const uint8_t *rgba, int w, int h) { return 0; }
void abs_ov_image(unsigned tex, float x, float y, float w, float h, uint32_t rgba) {}
void abs_ov_yuv(const uint8_t *const planes[3], const int strides[3], int w, int h, float x, float y,
                float dw, float dh) {}
void abs_ov_text(float x, float y, float px, uint32_t rgba, const char *utf8) {}
#endif
