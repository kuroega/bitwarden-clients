/* Exercise configuration without reading or changing the owner's local file. */
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <wchar.h>
static wchar_t module_path[1024];
static DWORD WINAPI test_module_path(HMODULE module,LPWSTR path,DWORD capacity){
    (void)module;assert(wcslen(module_path)<capacity);wcscpy(path,module_path);return (DWORD)wcslen(path);
}
#define GetModuleFileNameW test_module_path
#include "../src/backup.c"
#undef GetModuleFileNameW
static void write_config(const wchar_t *path,const char *text){
    FILE *file=_wfopen(path,L"wb");assert(file);assert(fputs(text,file)>=0);assert(!fclose(file));
    WritePrivateProfileStringW(NULL,NULL,NULL,path);
}
int wmain(void){
    wchar_t temp[PATH_CAP],folder[PATH_CAP],path[PATH_CAP],value[PATH_CAP];ServerConfig server;
    assert(GetTempPathW(PATH_CAP,temp));
    swprintf(folder,PATH_CAP,L"%lsvault-config-test-%lu-%llu",temp,GetCurrentProcessId(),(unsigned long long)GetTickCount64());
    assert(CreateDirectoryW(folder,NULL));
    swprintf(module_path,PATH_CAP,L"%ls\\bin\\config-test.exe",folder);
    swprintf(path,PATH_CAP,L"%ls\\server.local.ini",folder);
    assert(!server_config_load(&server));
    assert(!local_path_load(L"ssh_key",value,PATH_CAP)&&!*value);
    write_config(path,"[server]\nssh_target=admin@vault.example.invalid\nhost_key_alias=vault.example.invalid\nvault_url=https://vault.example.invalid\n[paths]\nssh_key=C:\\Example User\\keys\\test-key\nknown_hosts=D:\\Example VM\\known_hosts\n");
    assert(server_config_load(&server));
    assert(!wcscmp(server.ssh_target,L"admin@vault.example.invalid"));
    assert(!wcscmp(server.vault_url,L"https://vault.example.invalid"));
    assert(local_path_load(L"ssh_key",value,PATH_CAP)&&!wcscmp(value,L"C:\\Example User\\keys\\test-key"));
    assert(local_path_load(L"known_hosts",value,PATH_CAP)&&!wcscmp(value,L"D:\\Example VM\\known_hosts"));
    assert(!local_path_load(L"missing",value,PATH_CAP)&&!*value);
    assert(!local_path_load(L"ssh_key",value,8)&&!*value);
    write_config(path,"[server]\nssh_target=-oProxyCommand=bad\nhost_key_alias=host\nvault_url=https://vault.example.invalid\n");
    assert(!server_config_load(&server));
    write_config(path,"[server]\nssh_target=admin@host\nhost_key_alias=host\nvault_url=http://vault.example.invalid\n");
    assert(!server_config_load(&server));
    write_config(path,"[server]\nssh_target=admin@host\nhost_key_alias=host\nvault_url=https://user@vault.example.invalid\n");
    assert(!server_config_load(&server));
    write_config(path,"[server]\nssh_target=admin@host\nhost_key_alias=host\n");
    assert(!server_config_load(&server));
    assert(DeleteFileW(path));assert(RemoveDirectoryW(folder));
    puts("Server configuration: missing values, runtime values, option injection and invalid URLs checked.");return 0;
}
