#include "store.hpp"
#include <commctrl.h>
#include <optional>
#include <memory>
#include <sstream>
#include <iomanip>
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

HINSTANCE instance;HFONT font,titleFont;HWND mainWindow,list,searchBox,location,status;
std::unique_ptr<FileStore> store;fs::path current=L".";std::vector<fs::path> rows;
enum {HOME=100,UP,NEWFILE,NEWFOLDER,OPEN,RENAME,COPY,MOVE,REMOVE,BACKUP,REFRESH,SEARCH};
std::wstring textOf(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
HWND control(HWND parent,const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0){
    HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,(HMENU)(INT_PTR)id,instance,nullptr);
    SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;
}
void error(HWND parent,const std::exception& e){MessageBoxW(parent,wide(e.what()).c_str(),L"Unable to complete action",MB_OK|MB_ICONERROR);}
struct Dialog {bool editor=false,accepted=false;HWND edit=nullptr;std::wstring value,label;fs::path file;};
LRESULT CALLBACK dialogProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    auto d=(Dialog*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(msg==WM_NCCREATE){d=(Dialog*)((CREATESTRUCTW*)l)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)d);}
    if(!d)return DefWindowProcW(h,msg,w,l);
    if(msg==WM_CREATE){
        if(d->editor){
            d->edit=control(h,L"EDIT",d->value.c_str(),WS_BORDER|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL,16,16,730,420,10);
            SendMessageW(d->edit,EM_SETLIMITTEXT,2000000,0);
            control(h,L"BUTTON",L"Save file",WS_TABSTOP|BS_DEFPUSHBUTTON,16,450,120,34,IDOK);
            control(h,L"BUTTON",L"Close",WS_TABSTOP,150,450,100,34,IDCANCEL);
        }else{
            control(h,L"STATIC",d->label.c_str(),0,20,16,500,45);
            d->edit=control(h,L"EDIT",d->value.c_str(),WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,20,66,500,28,10);
            control(h,L"BUTTON",L"OK",WS_TABSTOP|BS_DEFPUSHBUTTON,310,110,100,32,IDOK);
            control(h,L"BUTTON",L"Cancel",WS_TABSTOP,420,110,100,32,IDCANCEL);
        }
        SendMessageW(d->edit,EM_SETMODIFY,FALSE,0);SetFocus(d->edit);return 0;
    }
    if(msg==WM_COMMAND && LOWORD(w)==IDOK){
        try{d->value=textOf(d->edit);if(d->editor){store->write(d->file,d->value);SendMessageW(d->edit,EM_SETMODIFY,FALSE,0);SetWindowTextW(status,L"Saved text file to disk.");}else{d->accepted=true;DestroyWindow(h);}}
        catch(const std::exception& e){error(h,e);}return 0;
    }
    if(msg==WM_CLOSE || (msg==WM_COMMAND && LOWORD(w)==IDCANCEL)){
        if(!d->editor || !SendMessageW(d->edit,EM_GETMODIFY,0,0) || MessageBoxW(h,L"Discard unsaved changes?",L"Unsaved changes",MB_YESNO|MB_ICONQUESTION)==IDYES)DestroyWindow(h);return 0;
    }
    return DefWindowProcW(h,msg,w,l);
}
void modal(Dialog& d,const wchar_t* title){
    RECT r;GetWindowRect(mainWindow,&r);
    HWND h=CreateWindowExW(WS_EX_DLGMODALFRAME,L"FileDeskDialog",title,WS_CAPTION|WS_SYSMENU,r.left+80,r.top+80,d.editor?780:560,d.editor?540:200,mainWindow,nullptr,instance,&d);
    if(!h)throw std::runtime_error("Cannot create dialog.");
    EnableWindow(mainWindow,FALSE);ShowWindow(h,SW_SHOW);SetFocus(d.edit);
    MSG msg;while(IsWindow(h) && GetMessageW(&msg,nullptr,0,0)>0){
        if(d.editor && msg.message==WM_KEYDOWN && msg.wParam=='S' && GetKeyState(VK_CONTROL)<0){SendMessageW(h,WM_COMMAND,IDOK,0);continue;}
        if(d.editor && msg.message==WM_KEYDOWN && msg.wParam==VK_RETURN && msg.hwnd==d.edit){TranslateMessage(&msg);DispatchMessageW(&msg);continue;}
        if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    EnableWindow(mainWindow,TRUE);SetActiveWindow(mainWindow);
}
std::optional<std::wstring> ask(const wchar_t* title,const wchar_t* label,std::wstring initial=L""){
    Dialog d;d.label=label;d.value=initial;modal(d,title);if(d.accepted)return d.value;return {};
}
void refresh(){
    ListView_DeleteAllItems(list);rows.clear();
    SetWindowTextW(location,(L"Storage / "+(current==L"."?L"":current.wstring())).c_str());
    std::vector<fs::directory_entry> entries;for(auto& e:fs::directory_iterator(store->path(current)))entries.push_back(e);
    std::sort(entries.begin(),entries.end(),[](auto& a,auto& b){if(a.is_directory()!=b.is_directory())return a.is_directory();return lower(a.path().filename().wstring())<lower(b.path().filename().wstring());});
    auto query=lower(textOf(searchBox));int skipped=0;
    for(auto& e:entries){
        auto name=e.path().filename().wstring();if(lower(name).find(query)==std::wstring::npos)continue;
        try{store->path(current/name);}catch(...){++skipped;continue;}
        WIN32_FILE_ATTRIBUTE_DATA data{};if(!GetFileAttributesExW(e.path().c_str(),GetFileExInfoStandard,&data)){++skipped;continue;}
        SYSTEMTIME utc{},local{};FileTimeToSystemTime(&data.ftLastWriteTime,&utc);SystemTimeToTzSpecificLocalTime(nullptr,&utc,&local);
        wchar_t date[50];swprintf_s(date,L"%04u-%02u-%02u %02u:%02u",local.wYear,local.wMonth,local.wDay,local.wHour,local.wMinute);
        std::wstring kind=e.is_directory()?L"Folder":L"File",size=e.is_directory()?L"-":std::to_wstring(e.file_size());
        int i=(int)rows.size();rows.push_back(current/name);LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=name.data();ListView_InsertItem(list,&item);
        ListView_SetItemText(list,i,1,kind.data());ListView_SetItemText(list,i,2,size.data());ListView_SetItemText(list,i,3,date);
    }
    SetWindowTextW(status,(std::to_wstring(rows.size())+L" items shown. Double-click to open.  "+(skipped?std::to_wstring(skipped)+L" unsupported items skipped.":L"Files persist in Storage.")).c_str());
}
fs::path selected(){int i=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(i<0 || i>=(int)rows.size())throw std::runtime_error("Select a file or folder first.");return rows[i];}
void action(int id){
    if(id==HOME || id==UP){current=id==HOME?fs::path(L"."):current.parent_path();if(current.empty())current=L".";SetWindowTextW(searchBox,L"");refresh();return;}
    if(id==REFRESH){refresh();return;}
    if(id==NEWFILE || id==NEWFOLDER){auto name=ask(id==NEWFILE?L"New file":L"New folder",L"Name (for example: notes.txt or Documents):");if(name)store->create(current,*name,id==NEWFOLDER);}
    else if(id==OPEN){auto p=selected();if(fs::is_directory(store->path(p))){current=p;SetWindowTextW(searchBox,L"");}else{Dialog d;d.editor=true;d.file=p;d.value=store->read(p);modal(d,(L"Edit: "+p.filename().wstring()).c_str());}}
    else if(id==RENAME){auto p=selected();auto name=ask(L"Rename",L"New name:",p.filename().wstring());if(name && *name!=p.filename())store->transfer(p,current,*name,true);}
    else if(id==COPY || id==MOVE){auto p=selected();auto dest=ask(id==MOVE?L"Move":L"Copy",L"Existing destination folder relative to Storage (. means root):",current.wstring());if(!dest)return;auto name=ask(L"Destination name",L"Name at destination:",p.filename().wstring());if(name)store->transfer(p,*dest,*name,id==MOVE);}
    else if(id==REMOVE){auto p=selected();if(MessageBoxW(mainWindow,(L"Permanently delete "+p.filename().wstring()+L" and all its contents?").c_str(),L"Confirm deletion",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)==IDYES)store->remove(p);}
    else if(id==BACKUP){auto folder=store->root().parent_path()/L"backups";fs::create_directories(folder);SYSTEMTIME t;GetLocalTime(&t);wchar_t name[100];swprintf_s(name,L"FileDesk-%04u%02u%02u-%02u%02u%02u-%03u.zip",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);auto dest=folder/name;store->backup(dest);MessageBoxW(mainWindow,(L"Backup saved:\n"+dest.wstring()).c_str(),L"Backup complete",MB_OK|MB_ICONINFORMATION);}
    refresh();
}
LRESULT CALLBACK windowProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    try{
        if(msg==WM_CREATE){mainWindow=h;
            HWND title=control(h,L"STATIC",L"FileDesk",0,24,18,500,48);SendMessageW(title,WM_SETFONT,(WPARAM)titleFont,TRUE);
            control(h,L"STATIC",L"C++ FILE MANAGEMENT SYSTEM  /  Operating Systems Project",0,26,73,900,25);
            control(h,L"BUTTON",L"Home",WS_TABSTOP,24,112,80,32,HOME);control(h,L"BUTTON",L"Up",WS_TABSTOP,112,112,64,32,UP);
            location=control(h,L"STATIC",L"Storage /",0,192,118,540,26);
            control(h,L"STATIC",L"Filter:",0,780,118,55,26);searchBox=control(h,L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,840,112,245,30,SEARCH);
            const wchar_t* names[]={L"New file",L"New folder",L"Open / Edit",L"Rename",L"Copy",L"Move",L"Delete",L"Backup ZIP",L"Refresh"};
            for(int i=0;i<9;++i)control(h,L"BUTTON",names[i],WS_TABSTOP,24+i*118,160,110,34,NEWFILE+i);
            list=control(h,WC_LISTVIEWW,L"",WS_BORDER|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,24,212,1060,390);
            ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
            const wchar_t* heads[]={L"Name",L"Type",L"Size (bytes)",L"Last modified"};int widths[]={470,130,160,270};
            for(int i=0;i<4;++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(wchar_t*)heads[i];c.cx=widths[i];ListView_InsertColumn(list,i,&c);}
            status=control(h,L"STATIC",L"Ready",0,24,620,1060,36);refresh();return 0;
        }
        if(msg==WM_SIZE && list){int width=LOWORD(l),height=HIWORD(l);MoveWindow(list,24,212,width-48,height-276,TRUE);MoveWindow(status,24,height-48,width-48,36,TRUE);return 0;}
        if(msg==WM_GETMINMAXINFO){auto m=(MINMAXINFO*)l;m->ptMinTrackSize={1140,600};return 0;}
        if(msg==WM_COMMAND){if(LOWORD(w)==SEARCH && HIWORD(w)==EN_CHANGE){if(list)refresh();}else if(HIWORD(w)==BN_CLICKED)action(LOWORD(w));return 0;}
        if(msg==WM_NOTIFY && ((NMHDR*)l)->hwndFrom==list){auto n=(NMHDR*)l;if(n->code==NM_DBLCLK)action(OPEN);if(n->code==LVN_KEYDOWN){auto k=(NMLVKEYDOWN*)l;if(k->wVKey==VK_RETURN)action(OPEN);if(k->wVKey==VK_F5)refresh();}return 0;}
        if(msg==WM_CTLCOLORSTATIC){SetTextColor((HDC)w,RGB(20,44,73));SetBkColor((HDC)w,RGB(238,242,247));SetDCBrushColor((HDC)w,RGB(238,242,247));return (LRESULT)GetStockObject(DC_BRUSH);}
        if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    }catch(const std::exception& e){error(h,e);return 0;}
    return DefWindowProcW(h,msg,w,l);
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,PWSTR,int show){
    instance=inst;SetProcessDPIAware();INITCOMMONCONTROLSEX cc{sizeof cc,ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&cc);
    font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    titleFont=CreateFontW(-38,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    auto background=CreateSolidBrush(RGB(238,242,247));
    WNDCLASSW c{};c.hInstance=inst;c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.hbrBackground=background;c.lpfnWndProc=windowProc;c.lpszClassName=L"FileDeskMain";RegisterClassW(&c);
    c.lpfnWndProc=dialogProc;c.lpszClassName=L"FileDeskDialog";RegisterClassW(&c);
    try{wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);store=std::make_unique<FileStore>(fs::path(exe).parent_path()/L"storage");
        HWND h=CreateWindowW(L"FileDeskMain",L"FileDesk | C++ File Management System",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1160,750,nullptr,nullptr,inst,nullptr);
        if(!h)throw std::runtime_error("Cannot create the main window.");ShowWindow(h,show);UpdateWindow(h);
        MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN && msg.wParam==VK_F5){action(REFRESH);continue;}if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    }catch(const std::exception& e){error(nullptr,e);return 1;}
    DeleteObject(font);DeleteObject(titleFont);DeleteObject(background);return 0;
}
