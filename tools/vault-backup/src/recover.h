#ifndef VAULT_RECOVER_H
#define VAULT_RECOVER_H
#include "backup.h"
typedef enum {RECOVER_OK,RECOVER_PATH,RECOVER_DIRECTORY,RECOVER_AUTH,RECOVER_FORMAT,RECOVER_SAVE,RECOVER_CLEANUP,RECOVER_REMOTE} RecoverResult;
typedef struct {
    wchar_t source[PATH_CAP],destination[PATH_CAP],output[PATH_CAP],password[256];
    wchar_t key[PATH_CAP],known_hosts[PATH_CAP],error[1024];
    int to_vm,vm_restored;
    RecoverResult result;
    HWND notify;
} RecoverJob;
int recover_remote(const wchar_t *input,const wchar_t *key,const wchar_t *hosts,const wchar_t *output,const wchar_t *log,wchar_t *error,HWND notify);
int recover_run(RecoverJob *job);
#endif
