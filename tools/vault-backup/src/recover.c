#include "recover.h"
#include "crypto.h"
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

int recover_run(RecoverJob *job){
    wchar_t folder[PATH_CAP]=L"",partial[PATH_CAP]=L"",suffix[33];
    unsigned char random[16];int created=0,ok=0;
    job->vm_restored=0;job->error[0]=0;job->output[0]=0;job->result=RECOVER_PATH;
    size_t length=wcslen(job->destination);
    if(length<3||length>220||job->destination[1]!=L':'||job->destination[2]!=L'\\')goto done;
    wchar_t absolute[PATH_CAP];DWORD count=GetFullPathNameW(job->destination,PATH_CAP,absolute,NULL);
    if(!count||count>220||count>=PATH_CAP)goto done;
    wcscpy(job->destination,absolute);length=wcslen(job->destination);
    wchar_t root[4]={job->destination[0],L':',L'\\',0};UINT type=GetDriveTypeW(root);
    if(type!=DRIVE_FIXED&&type!=DRIVE_REMOVABLE)goto done;
    DWORD attrs=GetFileAttributesW(job->destination);
    if(attrs==INVALID_FILE_ATTRIBUTES||!(attrs&FILE_ATTRIBUTE_DIRECTORY)||(attrs&FILE_ATTRIBUTE_REPARSE_POINT))goto done;
    /* A parent junction must not redirect plaintext onto another drive/share. */
    for(size_t i=3;i<length;i++)if(absolute[i]==L'\\'){
        absolute[i]=0;attrs=GetFileAttributesW(absolute);absolute[i]=L'\\';
        if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_REPARSE_POINT))goto done;
    }
    job->result=RECOVER_DIRECTORY;
    if(BCryptGenRandom(NULL,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)goto done;
    for(int i=0;i<16;i++)swprintf(suffix+i*2,3,L"%02x",random[i]);
    /* Create a fresh child only; never change the selected parent's ACL. */
    swprintf(folder,PATH_CAP,L"%ls%lsRecovered-%ls",job->destination,job->destination[length-1]==L'\\'?L"":L"\\",suffix);
    /* Keep paths within the existing Windows CRT path limit. */
    if(wcslen(folder)+wcslen(L"\\vault.tar.zst.partial")>=260){job->result=RECOVER_PATH;goto done;}
    if(!CreateDirectoryW(folder,NULL))goto done;created=1;
    if(!private_directory(folder))goto done;
    swprintf(partial,PATH_CAP,L"%ls\\vault.tar.zst.partial",folder);
    swprintf(job->output,PATH_CAP,L"%ls\\vault.tar.zst",folder);
    job->result=RECOVER_AUTH;
    VaultProgress decrypt_progress={job->notify,0,job->to_vm?40:95,-1};
    if(!vault_decrypt_progress(job->source,partial,job->password,&decrypt_progress))goto done;
    SecureZeroMemory(job->password,sizeof(job->password));
    job->result=RECOVER_FORMAT;
    FILE *file=_wfopen(partial,L"rb");unsigned char magic[4];
    if(!file)goto done;
    int valid=fread(magic,1,4,file)==4&&!memcmp(magic,"\x28\xb5\x2f\xfd",4);fclose(file);
    if(!valid)goto done;
    job->result=RECOVER_SAVE;
    if(!MoveFileExW(partial,job->output,MOVEFILE_WRITE_THROUGH))goto done;
    if(job->to_vm){
        wchar_t response[PATH_CAP],log[PATH_CAP];
        swprintf(response,PATH_CAP,L"%ls\\remote-result.txt",folder);swprintf(log,PATH_CAP,L"%ls\\remote-error.txt",folder);
        job->result=RECOVER_REMOTE;
        if(job->notify)PostMessageW(job->notify,WM_VAULT_PERCENT,45,0);
        int restored=recover_remote(job->output,job->key,job->known_hosts,response,log,job->error,job->notify);
        DeleteFileW(response);DeleteFileW(log);
        if(!restored){DeleteFileW(job->output);goto done;}
        job->vm_restored=1;
        if(!DeleteFileW(job->output)||!RemoveDirectoryW(folder)){job->result=RECOVER_CLEANUP;wcscpy(job->output,folder);goto done;}
        created=0;job->output[0]=0;
    }
    job->result=RECOVER_OK;ok=1;
    if(job->notify)PostMessageW(job->notify,WM_VAULT_PERCENT,100,0);
done:
    SecureZeroMemory(job->password,sizeof(job->password));
    if(!ok){
        int cleaned=1;
        if(*partial&&!DeleteFileW(partial)&&GetLastError()!=ERROR_FILE_NOT_FOUND)cleaned=0;
        if(created&&!RemoveDirectoryW(folder))cleaned=0;
        if(cleaned)job->output[0]=0;
        else {job->result=RECOVER_CLEANUP;wcscpy(job->output,folder);}
    }
    return ok;
}
