// rrserver/gps.c: GPS related bits
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Rig-scoped GPS publication. Adapters supply validated receiver sentences;
// configured coordinates generate RMC in manual mode. No floating point.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/io.serial.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/ws.serial.h>
#include <rrserver/gps.h>
#include <rrserver/discovery.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.registry.h>
#include <rrserver/rig.config.h>

extern struct GlobalState rig;
extern time_t now;

#define GPS_SOURCES_MAX 33
struct gps_source {
   char alias[64];
   struct rr_mediachan *channel;
   bool own_input, fixed, published, received, valid, manual;
   int32_t lat, lon; // degrees * 10^7
   uint64_t sent_at;
};
static struct gps_source sources[GPS_SOURCES_MAX];
static unsigned count;
static rr_event_token_t input_token, poll_token, subscribed_token, inventory_token;

static bool coordinate(const char **cursor, int32_t *result, unsigned maximum) {
   const char *p = *cursor;
   while (isspace((unsigned char)*p)) {
      p++;
   }

   bool negative = *p == '-';
   if (*p == '-' || *p == '+') {
      p++;
   }

   if (!isdigit((unsigned char)*p)) {
      return false;
   }
   unsigned whole = 0, fraction = 0, scale = 1000000;
   while (isdigit((unsigned char)*p)) {
      whole = whole * 10 + (*p++ - '0');
      if (whole > maximum) {
         return false;
      }
   }
   if (*p == '.') {
      p++;

      if (!isdigit((unsigned char)*p)) {
         return false;
      }
      while (isdigit((unsigned char)*p)) {
         if (!scale) {
            return false;
         }
         fraction += (*p++ - '0') * scale;
         scale /= 10;
      }
   }

   if (whole == maximum && fraction) {
      return false;
   }
   *result = (int32_t)(whole * 10000000 + fraction);
   if (negative) {
      *result = -*result;
   }

   while (isspace((unsigned char)*p)) {
      p++;
   }
   *cursor = p;
   return true;
}

bool rrserver_gps_position_parse(const char *text, int32_t *lat, int32_t *lon) {
   if (!text || !lat || !lon) {
      return false;
   }
   int32_t a, b;
   if (!coordinate(&text, &a, 90) || *text++ != ',' || !coordinate(&text, &b, 180) || *text) {
      return false;
   }
   *lat = a; *lon = b; return true;
}

static struct gps_source *find(const char *alias) {
   for (unsigned i = 0; i < count; i++) {
      if (!strcmp(sources[i].alias, alias)) {
         return &sources[i];
      }
   }
   return NULL;
}

static const struct gps_source *effective(const struct gps_source *source) {
   if (source==sources || source->own_input || source->fixed || source->received) {
      return source;
   }
   return sources;
}

static bool available(const struct gps_source *source) {
   return source->fixed || source->received;
}

static void nmea_angle(int32_t angle, unsigned width, char *buf, size_t capacity) {
   uint32_t absolute = angle < 0 ? -(int64_t)angle : angle;
   unsigned degrees = absolute / 10000000;
   uint64_t minutes = ((uint64_t)(absolute % 10000000) * 60 + 5) / 10;
   if (minutes == 60000000) {
      degrees++;
      minutes = 0;
   }
   snprintf(buf, capacity, "%0*u%02u.%06u", width, degrees, (unsigned)(minutes / 1000000), (unsigned)(minutes % 1000000));
}

