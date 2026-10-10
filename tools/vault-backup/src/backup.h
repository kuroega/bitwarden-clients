#ifndef VAULT_BACKUP_H
#define VAULT_BACKUP_H
#include <windows.h>
#include "progress.h"
#define PATH_CAP 1024
typedef struct {
    wchar_t ssh_target[256], host_key_alias[256], vault_url[PATH_CAP];
} ServerConfig;
int server_config_load(ServerConfig *server);
int local_path_load(const wchar_t *name,wchar_t *value,DWORD capacity);
typedef struct {
    wchar_t destination[PATH_CAP], key[PATH_CAP], known_hosts[PATH_CAP], state[PATH_CAP];
    wchar_t password[256];
    wchar_t archive[PATH_CAP], sha256[65], error[1024], id[80], date[64];
    unsigned long long bytes;
    HWND notify;
    int success;
} BackupJob;
#define WM_VAULT_FINISHED (WM_APP+2)
int private_directory(const wchar_t *directory);
int backup_run(BackupJob *job);
int backup_record(const BackupJob *job);
#endif
