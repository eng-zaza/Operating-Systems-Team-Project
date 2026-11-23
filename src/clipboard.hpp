#pragma once
#include "store.hpp"
#include <shellapi.h>
#include <shlobj.h>

// CF_HDROP is the same file-list format used by Windows Explorer.
struct ClipboardFiles {std::vector<fs::path> paths;bool cut=false;DWORD sequence=0;};
inline HGLOBAL fileDropMemory(const std::vector<fs::path>& paths){
    size_t characters=1;for(auto& p:paths)characters+=p.wstring().size()+1;
    HGLOBAL memory=GlobalAlloc(GHND,sizeof(DROPFILES)+characters*sizeof(wchar_t));
    if(!memory)throw std::runtime_error("Not enough memory for the file selection.");
    auto header=(DROPFILES*)GlobalLock(memory);if(!header){GlobalFree(memory);throw std::runtime_error("Cannot prepare file selection.");}
    header->pFiles=sizeof(DROPFILES);header->fWide=TRUE;
    auto cursor=(wchar_t*)((BYTE*)header+sizeof(DROPFILES));
    for(auto& p:paths){auto s=p.wstring();memcpy(cursor,s.c_str(),(s.size()+1)*sizeof(wchar_t));cursor+=s.size()+1;}
    *cursor=0;GlobalUnlock(memory);return memory;
}
inline std::vector<fs::path> readFileDrop(HDROP drop){
    std::vector<fs::path> paths;UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);
    if(count>5000)throw std::runtime_error("Choose up to 5,000 files at once.");
    for(UINT i=0;i<count;++i){UINT length=DragQueryFileW(drop,i,nullptr,0);if(length>32767)throw std::runtime_error("A filename is too long.");
        std::wstring name(length+1,0);DragQueryFileW(drop,i,name.data(),length+1);name.resize(length);if(!name.empty())paths.emplace_back(name);}
    return paths;
}
inline DWORD copyFileClipboard(HWND owner,const std::vector<fs::path>& paths,bool cut){
    auto files=fileDropMemory(paths);HGLOBAL effect=GlobalAlloc(GHND,sizeof(DWORD));
    if(!effect){GlobalFree(files);throw std::runtime_error("Cannot prepare the clipboard.");}
    auto value=(DWORD*)GlobalLock(effect);if(!value){GlobalFree(files);GlobalFree(effect);throw std::runtime_error("Cannot prepare the clipboard.");}
    *value=cut?DROPEFFECT_MOVE:DROPEFFECT_COPY;GlobalUnlock(effect);
    if(!OpenClipboard(owner)){GlobalFree(files);GlobalFree(effect);throw std::runtime_error("The clipboard is busy. Try again.");}
    if(!EmptyClipboard()){CloseClipboard();GlobalFree(files);GlobalFree(effect);throw std::runtime_error("Cannot update the clipboard.");}
    bool ok=SetClipboardData(CF_HDROP,files)!=nullptr;
    if(!ok)GlobalFree(files);
    if(!SetClipboardData(RegisterClipboardFormatW(L"Preferred DropEffect"),effect)){GlobalFree(effect);ok=false;EmptyClipboard();}
    CloseClipboard();if(!ok)throw std::runtime_error("Cannot copy files to the clipboard.");return GetClipboardSequenceNumber();
}
inline ClipboardFiles readFileClipboard(HWND owner){
    if(!OpenClipboard(owner))throw std::runtime_error("The clipboard is busy. Try again.");
    ClipboardFiles result;
    try{
        auto files=(HDROP)GetClipboardData(CF_HDROP);if(!files)throw std::runtime_error("Copy or cut files first, then choose Paste.");
        result.paths=readFileDrop(files);
        auto effect=GetClipboardData(RegisterClipboardFormatW(L"Preferred DropEffect"));
        if(effect && GlobalSize(effect)>=sizeof(DWORD)){auto value=(DWORD*)GlobalLock(effect);if(value){result.cut=(*value&DROPEFFECT_MOVE)!=0;GlobalUnlock(effect);}}
        result.sequence=GetClipboardSequenceNumber();
    }catch(...){CloseClipboard();throw;}
    CloseClipboard();return result;
}
inline void clearCutClipboard(HWND owner,DWORD sequence){
    // Do not erase something the user copied while the background job was running.
    if(sequence && OpenClipboard(owner)){
        if(GetClipboardSequenceNumber()==sequence)EmptyClipboard();CloseClipboard();
    }
}
