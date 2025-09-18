//
// dict.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
/*-------------------------------------------------------------------------*/
/**
   @file    dict.c
   @author  N. Devillard
   @date    Apr 2011
   @brief   Dictionary object
*/
/*--------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
                                Includes
 ---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/logger.h>
#include <librustyaxe/util.math.h>

/*---------------------------------------------------------------------------
                                Defines
 ---------------------------------------------------------------------------*/
/** Minimum dictionary size to start with */
#define DICT_MIN_SZ     8

/** Dummy pointer to reference deleted keys */
#define DUMMY_PTR       ((void*)-1)
/** Used to hash further when handling collisions */
#define PERTURB_SHIFT   5
/** Beyond this size, a dictionary will not be grown by the same factor */
#define DICT_BIGSZ      64000

/** Define this to:
    0 for no debugging
    1 for moderate debugging
    2 for heavy debugging
 */
#define DICT_DEBUG           0

/*
 * Specify which hash function to use
 * MurmurHash is fast but may not work on all architectures
 * Dobbs is a tad bit slower but not by much and works everywhere
 */
#define dict_hash   dict_hash_murmur
/* #define dict_hash   dict_hash_dobbs */

/* Forward definitions */
static int dict_resize(dict * d);


/**
  This hash function has been taken from an Article in Dr Dobbs Journal.
  There are probably better ones out there but this one does the job.
 */
static unsigned dict_hash_dobbs(const char * key) {
   int         len;
   unsigned    hash;
   int         i;

   len = strlen(key);
   for (hash = 0, i = 0; i < len; i++) {
      hash += (unsigned)key[i];
      hash += (hash << 10);
      hash ^= (hash >> 6);
   }

   hash += (hash <<  3);
   hash ^= (hash >> 11);
   hash += (hash << 15);
   return hash;
}


/* Murmurhash */
static unsigned dict_hash_murmur(const char * key) {
   int         len;
   unsigned    h, k, seed;
   unsigned    m = 0x5bd1e995;
   int         r = 24;
   unsigned char * data;

   seed = 0x0badcafe;
   len  = (int)strlen(key);

   h = seed ^ len;
   data = (unsigned char *)key;
   while(len >= 4) {
       k = *(unsigned int *)data;

       k *= m;
       k ^= k >> r;
       k *= m;

       h *= m;
       h ^= k;

       data += 4;
       len -= 4;
   }
   switch(len) {
      case 3: h ^= data[2] << 16;
      case 2: h ^= data[1] << 8;
      case 1: h ^= data[0];
              h *= m;
   };

   h ^= h >> 13;
   h *= m;
   h ^= h >> 15;

   return h;
}

/** Lookup an element in a dict
    This implementation copied almost verbatim from the Python dictionary
    object, without the Pythonisms.
    */
static keypair * dict_lookup(dict * d, const char * key, unsigned hash) {
    keypair * freeslot;
    keypair * ep;
    unsigned    i;
    unsigned    perturb;

    if (!d || !key)
        return NULL;

    i = hash & (d->size-1);
    /* Look for empty slot */
    ep = d->table + i;
    if (ep->key == NULL || ep->key==key) {
        return ep;
    }
    if (ep->key == DUMMY_PTR) {
        freeslot = ep;
    } else {
        if (ep->hash == hash &&
            !strcmp(key, ep->key)) {
                return ep;
        }
        freeslot = NULL;
    }
    for (perturb = hash; ; perturb >>= PERTURB_SHIFT) {
        i = (i<<2) + i + perturb + 1;
        i &= (d->size-1);
        ep = d->table + i;
        if (ep->key == NULL) {
            return freeslot == NULL ? ep : freeslot;
        }
        if ((ep->key == key) ||
            (ep->hash == hash &&
             ep->key  != DUMMY_PTR &&
             !strcmp(ep->key, key))) {
            return ep;
        }
        if (ep->key==DUMMY_PTR && freeslot == NULL) {
            freeslot = ep;
        }
    }
    return NULL;
}

