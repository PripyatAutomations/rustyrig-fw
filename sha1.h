#if	!defined(__rr_sha1_h)
#define	__rr_sha1_h

// SHA1 constants and operations
#define SHA1_BLOCK_SIZE 64
#define SHA1_DIGEST_SIZE 20

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    unsigned char buffer[SHA1_BLOCK_SIZE];
} SHA1_CTX;

#if	0
extern void SHA1_Init(SHA1_CTX *context);
extern void SHA1_Update(SHA1_CTX *context, const unsigned char *data, size_t len);
extern void SHA1_Final(unsigned char *digest, SHA1_CTX *context);
extern void SHA1_Transform(uint32_t *state, const unsigned char *buffer);
#endif

extern void compute_sha1(const char *input, char *output);

#endif	// !defined(__rr_sha1_h)