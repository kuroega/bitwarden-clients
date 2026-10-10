/* Exercise responsive layout, DPI changes and US English text without backups. */
#define wWinMain unused_gui_entry
#include "../src/main.c"
#undef wWinMain
#include <assert.h>

static RECT child_rect(HWND child,HWND parent){
    RECT rect;assert(GetWindowRect(child,&rect));MapWindowPoints(NULL,parent,(POINT*)&rect,2);return rect;
}

int wmain(void){
    assert(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_LISTVIEW_CLASSES};assert(InitCommonControlsEx(&common));
    wchar_t temp[PATH_CAP];assert(GetTempPathW(PATH_CAP,temp));
    swprintf(config,PATH_CAP,L"%lsvault-dpi-%lu-settings.ini",temp,GetCurrentProcessId());
    swprintf(history,PATH_CAP,L"%lsvault-dpi-%lu-history.ini",temp,GetCurrentProcessId());
    assert(GetFileAttributesW(config)==INVALID_FILE_ATTRIBUTES);
    assert(ini_create(config));
    assert(WritePrivateProfileStringW(L"ui",L"english",L"0",config));
    WNDCLASSW cls={0};cls.lpfnWndProc=window_proc;cls.hInstance=GetModuleHandleW(NULL);cls.lpszClassName=L"VaultDpiTest";
    assert(RegisterClassW(&cls));
    HWND window=CreateWindowW(cls.lpszClassName,L"DPI test",WS_OVERLAPPEDWINDOW,0,0,1150,730,NULL,NULL,cls.hInstance,NULL);
    assert(window);assert(layout_count>12);
    wchar_t language_name[32];GetWindowTextW(language_combo,language_name,32);
    assert(!wcscmp(language_name,L"English (US)"));
    SetWindowTextW(pass_edit,L"test-only input");SetWindowTextW(confirm_edit,L"test-only input");
    const UINT scales[]={96,144,192,144,96};
    const int sizes[][2]={{760,440},{860,520},{1280,780},{760,440}};
    for(size_t i=0;i<sizeof(scales)/sizeof(scales[0]);i++){
        UINT dpi=scales[i];RECT bounds={0,0,MulDiv(860,dpi,96),MulDiv(520,dpi,96)};
        assert(AdjustWindowRectExForDpi(&bounds,(DWORD)GetWindowLongPtrW(window,GWL_STYLE),FALSE,0,GetDpiForWindow(window)));
        SendMessageW(window,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),(LPARAM)&bounds);
        LOGFONTW actual_font;assert(GetObjectW(font,sizeof(actual_font),&actual_font));
        assert(actual_font.lfHeight==-MulDiv(13,dpi,96));assert(actual_font.lfQuality==CLEARTYPE_QUALITY);
        for(size_t size=0;size<sizeof(sizes)/sizeof(sizes[0]);size++){
            bounds=(RECT){0,0,MulDiv(sizes[size][0],dpi,96),MulDiv(sizes[size][1],dpi,96)};
            assert(AdjustWindowRectExForDpi(&bounds,(DWORD)GetWindowLongPtrW(window,GWL_STYLE),FALSE,0,GetDpiForWindow(window)));
            assert(SetWindowPos(window,NULL,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOZORDER|SWP_NOACTIVATE));
            for(int pass=0;pass<2;pass++){
                set_status(TXT_VERIFY);apply_language(window);
                assert(SendMessageW(language_combo,CB_GETCOUNT,0,0)==1);assert(!IsWindowEnabled(language_combo));
                wchar_t value[256];GetWindowTextW(buttons[4],value,256);assert(!wcscmp(value,tr(TXT_BACKUP)));
                GetWindowTextW(status_label,value,256);assert(!wcscmp(value,tr(TXT_VERIFY)));
                GetWindowTextW(pass_edit,value,256);assert(!wcscmp(value,L"test-only input"));
                GetWindowTextW(confirm_edit,value,256);assert(!wcscmp(value,L"test-only input"));
                RECT client;assert(GetClientRect(window,&client));
                for(size_t j=0;j<layout_count;j++){
                    HWND child=layouts[j].window;RECT rect=child_rect(child,window);
                    assert(rect.left>=0&&rect.top>=0&&rect.right<=client.right&&rect.bottom<=client.bottom);
                    assert((HFONT)SendMessageW(child,WM_GETFONT,0,0)==font);
                    if(child!=combo&&child!=language_combo&&child!=progress_bar){
                        HDC dc=GetDC(child);HGDIOBJ old=SelectObject(dc,font);TEXTMETRICW metrics;assert(GetTextMetricsW(dc,&metrics));
                        assert(rect.bottom-rect.top>=metrics.tmHeight);
                        wchar_t name[32];GetClassNameW(child,name,32);
                        if(!wcscmp(name,L"BUTTON")||!wcscmp(name,L"STATIC")){
                            wchar_t label[256];GetWindowTextW(child,label,256);SIZE extent;assert(GetTextExtentPoint32W(dc,label,(int)wcslen(label),&extent));
                            if(child!=status_label)assert(extent.cx<=rect.right-rect.left);
                        }
                        SelectObject(dc,old);ReleaseDC(child,dc);
                    }
                }
                RECT destination=child_rect(combo,window),add=child_rect(buttons[0],window),remove=child_rect(buttons[1],window);
                assert(destination.right<=add.left&&add.right<=remove.left);
                RECT password=child_rect(pass_edit,window),confirm=child_rect(confirm_edit,window),backup=child_rect(buttons[4],window);
                assert(password.right<confirm.left&&confirm.right<=backup.left);
                RECT rows=child_rect(list,window),footer=child_rect(buttons[5],window),status=child_rect(status_label,window);
                assert(rows.top>=status.bottom&&rows.bottom<footer.top);
                RECT progress=child_rect(progress_bar,window);
                assert(progress.top>=status.bottom&&progress.bottom<rows.top);
                assert(progress.left>=0&&progress.right<=client.right);
                SendMessageW(mode_combo,CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(301,CBN_SELCHANGE),0);
                assert(recovering);
                GetWindowTextW(buttons[4],value,256);assert(!wcscmp(value,tr(TXT_RECOVER)));
                GetWindowTextW(pass_edit,value,256);assert(!*value);
                for(size_t j=0;j<layout_count;j++){
                    HWND child=layouts[j].window;if(!(GetWindowLongW(child,GWL_STYLE)&WS_VISIBLE))continue;
                    RECT rect=child_rect(child,window);assert(rect.left>=0&&rect.top>=0&&rect.right<=client.right&&rect.bottom<=client.bottom);
                    wchar_t name[32];GetClassNameW(child,name,32);
                    if(!wcscmp(name,L"BUTTON")||!wcscmp(name,L"STATIC")){
                        HDC dc=GetDC(child);HGDIOBJ old=SelectObject(dc,font);wchar_t label[512];GetWindowTextW(child,label,512);
                        SIZE extent;assert(GetTextExtentPoint32W(dc,label,(int)wcslen(label),&extent));
                        if(child!=status_label)assert(extent.cx<=rect.right-rect.left);
                        SelectObject(dc,old);ReleaseDC(child,dc);
                    }
                }
                RECT source=child_rect(source_edit,window),browse=child_rect(source_button,window),mode=child_rect(mode_combo,window),intro=child_rect(GetDlgItem(window,200),window);
                assert(source.right<=browse.left&&intro.right<=mode.left);
                SendMessageW(mode_combo,CB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(301,CBN_SELCHANGE),0);
                SetWindowTextW(pass_edit,L"test-only input");SetWindowTextW(confirm_edit,L"test-only input");
            }
        }
        assert(ListView_GetColumnWidth(list,0)==MulDiv(170,dpi,96));assert(banner_icon);
        wprintf(L"DPI %u: min/default/large sizes, EN-US text and inputs passed\n",dpi);
    }
    wchar_t diagnostic[256];display_diagnostic(L"\u76ee\u6807\u78c1\u76d8\u6821\u9a8c\u5931\u8d25。",diagnostic,256);assert(!wcscmp(diagnostic,L"Destination disk verification failed."));
    assert(ini_create(history));
    assert(WritePrivateProfileStringW(L"legacy-success",L"status",L"\u6210\u529f",history));
    assert(WritePrivateProfileStringW(L"legacy-failure",L"status",L"\u5931\u8d25",history));
    assert(WritePrivateProfileStringW(L"legacy-failure",L"detail",L"\u76ee\u6807\u78c1\u76d8\u6821\u9a8c\u5931\u8d25\u3002",history));
    load_history();assert(ListView_GetItemCount(list)==2);
    wchar_t value[256];ListView_GetItemText(list,0,1,value,256);assert(!wcscmp(value,L"Failed"));
    ListView_GetItemText(list,0,5,value,256);assert(!wcscmp(value,L"Destination disk verification failed."));
    ListView_GetItemText(list,1,1,value,256);assert(!wcscmp(value,L"Success"));
    GetPrivateProfileStringW(L"legacy-failure",L"status",L"",value,256,history);assert(!wcscmp(value,L"\u5931\u8d25"));
    DestroyWindow(window);assert(DeleteFileW(config));assert(DeleteFileW(history));return 0;
}
