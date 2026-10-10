/* Exercise the button handler with real controls and intercepted shell/dialog calls. */
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <assert.h>
#include <wchar.h>
static wchar_t opened[1024],message[1024];
static int open_count,message_count;
static INT_PTR shell_result=33;
static int confirm_result=IDNO,fail_delete;
static int first_result=IDNO,dialog_count;
static BOOL delete_data;
static int native_probe,native_check;
static HRESULT CALLBACK dialog_callback(HWND window,UINT notification,WPARAM a,LPARAM b,LONG_PTR data){
    (void)a;(void)b;(void)data;
    if(notification==TDN_CREATED){
        if(native_check)SendMessageW(window,TDM_CLICK_VERIFICATION,TRUE,0);
        SendMessageW(window,TDM_CLICK_BUTTON,IDNO,0);
    }
    return S_OK;
}
static HRESULT WINAPI test_dialog(const TASKDIALOGCONFIG *dialog,int *button,int *radio,BOOL *checked){
    (void)radio;assert(dialog->nDefaultButton==IDNO);
    assert(!(dialog->dwFlags&TDF_VERIFICATION_FLAG_CHECKED));
    assert(dialog->pszVerificationText);assert(wcsstr(dialog->pszContent,L"backup.vbk"));
    if(native_probe){
        TASKDIALOGCONFIG actual=*dialog;actual.pfCallback=dialog_callback;
        for(native_check=0;native_check<2;native_check++){
            BOOL selected=TRUE;int choice=0;
            assert(SUCCEEDED(TaskDialogIndirect(&actual,&choice,NULL,&selected))&&choice==IDNO&&selected==native_check);
        }
        native_probe=0;
    }
    wcscpy(message,dialog->pszContent);dialog_count++;*button=first_result;*checked=delete_data;return S_OK;
}
static BOOL WINAPI test_profile_write(LPCWSTR section,LPCWSTR key,LPCWSTR value,LPCWSTR file){
    if(fail_delete&&section&&!key&&!value)return FALSE;
    return WritePrivateProfileStringW(section,key,value,file);
}
static HINSTANCE WINAPI test_shell(HWND window,LPCWSTR verb,LPCWSTR path,LPCWSTR args,LPCWSTR directory,INT show){
    (void)window;(void)args;(void)directory;(void)show;
    assert(!wcscmp(verb,L"open"));wcscpy(opened,path);open_count++;return (HINSTANCE)shell_result;
}
static int WINAPI test_message(HWND window,LPCWSTR text,LPCWSTR title,UINT flags){
    (void)window;(void)title;wcscpy(message,text);message_count++;return (flags&MB_YESNO)?confirm_result:IDOK;
}
#define ShellExecuteW test_shell
#define MessageBoxW test_message
#define WritePrivateProfileStringW test_profile_write
#define TaskDialogIndirect test_dialog
#define wWinMain unused_gui_entry
#include "../src/main.c"
#undef wWinMain
#undef ShellExecuteW
#undef MessageBoxW
#undef WritePrivateProfileStringW
#undef TaskDialogIndirect

