#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include "backup.h"
#include "crypto.h"
#include "recover.h"
#include <commctrl.h>
#include <shlobj.h>
#include <commdlg.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wchar.h>
#include <io.h>
#include <fcntl.h>
#ifndef VAULT_CLI
#include <uxtheme.h>
#include "ui-text.h"
#endif

static wchar_t state[PATH_CAP], config[PATH_CAP], history[PATH_CAP];
#ifndef VAULT_CLI
static HWND combo,key_edit,hosts_edit,pass_edit,confirm_edit,list,status_label,buttons[6];
static HWND language_combo,mode_combo,source_edit,source_button,progress_bar;
static HWND size_tooltip,vm_check;
static uint64_t *history_sizes;
static wchar_t *history_sections;
static HWND delete_button;
static int size_hover_row=-1;
static wchar_t size_tip[96];
static int recovering;
static RecoverJob recovery;
static int busy;
static UiText status_text=TXT_READY;
static int progress_percent;
static const wchar_t *tr(UiText text){return ui_text[text];}
static void set_status(UiText text){
    status_text=text;
    wchar_t value[1024];
    if(busy||progress_percent==100){swprintf(value,1024,L"%ls — %d%%",tr(text),progress_percent);SetWindowTextW(status_label,value);}
    else SetWindowTextW(status_label,tr(text));
}
static void display_diagnostic(const wchar_t *source,wchar_t *output,size_t capacity){
    size_t used=0;
    while(*source&&used+1<capacity){
        const wchar_t *replacement=NULL;size_t matched=0;
        for(size_t i=0;i<sizeof(diagnostic_text)/sizeof(diagnostic_text[0]);i++){
            size_t length=wcslen(diagnostic_text[i][0]);if(!wcsncmp(source,diagnostic_text[i][0],length)){replacement=diagnostic_text[i][1];matched=length;break;}}
        if(replacement){while(*replacement&&used+1<capacity)output[used++]=*replacement++;source+=matched;}
        else output[used++]=*source++;
    }
    output[used]=0;
}
static HFONT font;
static HFONT title_font;
static HBRUSH paper_brush,field_brush,celadon_brush;
static int themed;
static UINT window_dpi=96;
static HICON banner_icon;
typedef struct {HWND window;int x,y,width,height;} ControlLayout;
static ControlLayout layouts[32];
static size_t layout_count;
static int pixels(int value){return MulDiv(value,(int)window_dpi,96);}
static RECT logical_rect(int left,int top,int right,int bottom){return (RECT){pixels(left),pixels(top),pixels(right),pixels(bottom)};}
static const COLORREF paper=RGB(248,245,235),field=RGB(255,253,247),celadon=RGB(203,225,218);
static const COLORREF ink=RGB(39,65,61),muted=RGB(80,100,94),gold=RGB(159,117,49),vermilion=RGB(159,54,38);
static int font_owned;
static HANDLE worker;
static BackupJob job;

