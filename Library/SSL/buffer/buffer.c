/* crypto/buffer/buffer.c */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 * 
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 * 
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from 
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 * 
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#ifndef COMPILE_OPTION_HOST_SERVICE_ONLY

#ifdef __GEOS__
#include <Ansi/stdio.h>
#else
#include <stdio.h>
#endif
#include "cryptlib.h"
#include "buffer.h"

BUF_MEM *BUF_MEM_new()
	{
	BUF_MEM *ret;

	ret=(BUF_MEM *)Malloc(sizeof(BUF_MEM));
	if (ret == NULL)
		{
		BUFerr(BUF_F_BUF_MEM_NEW,ERR_R_MALLOC_FAILURE);
		return(NULL);
		}
	ret->length=0;
	ret->max=0;
	ret->data=NULL;
	return(ret);
	}

void BUF_MEM_free(a)
BUF_MEM *a;
	{
	if (a->data != NULL)
		{
		memset(a->data,0,(unsigned int)a->max);
		Free(a->data);
		}
	Free(a);
	}

int BUF_MEM_grow(str, len)
BUF_MEM *str;
int len;
	{
	char *ret;
	unsigned int n;

	if (str->length >= len)
		{
		str->length=len;
		return(len);
		}
	if (str->max >= len)
		{
		memset(&(str->data[str->length]),0,len-str->length);
		str->length=len;
		return(len);
		}
	n=(len+3)/3*4;
	if (str->data == NULL)
		ret=(char *)Malloc(n);
	else
		ret=(char *)Realloc(str->data,n);
	if (ret == NULL)
		{
		BUFerr(BUF_F_BUF_MEM_GROW,ERR_R_MALLOC_FAILURE);
		len=0;
		}
	else
		{
		str->data=ret;
		str->length=len;
		str->max=n;
		}
	return(len);
	}

char *BUF_strdup(str)
char *str;
	{
	char *ret;
	int n;

	if (str == NULL) return(NULL);

	n=strlen(str);
	ret=Malloc(n+1);
	if (ret == NULL) 
		{
		BUFerr(BUF_F_BUF_STRDUP,ERR_R_MALLOC_FAILURE);
		return(NULL);
		}
	memcpy(ret,str,n+1);
	return(ret);
	}

#endif
