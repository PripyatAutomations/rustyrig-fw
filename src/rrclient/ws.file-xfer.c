//
// rrclient/ws.file-xfer.c: Support for sending files such as screen shots or audio recordings.
//
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// 3-space indentation please :)
#include "../../ext/libmongoose/mongoose.h"
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>

#if	0
#define CHUNK 32768

static uint64_t gen_id(void) {
   uint64_t x = mg_millis();
   x ^= (uint64_t)mg_random();  // not cryptographic, just uniqueness
   return x ? x : 1;
}

static void ws_send_file(struct mg_connection *c, const char *path, const char *mime) {
   FILE *fp = fopen(path, "rb");
   if (!fp) { MG_ERROR(("%s: open failed", path)); return; }

   fseeko(fp, 0, SEEK_END);
   uint64_t fsize = (uint64_t) ftello(fp);
   fseeko(fp, 0, SEEK_SET);

   uint64_t id = gen_id();
   uint32_t total = (uint32_t)((fsize + CHUNK - 1) / CHUNK);

   // meta (text frame)
   mg_ws_printf(c, WEBSOCKET_OP_TEXT,
      "{\"type\":\"file_meta\",\"id\":\"%llx\",\"name\":\"%s\",\"mime\":\"%s\",\"size\":%llu,\"chunk\":%u,\"total\":%u}",
      (unsigned long long) id, mg_basename(path), mime ? mime : "application/octet-stream",
      (unsigned long long) fsize, (unsigned) CHUNK, (unsigned) total);

   // chunk buffer: header(24) + payload
   uint8_t *buf = (uint8_t *) malloc(24 + CHUNK);
   if (!buf) { fclose(fp); return; }

   for (uint32_t idx = 0; ; idx++) {
      size_t n = fread(buf + 24, 1, CHUNK, fp);
      if (n == 0) break;

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

static struct mg_str k_meta = MG_STR("file_meta");

// Simple open-addressing table; replace with your own map if you have one
struct slot { uint64_t id; struct xfer xf; };
static struct slot g_tbl[64];

static struct xfer *xf_get(uint64_t id, bool create) {
   for (size_t i = 0; i < MG_ARRAY_SIZE(g_tbl); i++) {
      size_t j = (id + i) % MG_ARRAY_SIZE(g_tbl);
      if (g_tbl[j].id == id) return &g_tbl[j].xf;
      if (create && g_tbl[j].id == 0) { g_tbl[j].id = id; return &g_tbl[j].xf; }
   }
   return NULL;
}

static void xf_done(uint64_t id) {
   for (size_t j = 0; j < MG_ARRAY_SIZE(g_tbl); j++) {
      if (g_tbl[j].id == id) {
         if (g_tbl[j].xf.fp) fclose(g_tbl[j].xf.fp);
         memset(&g_tbl[j], 0, sizeof(g_tbl[j]));
         return;
      }
   }
}

static void on_ws_msg(struct mg_connection *c, int ev, void *ev_data) {
   if (ev != MG_EV_WS_MSG) return;
   struct mg_ws_message *m = (struct mg_ws_message *) ev_data;

   if (m->flags & WEBSOCKET_OP_TEXT) {
      // Parse meta
      struct mg_str type = mg_json_get_str(mg_str_n((char *) m->data.ptr, m->data.len), "$.type");
      if (mg_strcmp(type, k_meta) != 0) return;

      struct mg_str sid = mg_json_get_str(m->data, "$.id");
      char idbuf[32] = {0};
      mg_snprintf(idbuf, sizeof(idbuf), "%.*s", (int) sid.len, sid.ptr);
      uint64_t id = 0;
      sscanf(idbuf, "%llx", (unsigned long long *) &id);

      struct xfer *xf = xf_get(id, true);
      if (!xf) return;
      memset(xf, 0, sizeof(*xf));

      struct mg_str sname = mg_json_get_str(m->data, "$.name");
      struct mg_str smime = mg_json_get_str(m->data, "$.mime");
      struct mg_str ssz   = mg_json_get_str(m->data, "$.size");
      struct mg_str schunk= mg_json_get_str(m->data, "$.chunk");
      struct mg_str stotal= mg_json_get_str(m->data, "$.total");

      mg_snprintf(xf->name, sizeof(xf->name), "%.*s", (int) sname.len, sname.ptr);
      mg_snprintf(xf->mime, sizeof(xf->mime), "%.*s", (int) smime.len, smime.ptr);
      xf->size     = (uint64_t) mg_json_get_long(m->data, "$.size", 0);
      xf->chunk_sz = (uint32_t) mg_json_get_long(m->data, "$.chunk", 32768);
      xf->total    = (uint32_t) mg_json_get_long(m->data, "$.total", 0);

      // Open output (you can use a temp dir)
      char out[320];
      mg_snprintf(out, sizeof(out), "recv_%s", xf->name[0] ? xf->name : "file.bin");
      xf->fp = fopen(out, "wb");
      if (!xf->fp) { xf_done(id); }
      MG_INFO(("Start xfer id=%llx -> %s total=%u size=%llu",
               (unsigned long long) id, out, (unsigned) xf->total, (unsigned long long) xf->size));
      return;
   }

   if (m->flags & WEBSOCKET_OP_BINARY) {
      if (m->data.len < 24) return;
      const uint8_t *p = (const uint8_t *) m->data.ptr;
      uint64_t id; memcpy(&id, p + 0, 8);
      uint32_t idx, n; memcpy(&idx, p + 8, 4); memcpy(&n, p + 12, 4);

      struct xfer *xf = xf_get(id, false);
      if (!xf || !xf->fp) return;

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

#endif	// 0
