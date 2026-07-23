//
// rrgtk/ws.file-xfer.c: Support for sending files such as screen shots or audio recordings.
//
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// 3-space indentation please :)
#include <librustyaxe/core.h>
#include "ext/libmongoose/mongoose.h"
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#define CHUNK 32768

static uint64_t gen_id(void) {
   uint64_t x = (uint64_t) mg_millis();
   uint8_t rnd[8];
   mg_random(rnd, sizeof(rnd));
   memcpy(&x, rnd, sizeof(x));
   x ^= (uint64_t) mg_millis();
   return x ? x : 1;
}

static const char *rr_basename(const char *path) {
   if (!path) {
      return "file.bin";
   }
   const char *base = path;
   for (const char *p = path; *p; ++p) {
      if (*p == '/' || *p == '\\') {
         base = p + 1;
      }
   }
   return base;
}

static void ws_send_file(struct mg_connection *c, const char *path, const char *mime) {
   if (!c || !path || !mime) {
      return;
   }

   FILE *fp = fopen(path, "rb");
   if (!fp) {
      Log(LOG_CRIT, "ws.file-xfer", "Failed opening file %s - %d:%s", path, errno, strerror(errno));
      return;
   }

   fseeko(fp, 0, SEEK_END);
   uint64_t fsize = (uint64_t) ftello(fp);
   fseeko(fp, 0, SEEK_SET);

   uint64_t id = gen_id();
   uint32_t total = (uint32_t)((fsize + CHUNK - 1) / CHUNK);

   // meta (text frame)
   mg_ws_printf(c, WEBSOCKET_OP_TEXT,
      "{\"type\":\"file_meta\",\"id\":\"%llx\",\"name\":\"%s\",\"mime\":\"%s\",\"size\":%llu,\"chunk\":%u,\"total\":%u}",
      (unsigned long long) id, rr_basename(path), mime ? mime : "application/octet-stream",
      (unsigned long long) fsize, (unsigned) CHUNK, (unsigned) total);

   // chunk buffer: header(24) + payload
   uint8_t *buf = (uint8_t *) malloc(24 + CHUNK);
   if (!buf) {
      fclose(fp);
      return;
   }

   for (uint32_t idx = 0; ; idx++) {
      size_t n = fread(buf + 24, 1, CHUNK, fp);
      if (n == 0) {
         break;
      }

      // header pack (LE)
      memcpy(buf + 0,  &id, 8);
      uint32_t le_idx = idx, le_len = (uint32_t) n;
      memcpy(buf + 8,  &le_idx, 4);
      memcpy(buf + 12, &le_len, 4);
      uint64_t zero = 0;
      memcpy(buf + 16, &zero, 8);

      mg_ws_send(c, buf, 24 + n, WEBSOCKET_OP_BINARY);
   }

   free(buf);
   fclose(fp);
}

struct xfer {
   char name[256];
   char mime[64];
   uint64_t size, received;
   uint32_t total, chunk_sz, got_chunks;
   FILE *fp;
};

static struct mg_str k_meta = { .buf = "file_meta", .len = sizeof("file_meta") - 1 };

// Simple open-addressing table; replace with your own map if you have one
struct slot { uint64_t id; struct xfer xf; };
static struct slot g_tbl[64];

static struct xfer *xf_get(uint64_t id, bool create) {
   size_t count = sizeof(g_tbl) / sizeof(g_tbl[0]);
   for (size_t i = 0; i < count; i++) {
      size_t j = (id + i) % count;
      if (g_tbl[j].id == id) {
         return &g_tbl[j].xf;
      }
      if (create && g_tbl[j].id == 0) {
         g_tbl[j].id = id; return &g_tbl[j].xf;
      }
   }
   return NULL;
}

static void xf_done(uint64_t id) {
   size_t count = sizeof(g_tbl) / sizeof(g_tbl[0]);
   for (size_t j = 0; j < count; j++) {
      if (g_tbl[j].id == id) {
         if (g_tbl[j].xf.fp) {
            fclose(g_tbl[j].xf.fp);
         }
         memset(&g_tbl[j], 0, sizeof(g_tbl[j]));
         return;
      }
   }
}