#endif
static int ini_create(const wchar_t *path) {
    HANDLE h=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_EXISTS;
    WORD bom=0xfeff;DWORD n;BOOL ok=WriteFile(h,&bom,2,&n,NULL);CloseHandle(h);return ok&&n==2;
}
static int init_state(void) {
    wchar_t local[PATH_CAP];
    if(FAILED(SHGetFolderPathW(NULL,CSIDL_LOCAL_APPDATA,NULL,SHGFP_TYPE_CURRENT,local)))return 0;
    swprintf(state,PATH_CAP,L"%ls\\VaultBackup",local);
    swprintf(config,PATH_CAP,L"%ls\\settings.ini",state);
    swprintf(history,PATH_CAP,L"%ls\\history.ini",state);
    return private_directory(state)&&ini_create(config)&&ini_create(history);
}
#ifndef VAULT_CLI
static HFONT interface_font(void) {
    NONCLIENTMETRICSW metrics={0};metrics.cbSize=sizeof(metrics);
    typedef BOOL (WINAPI *MetricsForDpi)(UINT,UINT,PVOID,UINT,UINT);
    MetricsForDpi for_dpi=(MetricsForDpi)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),"SystemParametersInfoForDpi");
    /* Render at the monitor's physical DPI, never a stretched 96-DPI bitmap. */
    BOOL ok=for_dpi?for_dpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,window_dpi):
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0);
    if(ok){metrics.lfMessageFont.lfHeight=-pixels(13);metrics.lfMessageFont.lfQuality=CLEARTYPE_QUALITY;
        metrics.lfMessageFont.lfCharSet=DEFAULT_CHARSET;wcscpy(metrics.lfMessageFont.lfFaceName,L"Segoe UI");}
    HFONT result=ok?CreateFontIndirectW(&metrics.lfMessageFont):NULL;
    font_owned=result!=NULL;
    return result?result:(HFONT)GetStockObject(DEFAULT_GUI_FONT);
}
static HWND control(HWND parent,const wchar_t *cls,const wchar_t *text,DWORD style,int x,int y,int w,int h,int id) {
    if(themed&&!wcscmp(cls,L"BUTTON"))style=(style&~BS_TYPEMASK)|BS_OWNERDRAW;
    HWND c=CreateWindowExW(!wcscmp(cls,L"EDIT")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,pixels(x),pixels(y),pixels(w),pixels(h),parent,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    if(c&&layout_count<sizeof(layouts)/sizeof(layouts[0]))layouts[layout_count++]=(ControlLayout){c,x,y,w,h};
    if(themed&&(!wcscmp(cls,L"EDIT")||!wcscmp(cls,L"COMBOBOX")))SetWindowTheme(c,L"",L"");
    SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;
}
static void place_control(HWND window,int id,int x,int y,int width,int height){
    HWND child=GetDlgItem(window,id);if(!child)return;
    SetWindowPos(child,NULL,pixels(x),pixels(y),pixels(width),pixels(height),SWP_NOZORDER|SWP_NOACTIVATE);
    for(size_t i=0;i<layout_count;i++)if(layouts[i].window==child){layouts[i]=(ControlLayout){child,x,y,width,height};break;}
}
static void layout_interface(HWND window){
    if(!list)return;
    RECT client;GetClientRect(window,&client);int width=MulDiv(client.right,96,window_dpi),height=MulDiv(client.bottom,96,window_dpi);
    int right=width-20,password_width=(width-344)/2;
    place_control(window,209,width-210,15,64,22);place_control(window,300,width-140,11,128,200);
    place_control(window,200,12,58,width-24,22);
    place_control(window,200,12,58,width-190,22);place_control(window,301,width-164,54,144,200);
    place_control(window,201,20,100,90,22);place_control(window,100,114,96,width-310,220);
    place_control(window,101,right-172,96,82,28);place_control(window,102,right-82,96,82,28);
    place_control(window,202,20,134,90,22);place_control(window,103,114,130,width-226,28);place_control(window,104,right-82,130,82,28);
    place_control(window,203,20,168,90,22);place_control(window,105,114,164,width-226,28);place_control(window,106,right-82,164,82,28);
    place_control(window,112,114,130,width-226,28);place_control(window,113,right-82,130,82,28);
    place_control(window,210,20,164,width-40,28);
    SetWindowPos(vm_check,NULL,pixels(20),pixels(164),pixels(width-40),pixels(28),SWP_NOZORDER|SWP_NOACTIVATE);
    place_control(window,204,20,202,90,22);place_control(window,107,114,198,password_width,28);
    place_control(window,205,122+password_width,202,64,22);place_control(window,108,194+password_width,198,password_width,28);
    place_control(window,109,right-122,198,122,28);
    place_control(window,206,20,240,width-40,38);
    place_control(window,207,20,280,width-40,18);
    place_control(window,110,12,310,width-24,height-360);
    place_control(window,111,12,height-40,184,28);place_control(window,208,208,height-35,width-396,22);
    place_control(window,114,width-176,height-40,164,28);
    InvalidateRect(window,NULL,TRUE);
}
static void apply_language(HWND window){
    const struct {int id;UiText text;} labels[]={
        {200,TXT_INTRO},{201,TXT_LOCATION},{202,TXT_KEY},{203,TXT_HOSTS},{204,TXT_PASSWORD},{205,TXT_CONFIRM},
        {101,TXT_ADD},{102,TXT_REMOVE},{104,TXT_BROWSE},{106,TXT_BROWSE},{109,TXT_BACKUP},{111,TXT_OPEN},{114,TXT_DELETE_RECORD},{208,TXT_FOOTER},{209,TXT_LANGUAGE}
    };
    SetWindowTextW(window,tr(TXT_WINDOW));
    for(size_t i=0;i<sizeof(labels)/sizeof(labels[0]);i++)SetWindowTextW(GetDlgItem(window,labels[i].id),tr(labels[i].text));
    SetWindowTextW(vm_check,tr(TXT_RECOVER_VM));SetWindowTextW(source_button,tr(TXT_BROWSE));SetWindowTextW(GetDlgItem(window,210),tr(TXT_RECOVER_NOTE));
    SetWindowTextW(GetDlgItem(window,202),tr(recovering?TXT_SOURCE:TXT_KEY));
    SetWindowTextW(GetDlgItem(window,201),tr(recovering?TXT_RECOVER_LOCATION:TXT_LOCATION));
    SetWindowTextW(GetDlgItem(window,204),tr(recovering?TXT_PASSWORD_TITLE:TXT_PASSWORD));
    SetWindowTextW(buttons[4],tr(recovering?TXT_RECOVER:TXT_BACKUP));
    SetWindowTextW(buttons[5],tr(recovering?TXT_RECOVER_OPEN:TXT_OPEN));
    SendMessageW(mode_combo,CB_RESETCONTENT,0,0);
    SendMessageW(mode_combo,CB_ADDSTRING,0,(LPARAM)tr(TXT_MODE_BACKUP));SendMessageW(mode_combo,CB_ADDSTRING,0,(LPARAM)tr(TXT_MODE_RECOVER));
    SendMessageW(mode_combo,CB_SETCURSEL,recovering,0);
    for(int i=0;i<6;i++){LVCOLUMNW column={0};column.mask=LVCF_TEXT;column.pszText=(LPWSTR)tr((UiText)(TXT_DATE+i));ListView_SetColumn(list,i,&column);}
    set_status(status_text);SendMessageW(language_combo,CB_SETCURSEL,0,0);InvalidateRect(window,NULL,TRUE);
}
static void theme_init(void){
    HIGHCONTRASTW contrast={sizeof(contrast),0,NULL};
    themed=!(SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0)&&(contrast.dwFlags&HCF_HIGHCONTRASTON));
    paper_brush=CreateSolidBrush(paper);field_brush=CreateSolidBrush(field);celadon_brush=CreateSolidBrush(celadon);
    LOGFONTW title={0};GetObjectW(font,sizeof(title),&title);title.lfHeight=-pixels(19);title.lfWeight=FW_SEMIBOLD;title_font=CreateFontIndirectW(&title);
}
static void refresh_icons(HWND window){
    HINSTANCE instance=GetModuleHandleW(NULL);
    HICON small=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetricsForDpi(SM_CXSMICON,window_dpi),GetSystemMetricsForDpi(SM_CYSMICON,window_dpi),LR_SHARED);
    HICON large=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetricsForDpi(SM_CXICON,window_dpi),GetSystemMetricsForDpi(SM_CYICON,window_dpi),LR_SHARED);
    banner_icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,pixels(32),pixels(32),LR_SHARED);
    SendMessageW(window,WM_SETICON,ICON_SMALL,(LPARAM)small);SendMessageW(window,WM_SETICON,ICON_BIG,(LPARAM)large);
}
static void resize_for_dpi(HWND window,UINT dpi,const RECT *suggested){
    UINT previous_dpi=window_dpi;window_dpi=dpi;
    HFONT previous_font=font,previous_title=title_font;int previous_owned=font_owned;
    font=interface_font();LOGFONTW title={0};GetObjectW(font,sizeof(title),&title);title.lfHeight=-pixels(19);title.lfWeight=FW_SEMIBOLD;title_font=CreateFontIndirectW(&title);
    SendMessageW(window,WM_SETREDRAW,FALSE,0);
    for(size_t i=0;i<layout_count;i++){ControlLayout *layout=&layouts[i];
        SendMessageW(layout->window,WM_SETFONT,(WPARAM)font,FALSE);
    }
    SendMessageW(ListView_GetHeader(list),WM_SETFONT,(WPARAM)font,FALSE);
    for(int i=0;i<6;i++)ListView_SetColumnWidth(list,i,MulDiv(ListView_GetColumnWidth(list,i),(int)dpi,(int)previous_dpi));
    refresh_icons(window);
    if(suggested)SetWindowPos(window,NULL,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
    layout_interface(window);
    if(previous_owned)DeleteObject(previous_font);if(previous_title)DeleteObject(previous_title);
    SendMessageW(window,WM_SETREDRAW,TRUE,0);RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
}
static void rounded_panel(HDC dc,RECT rect,COLORREF fill,COLORREF border,int radius){
    HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,pixels(1),border);
    HGDIOBJ old_brush=SelectObject(dc,brush),old_pen=SelectObject(dc,pen);
    RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,pixels(radius),pixels(radius));
    SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(brush);DeleteObject(pen);
}
static void paint_theme(HWND w,HDC dc){
    RECT client;GetClientRect(w,&client);FillRect(dc,&client,themed?paper_brush:GetSysColorBrush(COLOR_BTNFACE));
    int width=MulDiv(client.right,96,window_dpi);
    RECT banner={0,0,client.right,pixels(48)};FillRect(dc,&banner,themed?celadon_brush:GetSysColorBrush(COLOR_BTNFACE));
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,themed?ink:GetSysColor(COLOR_WINDOWTEXT));
    HFONT old=(HFONT)SelectObject(dc,title_font?title_font:font);
    DrawIconEx(dc,pixels(12),pixels(8),banner_icon,pixels(32),pixels(32),0,NULL,DI_NORMAL);
    RECT title=logical_rect(56,6,width-230,42);DrawTextW(dc,tr(TXT_TITLE),-1,&title,DT_SINGLELINE|DT_VCENTER);
    if(themed){
        RECT line={0,pixels(47),client.right,pixels(49)};HBRUSH accent=CreateSolidBrush(gold);FillRect(dc,&line,accent);DeleteObject(accent);
        rounded_panel(dc,logical_rect(12,88,width-12,232),field,RGB(215,216,200),12);
        rounded_panel(dc,logical_rect(12,234,width-12,302),celadon,RGB(175,201,190),12);
    }
    SelectObject(dc,old);
}
static void draw_button(const DRAWITEMSTRUCT *item){
    FillRect(item->hDC,&item->rcItem,item->CtlID==111||item->CtlID==114?paper_brush:field_brush);
    int primary=item->CtlID==109,disabled=item->itemState&ODS_DISABLED,pressed=item->itemState&ODS_SELECTED;
    COLORREF fill=disabled?RGB(231,232,221):primary?(pressed?RGB(126,40,29):vermilion):(pressed?RGB(184,208,197):celadon);
    COLORREF text=disabled?muted:primary?field:ink;
    rounded_panel(item->hDC,item->rcItem,fill,primary&&!disabled?vermilion:RGB(151,180,167),10);
    wchar_t label[128];GetWindowTextW(item->hwndItem,label,128);RECT rect=item->rcItem;
    if(pressed)OffsetRect(&rect,pixels(1),pixels(1));SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,text);
    HFONT old=(HFONT)SelectObject(item->hDC,font);DrawTextW(item->hDC,label,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(item->hDC,old);
    if((item->itemState&ODS_FOCUS)&&!(item->itemState&ODS_NOFOCUSRECT)){rect=item->rcItem;InflateRect(&rect,-pixels(4),-pixels(4));DrawFocusRect(item->hDC,&rect);}
}
static void format_size(uint64_t bytes,wchar_t *output,size_t capacity){
    const wchar_t *units[]={L"B",L"KB",L"MB",L"GB",L"TB",L"PB",L"EB"};
    double value=(double)bytes;size_t unit=0;
    while(value>=1024.0&&unit<sizeof(units)/sizeof(units[0])-1){value/=1024.0;unit++;}
    /* Avoid rounding a value immediately below a boundary to 1024 units. */
    if(unit&&value>=1023.995&&unit<sizeof(units)/sizeof(units[0])-1){value/=1024.0;unit++;}
    if(!unit)swprintf(output,capacity,L"%llu B",(unsigned long long)bytes);
    else swprintf(output,capacity,L"%.2f %ls",value,units[unit]);
}
static void update_size_tooltip(HWND hwnd,int x,int y){
    LVHITTESTINFO hit={0};hit.pt=(POINT){x,y};ListView_SubItemHitTest(hwnd,&hit);
    int row=hit.iItem>=0&&hit.iSubItem==2?hit.iItem:-1;
    if(row==size_hover_row)return;
    size_hover_row=row;SendMessageW(size_tooltip,TTM_POP,0,0);
    TOOLINFOW tool={0};tool.cbSize=sizeof(tool);tool.hwnd=hwnd;tool.uId=1;
    size_tip[0]=0;
    if(row>=0&&history_sizes){
        swprintf(size_tip,sizeof(size_tip)/sizeof(size_tip[0]),tr(TXT_SIZE_BYTES),(unsigned long long)history_sizes[row]);
        ListView_GetSubItemRect(hwnd,row,2,LVIR_BOUNDS,&tool.rect);
    }
    tool.lpszText=size_tip;
    SendMessageW(size_tooltip,TTM_NEWTOOLRECTW,0,(LPARAM)&tool);
    SendMessageW(size_tooltip,TTM_UPDATETIPTEXTW,0,(LPARAM)&tool);
}
static LRESULT CALLBACK history_theme(HWND hwnd,UINT message,WPARAM a,LPARAM b,UINT_PTR id,DWORD_PTR data){
    (void)id;(void)data;
    if(message==WM_MOUSEMOVE)update_size_tooltip(hwnd,(int)(short)LOWORD(b),(int)(short)HIWORD(b));
    if(message==WM_MOUSELEAVE||message==WM_HSCROLL||message==WM_VSCROLL){
        size_hover_row=-1;SendMessageW(size_tooltip,TTM_POP,0,0);
    }
    if(themed&&message==WM_NOTIFY&&((NMHDR*)b)->code==NM_CUSTOMDRAW&&((NMHDR*)b)->hwndFrom==ListView_GetHeader(hwnd)){
        NMCUSTOMDRAW *draw=(NMCUSTOMDRAW*)b;
        if(draw->dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
        if(draw->dwDrawStage==CDDS_ITEMPREPAINT){
            FillRect(draw->hdc,&draw->rc,celadon_brush);wchar_t text[128];HDITEMW item={0};item.mask=HDI_TEXT;item.pszText=text;item.cchTextMax=128;
            Header_GetItem(draw->hdr.hwndFrom,(int)draw->dwItemSpec,&item);RECT rect=draw->rc;rect.left+=pixels(10);rect.right-=pixels(8);
            SetBkMode(draw->hdc,TRANSPARENT);SetTextColor(draw->hdc,ink);HFONT old=(HFONT)SelectObject(draw->hdc,font);
            DrawTextW(draw->hdc,text,-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(draw->hdc,old);return CDRF_SKIPDEFAULT;
        }
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,history_theme,1);
    return DefSubclassProc(hwnd,message,a,b);
}
static void save_settings(void) {
    wchar_t text[PATH_CAP],name[32];
    GetWindowTextW(key_edit,text,PATH_CAP);WritePrivateProfileStringW(L"ssh",L"key",text,config);
    GetWindowTextW(hosts_edit,text,PATH_CAP);WritePrivateProfileStringW(L"ssh",L"hosts",text,config);
    int count=(int)SendMessageW(combo,CB_GETCOUNT,0,0);
    swprintf(text,PATH_CAP,L"%d",count);WritePrivateProfileStringW(L"locations",L"count",text,config);
    swprintf(text,PATH_CAP,L"%ld",(long)SendMessageW(combo,CB_GETCURSEL,0,0));WritePrivateProfileStringW(L"locations",L"current",text,config);
    for(int i=0;i<count;i++){SendMessageW(combo,CB_GETLBTEXT,i,(LPARAM)text);swprintf(name,32,L"path%d",i);WritePrivateProfileStringW(L"locations",name,text,config);}
}
static void load_history(void) {
    /* Append in reverse chronological order instead of repeatedly shifting
     * every existing row; repaint once after the full batch. */
    SendMessageW(list,WM_SETREDRAW,FALSE,0);
    size_hover_row=-1;SendMessageW(size_tooltip,TTM_POP,0,0);
    ListView_DeleteAllItems(list);
    free(history_sections);history_sections=NULL;
    free(history_sizes);history_sizes=NULL;
    DWORD cap=65536,n;wchar_t *sections=NULL,**ordered=NULL;
    do {free(sections);sections=calloc(cap,sizeof(wchar_t));if(!sections)goto done;n=GetPrivateProfileSectionNamesW(sections,cap,history);if(n<cap-2)break;cap*=2;}while(cap<=4194304);
    size_t count=0;
    for(wchar_t *s=sections;*s;s+=wcslen(s)+1)count++;
    if(!count)goto done;
    ordered=malloc(count*sizeof(*ordered));if(!ordered)goto done;
    history_sizes=calloc(count,sizeof(*history_sizes));if(!history_sizes)goto done;
    size_t index=0;
    for(wchar_t *s=sections;*s;s+=wcslen(s)+1)ordered[index++]=s;
    int row=0;
    while(index){
        wchar_t *s=ordered[--index];
        wchar_t value[1024];GetPrivateProfileStringW(s,L"date",L"",value,1024,history);
        LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=row;item.pszText=value;item.lParam=(LPARAM)s;ListView_InsertItem(list,&item);
        const wchar_t *fields[]={L"status",L"bytes",L"path",L"sha256",L"detail"};
        for(int i=0;i<6-1;i++){GetPrivateProfileStringW(s,fields[i],L"",value,1024,history);if(i==0){if(!wcscmp(value,L"\u6210\u529f"))wcscpy(value,L"Success");else if(!wcscmp(value,L"\u5931\u8d25"))wcscpy(value,L"Failed");}
            wchar_t detail[2048];if(i==1){history_sizes[row]=wcstoull(value,NULL,10);format_size(history_sizes[row],detail,2048);ListView_SetItemText(list,row,i+1,detail);}
            else if(i==4){display_diagnostic(value,detail,2048);ListView_SetItemText(list,row,i+1,detail);}else ListView_SetItemText(list,row,i+1,value);}
        row++;
    }
done:
    free(ordered);history_sections=sections;
    EnableWindow(delete_button,!busy&&ListView_GetNextItem(list,-1,LVNI_SELECTED)>=0);
    SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,NULL,TRUE);
}
typedef struct {wchar_t path[PATH_CAP],hash[128],detail[1024];} BackupRecord;
static int copy_record_path(HWND window,const wchar_t *path){
    size_t bytes=(wcslen(path)+1)*sizeof(wchar_t);
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!memory)return 0;
    void *buffer=GlobalLock(memory);if(!buffer){GlobalFree(memory);return 0;}
    memcpy(buffer,path,bytes);GlobalUnlock(memory);
    if(!OpenClipboard(window)){GlobalFree(memory);return 0;}
    int copied=EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory)!=NULL;
    CloseClipboard();if(!copied)GlobalFree(memory);return copied;
}
static INT_PTR CALLBACK record_dialog(HWND window,UINT message,WPARAM a,LPARAM b){
    if(message==WM_INITDIALOG){
        BackupRecord *record=(BackupRecord*)b;SetWindowLongPtrW(window,DWLP_USER,b);
        SetWindowTextW(window,tr(TXT_RECORD));
        SetDlgItemTextW(window,401,tr(TXT_RECORD_PATH));SetDlgItemTextW(window,402,record->path);
        SetDlgItemTextW(window,403,tr(TXT_COPY));EnableWindow(GetDlgItem(window,403),record->path[0]!=0);
        SetDlgItemTextW(window,404,tr(TXT_HASH));SetDlgItemTextW(window,405,record->hash);
        SetDlgItemTextW(window,406,tr(TXT_DETAIL));SetDlgItemTextW(window,407,record->detail);
        SetDlgItemTextW(window,IDOK,tr(TXT_OK));return TRUE;
    }
    if(message==WM_COMMAND){
        if(LOWORD(a)==403&&HIWORD(a)==BN_CLICKED){
            BackupRecord *record=(BackupRecord*)GetWindowLongPtrW(window,DWLP_USER);
            if(copy_record_path(window,record->path))SetDlgItemTextW(window,403,tr(TXT_COPIED));
            else MessageBoxW(window,tr(TXT_COPY_ERROR),tr(TXT_RECORD),MB_OK|MB_ICONERROR);
            return TRUE;
        }
        if(LOWORD(a)==IDOK||LOWORD(a)==IDCANCEL){EndDialog(window,LOWORD(a));return TRUE;}
    }
    return FALSE;
}
static void show_backup_record(HWND window,int row){
    BackupRecord record={0};
    ListView_GetItemText(list,row,3,record.path,PATH_CAP);
    ListView_GetItemText(list,row,4,record.hash,128);
    ListView_GetItemText(list,row,5,record.detail,1024);
    DialogBoxParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(102),window,record_dialog,(LPARAM)&record);
}
static void delete_history_record(HWND window){
    if(busy)return;
    int row=ListView_GetNextItem(list,-1,LVNI_SELECTED);
    LVITEMW item={0};item.mask=LVIF_PARAM;item.iItem=row;
    if(row<0||!ListView_GetItem(list,&item)||!item.lParam)return;
    const wchar_t *section=(const wchar_t*)item.lParam;
    wchar_t date[128],path[PATH_CAP],prompt[1600];
    ListView_GetItemText(list,row,0,date,128);
    GetPrivateProfileStringW(section,L"path",L"",path,PATH_CAP,history);
    swprintf(prompt,1600,tr(TXT_DELETE_CONFIRM),date,path);
    TASKDIALOGCONFIG dialog={0};dialog.cbSize=sizeof(dialog);dialog.hwndParent=window;
    dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;
    dialog.dwCommonButtons=TDCBF_YES_BUTTON|TDCBF_NO_BUTTON;dialog.nDefaultButton=IDNO;
    dialog.pszWindowTitle=tr(TXT_DELETE_RECORD);dialog.pszMainIcon=TD_WARNING_ICON;
    dialog.pszContent=prompt;dialog.pszVerificationText=path[0]?tr(TXT_DELETE_DATA_CHECK):NULL;
    int choice=IDNO;BOOL delete_data=FALSE;
    if(FAILED(TaskDialogIndirect(&dialog,&choice,NULL,&delete_data))||choice!=IDYES)return;
    swprintf(prompt,1600,tr(delete_data?TXT_DELETE_FINAL_DATA:TXT_DELETE_FINAL_RECORD),date,path);
    if(MessageBoxW(window,prompt,tr(TXT_DELETE_RECORD),MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)return;
    if(delete_data){
        /* Delete one explicit archive only; never traverse or remove a directory. */
        size_t length=wcslen(path);DWORD error=ERROR_INVALID_NAME;
        int valid=length>6&&((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))&&
            path[1]==L':'&&path[2]==L'\\'&&!wcschr(path+2,L':')&&!_wcsicmp(path+length-4,L".vbk");
        if(valid){
            DWORD attributes=GetFileAttributesW(path);
            if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))error=ERROR_ACCESS_DENIED;
            else if(DeleteFileW(path))error=ERROR_SUCCESS;
            else {error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)error=ERROR_SUCCESS;}
        }
        if(error!=ERROR_SUCCESS){
            swprintf(prompt,1600,tr(TXT_DELETE_DATA_ERROR),(unsigned long)error,path);
            MessageBoxW(window,prompt,tr(TXT_DELETE_RECORD),MB_OK|MB_ICONERROR);return;
        }
    }
    if(!WritePrivateProfileStringW(section,NULL,NULL,history)){
        MessageBoxW(window,tr(delete_data?TXT_DELETE_PARTIAL_ERROR:TXT_DELETE_ERROR),tr(TXT_DELETE_RECORD),MB_OK|MB_ICONERROR);return;
    }
    load_history();
    int count=ListView_GetItemCount(list);
    if(count){if(row>=count)row=count-1;ListView_SetItemState(list,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(list,row,FALSE);}
    set_status(delete_data?TXT_DELETE_DATA_DONE:TXT_DELETE_DONE);
}
static void toggle(HWND window,int enabled){
    busy=!enabled;EnableWindow(combo,enabled);EnableWindow(key_edit,enabled);EnableWindow(hosts_edit,enabled);
    EnableWindow(pass_edit,enabled);EnableWindow(confirm_edit,enabled);
    for(int i=0;i<6;i++)EnableWindow(buttons[i],enabled);
    EnableWindow(vm_check,enabled);EnableWindow(mode_combo,enabled);EnableWindow(source_edit,enabled);EnableWindow(source_button,enabled);
    EnableWindow(delete_button,enabled&&ListView_GetNextItem(list,-1,LVNI_SELECTED)>=0);
    if(busy){progress_percent=0;SendMessageW(progress_bar,PBM_SETPOS,0,0);}
    ShowWindow(progress_bar,busy||progress_percent==100?SW_SHOW:SW_HIDE);
    (void)window;
}
static DWORD WINAPI run_worker(LPVOID unused){(void)unused;if(recovering)recover_run(&recovery);else backup_run(&job);PostMessageW(job.notify,WM_VAULT_FINISHED,0,0);return 0;}
static void change_mode(HWND window){
    progress_percent=0;ShowWindow(progress_bar,SW_HIDE);
    recovering=SendMessageW(mode_combo,CB_GETCURSEL,0,0)==1;
    /* Never carry a typed password between backup and recovery. */
    SetWindowTextW(pass_edit,L"");SetWindowTextW(confirm_edit,L"");
    const int backup_ids[]={103,104,203,105,106,205,108};
    for(size_t i=0;i<sizeof(backup_ids)/sizeof(backup_ids[0]);i++)ShowWindow(GetDlgItem(window,backup_ids[i]),recovering?SW_HIDE:SW_SHOW);
    ShowWindow(source_edit,recovering?SW_SHOW:SW_HIDE);ShowWindow(source_button,recovering?SW_SHOW:SW_HIDE);ShowWindow(GetDlgItem(window,210),SW_HIDE);ShowWindow(vm_check,recovering?SW_SHOW:SW_HIDE);
    set_status(recovering?TXT_RECOVER_READY:TXT_READY);apply_language(window);layout_interface(window);
}
static void begin_recovery(HWND window){
    ZeroMemory(&recovery,sizeof(recovery));
    recovery.to_vm=SendMessageW(vm_check,BM_GETCHECK,0,0)==BST_CHECKED;
    GetWindowTextW(key_edit,recovery.key,PATH_CAP);GetWindowTextW(hosts_edit,recovery.known_hosts,PATH_CAP);
    GetWindowTextW(source_edit,recovery.source,PATH_CAP);GetWindowTextW(pass_edit,recovery.password,256);
    if(!*recovery.source||!*recovery.password){SecureZeroMemory(recovery.password,sizeof(recovery.password));MessageBoxW(window,tr(TXT_RECOVER_PASSWORD),tr(TXT_RECOVER),MB_OK|MB_ICONERROR);return;}
    LRESULT index=SendMessageW(combo,CB_GETCURSEL,0,0);
    if(index==CB_ERR){SecureZeroMemory(recovery.password,sizeof(recovery.password));return;}
    SendMessageW(combo,CB_GETLBTEXT,index,(LPARAM)recovery.destination);
    if(recovery.to_vm)wcscpy(recovery.destination,state);
    if(recovery.to_vm){
        ServerConfig server;
        if(!server_config_load(&server)){SecureZeroMemory(recovery.password,sizeof(recovery.password));MessageBoxW(window,L"Missing or invalid server.local.ini next to the bin folder.",tr(TXT_RECOVER),MB_OK|MB_ICONERROR);return;}
        wchar_t text[2200];swprintf(text,2200,tr(TXT_RECOVER_VM_CONFIRM),server.ssh_target,recovery.source);
        if(MessageBoxW(window,text,tr(TXT_RECOVER),MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES){SecureZeroMemory(recovery.password,sizeof(recovery.password));return;}
    }
    job.notify=window;recovery.notify=window;SetWindowTextW(pass_edit,L"");SetWindowTextW(confirm_edit,L"");toggle(window,0);set_status(TXT_RECOVER_RUNNING);
    worker=CreateThread(NULL,0,run_worker,NULL,0,NULL);
    if(!worker){SecureZeroMemory(recovery.password,sizeof(recovery.password));toggle(window,1);set_status(TXT_THREAD_ERROR);}
}
static void choose_file(HWND window,HWND edit) {
    wchar_t path[PATH_CAP]=L"";OPENFILENAMEW o={0};o.lStructSize=sizeof(o);o.hwndOwner=window;o.lpstrFile=path;o.nMaxFile=PATH_CAP;o.lpstrFilter=tr(edit==source_edit?TXT_VBK_FILES:TXT_ALL_FILES);o.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&o)){SetWindowTextW(edit,path);save_settings();}
}
static void add_location(HWND window) {
    BROWSEINFOW b={0};b.hwndOwner=window;b.lpszTitle=tr(TXT_CHOOSE_FOLDER);b.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&b);wchar_t path[PATH_CAP];
    if(pid){if(SHGetPathFromIDListW(pid,path)){
        wchar_t drive[4]={path[0],L':',L'\\',0};UINT type=GetDriveTypeW(drive);
        if(wcslen(path)>220||path[1]!=L':'||(type!=DRIVE_FIXED&&type!=DRIVE_REMOVABLE))MessageBoxW(window,tr(TXT_LOCATION_ERROR),tr(TXT_LOCATION),MB_OK|MB_ICONERROR);
        else {LRESULT index=SendMessageW(combo,CB_FINDSTRINGEXACT,-1,(LPARAM)path);if(index==CB_ERR)index=SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)path);SendMessageW(combo,CB_SETCURSEL,index,0);save_settings();}
    }CoTaskMemFree(pid);}
}
static void begin_backup(HWND window) {
    wchar_t confirm[256];ZeroMemory(&job,sizeof(job));
    GetWindowTextW(pass_edit,job.password,256);GetWindowTextW(confirm_edit,confirm,256);
    int valid=wcslen(job.password)>=12&&!wcscmp(job.password,confirm);SecureZeroMemory(confirm,sizeof(confirm));
    if(!valid){SecureZeroMemory(job.password,sizeof(job.password));MessageBoxW(window,tr(TXT_PASSWORD_ERROR),tr(TXT_PASSWORD_TITLE),MB_OK|MB_ICONERROR);return;}
    int index=(int)SendMessageW(combo,CB_GETCURSEL,0,0);
    if(index==CB_ERR){SecureZeroMemory(job.password,sizeof(job.password));return;}
    SendMessageW(combo,CB_GETLBTEXT,index,(LPARAM)job.destination);GetWindowTextW(key_edit,job.key,PATH_CAP);GetWindowTextW(hosts_edit,job.known_hosts,PATH_CAP);
    wcscpy(job.state,state);job.notify=window;save_settings();SetWindowTextW(pass_edit,L"");SetWindowTextW(confirm_edit,L"");toggle(window,0);
    set_status(TXT_CONNECTING);
    worker=CreateThread(NULL,0,run_worker,NULL,0,NULL);
    if(!worker){SecureZeroMemory(job.password,sizeof(job.password));toggle(window,1);set_status(TXT_THREAD_ERROR);}
}
static void open_backup_folder(HWND window){
    wchar_t path[PATH_CAP]=L"";
    if(recovering&&recovery.to_vm&&recovery.result==RECOVER_OK){ServerConfig server;if(server_config_load(&server))ShellExecuteW(window,L"open",server.vault_url,NULL,NULL,SW_SHOWNORMAL);return;}
    if(recovering&&*recovery.output)wcscpy(path,recovery.output);
    int row=recovering?-1:ListView_GetNextItem(list,-1,LVNI_SELECTED);
    if(row>=0){
        ListView_GetItemText(list,row,3,path,PATH_CAP);
        wchar_t *separator=wcsrchr(path,L'\\');
        if(separator){
            /* Preserve the slash in a drive root: C:\\ is absolute, C: is not. */
            if(separator==path+2&&path[1]==L':')separator[1]=0;
            else *separator=0;
        }else path[0]=0;
    }
    if(recovering&&*path&&recovery.result!=RECOVER_CLEANUP){wchar_t *separator=wcsrchr(path,L'\\');if(separator)*separator=0;}
    if(!*path){
        LRESULT index=SendMessageW(combo,CB_GETCURSEL,0,0);
        if(index!=CB_ERR)SendMessageW(combo,CB_GETLBTEXT,index,(LPARAM)path);
    }
    DWORD attributes=*path?GetFileAttributesW(path):INVALID_FILE_ATTRIBUTES;
    if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)){
        MessageBoxW(window,tr(TXT_FOLDER_MISSING),tr(TXT_OPEN),MB_OK|MB_ICONERROR);return;
    }
    if((INT_PTR)ShellExecuteW(window,L"open",path,NULL,NULL,SW_SHOWNORMAL)<=32)
        MessageBoxW(window,tr(TXT_FOLDER_OPEN_ERROR),tr(TXT_OPEN),MB_OK|MB_ICONERROR);
}
static LRESULT CALLBACK window_proc(HWND w,UINT m,WPARAM a,LPARAM b){
    switch(m){
    case WM_CREATE:{
        window_dpi=GetDpiForWindow(w);font=interface_font();theme_init();
        control(w,L"STATIC",L"",0,12,58,820,22,200);
        control(w,L"STATIC",L"",0,20,100,90,22,201);combo=control(w,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,114,96,550,220,100);
        buttons[0]=control(w,L"BUTTON",L"",WS_TABSTOP,668,96,82,28,101);buttons[1]=control(w,L"BUTTON",L"",WS_TABSTOP,758,96,82,28,102);
        control(w,L"STATIC",L"",0,20,134,90,22,202);key_edit=control(w,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,114,130,634,28,103);buttons[2]=control(w,L"BUTTON",L"",WS_TABSTOP,758,130,82,28,104);
        control(w,L"STATIC",L"",0,20,168,90,22,203);hosts_edit=control(w,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,114,164,634,28,105);buttons[3]=control(w,L"BUTTON",L"",WS_TABSTOP,758,164,82,28,106);
        control(w,L"STATIC",L"",0,20,202,90,22,204);pass_edit=control(w,L"EDIT",L"",ES_PASSWORD|ES_AUTOHSCROLL|WS_TABSTOP,114,198,258,28,107);
        control(w,L"STATIC",L"",0,380,202,64,22,205);confirm_edit=control(w,L"EDIT",L"",ES_PASSWORD|ES_AUTOHSCROLL|WS_TABSTOP,452,198,258,28,108);
        SendMessageW(pass_edit,EM_SETLIMITTEXT,255,0);SendMessageW(confirm_edit,EM_SETLIMITTEXT,255,0);
        buttons[4]=control(w,L"BUTTON",L"",WS_TABSTOP|BS_DEFPUSHBUTTON,718,198,122,28,109);
        status_label=control(w,L"STATIC",L"",0,20,240,820,38,206);
        INITCOMMONCONTROLSEX progress_controls={sizeof(progress_controls),ICC_PROGRESS_CLASS};InitCommonControlsEx(&progress_controls);
        progress_bar=control(w,PROGRESS_CLASSW,L"",PBS_SMOOTH,20,280,820,18,207);
        SendMessageW(progress_bar,PBM_SETRANGE32,0,100);
        ShowWindow(progress_bar,SW_HIDE);
        list=control(w,WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|WS_TABSTOP|WS_BORDER,12,310,836,160,110);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        if(themed){ListView_SetBkColor(list,field);ListView_SetTextBkColor(list,field);ListView_SetTextColor(list,ink);SetWindowTheme(ListView_GetHeader(list),L"",L"");}
        size_tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,NULL,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,w,NULL,GetModuleHandleW(NULL),NULL);
        TOOLINFOW size_tool={0};size_tool.cbSize=sizeof(size_tool);size_tool.uFlags=TTF_SUBCLASS;size_tool.hwnd=list;size_tool.uId=1;size_tool.lpszText=size_tip;
        SendMessageW(size_tooltip,TTM_ADDTOOLW,0,(LPARAM)&size_tool);
        SetWindowSubclass(list,history_theme,1,0);
        int widths[]={170,70,110,280,220,320};
        for(int i=0;i<6;i++){LVCOLUMNW c={0};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(LPWSTR)tr((UiText)(TXT_DATE+i));c.cx=pixels(widths[i]);ListView_InsertColumn(list,i,&c);}
        buttons[5]=control(w,L"BUTTON",L"",WS_TABSTOP,12,480,184,28,111);
        delete_button=control(w,L"BUTTON",L"",WS_TABSTOP,684,480,164,28,114);EnableWindow(delete_button,FALSE);
        source_edit=control(w,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,114,130,634,28,112);
        SendMessageW(source_edit,EM_SETLIMITTEXT,PATH_CAP-1,0);
        source_button=control(w,L"BUTTON",L"",WS_TABSTOP,758,130,82,28,113);
        control(w,L"STATIC",L"",0,20,164,820,28,210);
        vm_check=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|BS_AUTOCHECKBOX|WS_TABSTOP,pixels(20),pixels(164),pixels(820),pixels(28),w,(HMENU)115,GetModuleHandleW(NULL),NULL);
        if(vm_check&&layout_count<sizeof(layouts)/sizeof(layouts[0]))layouts[layout_count++]=(ControlLayout){vm_check,20,164,820,28};
        SendMessageW(vm_check,WM_SETFONT,(WPARAM)font,TRUE);SendMessageW(vm_check,BM_SETCHECK,BST_CHECKED,0);
        mode_combo=control(w,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,696,54,144,200,301);
        control(w,L"STATIC",L"",0,208,485,640,22,208);
        control(w,L"STATIC",L"",0,650,15,64,22,209);
        language_combo=control(w,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,720,11,128,200,300);
        SendMessageW(language_combo,CB_ADDSTRING,0,(LPARAM)L"English (US)");EnableWindow(language_combo,FALSE);
        apply_language(w);
        wchar_t value[PATH_CAP],name[32];int count=GetPrivateProfileIntW(L"locations",L"count",0,config);if(count>100)count=100;
        for(int i=0;i<count;i++){swprintf(name,32,L"path%d",i);GetPrivateProfileStringW(L"locations",name,L"",value,PATH_CAP,config);if(*value&&wcslen(value)<=220)SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)value);}
        if(SendMessageW(combo,CB_GETCOUNT,0,0)==0){wchar_t profile[PATH_CAP];SHGetFolderPathW(NULL,CSIDL_PERSONAL,NULL,SHGFP_TYPE_CURRENT,profile);swprintf(value,PATH_CAP,L"%ls\\VaultBackups",profile);SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)value);}
        int current=GetPrivateProfileIntW(L"locations",L"current",0,config);if(current<0||current>=SendMessageW(combo,CB_GETCOUNT,0,0))current=0;SendMessageW(combo,CB_SETCURSEL,current,0);
        wchar_t default_path[PATH_CAP];local_path_load(L"ssh_key",default_path,PATH_CAP);
        GetPrivateProfileStringW(L"ssh",L"key",default_path,value,PATH_CAP,config);SetWindowTextW(key_edit,value);
        local_path_load(L"known_hosts",default_path,PATH_CAP);
        GetPrivateProfileStringW(L"ssh",L"hosts",default_path,value,PATH_CAP,config);SetWindowTextW(hosts_edit,value);
        load_history();change_mode(w);return 0;}
    case WM_COMMAND:
        if(busy)return 0;
        if(LOWORD(a)==301&&HIWORD(a)==CBN_SELCHANGE){change_mode(w);return 0;}
        switch(LOWORD(a)){
        case 100:if(HIWORD(a)==CBN_SELCHANGE)save_settings();break;
        case 101:add_location(w);break;
        case 102:{int i=(int)SendMessageW(combo,CB_GETCURSEL,0,0);if(SendMessageW(combo,CB_GETCOUNT,0,0)>1&&i>=0){SendMessageW(combo,CB_DELETESTRING,i,0);SendMessageW(combo,CB_SETCURSEL,0,0);save_settings();}break;}
        case 104:choose_file(w,key_edit);break;case 106:choose_file(w,hosts_edit);break;case 113:choose_file(w,source_edit);break;case 109:if(recovering)begin_recovery(w);else begin_backup(w);break;
        case 111:open_backup_folder(w);break;case 114:delete_history_record(w);break;}
        return 0;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);paint_theme(w,dc);EndPaint(w,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:if(a!=SIZE_MINIMIZED)layout_interface(w);return 0;
    case WM_GETMINMAXINFO:{RECT minimum=logical_rect(0,0,760,440);AdjustWindowRectExForDpi(&minimum,(DWORD)GetWindowLongPtrW(w,GWL_STYLE),FALSE,0,window_dpi);MINMAXINFO *info=(MINMAXINFO*)b;info->ptMinTrackSize.x=minimum.right-minimum.left;info->ptMinTrackSize.y=minimum.bottom-minimum.top;return 0;}
    case WM_DPICHANGED:resize_for_dpi(w,HIWORD(a),(const RECT*)b);return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:
        if(themed){HDC dc=(HDC)a;COLORREF background=(HWND)b==status_label?celadon:field;
            /* The introductory/footer labels sit on the paper background. */
            if(m==WM_CTLCOLORSTATIC&&(HWND)b!=status_label){RECT r;GetWindowRect((HWND)b,&r);MapWindowPoints(NULL,w,(POINT*)&r,2);if((HWND)b==GetDlgItem(w,209))background=celadon;else if((HWND)b==GetDlgItem(w,200)||(HWND)b==GetDlgItem(w,208))background=paper;}
            SetBkColor(dc,background);SetTextColor(dc,ink);return (LRESULT)(background==celadon?celadon_brush:background==paper?paper_brush:field_brush);}
        break;
    case WM_DRAWITEM:if(themed&&((DRAWITEMSTRUCT*)b)->CtlType==ODT_BUTTON){draw_button((DRAWITEMSTRUCT*)b);return TRUE;}break;
    case WM_NOTIFY:
        if(((NMHDR*)b)->hwndFrom==list&&((NMHDR*)b)->code==LVN_ITEMCHANGED){EnableWindow(delete_button,!busy&&ListView_GetNextItem(list,-1,LVNI_SELECTED)>=0);return 0;}
        if(((NMHDR*)b)->hwndFrom==list&&((NMHDR*)b)->code==LVN_KEYDOWN&&((NMLVKEYDOWN*)b)->wVKey==VK_DELETE){delete_history_record(w);return 0;}
        if(themed&&((NMHDR*)b)->hwndFrom==list&&((NMHDR*)b)->code==NM_CUSTOMDRAW){NMLVCUSTOMDRAW *draw=(NMLVCUSTOMDRAW*)b;
            if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){draw->clrText=ink;draw->clrTextBk=(draw->nmcd.dwItemSpec%2)?RGB(239,244,235):field;return CDRF_NEWFONT;}}
        if(((NMHDR*)b)->hwndFrom==list&&((NMHDR*)b)->code==NM_DBLCLK){int i=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(i>=0)show_backup_record(w,i);}return 0;
    case WM_VAULT_PROGRESS:{const UiText messages[]={TXT_CONNECTING,TXT_EXPORT,TXT_ENCRYPT,TXT_VERIFY,TXT_SAVE};if(busy&&!recovering&&a<5)set_status(messages[a]);return 0;}
    case WM_VAULT_PERCENT:
        if(busy&&a<=100&&(int)a>=progress_percent){
            progress_percent=(int)a;SendMessageW(progress_bar,PBM_SETPOS,a,0);
            if(recovering&&recovery.to_vm&&a<100){if(a>=90)status_text=TXT_RECOVER_VM_CHECK;else if(a>=75)status_text=TXT_RECOVER_VM_APPLY;else if(a>=55)status_text=TXT_RECOVER_VM_VALIDATE;else if(a>=45)status_text=TXT_RECOVER_VM_TRANSFER;}
            set_status(status_text);
        }
        return 0;
    case WM_VAULT_FINISHED:
        WaitForSingleObject(worker,INFINITE);CloseHandle(worker);worker=NULL;toggle(w,1);
        if(recovering){
            const UiText results[]={TXT_RECOVER_READY,TXT_RECOVER_PATH,TXT_RECOVER_DIRECTORY,TXT_RECOVER_AUTH,TXT_RECOVER_FORMAT,TXT_RECOVER_SAVE};
            if(recovery.result==RECOVER_OK){wchar_t detail[2400];swprintf(detail,2400,tr(recovery.to_vm?TXT_RECOVER_VM_SUCCESS:TXT_RECOVER_SUCCESS),recovery.output);if(*recovery.error){wchar_t diagnostic[1200];display_diagnostic(recovery.error,diagnostic,1200);size_t used=wcslen(detail);swprintf(detail+used,2400-used,L"\n\n%ls",diagnostic);}set_status(recovery.to_vm?TXT_RECOVER_VM_DONE:TXT_RECOVER_DONE);MessageBoxW(w,detail,tr(TXT_RECOVER),MB_OK|MB_ICONINFORMATION);}
            else if(recovery.result==RECOVER_CLEANUP){wchar_t detail[2400],cleanup[1600];swprintf(cleanup,1600,tr(TXT_RECOVER_CLEANUP),recovery.output);swprintf(detail,2400,L"%ls\n%ls",recovery.vm_restored?tr(TXT_RECOVER_VM_DONE):L"",cleanup);set_status(recovery.vm_restored?TXT_RECOVER_VM_DONE:TXT_RECOVER_AUTH);MessageBoxW(w,detail,tr(TXT_RECOVER),MB_OK|MB_ICONERROR);}
            else if(recovery.result==RECOVER_REMOTE){wchar_t detail[2400],diagnostic[1200];display_diagnostic(recovery.error,diagnostic,1200);swprintf(detail,2400,L"%ls\n\n%ls",tr(TXT_RECOVER_VM_ERROR),diagnostic);set_status(TXT_RECOVER_VM_ERROR);MessageBoxW(w,detail,tr(TXT_RECOVER),MB_OK|MB_ICONERROR);}
            else {set_status(results[recovery.result]);MessageBoxW(w,tr(results[recovery.result]),tr(TXT_RECOVER),MB_OK|MB_ICONERROR);}
            return 0;
        }
        load_history();set_status(job.success?TXT_SUCCESS:TXT_FAILURE);if(*job.error){wchar_t detail[2048];display_diagnostic(job.error,detail,2048);MessageBoxW(w,detail,job.success?tr(TXT_HISTORY_ERROR):tr(TXT_FAILURE),MB_OK|MB_ICONERROR);}return 0;
    case WM_QUERYENDSESSION:return !busy;
    case WM_CLOSE:if(busy){MessageBoxW(w,tr(TXT_CLOSE_ERROR),tr(TXT_BUSY),MB_OK|MB_ICONINFORMATION);return 0;}save_settings();DestroyWindow(w);return 0;
    case WM_DESTROY:free(history_sections);history_sections=NULL;free(history_sizes);history_sizes=NULL;if(font_owned)DeleteObject(font);if(title_font)DeleteObject(title_font);DeleteObject(paper_brush);DeleteObject(field_brush);DeleteObject(celadon_brush);PostQuitMessage(0);return 0;
    }return DefWindowProcW(w,m,a,b);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR command,int show){
    (void)previous;(void)command;CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX ic={sizeof(ic),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&ic);
    if(!init_state()){MessageBoxW(NULL,L"Unable to initialize the protected local state folder.",L"Bitwarden Vault Backup & Recover",MB_OK|MB_ICONERROR);CoUninitialize();return 1;}
    WNDCLASSW cls={0};cls.lpfnWndProc=window_proc;cls.hInstance=instance;cls.lpszClassName=L"VaultBackupWindow";cls.hCursor=LoadCursor(NULL,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&cls);
    HWND w=CreateWindowW(cls.lpszClassName,L"Bitwarden Vault Backup & Recover",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1150,730,NULL,NULL,instance,NULL);
    if(!w)return 1;
    RECT size=logical_rect(0,0,860,520);AdjustWindowRectExForDpi(&size,(DWORD)GetWindowLongPtrW(w,GWL_STYLE),FALSE,0,window_dpi);
    SetWindowPos(w,NULL,0,0,size.right-size.left,size.bottom-size.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    refresh_icons(w);
    if(themed){
        /* Windows 11 caption colors; older systems retain the system caption. */
        HMODULE dwm=LoadLibraryW(L"dwmapi.dll");
        if(dwm){typedef HRESULT (WINAPI *SetWindowAttribute)(HWND,DWORD,LPCVOID,DWORD);
            SetWindowAttribute set_attribute=(SetWindowAttribute)(void*)GetProcAddress(dwm,"DwmSetWindowAttribute");
            if(set_attribute){set_attribute(w,35,&celadon,sizeof(celadon));set_attribute(w,36,&ink,sizeof(ink));set_attribute(w,34,&gold,sizeof(gold));}
            FreeLibrary(dwm);}
    }
    ShowWindow(w,show);MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){if(!IsDialogMessageW(w,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}CoUninitialize();return 0;
}
#else
static int read_password(wchar_t password[256]){
    HANDLE h=GetStdHandle(STD_INPUT_HANDLE);DWORD mode,n;int ok=0;
    if(!GetConsoleMode(h,&mode)){fwprintf(stderr,L"Enter the password in an interactive console; piped and command-line passwords are not accepted.\n");return 0;}
    if(!SetConsoleMode(h,(mode|ENABLE_LINE_INPUT)&~ENABLE_ECHO_INPUT))return 0;
    fwprintf(stderr,L"Decryption password: ");fflush(stderr);
    if(ReadConsoleW(h,password,256,&n,NULL)){while(n&&(password[n-1]==L'\r'||password[n-1]==L'\n'))n--;if(n>0&&n<255){password[n]=0;ok=1;}}
    SetConsoleMode(h,mode);fwprintf(stderr,L"\n");if(!ok)SecureZeroMemory(password,512);return ok;
}
int wmain(int argc,wchar_t **argv){
    _setmode(_fileno(stdout),_O_U8TEXT);_setmode(_fileno(stderr),_O_U8TEXT);
    if(argc==3&&!wcscmp(argv[1],L"--selftest")){if(!private_directory(argv[2]))return 1;int ok=vault_crypto_selftest(argv[2]);wprintf(L"Encryption self-test: %ls\n",ok?L"Passed":L"Failed");return !ok;}
    if(argc==3&&!wcscmp(argv[1],L"--test-backup")){
        if(!init_state()||wcslen(argv[2])>220||!private_directory(argv[2]))return 1;BackupJob test={0};swprintf(test.state,PATH_CAP,L"%ls\\test-state-%lu",state,GetCurrentProcessId());if(!private_directory(test.state))return 1;wchar_t test_ini[PATH_CAP];swprintf(test_ini,PATH_CAP,L"%ls\\history.ini",test.state);if(!ini_create(test_ini))return 1;wcsncpy(test.destination,argv[2],PATH_CAP-1);
        if(!local_path_load(L"ssh_key",test.key,PATH_CAP)||!local_path_load(L"known_hosts",test.known_hosts,PATH_CAP)){fwprintf(stderr,L"Configure ssh_key and known_hosts in the [paths] section of server.local.ini.\n");return 1;}
        unsigned char random[32];wchar_t password[256]={0};if(BCryptGenRandom(NULL,random,32,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return 1;
        for(int i=0;i<32;i++)swprintf(password+i*2,3,L"%02x",random[i]);SecureZeroMemory(random,32);wcscpy(test.password,password);
        int ok=backup_run(&test);wchar_t output[PATH_CAP];swprintf(output,PATH_CAP,L"%ls\\restore-test.tar.zst",argv[2]);
        if(ok)ok=vault_decrypt(test.archive,output,password);SecureZeroMemory(password,sizeof(password));
        wprintf(L"Live backup test: %ls\n",ok?L"Passed (test use only; cleanup required)":L"Failed");if(!ok)fwprintf(stderr,L"%ls\n",test.error);return !ok;
    }
    if(argc==4&&!wcscmp(argv[1],L"decrypt")){wchar_t password[256]={0};if(!read_password(password))return 1;int ok=vault_decrypt(argv[2],argv[3],password);SecureZeroMemory(password,sizeof(password));wprintf(L"%ls\n",ok?L"Decryption and authentication completed.":L"Decryption failed: incorrect password, damaged file, existing output or disk write failure. No incomplete output was retained.");return !ok;}
    fwprintf(stderr,L"Usage: vault-backup-cli.exe decrypt <input.vbk> <output.tar.zst>\n       vault-backup-cli.exe --selftest <private-test-directory>\n");return 2;
}
#endif
