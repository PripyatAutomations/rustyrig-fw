#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "config.h"
#include "sha1.h"

void SHA1_Transform(uint32_t *state, const unsigned char *buffer) {
    uint32_t a, b, c, d, e, t, w[80];
    size_t i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t) buffer[i * 4] << 24) | ((uint32_t) buffer[i * 4 + 1] << 16) |
               ((uint32_t) buffer[i * 4 + 2] << 8) | (uint32_t) buffer[i * 4 + 3];
    }

    for (i = 16; i < 80; i++) {
        w[i] = (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]);
        w[i] = (w[i] << 1) | (w[i] >> 31);
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    for (i = 0; i < 80; i++) {
        if (i < 20) {
            t = ((b & c) | (~b & d)) + 0x5A827999;
        } else if (i < 40) {
            t = (b ^ c ^ d) + 0x6ED9EBA1;
        } else if (i < 60) {
            t = ((b & c) | (b & d) | (c & d)) + 0x8F1BBCDC;
        } else {
            t = (b ^ c ^ d) + 0xCA62C1D6;
        }
        t += (a << 5) | (a >> 27);
        t += e + w[i];
        e = d;
        d = c;
        c = (b << 30) | (b >> 2);
        b = a;
        a = t;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void SHA1_Init(SHA1_CTX *context) {
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->count[0] = context->count[1] = 0;
}

void SHA1_Update(SHA1_CTX *context, const unsigned char *data, size_t len) {
    size_t i;
    size_t j = context->count[0] / 8 % SHA1_BLOCK_SIZE;
    size_t remaining = len;
    size_t buffer_space = SHA1_BLOCK_SIZE - j;

    context->count[0] += len * 8;
    if (context->count[0] < len * 8) {
        context->count[1]++;
    }

    while (remaining >= buffer_space) {
        memcpy(&context->buffer[j], data, buffer_space);
        data += buffer_space;
        remaining -= buffer_space;
        SHA1_Transform(context->state, context->buffer);
        j = 0;
        buffer_space = SHA1_BLOCK_SIZE;
    }

    if (remaining > 0) {
        memcpy(&context->buffer[j], data, remaining);
    }
}

void SHA1_Final(unsigned char *digest, SHA1_CTX *context) {
    unsigned char finalcount[8];
    size_t i;

    for (i = 0; i < 8; i++) {
        finalcount[i] = (unsigned char)((context->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255); 
    }

    SHA1_Update(context, (unsigned char *)"\200", 1);
    while ((context->count[0] & 504) != 448) {
        SHA1_Update(context, (unsigned char *)"\0", 1);
    }

    SHA1_Update(context, finalcount, 8);
    for (i = 0; i < SHA1_DIGEST_SIZE; i++) {
        digest[i] = (unsigned char)((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}


// SHA1 wrapper to return the hash as a string
void compute_sha1(const char *input, char *output) {
    SHA1_CTX context;
    unsigned char hash[SHA1_DIGEST_SIZE];
    int i;

    SHA1_Init(&context);
    SHA1_Update(&context, (unsigned char*)input, strlen(input));
    SHA1_Final(hash, &context);

    // Convert hash to hex string
    for (i = 0; i < SHA1_DIGEST_SIZE; i++) {
        sprintf(output + i * 2, "%02x", hash[i]);
    }
}