static void fixed_sentence(const struct gps_source *source, char *buf, size_t capacity) {
   char lat[24], lon[24], utc[16], date[16];
   nmea_angle(source->lat, 2, lat, sizeof(lat));
   nmea_angle(source->lon, 3, lon, sizeof(lon));
   struct tm tm; gmtime_r(&now, &tm);
   strftime(utc, sizeof(utc), "%H%M%S", &tm);
   strftime(date, sizeof(date), "%d%m%y", &tm);
   // RMC mode M explicitly identifies manually configured coordinates.
   bool valid=source->received ? source->valid : source->fixed;
   int len;
   if (valid) {
      len=snprintf(buf,capacity,"$GPRMC,%s,A,%s,%c,%s,%c,0.0,,%s,,,%c", utc,lat,source->lat<0 ? 'S' : 'N',lon,source->lon<0 ? 'W' : 'E',date, source->received ? (source->manual ? 'M' : 'A') : 'M');
   } else {
      len=snprintf(buf,capacity,"$GPRMC,%s,V,,,,,0.0,,%s,,,N",utc,date);
   }

   unsigned checksum = 0;
   for (int i = 1; i < len; i++) {
      checksum ^= (unsigned char)buf[i];
   }
   snprintf(buf + len, capacity - len, "*%02X", checksum);
}

static void send_position(struct gps_source *source, rrconn_t *client) {
   if (!source->channel) {
      return;
   }
   const struct gps_source *position = effective(source);
   if (!available(position)) {
      return;
   }

   uint32_t lat = (uint32_t)position->lat, lon = (uint32_t)position->lon;
   uint8_t payload[RR_GPS_POSITION_PAYLOAD_LEN] = {
      (uint8_t)(lat >> 24), (uint8_t)(lat >> 16), (uint8_t)(lat >> 8), (uint8_t)lat,
      (uint8_t)(lon >> 24), (uint8_t)(lon >> 16), (uint8_t)(lon >> 8), (uint8_t)lon,
      (uint8_t)((position->valid || position->fixed ? RR_GPS_POSITION_VALID : 0) |
         (position->manual || position->fixed ? RR_GPS_POSITION_MANUAL : 0))
   };
   ws_media_send_frame(source->channel, client, payload, sizeof(payload), RR_GPS_FRAME_CODEC);
   if (!client) {
      dict *d = dict_new();
      if (d) {
         dict_add(d, "gps.source", source->alias);
         dict_add_int(d, "gps.lat", position->lat);
         dict_add_int(d, "gps.lon", position->lon);
         dict_add_int(d, "gps.flags", payload[8]);
         event_emit_dict("serial.gps.position", NULL, d);
         dict_free(d);
      }
      source->sent_at = mono_us();
      source->published = true;
   }
}
static void publish(struct gps_source *source, rrconn_t *client) {
   send_position(source, client);
}
static bool receiver_angle(const char *text,const char *hemisphere,bool latitude,int32_t *result) {
   if (!text || !hemisphere || strlen(hemisphere)!=1) return false;
   size_t whole=strcspn(text,".");
   unsigned degree_digits=latitude ? 2 : 3,maximum=latitude ? 90 : 180;
   if(whole!=degree_digits+2) return false;
   unsigned degrees=0;
   for(unsigned i=0;i<degree_digits;i++) {
      if(!isdigit((unsigned char)text[i])) return false;
      degrees=degrees*10+(text[i]-'0');
   }
   const char *minutes=text+degree_digits;int32_t min;
   if(!coordinate(&minutes,&min,60) || *minutes || min<0 || min>=600000000 ||
      degrees>maximum || (degrees==maximum && min)) return false;
   int32_t angle=degrees*10000000+(min+30)/60;
   if(hemisphere[0]==(latitude ? 'S' : 'W')) angle=-angle;
   else if(hemisphere[0]!=(latitude ? 'N' : 'E')) return false;
   *result=angle;return true;
}
static void receiver_position(struct gps_source *source,const char *sentence) {
   char copy[512];snprintf(copy,sizeof(copy),"%s",sentence);
   char *star=strchr(copy,'*');if(star)*star='\0';
   char *fields[24],*cursor=copy;unsigned count=0;
   while(cursor && count<24) fields[count++]=strsep(&cursor,",");
   if(count<7 || strlen(fields[0])!=6) return;
   unsigned lat,lon;bool valid,manual=false;
   const char *kind=fields[0]+3;
   if(!strcmp(kind,"RMC")) {
      lat=3;lon=5;valid=!strcmp(fields[2],"A");
      if(strcmp(fields[2],"A") && strcmp(fields[2],"V")) return;
      manual=count>12 && !strcmp(fields[12],"M");
   } else if(!strcmp(kind,"GGA")) {
      lat=2;lon=4;
      if(strlen(fields[6])!=1 || fields[6][0]<'0' || fields[6][0]>'8') return;
      valid=fields[6][0]!='0';manual=fields[6][0]=='7';
   } else if(!strcmp(kind,"GLL")) {
      lat=1;lon=3;valid=!strcmp(fields[6],"A");
      if(strcmp(fields[6],"A") && strcmp(fields[6],"V")) return;
      manual=count>7 && !strcmp(fields[7],"M");
   } else return;
   int32_t a,b;
   if(valid && (!receiver_angle(fields[lat],fields[lat+1],true,&a) ||
      !receiver_angle(fields[lon],fields[lon+1],false,&b))) return;
   source->received=true;source->valid=valid;source->manual=manual;
   if(valid) {source->lat=a;source->lon=b;}
}

