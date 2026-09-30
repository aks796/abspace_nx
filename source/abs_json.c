/* abs_json.c -- a small JSON reader for YouTube's player answers (abs_yt.c):
 * the whole text into a tree of values, strings unescaped to UTF-8. MIT. */
#include <stdlib.h>
#include <string.h>

#include "abs_net.h"

typedef struct {
  const char *p, *end;
  int depth;
} P;

static void ws(P *p) {
  while (p->p < p->end && (*p->p == ' ' || *p->p == '\t' || *p->p == '\n' || *p->p == '\r'))
    p->p++;
}

static void put_utf8(char **o, unsigned c) {
  char *d = *o;
  if (c < 0x80) {
    *d++ = (char)c;
  } else if (c < 0x800) {
    *d++ = (char)(0xc0 | c >> 6), *d++ = (char)(0x80 | (c & 63));
  } else if (c < 0x10000) {
    *d++ = (char)(0xe0 | c >> 12), *d++ = (char)(0x80 | (c >> 6 & 63)), *d++ = (char)(0x80 | (c & 63));
  } else {
    *d++ = (char)(0xf0 | c >> 18), *d++ = (char)(0x80 | (c >> 12 & 63));
    *d++ = (char)(0x80 | (c >> 6 & 63)), *d++ = (char)(0x80 | (c & 63));
  }
  *o = d;
}

static int hex4(const char *s, unsigned *v) {
  *v = 0;
  for (int i = 0; i < 4; i++) {
    char c = s[i];
    *v <<= 4;
    if (c >= '0' && c <= '9') *v |= (unsigned)(c - '0');
    else if (c >= 'a' && c <= 'f') *v |= (unsigned)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') *v |= (unsigned)(c - 'A' + 10);
    else return 0;
  }
  return 1;
}

static char *str(P *p) {
  /* at the opening quote */
  const char *s = ++p->p;
  size_t n = 0;
  while (s + n < p->end && s[n] != '"') {
    if (s[n] == '\\')
      n++;
    n++;
  }
  if (s + n >= p->end)
    return NULL;
  char *out = malloc(n + 1), *o = out;
  if (!out)
    return NULL;
  const char *e = s + n;
  while (s < e) {
    if (*s != '\\') {
      *o++ = *s++;
      continue;
    }
    s++;
    switch (*s) {
    case 'n': *o++ = '\n'; s++; break;
    case 't': *o++ = '\t'; s++; break;
    case 'r': *o++ = '\r'; s++; break;
    case 'b': *o++ = '\b'; s++; break;
    case 'f': *o++ = '\f'; s++; break;
    case 'u': {
      unsigned c = 0, c2 = 0;
      if (e - s < 5 || !hex4(s + 1, &c)) {
        s++;
        break;
      }
      s += 5;
      if (c >= 0xd800 && c < 0xdc00 && e - s >= 6 && s[0] == '\\' && s[1] == 'u' && hex4(s + 2, &c2) &&
          c2 >= 0xdc00 && c2 < 0xe000) {
        c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
        s += 6;
      }
      put_utf8(&o, c);
      break;
    }
    default: *o++ = *s++; break; /* \" \\ \/ */
    }
  }
  *o = 0;
  p->p = e + 1;
  return out;
}

static AbsJson *value(P *p);

static AbsJson *node(int t) {
  AbsJson *v = calloc(1, sizeof *v);
  if (v)
    v->t = t;
  return v;
}

static int push(AbsJson *v, char *key, AbsJson *item) {
  if (v->n == v->cap) {
    int cap = v->cap ? v->cap * 2 : 8;
    AbsJson **items = realloc(v->items, sizeof *items * (size_t)cap);
    if (!items)
      return 0;
    v->items = items;
    if (v->t == ABS_J_OBJ) {
      char **keys = realloc(v->keys, sizeof *keys * (size_t)cap);
      if (!keys)
        return 0;
      v->keys = keys;
    }
    v->cap = cap;
  }
  v->items[v->n] = item;
  if (v->t == ABS_J_OBJ)
    v->keys[v->n] = key;
  v->n++;
  return 1;
}

