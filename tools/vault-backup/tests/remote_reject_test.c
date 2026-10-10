/* Optional live SSH transport check. The input is deliberately invalid zstd,
 * so the remote helper must reject it before any deployment operation. */
#include "../src/recover.h"
#include "../src/crypto.h"
#include <assert.h>
#include <stdio.h>
#include <wchar.h>

int wmain(int argc,wchar_t **argv){
    assert(argc==4);
    RecoverJob job={0};wchar_t raw[PATH_CAP],encrypted[PATH_CAP];
    assert(private_directory(argv[1]));
    swprintf(raw,PATH_CAP,L"%ls\\invalid.zst",argv[1]);
    swprintf(encrypted,PATH_CAP,L"%ls\\invalid.vbk",argv[1]);
    FILE *file=_wfopen(raw,L"wb");assert(file);
    assert(fwrite("\x28\xb5\x2f\xfd\x00",1,5,file)==5&&!fclose(file));
    assert(vault_encrypt(raw,encrypted,L"test-only-not-a-real-backup"));
    wcscpy(job.source,encrypted);wcscpy(job.destination,argv[1]);
    wcscpy(job.key,argv[2]);wcscpy(job.known_hosts,argv[3]);
    wcscpy(job.password,L"test-only-not-a-real-backup");job.to_vm=1;
    assert(!recover_run(&job));assert(job.result==RECOVER_REMOTE);
    assert(!job.vm_restored&&!*job.output);
    assert(wcsstr(job.error,L"Command failed: zstd"));
    for(size_t i=0;i<256;i++)assert(job.password[i]==0);
    assert(DeleteFileW(raw)&&DeleteFileW(encrypted)&&RemoveDirectoryW(argv[1]));
    wprintf(L"Live SSH: authenticated input streamed, remote zstd rejection received, plaintext cleaned; no deployment changes requested.\n");
    return 0;
}