/** Add an item to a dictionary without copying key/val
    Used by dict_resize() only.
 */
static int dict_add_p(dict * d, const char * key, char * val) {
   unsigned  hash;
   keypair * slot;

   if (!d || !key) {
      return -1;
   }

#if DICT_DEBUG>2
   printf("dict_add_p[%s][%s]\n", key, val ? val : "UNDEF");
#endif
   hash = dict_hash(key);
   slot = dict_lookup(d, key, hash);
   if (slot) {
      slot->key  = key;
      slot->val  = val;
      slot->hash = hash;
      d->used++;
      d->fill++;
 
      if ((3*d->fill) >= (d->size*2)) {
         if (dict_resize(d)!=0) {
            return -1;
         }
      }
   }
   return 0;
}

/** Add an item to a dictionary by copying key/val into the dict. */
int dict_add(dict * d, const char * key, char * val)
{
    unsigned  hash;
    keypair * slot;

    if (!d || !key) {
        return -1;
    }

#if DICT_DEBUG>2
    printf("dict_add[%s][%s]\n", key, val ? val : "UNDEF");
#endif
    hash = dict_hash(key);
    slot = dict_lookup(d, key, hash);
    if (slot) {
        slot->key  = strdup(key);
        if (!(slot->key)) {
            return -1;
        }
        slot->val  = val ? strdup(val) : val;
        if (val && !(slot->val)) {
            free((char *)slot->key);
            return -1;
        }
        slot->hash = hash;
        d->used++;
        d->fill++;
        if ((3*d->fill) >= (d->size*2)) {
            if (dict_resize(d)!=0) {
                return -1;
            }
        }
    }
    return 0;
}

/** Resize a dictionary */
static int dict_resize(dict * d)
{
    unsigned      newsize;
    keypair      *oldtable;
    unsigned      i;
    unsigned      oldsize;
    unsigned      factor;

    newsize = d->size;
    /*
     * Re-sizing factor depends on the current dict size.
     * Small dicts will expand 4 times, bigger ones only 2 times
     */
    factor = (d->size > DICT_BIGSZ) ? 2 : 4;
    while (newsize <= (factor * d->used)) {
        newsize *= 2;
    }
    /* Exit early if no re-sizing needed */
    if (newsize == d->size) {
        return 0;
    }
#if DICT_DEBUG>2
    printf("resizing %d to %d (used: %d)\n", d->size, newsize, d->used);
#endif
    /* Shuffle pointers, re-allocate new table, re-insert data */
    oldtable = d->table;
    d->table = calloc(newsize, sizeof(keypair));

    if (!(d->table)) {
        /* Memory allocation failure */
        return -1;
    }
    oldsize  = d->size;
    d->size  = newsize;
    d->used  = 0;
    d->fill  = 0;
    for (i = 0; i < oldsize; i++) {
        if (oldtable[i].key && (oldtable[i].key!=DUMMY_PTR)) {
            dict_add_p(d, oldtable[i].key, oldtable[i].val);
        }
    }
    free(oldtable);
    return 0;
}


/** Public: allocate a new dict */
dict * dict_new(void) {
    dict * d;

    d = calloc(1, sizeof(dict));
    if (!d) {
       return NULL;
    }
    d->size  = DICT_MIN_SZ;
    d->used  = 0;
    d->fill  = 0;
    d->table = calloc(DICT_MIN_SZ, sizeof(keypair));
    return d;
}

/** Public: deallocate a dict */
void dict_free(dict * d) {
   int i;

   if (!d) {
      return;
   }

   for (i = 0; i < d->size; i++) {
      if (d->table[i].key && d->table[i].key != DUMMY_PTR) {
         free((char *)d->table[i].key);
         if (d->table[i].val) {
            free(d->table[i].val);
         }
      }
   }
   free(d->table);
   free(d);
   return;
}