static void on_ws_msg(struct mg_connection *c, int ev, void *ev_data) {
   if (ev != MG_EV_WS_MSG) {
      return;
   }
   struct mg_ws_message *m = (struct mg_ws_message *) ev_data;

   if (m->flags & WEBSOCKET_OP_TEXT) {
      // Parse meta
      char *type = mg_json_get_str(mg_str_n(m->data.buf, m->data.len), "$.type");
      if (!type || mg_strcmp(mg_str(type), k_meta) != 0) {
         mg_free(type);
         return;
      }
      mg_free(type);

      char *sid = mg_json_get_str(m->data, "$.id");
      char idbuf[32] = {0};
      if (sid) {
         mg_snprintf(idbuf, sizeof(idbuf), "%s", sid);
         mg_free(sid);
      }
      uint64_t id = 0;
      sscanf(idbuf, "%llx", (unsigned long long *) &id);

      struct xfer *xf = xf_get(id, true);
      if (!xf) {
         return;
      }
      memset(xf, 0, sizeof(*xf));

      char *sname = mg_json_get_str(m->data, "$.name");
      char *smime = mg_json_get_str(m->data, "$.mime");
      if (sname) {
         mg_snprintf(xf->name, sizeof(xf->name), "%s", sname);
         mg_free(sname);
      }
      if (smime) {
         mg_snprintf(xf->mime, sizeof(xf->mime), "%s", smime);
         mg_free(smime);
      }
      xf->size     = (uint64_t) mg_json_get_long(m->data, "$.size", 0);
      xf->chunk_sz = (uint32_t) mg_json_get_long(m->data, "$.chunk", 32768);
      xf->total    = (uint32_t) mg_json_get_long(m->data, "$.total", 0);

      // Open output (you can use a temp dir)
      char out[320];
      mg_snprintf(out, sizeof(out), "recv_%s", xf->name[0] ? xf->name : "file.bin");
      xf->fp = fopen(out, "wb");
      if (!xf->fp) {
         xf_done(id);
      }
      MG_INFO(("Start xfer id=%llx -> %s total=%u size=%llu",
               (unsigned long long) id, out, (unsigned) xf->total, (unsigned long long) xf->size));
      return;
   }

   if (m->flags & WEBSOCKET_OP_BINARY) {
      if (m->data.len < 24) {
         return;
      }
      const uint8_t *p = (const uint8_t *) m->data.buf;
      uint64_t id; memcpy(&id, p + 0, 8);
      uint32_t idx, n; memcpy(&idx, p + 8, 4); memcpy(&n, p + 12, 4);

      struct xfer *xf = xf_get(id, false);
      if (!xf || !xf->fp) {
         return;
      }

      // For simplicity we append in arrival order. If you need random access,
      // seek to (idx * chunk_sz) and write fixed-sized chunks except last.
      // Here’s the robust variant:
      uint64_t offset = (uint64_t) idx * (uint64_t) xf->chunk_sz;
      fseeko(xf->fp, (off_t) offset, SEEK_SET);
      fwrite(p + 24, 1, n, xf->fp);

      xf->got_chunks++;
      xf->received += n;

      if (xf->got_chunks >= xf->total || xf->received >= xf->size) {
         MG_INFO(("Complete id=%llx bytes=%llu", (unsigned long long) id, (unsigned long long) xf->received));
         xf_done(id);
      }
   }
}

/*
static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
   switch (ev) {
      case MG_EV_WS_OPEN: MG_INFO(("WS open")); break;
      case MG_EV_WS_MSG:  on_ws_msg(c, ev, ev_data); break;
      default: break;
   }
   (void) fn_data;
}

// Example: call ws_send_file(c, "/path/to/file.png", "image/png") after WS is open
*/