static AbsJson *value(P *p) {
  ws(p);
  if (p->p >= p->end || ++p->depth > 200)
    return NULL;
  AbsJson *v = NULL;
  char c = *p->p;
  if (c == '{' || c == '[') {
    int obj = c == '{';
    v = node(obj ? ABS_J_OBJ : ABS_J_ARR);
    if (!v)
      return NULL;
    p->p++;
    ws(p);
    if (p->p < p->end && *p->p == (obj ? '}' : ']')) {
      p->p++;
    } else {
      for (;;) {
        char *key = NULL;
        ws(p);
        if (obj) {
          if (p->p >= p->end || *p->p != '"' || !(key = str(p)))
            goto fail;
          ws(p);
          if (p->p >= p->end || *p->p != ':') {
            free(key);
            goto fail;
          }
          p->p++;
        }
        AbsJson *item = value(p);
        if (!item || !push(v, key, item)) {
          free(key);
          abs_json_free(item);
          goto fail;
        }
        ws(p);
        if (p->p < p->end && *p->p == ',') {
          p->p++;
          continue;
        }
        if (p->p < p->end && *p->p == (obj ? '}' : ']')) {
          p->p++;
          break;
        }
        goto fail;
      }
    }
  } else if (c == '"') {
    v = node(ABS_J_STR);
    if (!v || !(v->s = str(p)))
      goto fail;
  } else if (c == 't' && p->end - p->p >= 4 && !memcmp(p->p, "true", 4)) {
    v = node(ABS_J_BOOL), p->p += 4;
    if (v) v->num = 1;
  } else if (c == 'f' && p->end - p->p >= 5 && !memcmp(p->p, "false", 5)) {
    v = node(ABS_J_BOOL), p->p += 5;
  } else if (c == 'n' && p->end - p->p >= 4 && !memcmp(p->p, "null", 4)) {
    v = node(ABS_J_NULL), p->p += 4;
  } else {
    char *e;
    double d = strtod(p->p, &e);
    if (e == p->p)
      return NULL;
    v = node(ABS_J_NUM);
    if (v) v->num = d;
    p->p = e;
  }
  p->depth--;
  return v;
fail:
  abs_json_free(v);
  return NULL;
}

AbsJson *abs_json_parse(const char *text, size_t len) {
  P p = {text, text + len, 0};
  return value(&p);
}

void abs_json_free(AbsJson *v) {
  if (!v)
    return;
  for (int i = 0; i < v->n; i++) {
    abs_json_free(v->items[i]);
    if (v->keys)
      free(v->keys[i]);
  }
  free(v->items);
  free(v->keys);
  free(v->s);
  free(v);
}

AbsJson *abs_json_get(const AbsJson *v, const char *key) {
  if (!v || v->t != ABS_J_OBJ)
    return NULL;
  for (int i = 0; i < v->n; i++)
    if (!strcmp(v->keys[i], key))
      return v->items[i];
  return NULL;
}

AbsJson *abs_json_at(const AbsJson *v, int i) {
  if (!v || (v->t != ABS_J_ARR && v->t != ABS_J_OBJ) || i < 0 || i >= v->n)
    return NULL;
  return v->items[i];
}

const char *abs_json_str(const AbsJson *v) { return v && v->t == ABS_J_STR ? v->s : NULL; }

double abs_json_num(const AbsJson *v, double def) {
  if (!v)
    return def;
  if (v->t == ABS_J_NUM || v->t == ABS_J_BOOL)
    return v->num;
  if (v->t == ABS_J_STR && v->s) {
    char *e;
    double d = strtod(v->s, &e);
    return e != v->s ? d : def;
  }
  return def;
}
