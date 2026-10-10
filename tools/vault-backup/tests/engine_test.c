#include "../src/backup.h"
#include "../src/crypto.h"
#include <stdio.h>
#include <wchar.h>
#include <io.h>
#include <fcntl.h>

static int failed_job(const wchar_t *state,const wchar_t *destination){
    BackupJob job={0};wcscpy(job.state,state);wcscpy(job.destination,destination);wcscpy(job.password,L"test-only-password");
    if(backup_run(&job)||job.success||!job.error[0])return 0;
    for(size_t i=0;i<256;i++)if(job.password[i])return 0;
    wchar_t ini[PATH_CAP],status[32];swprintf(ini,PATH_CAP,L"%ls\\history.ini",state);
    GetPrivateProfileStringW(job.id,L"status",L"",status,32,ini);
    return !wcscmp(status,L"Failed");
}
int wmain(int argc,wchar_t **argv){
    _setmode(_fileno(stdout),_O_U8TEXT);
    if(argc!=2||wcslen(argv[1])>200||!private_directory(argv[1]))return 1;
    wchar_t ini[PATH_CAP],file[PATH_CAP];swprintf(ini,PATH_CAP,L"%ls\\history.ini",argv[1]);
    HANDLE h=CreateFileW(ini,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);if(h==INVALID_HANDLE_VALUE)return 1;
    WORD bom=0xfeff;DWORD n;WriteFile(h,&bom,2,&n,NULL);CloseHandle(h);
    swprintf(file,PATH_CAP,L"%ls\\destination-file",argv[1]);h=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);if(h==INVALID_HANDLE_VALUE)return 1;CloseHandle(h);
    int ok=failed_job(argv[1],L"\\\\server\\share")&&failed_job(argv[1],file);
    DeleteFileW(file);DeleteFileW(ini);
    wprintf(L"Failure paths, password clearing and English history records: %ls\n",ok?L"Passed":L"Failed");return !ok;
}
