#include "backup.h"
#include "crypto.h"
#include <bcrypt.h>
#include <sddl.h>
#include <aclapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static int local_config_file(wchar_t *path) {
    DWORD length=GetModuleFileNameW(NULL,path,PATH_CAP);
    if(!length||length>=PATH_CAP)return 0;
    wchar_t *slash=wcsrchr(path,L'\\');if(!slash)return 0;*slash=0;
    slash=wcsrchr(path,L'\\');if(!slash)return 0;*slash=0;
    if(wcslen(path)+18>=PATH_CAP)return 0;
    wcscat(path,L"\\server.local.ini");
    return 1;
}

int local_path_load(const wchar_t *name,wchar_t *value,DWORD capacity) {
    wchar_t path[PATH_CAP];
    if(!capacity)return 0;*value=0;
    if(!local_config_file(path))return 0;
    DWORD length=GetPrivateProfileStringW(L"paths",name,L"",value,capacity,path);
    if(!length||length>=capacity-1){*value=0;return 0;}
    return 1;
}

int server_config_load(ServerConfig *server) {
    wchar_t path[PATH_CAP];
    ZeroMemory(server,sizeof(*server));
    if(!local_config_file(path))return 0;
    GetPrivateProfileStringW(L"server",L"ssh_target",L"",server->ssh_target,256,path);
    GetPrivateProfileStringW(L"server",L"host_key_alias",L"",server->host_key_alias,256,path);
    GetPrivateProfileStringW(L"server",L"vault_url",L"",server->vault_url,PATH_CAP,path);
    if(!*server->ssh_target||!*server->host_key_alias||wcslen(server->ssh_target)>=255||wcslen(server->host_key_alias)>=255||
       wcsncmp(server->vault_url,L"https://",8)||!server->vault_url[8]||wcslen(server->vault_url)>=PATH_CAP-1)return 0;
    const wchar_t *values[]={server->ssh_target,server->host_key_alias};
    for(size_t i=0;i<2;i++){
        if(values[i][0]==L'-')return 0;
        for(const wchar_t *p=values[i];*p;p++)if(!((*p>=L'a'&&*p<=L'z')||(*p>=L'A'&&*p<=L'Z')||(*p>=L'0'&&*p<=L'9')||wcschr(L"@._-:[]",*p)))return 0;
    }
    for(const wchar_t *p=server->vault_url+8;*p;p++)if(*p<=32||wcschr(L"/@?#\\",*p))return 0;
    return 1;
}

int private_directory(const wchar_t *path) {
    HANDLE token=NULL; DWORD required=0; TOKEN_USER *user=NULL;
    LPWSTR sid=NULL; PSECURITY_DESCRIPTOR descriptor=NULL;
    wchar_t sddl[512]; int ok=0;
    if (!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) goto done;
    GetTokenInformation(token,TokenUser,NULL,0,&required);
    user=malloc(required); if (!user || !GetTokenInformation(token,TokenUser,user,required,&required)) goto done;
    if (!ConvertSidToStringSidW(user->User.Sid,&sid)) goto done;
    swprintf(sddl,512,L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;%ls)",sid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&descriptor,NULL)) goto done;
    int result=SHCreateDirectoryExW(NULL,path,NULL);
    if (result!=ERROR_SUCCESS && result!=ERROR_ALREADY_EXISTS && result!=ERROR_FILE_EXISTS) goto done;
    DWORD attrs=GetFileAttributesW(path);
    if (attrs==INVALID_FILE_ATTRIBUTES || !(attrs&FILE_ATTRIBUTE_DIRECTORY) || (attrs&FILE_ATTRIBUTE_REPARSE_POINT)) goto done;
    BOOL present,def; PACL acl=NULL;
    if (!GetSecurityDescriptorDacl(descriptor,&present,&acl,&def)) goto done;
    ok=SetNamedSecurityInfoW((LPWSTR)path,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,NULL,NULL,acl,NULL)==ERROR_SUCCESS;
done:
    if (descriptor) LocalFree(descriptor); if (sid) LocalFree(sid);
    free(user); if (token) CloseHandle(token); return ok;
}

static void progress(BackupJob *j,int step) {if(j->notify) PostMessageW(j->notify,WM_VAULT_PROGRESS,(WPARAM)step,0);}
/* Windows CreateProcess argv quoting, including trailing backslashes/quotes. */
static int argument(wchar_t *command,size_t cap,const wchar_t *arg) {
    size_t used=wcslen(command);
    if (used+3>=cap) return 0;
    if (used) command[used++]=L' ';
    command[used++]=L'"';
    while (*arg) {
        size_t slashes=0;
        while (*arg==L'\\') {slashes++;arg++;}
        size_t emit=(*arg==L'"'||!*arg)?slashes*2:slashes;
        if (*arg==L'"') emit++;
        if (used+emit+3>=cap) return 0;
        while (emit--) command[used++]=L'\\';
        if (*arg) command[used++]=*arg++;
    }
    command[used++]=L'"'; command[used]=0; return 1;
}

