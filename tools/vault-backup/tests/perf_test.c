/* Synthetic local crypto performance test; no VM access or UI automation. */
#include "../src/backup.h"
#include "../src/crypto.h"
#include <bcrypt.h>
#include <psapi.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static double clock_seconds(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart/f.QuadPart;}
static double cpu_seconds(void){FILETIME c,e,k,u;ULARGE_INTEGER a,b;GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);a.LowPart=k.dwLowDateTime;a.HighPart=k.dwHighDateTime;b.LowPart=u.dwLowDateTime;b.HighPart=u.dwHighDateTime;return (a.QuadPart+b.QuadPart)/10000000.0;}
static PROCESS_MEMORY_COUNTERS_EX memory(void){PROCESS_MEMORY_COUNTERS_EX m={0};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&m,sizeof(m));return m;}
static DWORD handles(void){DWORD n=0;GetProcessHandleCount(GetCurrentProcess(),&n);return n;}
int wmain(int argc,wchar_t **argv){
    if(argc!=2||wcslen(argv[1])>200||!private_directory(argv[1]))return 1;
    wchar_t input[1024],encrypted[1024],output[1024],password[65]={0},h1[65],h2[65];unsigned char random[32];
    swprintf(input,1024,L"%ls\\synthetic-input.bin",argv[1]);swprintf(encrypted,1024,L"%ls\\synthetic-encrypted.vbk",argv[1]);swprintf(output,1024,L"%ls\\synthetic-output.bin",argv[1]);
    if(BCryptGenRandom(NULL,random,32,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return 1;
    for(int i=0;i<32;i++)swprintf(password+i*2,3,L"%02x",random[i]);SecureZeroMemory(random,sizeof(random));
    unsigned char *buffer=malloc(1024*1024);if(!buffer)return 1;for(unsigned i=0;i<1024*1024;i++)buffer[i]=(unsigned char)(i*37u%251);
    int ok=1;
    for(int cycle=0;cycle<9;cycle++){
        unsigned mib=cycle==8?256:16;
        HANDLE file=CreateFileW(input,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);if(file==INVALID_HANDLE_VALUE){ok=0;break;}
        for(unsigned i=0;i<mib;i++){DWORD n;if(!WriteFile(file,buffer,1024*1024,&n,NULL)||n!=1024*1024){ok=0;break;}}CloseHandle(file);if(!ok)break;
        double wall=clock_seconds(),cpu=cpu_seconds();
        ok=vault_encrypt(input,encrypted,password);double encrypt_time=clock_seconds()-wall,encrypt_cpu=cpu_seconds()-cpu;
        wall=clock_seconds();cpu=cpu_seconds();
        if(ok)ok=vault_decrypt(encrypted,output,password);
        double decrypt_time=clock_seconds()-wall,decrypt_cpu=cpu_seconds()-cpu;
        wall=clock_seconds();cpu=cpu_seconds();
        if(ok)ok=vault_hash(input,h1)&&vault_hash(output,h2)&&!wcscmp(h1,h2);
        double hash_time=clock_seconds()-wall,hash_cpu=cpu_seconds()-cpu;
        PROCESS_MEMORY_COUNTERS_EX m=memory();
        printf("{\"cycle\":%d,\"input_mib\":%u,\"ok\":%s,\"encrypt_ms\":%.3f,\"encrypt_cpu_ms\":%.3f,\"encrypt_mib_per_sec\":%.3f,\"decrypt_ms\":%.3f,\"decrypt_cpu_ms\":%.3f,\"hash_two_files_ms\":%.3f,\"hash_cpu_ms\":%.3f,\"working_set_mib\":%.3f,\"private_commit_mib\":%.3f,\"peak_working_set_mib\":%.3f,\"peak_commit_mib\":%.3f,\"handles\":%lu}\n",cycle+1,mib,ok?"true":"false",encrypt_time*1000,encrypt_cpu*1000,mib/encrypt_time,decrypt_time*1000,decrypt_cpu*1000,hash_time*1000,hash_cpu*1000,m.WorkingSetSize/1048576.0,m.PrivateUsage/1048576.0,m.PeakWorkingSetSize/1048576.0,m.PeakPagefileUsage/1048576.0,(unsigned long)handles());
        DeleteFileW(input);DeleteFileW(encrypted);DeleteFileW(output);if(!ok)break;
    }
    SecureZeroMemory(password,sizeof(password));SecureZeroMemory(buffer,1024*1024);free(buffer);
    DeleteFileW(input);DeleteFileW(encrypted);DeleteFileW(output);return !ok;
}
