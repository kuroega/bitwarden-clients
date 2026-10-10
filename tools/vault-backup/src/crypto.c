#include "crypto.h"
#include <bcrypt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

#define CHUNK (1024u * 1024u)
#define HEADER 44
#define ROUNDS 600000
#define OK(s) ((s) >= 0)

/* Extended absolute paths preserve the documented 220-character directory limit. */
static int full_path(const wchar_t *path,wchar_t result[2048]) {
    if(!wcsncmp(path,L"\\\\?\\",4)){if(wcslen(path)>=2048)return 0;wcscpy(result,path);return 1;}
    wchar_t absolute[2048];DWORD n=GetFullPathNameW(path,2048,absolute,NULL);
    if(!n||n>=2040)return 0;
    if(!wcsncmp(absolute,L"\\\\",2))swprintf(result,2048,L"\\\\?\\UNC\\%ls",absolute+2);
    else swprintf(result,2048,L"\\\\?\\%ls",absolute);
    return 1;
}

static void put32(unsigned char *p, uint32_t n) {
    for (int i=0;i<4;i++) p[i]=(unsigned char)(n>>(i*8));
}
static uint32_t get32(const unsigned char *p) {
    uint32_t n=0; for (int i=0;i<4;i++) n|=(uint32_t)p[i]<<(8*i); return n;
}
static void put64(unsigned char *p, uint64_t n) {
    for (int i=0;i<8;i++) p[i]=(unsigned char)(n>>(i*8));
}
static uint64_t get64(const unsigned char *p) {
    uint64_t n=0; for (int i=0;i<8;i++) n|=(uint64_t)p[i]<<(8*i); return n;
}

/* Independent authenticated 1 MiB chunks: header + position + size is AAD.
 * The authenticated total length rejects truncation; EOF rejects extra data.
 * Salt and nonce prefix are fresh per archive. No password goes into argv. */