static void test_delete(HWND window,const wchar_t *archive){
    FILE *file=_wfopen(archive,L"wb");assert(file&&fwrite("test",1,4,file)==4&&!fclose(file));
    const wchar_t *ids[]={L"first",L"second",L"third"};
    for(int i=0;i<3;i++){
        assert(WritePrivateProfileStringW(ids[i],L"date",L"same date",history));
        assert(WritePrivateProfileStringW(ids[i],L"path",archive,history));
        assert(WritePrivateProfileStringW(ids[i],L"bytes",L"94635",history));
    }
    load_history();assert(ListView_GetItemCount(list)==3&&!IsWindowEnabled(delete_button));
    message_count=dialog_count=0;SendMessageW(window,WM_COMMAND,114,0);assert(!message_count&&!dialog_count);
    ListView_SetItemState(list,1,LVIS_SELECTED,LVIS_SELECTED);assert(IsWindowEnabled(delete_button));
    for(int pass=0;pass<2;pass++){
        apply_language(window);first_result=IDNO;native_probe=1;SendMessageW(window,WM_COMMAND,114,0);
        assert(ListView_GetItemCount(list)==3&&wcsstr(message,archive));
        first_result=IDYES;confirm_result=IDNO;SendMessageW(window,WM_COMMAND,114,0);
        assert(ListView_GetItemCount(list)==3&&GetFileAttributesW(archive)!=INVALID_FILE_ATTRIBUTES);
    }
    confirm_result=IDYES;toggle(window,0);assert(!IsWindowEnabled(delete_button));
    message_count=0;SendMessageW(window,WM_COMMAND,114,0);assert(!message_count&&ListView_GetItemCount(list)==3);toggle(window,1);
    fail_delete=1;SendMessageW(window,WM_COMMAND,114,0);assert(ListView_GetItemCount(list)==3&&!wcscmp(message,tr(TXT_DELETE_ERROR)));fail_delete=0;
    SendMessageW(window,WM_COMMAND,114,0);assert(ListView_GetItemCount(list)==2&&status_text==TXT_DELETE_DONE);
    wchar_t value[128];GetPrivateProfileStringW(L"second",L"date",L"missing",value,128,history);assert(!wcscmp(value,L"missing"));
    GetPrivateProfileStringW(L"first",L"date",L"missing",value,128,history);assert(!wcscmp(value,L"same date"));
    GetPrivateProfileStringW(L"third",L"date",L"missing",value,128,history);assert(!wcscmp(value,L"same date"));
    load_history();assert(ListView_GetItemCount(list)==2&&!IsWindowEnabled(delete_button));
    ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);
    NMLVKEYDOWN key={0};key.hdr.hwndFrom=list;key.hdr.code=LVN_KEYDOWN;key.wVKey=VK_DELETE;
    SendMessageW(window,WM_NOTIFY,110,(LPARAM)&key);assert(ListView_GetItemCount(list)==1);
    SendMessageW(window,WM_COMMAND,114,0);assert(!ListView_GetItemCount(list)&&!IsWindowEnabled(delete_button));
    load_history();assert(!ListView_GetItemCount(list));
    file=_wfopen(archive,L"rb");char bytes[4];assert(file&&fread(bytes,1,4,file)==4&&!memcmp(bytes,"test",4)&&!fclose(file));
    assert(DeleteFileW(archive));

    /* Only temporary fixtures are deleted; never touch real backup history. */
    for(int pass=0;pass<2;pass++){
        assert(WritePrivateProfileStringW(L"data",L"path",archive,history));load_history();
        ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);delete_data=TRUE;
        file=_wfopen(archive,L"wb");assert(file&&!fclose(file));
        confirm_result=IDNO;SendMessageW(window,WM_COMMAND,114,0);
        assert(ListView_GetItemCount(list)==1&&GetFileAttributesW(archive)!=INVALID_FILE_ATTRIBUTES);
        confirm_result=IDYES;
        HANDLE locked=CreateFileW(archive,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(locked!=INVALID_HANDLE_VALUE);
        SendMessageW(window,WM_COMMAND,114,0);assert(ListView_GetItemCount(list)==1&&wcsstr(message,archive));CloseHandle(locked);
        fail_delete=1;SendMessageW(window,WM_COMMAND,114,0);
        assert(ListView_GetItemCount(list)==1&&GetFileAttributesW(archive)==INVALID_FILE_ATTRIBUTES&&!wcscmp(message,tr(TXT_DELETE_PARTIAL_ERROR)));
        fail_delete=0;SendMessageW(window,WM_COMMAND,114,0);
        assert(!ListView_GetItemCount(list)&&status_text==TXT_DELETE_DATA_DONE);
        assert(WritePrivateProfileStringW(L"data",L"path",archive,history));load_history();ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);
        assert(CreateDirectoryW(archive,NULL));SendMessageW(window,WM_COMMAND,114,0);
        assert(ListView_GetItemCount(list)==1&&GetFileAttributesW(archive)&FILE_ATTRIBUTE_DIRECTORY);assert(RemoveDirectoryW(archive));
        file=_wfopen(archive,L"wb");assert(file&&fwrite("test",1,4,file)==4&&!fclose(file));
        SendMessageW(window,WM_COMMAND,114,0);
        assert(!ListView_GetItemCount(list)&&GetFileAttributesW(archive)==INVALID_FILE_ATTRIBUTES&&status_text==TXT_DELETE_DATA_DONE);
    }
    delete_data=FALSE;
}