/** Public: get an item from a dict */
char * dict_get(dict * d, const char * key, char * defval)
{
    keypair * kp;
    unsigned  hash;

    if (!d || !key)
        return defval;

    hash = dict_hash(key);
    kp = dict_lookup(d, key, hash);

    if (kp) {
        return kp->val;
    }
    return defval;
}

/** Public: delete an item in a dict */
int dict_del(dict * d, const char * key)
{
    unsigned    hash;
    keypair *   kp;

    if (!d || !key)
        return -1;

    hash = dict_hash(key);
    kp = dict_lookup(d, key, hash);
    if (!kp)
        return -1;
    if (kp->key && kp->key!=DUMMY_PTR)
        free((char *)kp->key);
    kp->key = DUMMY_PTR;
    if (kp->val)
        free(kp->val);
    kp->val = NULL;
    d->used --;
    return 0;
}

/** Public: enumerate a dictionary */
int dict_enumerate(dict * d, int rank, const char ** key, char ** val)
{
    if (!d || !key || !val || (rank<0))
        return -1;

    while ((d->table[rank].key == NULL || d->table[rank].key==DUMMY_PTR) &&
           (rank<d->size))
        rank++;

    if (rank>=d->size) {
        *key = NULL;
        *val = NULL;
        rank = -1;
    } else {
        *key = d->table[rank].key;
        *val = d->table[rank].val;
        rank++;
    }
    return rank;
}

/** Public: dump a dict to a file pointer */
void dict_dump(dict * d, FILE * out)
{
    const char * key;
    char * val;
    int    rank=0;

    if (!d || !out)
        return;

    while (1) {
        rank = dict_enumerate(d, rank, &key, &val);
        if (rank<0)
            break;
        fprintf(out, "%20s=%s\n", key, val ? val : "UNDEF");
    }
    return;
}

////////////////////////////////////////////////////
// rustyaxe additions, all bugs below are mine ;) //
////////////////////////////////////////////////////
bool dict_get_bool(dict *d, const char *key, bool def) {
   if (!d || !key) {
      return def;
   }

   const char *s = dict_get_exp(d, key);
   bool rv = def;

   if (!s) {
      return rv;
   }

   if (strcasecmp(s, "true") == 0 ||
       strcasecmp(s, "yes") == 0 ||
       strcasecmp(s, "on") == 0 ||
       strcasecmp(s, "1") == 0) {
      rv = true;
   } else if (strcasecmp(s, "false") == 0 ||
              strcasecmp(s, "no") == 0 ||
              strcasecmp(s, "off") == 0 ||
              strcasecmp(s, "0") == 0) {
      rv = false;
   }

   free((void *)s);
   return rv;
}

int dict_get_int(dict *d, const char *key, int def) {
   if (!key) {
      return def;
   }

   const char *s = dict_get_exp(d, key);
   if (s) {
      int val = atoi(s);
      free((void *)s);
      return val;
   }
   return def;
}

unsigned int dict_get_uint(dict *d, const char *key, unsigned int def) {
   if (!key) {
      return def;
   }

   const char *s = dict_get_exp(d, key);
   if (s) {
      char *ep = NULL;
      unsigned int val = (uint32_t)strtoul(s, &ep, 10);
      free((void *)s);

      // incomplete parse
      if (*ep != '\0') {
         return def;
      } else {
         return val;
      }
   }
   return def;
}


time_t dict_get_time_t(dict *d, const char *key, time_t def) {
   if (!key) {
      return def;
   }

   const char *s = dict_get_exp(d, key);
   if (s) {
      char *ep = NULL;
      errno = 0;
      long long val = strtoll(s, &ep, 10);
      free((void *)s);

      if (*ep != '\0' || errno == ERANGE) {
         return def;   // incomplete parse or out of range
      } else {
         return (time_t)val;
      }
   }
   return def;
}
double dict_get_double(dict *d, const char *key, double def) {
   if (!key) {
      return def;
   }

   const char *s = dict_get_exp(d, key);

   if (s) {
      double val = safe_atod(s);
      free((void *)s);
      if (val == NAN) {
         return def;
      }
   }
   return def;
}