static int transform(const wchar_t *input, const wchar_t *output, const wchar_t *password, int decrypt,VaultProgress *progress) {
    vault_progress(progress,0,0,0);
    wchar_t input_path[2048],output_path[2048];
    if(!full_path(input,input_path)||!full_path(output,output_path))return 0;
    input=input_path;output=output_path;
    int success=0, output_created=0, password_bytes=0;
    FILE *in=NULL, *out=NULL;
    unsigned char h[HEADER]={0}, key_bytes[32]={0}, nonce[12], aad[HEADER+8], tag[16];
    unsigned char *plain=NULL,*cipher=NULL;
    char *utf8=NULL;
    BCRYPT_ALG_HANDLE hash=NULL,aes=NULL;
    BCRYPT_KEY_HANDLE key=NULL;
    uint64_t total=0,remaining;
    uint32_t index=0,rounds=ROUNDS;
    ULONG written=0;
    if (!password || !*password || !(in=_wfopen(input,L"rb"))) goto done;
    if (decrypt) {
        if (fread(h,1,HEADER,in)!=HEADER || memcmp(h,"VBKZST01",8)) goto done;
        rounds=get32(h+32); total=get64(h+36);
        if (rounds<600000 || rounds>2000000 || !total || total>(1ULL<<40)) goto done;
    } else {
        if (_fseeki64(in,0,SEEK_END) || _ftelli64(in)<=0) goto done;
        total=(uint64_t)_ftelli64(in); if (total>(1ULL<<40) || _fseeki64(in,0,SEEK_SET)) goto done;
        memcpy(h,"VBKZST01",8);
        if (!OK(BCryptGenRandom(NULL,h+8,24,BCRYPT_USE_SYSTEM_PREFERRED_RNG))) goto done;
        put32(h+32,rounds); put64(h+36,total);
    }
    password_bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,password,-1,NULL,0,NULL,NULL);
    if (password_bytes<=1 || !(utf8=malloc((size_t)password_bytes))) goto done;
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,password,-1,utf8,password_bytes,NULL,NULL)) goto done;
    if (!OK(BCryptOpenAlgorithmProvider(&hash,BCRYPT_SHA256_ALGORITHM,NULL,BCRYPT_ALG_HANDLE_HMAC_FLAG))) goto done;
    if (!OK(BCryptDeriveKeyPBKDF2(hash,(PUCHAR)utf8,(ULONG)password_bytes-1,h+8,16,rounds,key_bytes,32,0))) goto done;
    if (!OK(BCryptOpenAlgorithmProvider(&aes,BCRYPT_AES_ALGORITHM,NULL,0))) goto done;
    if (!OK(BCryptSetProperty(aes,BCRYPT_CHAINING_MODE,(PUCHAR)BCRYPT_CHAIN_MODE_GCM,sizeof(BCRYPT_CHAIN_MODE_GCM),0))) goto done;
    if (!OK(BCryptGenerateSymmetricKey(aes,&key,NULL,0,key_bytes,32,0))) goto done;
    plain=malloc(CHUNK); cipher=malloc(CHUNK);
    if (!plain || !cipher) goto done;
    /* CREATE_NEW prevents clobbering an existing backup/decrypted file. */
    HANDLE exclusive=CreateFileW(output,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if (exclusive==INVALID_HANDLE_VALUE) goto done;
    output_created=1;
    int fd=_open_osfhandle((intptr_t)exclusive,_O_BINARY|_O_WRONLY);
    if(fd<0){CloseHandle(exclusive);goto done;}
    out=_fdopen(fd,"wb"); if (!out) {_close(fd);goto done;}
    if (!decrypt && fwrite(h,1,HEADER,out)!=HEADER) goto done;
    remaining=total;
    while (remaining) {
        ULONG amount=(ULONG)(remaining>CHUNK?CHUNK:remaining);
        memcpy(aad,h,HEADER); put32(aad+HEADER,index); put32(aad+HEADER+4,amount);
        memcpy(nonce,h+24,8);
        nonce[8]=(unsigned char)(index>>24); nonce[9]=(unsigned char)(index>>16);
        nonce[10]=(unsigned char)(index>>8); nonce[11]=(unsigned char)index;
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
        BCRYPT_INIT_AUTH_MODE_INFO(auth);
        auth.pbNonce=nonce; auth.cbNonce=12; auth.pbAuthData=aad; auth.cbAuthData=sizeof(aad);
        auth.pbTag=tag; auth.cbTag=16;
        if (decrypt) {
            if (fread(cipher,1,amount,in)!=amount || fread(tag,1,16,in)!=16) goto done;
            if (!OK(BCryptDecrypt(key,cipher,amount,&auth,NULL,0,plain,amount,&written,0))) goto done;
            if (written!=amount || fwrite(plain,1,amount,out)!=amount) goto done;
        } else {
            if (fread(plain,1,amount,in)!=amount) goto done;
            if (!OK(BCryptEncrypt(key,plain,amount,&auth,NULL,0,cipher,amount,&written,0))) goto done;
            if (written!=amount || fwrite(cipher,1,amount,out)!=amount || fwrite(tag,1,16,out)!=16) goto done;
        }
        remaining-=amount; index++;
        vault_progress(progress,total-remaining,total,0);
    }
    if (fgetc(in)!=EOF || ferror(in) || fflush(out)) goto done;
    if (!FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(out)))) goto done;
    success=1;
done:
    if (out && fclose(out)) success=0;
    if (in) fclose(in);
    if (key) BCryptDestroyKey(key);
    if (aes) BCryptCloseAlgorithmProvider(aes,0);
    if (hash) BCryptCloseAlgorithmProvider(hash,0);
    SecureZeroMemory(key_bytes,sizeof(key_bytes));
    if (utf8) { SecureZeroMemory(utf8,(size_t)password_bytes); free(utf8); }
    if (plain) { SecureZeroMemory(plain,CHUNK); free(plain); } free(cipher);
    if (!success && output_created) DeleteFileW(output);
    if(success)vault_progress(progress,total,total,1);
    return success;
}
int vault_encrypt_progress(const wchar_t *a,const wchar_t *b,const wchar_t *p,VaultProgress *progress) {return transform(a,b,p,0,progress);}
int vault_decrypt_progress(const wchar_t *a,const wchar_t *b,const wchar_t *p,VaultProgress *progress) {return transform(a,b,p,1,progress);}
int vault_encrypt(const wchar_t *a,const wchar_t *b,const wchar_t *p) {return transform(a,b,p,0,NULL);}
int vault_decrypt(const wchar_t *a,const wchar_t *b,const wchar_t *p) {return transform(a,b,p,1,NULL);}

