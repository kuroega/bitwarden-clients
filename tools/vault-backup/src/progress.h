#ifndef VAULT_PROGRESS_H
#define VAULT_PROGRESS_H
#include <windows.h>
#include <stdint.h>
#define WM_VAULT_PROGRESS (WM_APP+1)
#define WM_VAULT_PERCENT (WM_APP+3)
/* Percentages measure weighted completed work, never elapsed time. */
typedef struct {
    HWND notify;
    int start, end, last;
} VaultProgress;
static inline void vault_progress(VaultProgress *p,uint64_t done,uint64_t total,int complete){
    if(!p||!p->notify)return;
    int percent=p->start;
    if(complete)percent=p->end;
    else if(total){
        if(done>total)done=total;
        percent+=(int)((double)done/(double)total*(p->end-p->start));
        if(percent>=p->end)percent=p->end-1;
    }
    if(percent!=p->last){p->last=percent;PostMessageW(p->notify,WM_VAULT_PERCENT,(WPARAM)percent,0);}
}
#endif