float dict_get_float(dict *d, const char *key, float def) {
   if (!key) {
      return def;
   }

   const char *s = dict_get_exp(d, key);

   if (s) {
      float val = safe_atof(s);
      free((void *)s);
      if (val == NAN) {
         return def;
      }
   }
   return def;
}

long dict_get_long(dict *d, const char *key, long def) {
   if (!key) {
      return def;
   }

   const char *s = dict_get_exp(d, key);

   if (s) {
      long val = safe_atol(s);
      free((void *)s);
      if (val == NAN) {
         return def;
      }
   }
   return def;
}

long long dict_get_llong(dict *d, const char *key, long long def) {
   if (!key) {
      return def;
   }

   const char *s = dict_get_exp(d, key);

   if (s) {
      long long val = safe_atol(s);
      free((void *)s);
      if (val == NAN) {
         return def;
      }
   }
   return def;
}

// You *MUST* free the return value
const char *dict_get_exp(dict *d, const char *key) {
   if (!d) {
      return NULL;
   }

   if (!key) {
      Log(LOG_WARN, "config", "dict_get_exp: NULL key!");
      return NULL;
   }

   const char *p = dict_get(d, key, NULL);
   if (!p) {
      Log(LOG_DEBUG, "config", "dict_get_exp: key |%s| not found", key);
//      dict_dump(d, stderr);
      return NULL;
   }

   char *buf = malloc(MAX_CFG_EXP_STRLEN);
   if (!buf) {
      fprintf(stderr, "OOM in dict_get_exp!\n");
      return NULL;
   }

   strncpy(buf, p, MAX_CFG_EXP_STRLEN - 1);
   buf[MAX_CFG_EXP_STRLEN - 1] = '\0';

   for (int depth = 0; depth < MAX_CFG_EXP_RECURSION; depth++) {
      char tmp[MAX_CFG_EXP_STRLEN];
      char *dst = tmp;
      const char *src = buf;
      int changed = 0;

      while (*src && (dst - tmp) < MAX_CFG_EXP_STRLEN - 1) {
         if (src[0] == '$' && src[1] == '{') {
            const char *end = strchr(src + 2, '}');

            if (end) {
               size_t klen = end - (src + 2);
               char keybuf[256];

               if (klen >= sizeof(keybuf)) {
                  klen = sizeof(keybuf) - 1;
               }

               memcpy(keybuf, src + 2, klen);
               keybuf[klen] = '\0';

               const char *val = cfg_get(keybuf);
               if (val) {
                  size_t vlen = strlen(val);

                  if ((dst - tmp) + vlen >= MAX_CFG_EXP_STRLEN - 1) {
                     vlen = MAX_CFG_EXP_STRLEN - 1 - (dst - tmp);
                  }

                  memcpy(dst, val, vlen);
                  dst += vlen;
                  changed = 1;
               }
               src = end + 1;
               continue;
            }
         }
         *dst++ = *src++;
      }
      *dst = '\0';

      if (!changed) {
         break; // No more expansions
      }

      strncpy(buf, tmp, MAX_CFG_EXP_STRLEN - 2);
      buf[MAX_CFG_EXP_STRLEN - 1] = '\0';
   }

   // Shrink the allocation down to it's actual size
   size_t final_len = strlen(buf) + 1;
   char *shrunk = realloc(buf, final_len);

   if (shrunk) {
      buf = shrunk;
   }

//   fprintf(stderr, "dict_get_exp: returning %lu bytes for key %s => %s\n", (unsigned long)final_len, key, buf);
   return buf;
}