static int ssh_archive(BackupJob *j,const wchar_t *output,const wchar_t *log,const wchar_t *input) {
    ServerConfig server;
    if(!server_config_load(&server)){
        wcscpy(j->error,L"Missing or invalid server.local.ini next to the bin folder. Configure ssh_target, host_key_alias and vault_url.");return 0;
    }
    wchar_t alias_option[320];swprintf(alias_option,320,L"HostKeyAlias=%ls",server.host_key_alias);
    wchar_t system[PATH_CAP],ssh[PATH_CAP],command[8192]=L"";
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};
    HANDLE out=INVALID_HANDLE_VALUE,err=INVALID_HANDLE_VALUE,in=INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi={0}; STARTUPINFOW si={0}; int ok=0;
    if (!GetSystemDirectoryW(system,PATH_CAP)) return 0;
    swprintf(ssh,PATH_CAP,L"%ls\\OpenSSH\\ssh.exe",system);
    const wchar_t *args[]={ssh,L"-T",L"-i",j->key,L"-o",L"BatchMode=yes",L"-o",L"ConnectTimeout=15",
        L"-o",L"StrictHostKeyChecking=yes",L"-o",L"IdentitiesOnly=yes",L"-o",alias_option,
        L"-o",L"ServerAliveInterval=15",L"-o",L"ServerAliveCountMax=4",
        L"-o",L"UserKnownHostsFile",j->known_hosts,
        server.ssh_target,input?L"sudo -n /usr/local/sbin/vault-recover":L"sudo -n /usr/local/sbin/vault-backup"};
    /* UserKnownHostsFile is one SSH option argument, not two. */
    wchar_t hosts_option[PATH_CAP+64];
    swprintf(hosts_option,PATH_CAP+64,L"UserKnownHostsFile=%ls",j->known_hosts);
    for (size_t i=0;i<sizeof(args)/sizeof(args[0]);i++) {
        if (!wcscmp(args[i],L"UserKnownHostsFile")) {if(!argument(command,8192,hosts_option)) goto done; i++;}
        else if (!argument(command,8192,args[i])) goto done;
    }
    out=CreateFileW(output,GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,NULL);
    err=CreateFileW(log,GENERIC_WRITE,0,&sa,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,NULL);
    in=CreateFileW(input?input:L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,NULL);
    if(out==INVALID_HANDLE_VALUE || err==INVALID_HANDLE_VALUE || in==INVALID_HANDLE_VALUE) goto done;
    si.cb=sizeof(si); si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
    si.hStdOutput=out; si.hStdError=err; si.hStdInput=in;
    if(!CreateProcessW(ssh,command,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)) goto done;
    /* Do not terminate SSH while the remote helper is resuming the container.
     * SSH keepalive + server operation timeouts bound network/processing waits. */
    int last_report=45;
    while(WaitForSingleObject(pi.hProcess,200)==WAIT_TIMEOUT){
        if(input&&j->notify){
            FILE *file=_wfopen(output,L"rb");char line[256];
            if(file){while(fgets(line,sizeof(line),file)){int percent=0;if(sscanf(line,"RECOVER_PROGRESS %d",&percent)==1&&percent>last_report&&percent<100){last_report=percent;PostMessageW(j->notify,WM_VAULT_PERCENT,(WPARAM)percent,0);}}fclose(file);}
        }
    }
    DWORD exit_code=1; if(GetExitCodeProcess(pi.hProcess,&exit_code)) ok=exit_code==0;
done:
    if(pi.hThread) CloseHandle(pi.hThread); if(pi.hProcess) CloseHandle(pi.hProcess);
    if(out!=INVALID_HANDLE_VALUE) CloseHandle(out);
    if(err!=INVALID_HANDLE_VALUE) CloseHandle(err);
    if(in!=INVALID_HANDLE_VALUE) CloseHandle(in);
    if(!ok) {
        FILE *file=_wfopen(log,L"rb"); char text[2048]={0};
        if(file){fread(text,1,sizeof(text)-1,file);fclose(file);}
        MultiByteToWideChar(CP_UTF8,0,text,-1,j->error,1024);
        if(!*j->error){if(input)wcscpy(j->error,L"SSH recovery failed. Check Tailscale, the SSH key, known_hosts and the VM recovery helper.");else swprintf(j->error,1024,L"SSH export failed (Windows error %lu). Check Tailscale, the SSH key and the VM backup helper.",GetLastError());}
    }
    return ok;
}

