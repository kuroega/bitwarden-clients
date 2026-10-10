/* Real GUI handlers and crypto, with informational dialogs intercepted. */
#include <windows.h>
#include <assert.h>
#include <wchar.h>
#include <aclapi.h>
#include <sddl.h>
#include "../src/backup.h"
static int test_server_config(ServerConfig *server){
    ZeroMemory(server,sizeof(*server));
    wcscpy(server->ssh_target,L"vaultadmin@vault.example.invalid");
    wcscpy(server->host_key_alias,L"vault.example.invalid");
    wcscpy(server->vault_url,L"https://vault.example.invalid");return 1;
}
static int messages;
static wchar_t last_message[2048];
static int fail_thread;
static int confirm_vm,remote_ok=1,remote_calls;
static HANDLE WINAPI test_thread(LPSECURITY_ATTRIBUTES attributes,SIZE_T stack,LPTHREAD_START_ROUTINE start,LPVOID argument,DWORD flags,LPDWORD id){
    if(fail_thread){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return NULL;}
    return CreateThread(attributes,stack,start,argument,flags,id);
}
static int WINAPI test_message(HWND window,LPCWSTR text,LPCWSTR title,UINT flags){
    (void)window;(void)title;messages++;wcsncpy(last_message,text,2047);return (flags&MB_YESNO)?(confirm_vm?IDYES:IDNO):IDOK;
}
#define MessageBoxW test_message
#define CreateThread test_thread
#define wWinMain unused_gui_entry
#define server_config_load test_server_config
#include "../src/main.c"
#undef server_config_load
#undef wWinMain
#undef MessageBoxW
#undef CreateThread

static int test_remote(const wchar_t *input,const wchar_t *key,const wchar_t *hosts,const wchar_t *output,const wchar_t *log,wchar_t *error,HWND notify){
    (void)key;(void)hosts;(void)output;(void)log;(void)notify;
    remote_calls++;
    for(size_t i=0;i<256;i++)assert(recovery.password[i]==0);
    assert(GetFileAttributesW(input)!=INVALID_FILE_ATTRIBUTES);
    if(!remote_ok)wcscpy(error,L"fixture remote failure");return remote_ok;
}
#define recover_remote test_remote
#include "../src/recover.c"
#undef recover_remote

static int progress_visible(void){return (GetWindowLongW(progress_bar,GWL_STYLE)&WS_VISIBLE)!=0;}
static void wait_for_job(HWND window){
    DWORD deadline=GetTickCount()+30000;
    int intermediate=0;
    while(busy){MSG msg;while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){
        if(msg.hwnd==window&&msg.message==WM_VAULT_PERCENT){
            assert((int)msg.wParam>=progress_percent&&msg.wParam<=100);
            if(msg.wParam>0&&msg.wParam<95)intermediate++;
        }
        TranslateMessage(&msg);DispatchMessageW(&msg);}
        assert((LONG)(deadline-GetTickCount())>0);if(busy)MsgWaitForMultipleObjects(0,NULL,FALSE,20,QS_ALLINPUT);}
    assert(!worker&&IsWindowEnabled(mode_combo));
    int success=recovering?recovery.result==RECOVER_OK:job.success;
    assert(progress_visible()==success);
    assert(success?progress_percent==100:progress_percent<100);
    if(success&&recovering)assert(intermediate>0);
    if(success){wchar_t status[1024];GetWindowTextW(status_label,status,1024);assert(wcsstr(status,L"100%"));}
    (void)window;
}
static void test_sizes(void){
    const struct {uint64_t bytes;const wchar_t *text;} cases[]={
        {0,L"0 B"},{1023,L"1023 B"},{1024,L"1.00 KB"},{94635,L"92.42 KB"},
        {1048576,L"1.00 MB"},{1073741824,L"1.00 GB"},{1099511627776ULL,L"1.00 TB"},
        {1125899906842624ULL,L"1.00 PB"},{1152921504606846976ULL,L"1.00 EB"},
        {1048575,L"1.00 MB"},{UINT64_MAX,L"16.00 EB"}
    };
    wchar_t value[128];
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
        format_size(cases[i].bytes,value,128);assert(!wcscmp(value,cases[i].text));
    }
    assert(WritePrivateProfileStringW(L"size-test",L"bytes",L"94635",history));load_history();
    ListView_GetItemText(list,0,2,value,128);assert(!wcscmp(value,L"92.42 KB"));
    RECT rect;assert(ListView_GetSubItemRect(list,0,2,LVIR_BOUNDS,&rect));
    for(int pass=0;pass<2;pass++){
        apply_language(GetParent(list));load_history();
        SendMessageW(list,WM_MOUSEMOVE,0,MAKELPARAM(rect.left+2,(rect.top+rect.bottom)/2));
        assert(size_hover_row==0&&!wcscmp(size_tip,L"94635 bytes"));
        TOOLINFOW tool={0};tool.cbSize=sizeof(tool);tool.hwnd=list;tool.uId=1;
        assert(SendMessageW(size_tooltip,TTM_GETTOOLINFOW,0,(LPARAM)&tool));
        assert(tool.rect.left==rect.left&&tool.rect.right==rect.right);
        SendMessageW(list,WM_MOUSEMOVE,0,MAKELPARAM(5,(rect.top+rect.bottom)/2));assert(size_hover_row==-1&&!*size_tip);
    }
    assert(WritePrivateProfileStringW(L"size-test",NULL,NULL,history));load_history();
    assert(!history_sizes&&ListView_GetItemCount(list)==0);
}

