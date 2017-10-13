#include <stdio.h>
#include <stdlib.h>
#include "b64.h"


const char *const base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char		  pad	 = '=';

char *b64_encode(const unsigned char *src, size_t src_len)
{
	size_t pad_len 	= src_len % 3;
	pad_len = pad_len ? 3-pad_len : pad_len;
	size_t dest_len = (src_len + pad_len) / 3 * 4;
	printf("[b64] dest_len: %d\n", dest_len);
	printf("[b64] pad_len: %d\n", pad_len);
	size_t src_idx  = 0, dest_idx = 0;

	char *dest = malloc((dest_len+1)*sizeof(*dest));	// Null terminate

	while(src_len > 2){
		dest[dest_idx++] = base64[src[src_idx] >> 2];
		dest[dest_idx++] = base64[((src[src_idx] & 0x03) << 4) + (src[src_idx+1] >> 4)];
		src_idx++;
		dest[dest_idx++] = base64[((src[src_idx] & 0x0F) << 2) + (src[src_idx+1] >> 6)];
		src_idx++;
		dest[dest_idx++] = base64[src[src_idx++] & 0x3F];

		src_len -= 3;
	}

	if(src_len){
		dest[dest_idx++] = base64[src[src_idx] >> 2];
		dest[dest_idx++] = base64[((src[src_idx] & 0x03) << 4)];
		if(pad_len == 2){
			dest[dest_idx++] = pad;
		}else{
			dest[dest_idx++] = base64[((src[src_idx] & 0x0F) << 2)];
		}
		dest[dest_idx++] = pad;
	}

	dest[dest_idx] = '\0';
//	printf("[b64] dest_idx: %d\n", dest_idx);

	return dest;
}
