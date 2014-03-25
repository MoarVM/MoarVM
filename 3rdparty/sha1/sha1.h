/* public api for steve reid's public domain SHA-1 implementation */
/* this file is in the public domain */

#ifndef __SHA1_H
#define __SHA1_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned int  state[5];
    unsigned int  count[2];
    unsigned char buffer[64];
} SHA1Context;

#define SHA1_DIGEST_SIZE 20

void SHA1Init(SHA1Context* context);
void SHA1Update(SHA1Context* context, const unsigned char* data, const size_t len);
void SHA1_Digest(SHA1Context* context, unsigned char digest[SHA1_DIGEST_SIZE]);
void SHA1Final(SHA1Context* context, char *output);

#ifdef __cplusplus
}
#endif

#endif /* __SHA1_H */