static void assert_wiped(void){for(size_t i=0;i<256;i++)assert(recovery.password[i]==0);}
static void assert_private(const wchar_t *path){
    PACL acl=NULL;PSECURITY_DESCRIPTOR descriptor=NULL;
    assert(GetNamedSecurityInfoW((LPWSTR)path,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,NULL,NULL,&acl,NULL,&descriptor)==ERROR_SUCCESS);
    assert(acl&&acl->AceCount==2);
    HANDLE token;DWORD size=0;assert(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token));
    GetTokenInformation(token,TokenUser,NULL,0,&size);TOKEN_USER *user=malloc(size);assert(user);
    assert(GetTokenInformation(token,TokenUser,user,size,&size));
    BYTE system_sid[SECURITY_MAX_SID_SIZE];DWORD sid_size=sizeof(system_sid);assert(CreateWellKnownSid(WinLocalSystemSid,NULL,system_sid,&sid_size));
    int found_user=0,found_system=0;
    for(DWORD i=0;i<acl->AceCount;i++){
        ACCESS_ALLOWED_ACE *ace;assert(GetAce(acl,i,(void**)&ace));assert(ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE);
        PSID sid=&ace->SidStart;found_user+=EqualSid(sid,user->User.Sid);found_system+=EqualSid(sid,system_sid);
        assert(EqualSid(sid,user->User.Sid)||EqualSid(sid,system_sid));
    }
    assert(found_user==1&&found_system==1);free(user);CloseHandle(token);LocalFree(descriptor);
}
static int child_count(const wchar_t *folder){
    wchar_t pattern[PATH_CAP];swprintf(pattern,PATH_CAP,L"%ls\\Recovered-*",folder);
    WIN32_FIND_DATAW data;HANDLE search=FindFirstFileW(pattern,&data);if(search==INVALID_HANDLE_VALUE)return 0;
    int count=0;do{count++;}while(FindNextFileW(search,&data));FindClose(search);return count;
}
static void run_gui(HWND window,const wchar_t *input,const wchar_t *password){
    SendMessageW(vm_check,BM_SETCHECK,BST_UNCHECKED,0);SetWindowTextW(source_edit,input);SetWindowTextW(pass_edit,password);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(109,BN_CLICKED),(LPARAM)buttons[4]);
    assert(busy&&worker);assert(!IsWindowEnabled(mode_combo)&&!IsWindowEnabled(source_edit));
    assert(progress_visible()&&!(GetWindowLongW(progress_bar,GWL_STYLE)&PBS_MARQUEE));
    assert(progress_percent==0&&SendMessageW(progress_bar,PBM_GETPOS,0,0)==0);
    wchar_t value[256];GetWindowTextW(pass_edit,value,256);assert(!*value);
    SendMessageW(mode_combo,CB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(301,CBN_SELCHANGE),0);assert(recovering);
    /* Refreshing US English labels must preserve the running operation. */
    apply_language(window);
    assert(busy&&recovering&&SendMessageW(mode_combo,CB_GETCURSEL,0,0)==1);
    assert(progress_visible());
    wait_for_job(window);assert_wiped();
}
int wmain(void){
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_LISTVIEW_CLASSES};assert(InitCommonControlsEx(&common));
    wchar_t temp[PATH_CAP],folder[PATH_CAP],raw[PATH_CAP],encrypted[PATH_CAP],original_hash[65],hash[65];
    assert(GetTempPathW(PATH_CAP,temp));swprintf(folder,PATH_CAP,L"%lsvault-recover-%lu space \u4e2d\u6587",temp,GetCurrentProcessId());
    assert(GetFileAttributesW(folder)==INVALID_FILE_ATTRIBUTES&&private_directory(folder));
    swprintf(raw,PATH_CAP,L"%ls\\source.tar.zst",folder);swprintf(encrypted,PATH_CAP,L"%ls\\source.vbk",folder);
    FILE *file=_wfopen(raw,L"wb");assert(file);assert(fwrite("\x28\xb5\x2f\xfd",1,4,file)==4);
    /* Synthetic authenticated payload; this test does not claim zstd extraction. */
    for(int i=0;i<1100000;i++)assert(fputc(i&255,file)!=EOF);assert(!fclose(file));
    const wchar_t *password=L"test-only-\u6062\u590d-123456";
    assert(vault_encrypt(raw,encrypted,password));assert(vault_hash(encrypted,original_hash));
    swprintf(config,PATH_CAP,L"%ls\\settings.ini",folder);swprintf(history,PATH_CAP,L"%ls\\history.ini",folder);assert(ini_create(config)&&ini_create(history));
    wcscpy(state,folder);
    WNDCLASSW cls={0};cls.lpfnWndProc=window_proc;cls.hInstance=GetModuleHandleW(NULL);cls.lpszClassName=L"VaultRecoveryTest";assert(RegisterClassW(&cls));
    HWND window=CreateWindowW(cls.lpszClassName,L"Recovery test",WS_OVERLAPPEDWINDOW,0,0,1150,730,NULL,NULL,cls.hInstance,NULL);assert(window);
    assert(progress_bar&&!progress_visible());
    SendMessageW(combo,CB_RESETCONTENT,0,0);SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)folder);SendMessageW(combo,CB_SETCURSEL,0,0);
    SetWindowTextW(pass_edit,L"must-clear");SetWindowTextW(confirm_edit,L"must-clear");
    SendMessageW(mode_combo,CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(301,CBN_SELCHANGE),0);
    assert(recovering&&!(GetWindowLongW(confirm_edit,GWL_STYLE)&WS_VISIBLE));
    wchar_t value[256];GetWindowTextW(pass_edit,value,256);assert(!*value);
    assert(!(GetWindowLongW(key_edit,GWL_STYLE)&WS_VISIBLE)&&(GetWindowLongW(source_edit,GWL_STYLE)&WS_VISIBLE));
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(109,BN_CLICKED),0);assert(!busy&&messages==1);assert_wiped();
    assert(!progress_visible());
    assert(SendMessageW(vm_check,BM_GETCHECK,0,0)==BST_CHECKED);
    SetWindowTextW(source_edit,encrypted);SetWindowTextW(pass_edit,password);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(109,BN_CLICKED),0);
    assert(!busy&&!worker);assert_wiped(); /* Declining cannot touch the VM. */
    assert(remote_calls==0);
    confirm_vm=1;SetWindowTextW(pass_edit,password);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(109,BN_CLICKED),0);assert(busy);wait_for_job(window);
    assert(recovery.result==RECOVER_OK&&recovery.vm_restored&&remote_calls==1&&child_count(folder)==0);
    assert(status_text==TXT_RECOVER_VM_DONE&&!*recovery.output);
    remote_ok=0;SetWindowTextW(pass_edit,password);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(109,BN_CLICKED),0);assert(busy);wait_for_job(window);
    assert(recovery.result==RECOVER_REMOTE&&remote_calls==2&&child_count(folder)==0);
    assert(status_text==TXT_RECOVER_VM_ERROR&&wcsstr(last_message,L"fixture remote failure"));
    remote_ok=1;confirm_vm=0;
    SendMessageW(vm_check,BM_SETCHECK,BST_UNCHECKED,0);
    /* A failed worker launch must stop activity and allow another attempt. */
    SetWindowTextW(source_edit,encrypted);SetWindowTextW(pass_edit,password);fail_thread=1;
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(109,BN_CLICKED),0);
    assert(!busy&&!worker&&!progress_visible()&&status_text==TXT_THREAD_ERROR);assert_wiped();fail_thread=0;
    run_gui(window,encrypted,password);assert(recovery.result==RECOVER_OK&&child_count(folder)==1);
    wchar_t first[PATH_CAP],first_dir[PATH_CAP];wcscpy(first,recovery.output);wcscpy(first_dir,first);*wcsrchr(first_dir,L'\\')=0;
    assert_private(first_dir);assert_private(first);assert(vault_hash(raw,hash));wchar_t recovered_hash[65];assert(vault_hash(first,recovered_hash)&&!wcscmp(hash,recovered_hash));
    assert(wcsstr(last_message,first));
    run_gui(window,encrypted,L"incorrect-password");assert(recovery.result==RECOVER_AUTH&&child_count(folder)==1&&! *recovery.output);
    assert(GetFileAttributesW(first)!=INVALID_FILE_ATTRIBUTES);
    run_gui(window,encrypted,password);assert(recovery.result==RECOVER_OK&&child_count(folder)==2&&wcscmp(first,recovery.output));
    wchar_t second[PATH_CAP],second_dir[PATH_CAP];wcscpy(second,recovery.output);wcscpy(second_dir,second);*wcsrchr(second_dir,L'\\')=0;
    assert(vault_hash(encrypted,hash)&&!wcscmp(hash,original_hash));
    /* Tampered ciphertext cannot leave a recovered file or new directory. */
    file=_wfopen(encrypted,L"r+b");assert(file&&_fseeki64(file,60,SEEK_SET)==0);int byte=fgetc(file);assert(byte!=EOF&&_fseeki64(file,60,SEEK_SET)==0);assert(fputc(byte^1,file)!=EOF&&!fclose(file));
    run_gui(window,encrypted,password);assert(recovery.result==RECOVER_AUTH&&child_count(folder)==2);
    /* Restore the byte, then append and truncate independently. */
    file=_wfopen(encrypted,L"r+b");assert(file&&_fseeki64(file,60,SEEK_SET)==0&&fputc(byte,file)!=EOF&&!fclose(file));
    file=_wfopen(encrypted,L"ab");assert(file&&fputc(1,file)!=EOF&&!fclose(file));
    run_gui(window,encrypted,password);assert(recovery.result==RECOVER_AUTH&&child_count(folder)==2);
    HANDLE archive=CreateFileW(encrypted,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);assert(archive!=INVALID_HANDLE_VALUE);
    LARGE_INTEGER size;assert(GetFileSizeEx(archive,&size));size.QuadPart-=2;
    assert(SetFilePointerEx(archive,size,NULL,FILE_BEGIN)&&SetEndOfFile(archive));CloseHandle(archive);
    run_gui(window,encrypted,password);assert(recovery.result==RECOVER_AUTH&&child_count(folder)==2);
    assert(DeleteFileW(encrypted));
    file=_wfopen(raw,L"wb");assert(file);assert(fwrite("not a zstd archive",1,18,file)==18&&!fclose(file));assert(vault_encrypt(raw,encrypted,password));
    run_gui(window,encrypted,password);assert(recovery.result==RECOVER_FORMAT&&child_count(folder)==2);
    wcscpy(recovery.destination,L"\\\\server\\share");wcscpy(recovery.password,password);assert(!recover_run(&recovery)&&recovery.result==RECOVER_PATH);assert_wiped();
    wcscpy(recovery.destination,raw);wcscpy(recovery.password,password);assert(!recover_run(&recovery)&&recovery.result==RECOVER_PATH);assert_wiped();
    swprintf(recovery.destination,PATH_CAP,L"%ls\\missing",folder);wcscpy(recovery.password,password);assert(!recover_run(&recovery)&&recovery.result==RECOVER_PATH);assert_wiped();
    DWORD history_size;WIN32_FILE_ATTRIBUTE_DATA attrs;assert(GetFileAttributesExW(history,GetFileExInfoStandard,&attrs));history_size=attrs.nFileSizeLow;assert(history_size==2);assert(ListView_GetItemCount(list)==0);
    test_sizes();
    SendMessageW(mode_combo,CB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(301,CBN_SELCHANGE),0);
    assert(!recovering&&(GetWindowLongW(confirm_edit,GWL_STYLE)&WS_VISIBLE)&&(GetWindowLongW(key_edit,GWL_STYLE)&WS_VISIBLE));
    GetWindowTextW(buttons[4],value,256);assert(!wcscmp(value,tr(TXT_BACKUP)));
    /* Exercise backup launch/finish without connecting to the production VM. */
    SendMessageW(combo,CB_RESETCONTENT,0,0);SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)L"\\\\server\\share");SendMessageW(combo,CB_SETCURSEL,0,0);
    wcscpy(state,folder);SetWindowTextW(pass_edit,password);SetWindowTextW(confirm_edit,password);
    fail_thread=1;begin_backup(window);assert(!busy&&!progress_visible()&&status_text==TXT_THREAD_ERROR);fail_thread=0;
    SetWindowTextW(pass_edit,password);SetWindowTextW(confirm_edit,password);begin_backup(window);
    assert(busy&&worker&&progress_visible());
    SendMessageW(window,WM_VAULT_PERCENT,42,0);assert(progress_percent==42&&SendMessageW(progress_bar,PBM_GETPOS,0,0)==42);
    SendMessageW(window,WM_VAULT_PERCENT,20,0);assert(progress_percent==42);
    SendMessageW(window,WM_VAULT_PERCENT,101,0);assert(progress_percent==42);
    SendMessageW(window,WM_VAULT_PROGRESS,2,0);assert(status_text==TXT_ENCRYPT&&progress_visible());
    wait_for_job(window);assert(!job.success&&status_text==TXT_FAILURE);
    SendMessageW(window,WM_VAULT_PROGRESS,3,0);assert(status_text==TXT_FAILURE&&!progress_visible());
    SendMessageW(window,WM_VAULT_PERCENT,100,0);assert(progress_percent==42);
    DestroyWindow(window);
    assert(DeleteFileW(first)&&RemoveDirectoryW(first_dir));assert(DeleteFileW(second)&&RemoveDirectoryW(second_dir));
    assert(DeleteFileW(raw)&&DeleteFileW(encrypted)&&DeleteFileW(config)&&DeleteFileW(history)&&RemoveDirectoryW(folder));
    wprintf(L"GUI recovery: authenticated multi-chunk round-trip, ACLs, wrong password, tampering, invalid format, isolation, password clearing and mode switching passed\n");return 0;
}