int vault_hash_progress(const wchar_t *path,wchar_t hex[65],VaultProgress *progress) {
    vault_progress(progress,0,0,0);
    wchar_t absolute[2048];if(!full_path(path,absolute))return 0;path=absolute;
    BCRYPT_ALG_HANDLE alg=NULL; BCRYPT_HASH_HANDLE hash=NULL; unsigned char digest[32],buffer[65536];
    FILE *file=_wfopen(path,L"rb"); int success=0; size_t n;
    if (!file) return 0;
    uint64_t total=0,done_bytes=0;
    LARGE_INTEGER size;
    if(GetFileSizeEx((HANDLE)_get_osfhandle(_fileno(file)),&size))total=(uint64_t)size.QuadPart;
    if (!OK(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)) ||
        !OK(BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0))) goto done;
    while ((n=fread(buffer,1,sizeof(buffer),file))){
        if (!OK(BCryptHashData(hash,buffer,(ULONG)n,0))) goto done;
        done_bytes+=n;vault_progress(progress,done_bytes,total,0);
    }
    if (ferror(file) || !OK(BCryptFinishHash(hash,digest,32,0))) goto done;
    for (int i=0;i<32;i++) swprintf(hex+2*i,3,L"%02x",digest[i]);
    success=1;
done:
    if (hash) BCryptDestroyHash(hash); if (alg) BCryptCloseAlgorithmProvider(alg,0);
    SecureZeroMemory(buffer,sizeof(buffer)); fclose(file);
    if(success)vault_progress(progress,total,total,1);
    return success;
}
int vault_hash(const wchar_t *path,wchar_t hex[65]) {return vault_hash_progress(path,hex,NULL);}

int vault_crypto_selftest(const wchar_t *dir) {
    wchar_t a[1024],b[1024],c[1024],ha[65],hc[65];
    swprintf(a,1024,L"%ls\\test-input",dir); swprintf(b,1024,L"%ls\\test-encrypted",dir);
    swprintf(c,1024,L"%ls\\test-output",dir);
    FILE *f=_wfopen(a,L"wb"); if (!f) return 0;
    /* More than two chunks, non-aligned tail, and Unicode password. */
    for (unsigned i=0;i<CHUNK*2+127;i++) fputc((int)(i*37u%251),f);
    fclose(f); int ok=vault_encrypt(a,b,L"test-only-\u5bc6\u7801-123456");
    ok=ok && !vault_decrypt(b,c,L"wrong-password") && GetFileAttributesW(c)==INVALID_FILE_ATTRIBUTES;
    ok=ok && vault_decrypt(b,c,L"test-only-\u5bc6\u7801-123456");
    ok=ok && vault_hash(a,ha) && vault_hash(c,hc) && !wcscmp(ha,hc);
    DeleteFileW(c);
    /* Reject truncation, appended bytes, authenticated header changes, and
     * never overwrite an existing output. Rebuild pristine ciphertext each time. */
    ok=ok && !vault_decrypt(b,a,L"test-only-\u5bc6\u7801-123456");
    for(int mode=0;mode<3;mode++) {
        DeleteFileW(b);
        if(!vault_encrypt(a,b,L"test-only-\u5bc6\u7801-123456")){ok=0;break;}
        f=_wfopen(b,L"r+b");
        if(!f){ok=0;break;}
        if(mode==0){_fseeki64(f,0,SEEK_END);__int64 size=_ftelli64(f);if(_chsize_s(_fileno(f),size-1))ok=0;}
        if(mode==1){_fseeki64(f,0,SEEK_END);fputc(42,f);}
        if(mode==2){_fseeki64(f,8,SEEK_SET);int ch=fgetc(f);_fseeki64(f,-1,SEEK_CUR);fputc(ch^1,f);}
        fclose(f);
        ok=ok && !vault_decrypt(b,c,L"test-only-\u5bc6\u7801-123456") && GetFileAttributesW(c)==INVALID_FILE_ATTRIBUTES;
    }
    DeleteFileW(b);ok=ok && vault_encrypt(a,b,L"test-only-\u5bc6\u7801-123456");
    f=_wfopen(b,L"r+b"); if (!f) ok=0; else { _fseeki64(f,HEADER+30,SEEK_SET); int ch=fgetc(f); _fseeki64(f,-1,SEEK_CUR); fputc(ch^1,f); fclose(f); }
    ok=ok && !vault_decrypt(b,c,L"test-only-\u5bc6\u7801-123456") && GetFileAttributesW(c)==INVALID_FILE_ATTRIBUTES;
    DeleteFileW(a); DeleteFileW(b); DeleteFileW(c); return ok;
}