static int export_archive(BackupJob *j,const wchar_t *output,const wchar_t *log){return ssh_archive(j,output,log,NULL);}
int recover_remote(const wchar_t *input,const wchar_t *key,const wchar_t *hosts,const wchar_t *output,const wchar_t *log,wchar_t *error,HWND notify){
    BackupJob transport={0};transport.notify=notify;wcscpy(transport.key,key);wcscpy(transport.known_hosts,hosts);
    int ok=ssh_archive(&transport,output,log,input);
    if(ok){
        FILE *file=_wfopen(output,L"rb");char value[1024]={0};
        if(file){fread(value,1,sizeof(value)-1,file);fclose(file);}
        ok=strstr(value,"RECOVER_OK /opt/bitwarden-before-recover-")!=NULL;
        if(ok&&strstr(value,"RECOVER_WARNING"))wcscpy(transport.error,L"Previous-container cleanup was incomplete. Check the recovery journal and handle the retained old container before Compose maintenance.");
        if(!ok)wcscpy(transport.error,L"The remote helper did not return a recovery success receipt. Check recovery status on the VM.");
    }
    wcscpy(error,transport.error);return ok;
}

static int copy_new(const wchar_t *source,const wchar_t *target,int *created,VaultProgress *progress) {
    vault_progress(progress,0,0,0);
    HANDLE in=CreateFileW(source,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(in==INVALID_HANDLE_VALUE)return 0;
    HANDLE out=CreateFileW(target,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(out==INVALID_HANDLE_VALUE){CloseHandle(in);return 0;}
    *created=1;unsigned char buffer[65536];DWORD n=0,written;int ok=1;
    LARGE_INTEGER size;uint64_t total=0,done_bytes=0;
    if(GetFileSizeEx(in,&size))total=(uint64_t)size.QuadPart;
    while(1){
        if(!ReadFile(in,buffer,sizeof(buffer),&n,NULL)){ok=0;break;}
        if(!n)break;
        if(!WriteFile(out,buffer,n,&written,NULL)||written!=n){ok=0;break;}
        done_bytes+=written;vault_progress(progress,done_bytes,total,0);
    }
    if(ok)ok=FlushFileBuffers(out);
    CloseHandle(out);CloseHandle(in);if(ok)vault_progress(progress,total,total,1);return ok;
}

int backup_record(const BackupJob *j) { int ok=1;
    wchar_t ini[PATH_CAP],size[32]; swprintf(ini,PATH_CAP,L"%ls\\history.ini",j->state);
    swprintf(size,32,L"%llu",j->bytes);
    ok &= WritePrivateProfileStringW(j->id,L"date",j->date,ini);
    ok &= WritePrivateProfileStringW(j->id,L"status",j->success?L"Success":L"Failed",ini);
    ok &= WritePrivateProfileStringW(j->id,L"path",j->archive,ini);
    ok &= WritePrivateProfileStringW(j->id,L"bytes",size,ini);
    ok &= WritePrivateProfileStringW(j->id,L"sha256",j->sha256,ini);
    /* Keep history to one line; secrets are never present in helper errors. */
    wchar_t detail[1024]; wcsncpy(detail,j->error,1023); detail[1023]=0;
    for(wchar_t *p=detail;*p;p++) if(*p==L'\n'||*p==L'\r') *p=L' ';
    ok &= WritePrivateProfileStringW(j->id,L"detail",detail,ini); return ok;
}

int backup_run(BackupJob *j) {
    wchar_t stage[PATH_CAP],raw[PATH_CAP],encrypted[PATH_CAP],verify[PATH_CAP],log[PATH_CAP],partial[PATH_CAP]=L"",final_path[PATH_CAP],hash1[65],hash2[65];
    int partial_created=0;
    SYSTEMTIME t; unsigned char random[8];
    GetSystemTime(&t);
    j->success=0; j->bytes=0; j->error[0]=j->sha256[0]=0;
    swprintf(j->date,64,L"%04u-%02u-%02u %02u:%02u:%02u UTC",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    if(BCryptGenRandom(NULL,random,8,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) {
        swprintf(j->id,80,L"error-%lu-%llu",GetCurrentProcessId(),(unsigned long long)GetTickCount64());
        wcscpy(j->error,L"Unable to generate secure random data.");goto record;
    }
    swprintf(j->id,80,L"vault-%04u%02u%02u-%02u%02u%02u-%02x%02x%02x%02x%02x%02x%02x%02x",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,
        random[0],random[1],random[2],random[3],random[4],random[5],random[6],random[7]);
    swprintf(j->archive,PATH_CAP,L"%ls\\%ls.tar.zst.vbk",j->destination,j->id);
    wchar_t drive[4]={j->destination[0],L':',L'\\',0};
    UINT drive_type=GetDriveTypeW(drive);
    if(wcslen(j->destination)<3 || wcslen(j->destination)>220 || j->destination[1]!=L':' || j->destination[2]!=L'\\' || (drive_type!=DRIVE_FIXED && drive_type!=DRIVE_REMOVABLE)) {wcscpy(j->error,L"Choose a local drive folder with a path of at most 220 characters.");goto record;}
    SHCreateDirectoryExW(NULL,j->destination,NULL);
    DWORD destination_attrs=GetFileAttributesW(j->destination);
    if(destination_attrs==INVALID_FILE_ATTRIBUTES || !(destination_attrs&FILE_ATTRIBUTE_DIRECTORY)){wcscpy(j->error,L"Unable to create the backup folder, or the destination is not a folder.");goto record;}
    swprintf(stage,PATH_CAP,L"%ls\\staging",j->state);
    if(!private_directory(stage)){wcscpy(j->error,L"Unable to create a staging folder restricted to the current user and SYSTEM.");goto record;}
    swprintf(raw,PATH_CAP,L"%ls\\%ls.zst.part",stage,j->id);
    swprintf(encrypted,PATH_CAP,L"%ls\\%ls.vbk.part",stage,j->id);
    swprintf(verify,PATH_CAP,L"%ls\\%ls.verify.part",stage,j->id);
    swprintf(log,PATH_CAP,L"%ls\\%ls.log",stage,j->id);
    progress(j,1);
    if(!export_archive(j,raw,log)) goto cleanup;
    FILE *file=_wfopen(raw,L"rb"); unsigned char magic[4];
    int valid=file && fread(magic,1,4,file)==4 && magic[0]==0x28 && magic[1]==0xb5 && magic[2]==0x2f && magic[3]==0xfd;
    if(file) fclose(file);
    if(!valid){wcscpy(j->error,L"The exported file is not a valid zstd stream.");goto cleanup;}
    progress(j,2);
    VaultProgress encrypt_progress={j->notify,30,50,-1},decrypt_progress={j->notify,50,65,-1};
    VaultProgress raw_progress={j->notify,65,70,-1},verify_progress={j->notify,70,75,-1};
    VaultProgress hash_progress={j->notify,75,80,-1},copy_progress={j->notify,80,90,-1},destination_progress={j->notify,90,98,-1};
    if(!vault_encrypt_progress(raw,encrypted,j->password,&encrypt_progress)){wcscpy(j->error,L"Encryption failed. Check disk space and write permissions.");goto cleanup;}
    progress(j,3);
    if(!vault_decrypt_progress(encrypted,verify,j->password,&decrypt_progress) || !vault_hash_progress(raw,hash1,&raw_progress) || !vault_hash_progress(verify,hash2,&verify_progress) || wcscmp(hash1,hash2)){
        wcscpy(j->error,L"Verification after encryption failed; no successful backup was saved.");goto cleanup;
    }
    if(!vault_hash_progress(encrypted,j->sha256,&hash_progress)){wcscpy(j->error,L"Unable to calculate the backup checksum.");goto cleanup;}
    progress(j,4);
    swprintf(final_path,PATH_CAP,L"\\\\?\\%ls",j->archive);
    swprintf(partial,PATH_CAP,L"%ls.partial",final_path);
    if(!copy_new(encrypted,partial,&partial_created,&copy_progress)){wcscpy(j->error,L"Unable to save to the backup folder. Check space and permissions.");goto cleanup;}
    /* Verify the final destination, including cross-volume copies. */
    if(!vault_hash_progress(partial,hash2,&destination_progress)||wcscmp(j->sha256,hash2)){
        wcscpy(j->error,L"Destination disk verification failed.");goto cleanup;
    }
    HANDLE final=CreateFileW(partial,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(final==INVALID_HANDLE_VALUE){wcscpy(j->error,L"Unable to flush the destination file.");goto cleanup;}
    BOOL flushed=FlushFileBuffers(final);CloseHandle(final);
    if(!flushed || !MoveFileExW(partial,final_path,MOVEFILE_WRITE_THROUGH)){wcscpy(j->error,L"Unable to save the backup atomically, or the file already exists.");goto cleanup;}
    partial_created=0;
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if(GetFileAttributesExW(final_path,GetFileExInfoStandard,&attrs)) j->bytes=((uint64_t)attrs.nFileSizeHigh<<32)|attrs.nFileSizeLow;
    j->success=1;
cleanup:
    if(partial_created) DeleteFileW(partial);
    DeleteFileW(raw); DeleteFileW(encrypted); DeleteFileW(verify); DeleteFileW(log);
record:
    SecureZeroMemory(j->password,sizeof(j->password));
    if(!backup_record(j)){size_t used=wcslen(j->error);swprintf(j->error+used,1024-used,L"%lsUnable to write history. Check the state folder's permissions and free space.",used?L" ":L"");}
    if(j->success&&j->notify)PostMessageW(j->notify,WM_VAULT_PERCENT,100,0);
    return j->success;
}
