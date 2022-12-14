/*------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.


Midas Zhou
midaszhou@yahoo.com(Not in use since 2022_03_01)
------------------------------------------------------------------*/
#ifndef __EGI_AES_H__
#define __EGI_AES_H__

#include <stdint.h>

/* OpenSSL */
#include <openssl/aes.h>
int AES_cbc128_encrypt(unsigned char *indata, size_t insize,  unsigned char *outdata, size_t *outsize,
			const unsigned char *ukey, const unsigned char *uiv, int encode);

/* EGi */
void aes_PrintState(const uint8_t *state);
int aes_DataToState(const uint8_t *data, uint8_t *state);
int aes_StateToData(const uint8_t *state, uint8_t *data);
int aes_ShiftRows(uint8_t *state);
int aes_InvShiftRows(uint8_t *state);
int aes_ExpRoundKeys(uint8_t Nr, uint8_t Nk, const uint8_t *inkey, uint32_t *keywords);
int aes_AddRoundKey(uint8_t Nr, uint8_t Nk, uint8_t round, uint8_t *state, const uint32_t *keywords);
int aes_EncryptState(uint8_t Nr, uint8_t Nk, uint32_t *keywords, uint8_t *state);
int aes_DecryptState(uint8_t Nr, uint8_t Nk, uint32_t *keywords, uint8_t *state);

int egi_AES128CBC_encrypt(uint8_t *indata, size_t insize,  uint8_t *outdata, size_t *outsize,
                          const uint8_t ukey[16],  const uint8_t uiv[16], int encode);

#endif