static void gps_input(const char *event, const char *data, rrconn_t *client, void *user) {
   dict *d = data && data[0] == '{' ? json2dict(data) : NULL;
   const char *sentence = d ? dict_get(d, "gps.nmea", NULL) : data;
   const char *alias = d ? dict_get(d, "gps.source", "station") : "station";
   struct gps_source *source = find(alias);
   if (source && !source->fixed && sentence && strlen(sentence) >= 6 && strlen(sentence) <= 509 && rr_nmea_valid(sentence)) {
      receiver_position(source,sentence);
   }
   if (d) dict_free(d);
}
static void poll_gps(const char *event, const char *data, rrconn_t *client, void *user) {
   uint64_t time = mono_us();
   for (unsigned i = 0; i < count; i++)
      if (!sources[i].published || time - sources[i].sent_at >= UINT64_C(300000000)) {
         publish(&sources[i], NULL);
      }
}
static void subscribed(const char *event, const char *data, rrconn_t *client, void *user) {
   dict *d = json2dict(data); if (!d) return;
   const char *uuid = dict_get(d, "media.chan-uuid", "");
   for (unsigned i = 0; i < count; i++)
      if (sources[i].channel && !strcmp(sources[i].channel->uuid, uuid)) {
         publish(&sources[i], client);
      }
   dict_free(d);
}
static bool configure_source(struct gps_source *source, const char *position) {
   if (position && *position) {
      source->fixed = rrserver_gps_position_parse(position, &source->lat, &source->lon);
      if (!source->fixed) Log(LOG_CRIT, "gps", "Invalid GPS position for %s: %s", source->alias, position);
      source->own_input = true;
      return source->fixed;
   }
   const char *key; char *value; int rank = 0;
   char target[96]; snprintf(target, sizeof(target), "%s.gps-in", source->alias);
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      if (!key || strncmp(key, "serial.", 7) || !value) {
         continue;
      }

      size_t n = strcspn(value, "@");
      if (n == strlen(target) && !memcmp(value, target, n)) {
         source->own_input = true;
      }
   }

   const char *gpsd = cfg_get("gpsd.target");
   if (gpsd && !strcmp(gpsd, source->alias) && cfg_get("module:rrserver-gpsd.options")) {
      source->own_input = true;
   }
   return true;
}
static bool add_rig(rr_server_rig_t *radio, void *user) {
   (void)user;
   if (count == GPS_SOURCES_MAX) return true;
   struct gps_source *source = &sources[count++];
   const char *alias = rr_rig_registry_alias(rig.rigs, radio);
   snprintf(source->alias, sizeof(source->alias), "%s", alias);
   source->channel = media_chan_add(RR_BINFRAME_SUBSYS_MODEM, RR_BINFRAME_DIR_RX,
      RR_BINFRAME_VFO_NA, rr_rig_registry_media_index(rig.rigs, radio), RR_GPS_FRAME_CODEC, "Rig GPS position");
   if (source->channel) {
      snprintf(source->channel->name, sizeof(source->channel->name), "%s.gps.rx", alias);
      snprintf(source->channel->room, sizeof(source->channel->room), "%s", rr_rig_registry_room(rig.rigs, radio));
      snprintf(source->channel->rig_uuid, sizeof(source->channel->rig_uuid), "%s", rr_server_rig_id(radio));
   }
   return !configure_source(source, rr_rig_config_get(alias, "gps.position"));
}
static void inventory_gps(const char *event, const char *data, rrconn_t *client, void *user) {
   if (!client || !client->authenticated) {
      return;
   }
   dict *request = json2dict(data);

   if (!request) {
      return;
   }
   const char *scope = dict_get(request, "inventory.scope", "");
   struct gps_source *source = find(scope);

   if (source) {
      const struct gps_source *position = effective(source);

      for (unsigned output = 0; output < 2; output++) {
         char name[80]; snprintf(name, sizeof(name), "%s.gps-%s", scope, output ? "out" : "in");
         dict *row = rr_inventory_row(dict_get(request, "request.id", ""),
            dict_get_uint(request, "inventory.depth", 1), "gps", name,
            output && source->channel ? source->channel->uuid : NULL);

         if (!row) {
            continue;
         }
         dict_add(row, "inventory.state", output ?
            (available(position) && (position->fixed || position->valid) ? "position-known" : "no-fix") :
            source->fixed ? "disabled-by-fixed-position" : source->own_input ? "receiver" : source == sources ? "unconfigured" : "station-fallback");
         dict_add(row, "inventory.source", position->alias);
         dict_add(row, "inventory.action", output ? "/gps subscribe|unsubscribe <scope>" : "server-config-only");

         if (output && source->channel) {
            dict_add(row, "inventory.room", source->channel->room);
         }

         if (output && available(position) && (position->fixed || position->valid)) {
            int64_t lat = position->lat, lon = position->lon;
            char coordinates[64];
            snprintf(coordinates, sizeof(coordinates), "%s%lld.%07lld,%s%lld.%07lld",
               lat < 0 ? "-" : "", (long long)(llabs(lat) / 10000000), (long long)(llabs(lat) % 10000000),
               lon < 0 ? "-" : "", (long long)(llabs(lon) / 10000000), (long long)(llabs(lon) % 10000000));
            dict_add(row, "inventory.coordinates", coordinates);
         }
         rr_inventory_send(client, row);
      }
   }
   dict_free(request);
}
bool rrserver_gps_init(void) {
   memset(sources, 0, sizeof(sources)); count = 1;
   snprintf(sources[0].alias, sizeof(sources[0].alias), "station");
   sources[0].channel = media_chan_add(RR_BINFRAME_SUBSYS_MODEM, RR_BINFRAME_DIR_RX, RR_BINFRAME_VFO_NA, RR_BINFRAME_RIG_NA, RR_GPS_FRAME_CODEC, "Station GPS position");

   if (sources[0].channel) {
      snprintf(sources[0].channel->name, sizeof(sources[0].channel->name), "station.gps.rx");
      snprintf(sources[0].channel->room, sizeof(sources[0].channel->room), "%s", ws_site_room());
   }

   if (!configure_source(&sources[0],cfg_get("station.gps.position")) || rr_rig_registry_foreach(rig.rigs,add_rig,NULL)) {
      rrserver_gps_fini();return true;
   }
   inventory_token = event_on_token(RR_INVENTORY_EVENT, inventory_gps, NULL);
   input_token = event_on_token("gps.nmea.input", gps_input, NULL);
   poll_token = event_on_token("server.poll", poll_gps, NULL);
   subscribed_token = event_on_token("media.subscribed", subscribed, NULL);
   return false;
}
void rrserver_gps_fini(void) {
   event_off_token(inventory_token); inventory_token = NULL;
   event_off_token(input_token); input_token = NULL;
   event_off_token(poll_token); poll_token = NULL;
   event_off_token(subscribed_token); subscribed_token = NULL;

   for (unsigned i = 0; i < count; i++) if (sources[i].channel) {
      media_send_chan_removed_all(sources[i].channel);
      media_chan_remove(sources[i].channel->uuid);
   }
   memset(sources, 0, sizeof(sources)); count = 0;
}