static void click(HWND window){
    open_count=message_count=0;opened[0]=message[0]=0;
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(111,BN_CLICKED),(LPARAM)buttons[5]);
}
int wmain(void){
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_LISTVIEW_CLASSES};assert(InitCommonControlsEx(&common));
    wchar_t temp[PATH_CAP],folder[PATH_CAP],archive[PATH_CAP];assert(GetTempPathW(PATH_CAP,temp));
    swprintf(folder,PATH_CAP,L"%lsvault-folder-%lu space \u4e2d\u6587",temp,GetCurrentProcessId());assert(CreateDirectoryW(folder,NULL));
    swprintf(config,PATH_CAP,L"%ls\\settings.ini",folder);swprintf(history,PATH_CAP,L"%ls\\history.ini",folder);
    assert(ini_create(config)&&ini_create(history));
    WNDCLASSW cls={0};cls.lpfnWndProc=window_proc;cls.hInstance=GetModuleHandleW(NULL);cls.lpszClassName=L"VaultFolderTest";assert(RegisterClassW(&cls));
    HWND window=CreateWindowW(cls.lpszClassName,L"Folder test",WS_OVERLAPPEDWINDOW,0,0,860,520,NULL,NULL,cls.hInstance,NULL);assert(window);
    SendMessageW(combo,CB_RESETCONTENT,0,0);SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)folder);SendMessageW(combo,CB_SETCURSEL,0,0);
    click(window);assert(open_count==1&&!message_count&&!wcscmp(opened,folder));
    LVITEMW row={0};row.mask=LVIF_TEXT;row.pszText=L"test";assert(ListView_InsertItem(list,&row)==0);
    swprintf(archive,PATH_CAP,L"%ls\\backup.vbk",folder);ListView_SetItemText(list,0,3,archive);ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);
    click(window);assert(open_count==1&&!message_count&&!wcscmp(opened,folder));
    wchar_t root[4]={folder[0],L':',L'\\',0};swprintf(archive,PATH_CAP,L"%lsbackup.vbk",root);ListView_SetItemText(list,0,3,archive);
    click(window);assert(open_count==1&&!message_count&&!wcscmp(opened,root));
    ListView_SetItemText(list,0,3,L"");click(window);assert(open_count==1&&!message_count&&!wcscmp(opened,folder));
    swprintf(archive,PATH_CAP,L"%ls\\missing\\backup.vbk",folder);ListView_SetItemText(list,0,3,archive);
    for(int pass=0;pass<2;pass++){click(window);assert(!open_count&&message_count==1&&!wcscmp(message,tr(TXT_FOLDER_MISSING)));}
    ListView_SetItemState(list,0,0,LVIS_SELECTED);shell_result=SE_ERR_ACCESSDENIED;
    for(int pass=0;pass<2;pass++){click(window);assert(open_count==1&&message_count==1&&!wcscmp(message,tr(TXT_FOLDER_OPEN_ERROR)));}
    shell_result=33;busy=1;click(window);assert(!open_count&&!message_count);busy=0;
    SendMessageW(combo,CB_RESETCONTENT,0,0);click(window);assert(!open_count&&message_count==1);
    swprintf(archive,PATH_CAP,L"%ls\\backup.vbk",folder);test_delete(window,archive);
    DestroyWindow(window);assert(DeleteFileW(config));assert(DeleteFileW(history));assert(RemoveDirectoryW(folder));
    wprintf(L"Folder and history deletion: EN-US, double confirmation, optional file deletion, locked file, directory rejection, partial failure, missing file, persistence and file preservation passed\n");return 0;
}
