#include "store.hpp"
#include <commctrl.h>
#include <optional>
#include <memory>
#include <sstream>
#include <iomanip>
#include <future>
#include <shellapi.h>
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

HINSTANCE instance;HFONT font,titleFont;HWND mainWindow,list,searchBox,location,status,progressBar,scopeBox;
std::unique_ptr<FileStore> store;fs::path current=L".";
enum {HOME=100,UP,NEWFILE,NEWFOLDER,OPEN,RENAME,COPY,MOVE,REMOVE,BACKUP,REFRESH,SEARCH,BIN,RESTORE,FIND,SCOPE,CLEAR};
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
struct Row {fs::path path;std::wstring id,name,type,size,date,where;bool folder=false;};
struct Result {std::vector<Row> items;std::wstring message;bool listing=false,reload=false;};
std::vector<Row> items;
std::future<Result> job;
// Workers publish counters only. The UI thread alone updates windows and consumes results.
std::atomic<uint64_t> jobDone{0},jobTotal{0};
bool busy=false,binView=false,closing=false;
std::wstring jobLabel,afterReload;
std::vector<HWND> buttons;
void refresh();
void progressMode(bool indeterminate){
    LONG_PTR style=GetWindowLongPtrW(progressBar,GWL_STYLE);
    if(indeterminate){SetWindowLongPtrW(progressBar,GWL_STYLE,style|PBS_MARQUEE);SendMessageW(progressBar,PBM_SETMARQUEE,TRUE,40);}
    else{SendMessageW(progressBar,PBM_SETMARQUEE,FALSE,0);SetWindowLongPtrW(progressBar,GWL_STYLE,style&~PBS_MARQUEE);}
}
void updateActions(){
    bool selected=ListView_GetNextItem(list,-1,LVNI_SELECTED)>=0;
    for(auto h:buttons){int id=GetDlgCtrlID(h);bool enabled=!busy;
        if(id==OPEN || id==RENAME || id==COPY || id==MOVE || id==REMOVE)enabled=enabled && selected && !binView;
        if(id==RESTORE)enabled=enabled && selected && binView;
        if(id==NEWFILE || id==NEWFOLDER || id==UP)enabled=enabled && !binView;
        if(id==FIND || id==CLEAR)enabled=enabled && !binView;
        EnableWindow(h,enabled);
    }
    EnableWindow(searchBox,!busy && !binView);EnableWindow(scopeBox,!busy && !binView);
}
void beginJob(std::wstring label,std::function<Result(Progress)> work){
    if(busy)return;
    jobDone=0;jobTotal=0;jobLabel=std::move(label);
    job=std::async(std::launch::async,[work=std::move(work)](){return work([](uint64_t d,uint64_t t){jobDone=d;jobTotal=t;});});
    busy=true;updateActions();SetWindowTextW(status,jobLabel.c_str());
    SendMessageW(progressBar,PBM_SETPOS,0,0);progressMode(true);SetTimer(mainWindow,1,75,nullptr);
}
std::wstring modified(const fs::path& p){
    WIN32_FILE_ATTRIBUTE_DATA d{};if(!GetFileAttributesExW(p.c_str(),GetFileExInfoStandard,&d))throw std::runtime_error("Cannot read file attributes.");
    SYSTEMTIME utc{},local{};FileTimeToSystemTime(&d.ftLastWriteTime,&utc);SystemTimeToTzSpecificLocalTime(nullptr,&utc,&local);
    wchar_t out[40];swprintf_s(out,L"%04u-%02u-%02u %02u:%02u",local.wYear,local.wMonth,local.wDay,local.wHour,local.wMinute);return out;
}
std::wstring sizeLabel(uintmax_t bytes){
    if(bytes<1024)return std::to_wstring(bytes)+L" B";
    std::wostringstream out;out<<std::fixed<<std::setprecision(1);
    if(bytes<1024*1024)out<<(double)bytes/1024<<L" KB";else out<<(double)bytes/(1024*1024)<<L" MB";return out.str();
}
void renderRows(Result& result){
    SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);items=std::move(result.items);
    for(size_t index=0;index<items.size();++index){
        auto& r=items[index];SHFILEINFOW shell{};
        SHGetFileInfoW(r.name.c_str(),r.folder?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL,&shell,sizeof shell,SHGFI_SYSICONINDEX|SHGFI_SMALLICON|SHGFI_USEFILEATTRIBUTES);
        LVITEMW item{};item.mask=LVIF_TEXT|LVIF_IMAGE;item.iItem=(int)index;item.pszText=r.name.data();item.iImage=shell.iIcon;
        ListView_InsertItem(list,&item);ListView_SetItemText(list,(int)index,1,r.type.data());ListView_SetItemText(list,(int)index,2,r.size.data());
        ListView_SetItemText(list,(int)index,3,r.date.data());ListView_SetItemText(list,(int)index,4,r.where.data());
    }
    LVCOLUMNW c{};c.mask=LVCF_TEXT;c.pszText=(wchar_t*)(binView?L"Deleted":L"Modified");ListView_SetColumn(list,3,&c);
    SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);
    SetWindowTextW(status,(afterReload.empty()?result.message:afterReload+L"  "+result.message).c_str());afterReload.clear();updateActions();
}
void refresh(){
    if(busy)return;
    auto folder=current;auto query=textOf(searchBox);bool recursive=SendMessageW(scopeBox,BM_GETCHECK,0,0)==BST_CHECKED;
    bool trash=binView;
    SetWindowTextW(location,trash?L"Recycle bin / restore to original location":(L"Storage / "+(current==L"."?L"":current.wstring())).c_str());
    beginJob(trash?L"Loading recycle bin...":L"Searching files...",[folder,query,recursive,trash](Progress progress){
        Result result;result.listing=true;
        if(trash){for(auto& r:store->trashItems())result.items.push_back({r.original,r.id,r.original.filename().wstring(),r.folder?L"Folder":L"File",L"-",r.deleted,r.original.wstring(),r.folder});
            result.message=result.items.empty()?L"Recycle bin is empty. Deleted items will appear here.":std::to_wstring(result.items.size())+(result.items.size()==1?L" recycled item. ":L" recycled items. ")+L"Select an item and choose Restore.";return result;}
        auto found=store->search(folder,query,recursive && !query.empty(),progress);
        for(auto& relative:found.paths){try{auto p=store->path(relative);bool directory=fs::is_directory(p);result.items.push_back({relative,L"",p.filename().wstring(),directory?L"Folder":L"File",directory?L"-":sizeLabel(fs::file_size(p)),modified(p),relative.parent_path().wstring(),directory});}catch(const std::exception&){++found.skipped;}}
        std::sort(result.items.begin(),result.items.end(),[](auto& a,auto& b){if(a.folder!=b.folder)return a.folder;return lower(a.path.wstring())<lower(b.path.wstring());});
        result.message=std::to_wstring(result.items.size())+(result.items.size()==1?L" item. ":L" items. ")+(query.empty()?L"Double-click to open. Delete moves to Recycle bin.":L"Search complete. Location shows each match's folder.");
        if(result.items.empty())result.message=query.empty()?L"This folder is empty. Choose New file or New folder to start.":L"No matching names. Try another search or change the subfolder option.";
        if(found.skipped)result.message+=L" Skipped "+std::to_wstring(found.skipped)+L" inaccessible or linked items.";
        if(found.truncated)result.message+=L" Showing the first 5,000 matches; narrow your search.";
        return result;
    });
}
Row selected(){int i=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(i<0 || i>=(int)items.size())throw std::runtime_error("Select a file or folder first.");return items[i];}
void action(int id){
    if(busy)return;
    if(id==FIND || id==REFRESH){refresh();return;}
    if(id==CLEAR){SetWindowTextW(searchBox,L"");refresh();return;}
    if(id==HOME || id==UP || id==BIN){binView=id==BIN;current=id==UP?current.parent_path():fs::path(L".");if(current.empty())current=L".";SetWindowTextW(searchBox,L"");refresh();return;}
    if(id==RESTORE){if(!binView)return;auto row=selected();beginJob(L"Restoring item...",[row](Progress p){auto original=store->restore(row.id,p);return Result{{},L"Restored: "+original.wstring(),false,true};});return;}
    if(binView && id!=BACKUP)return;
    if(id==NEWFILE || id==NEWFOLDER){auto name=ask(id==NEWFILE?L"New file":L"New folder",L"Name (for example: notes.txt or Documents):");if(name){store->create(current,*name,id==NEWFOLDER);afterReload=L"Created "+*name+L".";}}
    else if(id==OPEN){auto row=selected();if(row.folder){current=row.path;SetWindowTextW(searchBox,L"");}else{Dialog d;d.editor=true;d.file=row.path;d.value=store->read(row.path);modal(d,(L"Edit: "+row.name).c_str());}}
    else if(id==RENAME){auto row=selected();auto name=ask(L"Rename",L"New name:",row.name);if(name && *name!=row.name){beginJob(L"Renaming item...",[row,name](Progress p){store->transfer(row.path,row.path.parent_path(),*name,true,p);return Result{{},L"Renamed to "+*name,false,true};});return;}}
    else if(id==COPY || id==MOVE){auto row=selected();auto dest=ask(id==MOVE?L"Move":L"Copy",L"Existing destination folder relative to Storage (. means root):",current.wstring());if(!dest)return;auto name=ask(L"Destination name",L"Name at destination:",row.name);if(name){beginJob(id==MOVE?L"Moving files...":L"Copying files...",[row,dest,name,id](Progress p){store->transfer(row.path,*dest,*name,id==MOVE,p);return Result{{},id==MOVE?L"Move complete.":L"Copy complete.",false,true};});return;}}
    else if(id==REMOVE){auto row=selected();if(MessageBoxW(mainWindow,(L"Move "+row.name+L" to FileDesk's recycle bin?\nYou can restore it later.").c_str(),L"Move to recycle bin",MB_YESNO|MB_ICONQUESTION)==IDYES){beginJob(L"Moving to recycle bin...",[row](Progress p){store->recycle(row.path,p);return Result{{},L"Moved to recycle bin. Choose Recycle bin to restore.",false,true};});return;}}
    else if(id==BACKUP){auto folder=store->root().parent_path()/L"backups";fs::create_directories(folder);SYSTEMTIME t;GetLocalTime(&t);wchar_t name[100];swprintf_s(name,L"FileDesk-%04u%02u%02u-%02u%02u%02u-%03u.zip",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);auto dest=folder/name;
        beginJob(L"Creating ZIP backup...",[dest](Progress p){store->backup(dest,p);return Result{{},L"Backup saved: backups/"+dest.filename().wstring(),false,true};});return;}
    refresh();
}
void pollJob(){
    uint64_t total=jobTotal.load(),done=jobDone.load();
    if(total){progressMode(false);int percent=(int)std::min(100.0,100.0*(double)done/(double)total);SendMessageW(progressBar,PBM_SETPOS,percent,0);SetWindowTextW(status,(jobLabel+L" "+std::to_wstring(percent)+L"%").c_str());}
    else SetWindowTextW(status,(jobLabel+(done?L" "+std::to_wstring(done)+L" items scanned":L"")).c_str());
    if(job.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
    KillTimer(mainWindow,1);progressMode(false);busy=false;
    try{auto result=job.get();SendMessageW(progressBar,PBM_SETPOS,100,0);
        if(closing){DestroyWindow(mainWindow);return;}
        if(result.listing)renderRows(result);else if(result.reload){afterReload=result.message;refresh();}else SetWindowTextW(status,result.message.c_str());
    }catch(const std::exception& e){if(closing){DestroyWindow(mainWindow);return;}SendMessageW(progressBar,PBM_SETPOS,0,0);SetWindowTextW(status,L"Operation failed. Review the error and try again.");error(mainWindow,e);}
    updateActions();
}
void layout(HWND h){
    RECT r;GetClientRect(h,&r);int width=r.right,height=r.bottom;
    MoveWindow(searchBox,148,108,width-558,32,TRUE);MoveWindow(GetDlgItem(h,FIND),width-398,108,90,32,TRUE);
    MoveWindow(GetDlgItem(h,CLEAR),width-300,108,76,32,TRUE);MoveWindow(scopeBox,width-212,110,190,28,TRUE);
    MoveWindow(location,24,244,width-48,28,TRUE);MoveWindow(list,24,282,width-48,height-382,TRUE);
    MoveWindow(status,24,height-88,width-48,38,TRUE);MoveWindow(progressBar,24,height-36,width-48,12,TRUE);
    ListView_SetColumnWidth(list,0,(width-48)*28/100);ListView_SetColumnWidth(list,1,90);ListView_SetColumnWidth(list,2,100);ListView_SetColumnWidth(list,3,165);ListView_SetColumnWidth(list,4,std::max(100,(width-48)*72/100-375));
}
LRESULT CALLBACK windowProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    try{
        if(msg==WM_CREATE){mainWindow=h;
            auto title=control(h,L"STATIC",L"FileDesk",0,24,14,400,48);SendMessageW(title,WM_SETFONT,(WPARAM)titleFont,TRUE);
            control(h,L"STATIC",L"Your files, organized. Search folders, recover deleted items, and keep a backup.",0,26,68,1000,26);
            control(h,L"STATIC",L"Search by name",0,24,115,122,24);
            searchBox=control(h,L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,148,108,476,32,SEARCH);SendMessageW(searchBox,EM_SETCUEBANNER,TRUE,(LPARAM)L"Type a filename and press Enter");
            buttons.push_back(control(h,L"BUTTON",L"Search",WS_TABSTOP,640,108,90,32,FIND));buttons.push_back(control(h,L"BUTTON",L"Clear",WS_TABSTOP,740,108,76,32,CLEAR));
            scopeBox=control(h,L"BUTTON",L"Include subfolders",WS_TABSTOP|BS_AUTOCHECKBOX,830,110,190,28,SCOPE);SendMessageW(scopeBox,BM_SETCHECK,BST_CHECKED,0);
            struct Tool{int id;const wchar_t* title;};
            Tool nav[]={{HOME,L"Home"},{UP,L"Up"},{NEWFILE,L"New file"},{NEWFOLDER,L"New folder"},{OPEN,L"Open / Edit"},{RENAME,L"Rename"},{COPY,L"Copy"},{MOVE,L"Move"}};
            for(int i=0;i<8;++i)buttons.push_back(control(h,L"BUTTON",nav[i].title,WS_TABSTOP,24+i*128,154,120,34,nav[i].id));
            Tool second[]={{REMOVE,L"Recycle"},{BIN,L"Recycle bin"},{RESTORE,L"Restore"},{BACKUP,L"Backup ZIP"},{REFRESH,L"Refresh"}};
            for(int i=0;i<5;++i)buttons.push_back(control(h,L"BUTTON",second[i].title,WS_TABSTOP,24+i*128,198,120,34,second[i].id));
            control(h,L"STATIC",L"F2 Rename   •   Delete Recycle   •   Ctrl+F Search",0,680,206,410,24);
            location=control(h,L"STATIC",L"Storage /",0,24,244,1000,28);
            list=control(h,WC_LISTVIEWW,L"",WS_BORDER|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_SHAREIMAGELISTS,24,282,1060,340);
            ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
            SHFILEINFOW shell{};auto icons=(HIMAGELIST)SHGetFileInfoW(L"file.txt",FILE_ATTRIBUTE_NORMAL,&shell,sizeof shell,SHGFI_SYSICONINDEX|SHGFI_SMALLICON|SHGFI_USEFILEATTRIBUTES);ListView_SetImageList(list,icons,LVSIL_SMALL);
            const wchar_t* heads[]={L"Name",L"Type",L"Size",L"Modified",L"Location / Original path"};
            for(int i=0;i<5;++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(wchar_t*)heads[i];c.cx=180;ListView_InsertColumn(list,i,&c);}
            status=control(h,L"STATIC",L"Ready",0,24,630,1060,38);
            progressBar=control(h,PROGRESS_CLASSW,L"",PBS_SMOOTH|PBS_MARQUEE,24,680,1060,12);SendMessageW(progressBar,PBM_SETRANGE32,0,100);
            layout(h);refresh();return 0;
        }
        if(msg==WM_SIZE && list){layout(h);return 0;}
        if(msg==WM_GETMINMAXINFO){auto m=(MINMAXINFO*)l;m->ptMinTrackSize={1120,650};return 0;}
        if(msg==WM_TIMER && w==1){pollJob();return 0;}
        if(msg==WM_COMMAND){if(LOWORD(w)!=SEARCH && LOWORD(w)!=SCOPE && HIWORD(w)==BN_CLICKED)action(LOWORD(w));return 0;}
        if(msg==WM_NOTIFY && ((NMHDR*)l)->hwndFrom==list){auto n=(NMHDR*)l;
            if(n->code==NM_DBLCLK)action(binView?RESTORE:OPEN);
            if(n->code==LVN_ITEMCHANGED)updateActions();
            return 0;
        }
        if(msg==WM_CONTEXTMENU && (HWND)w==list && !busy){
            POINT pt{(short)LOWORD(l),(short)HIWORD(l)};
            if(pt.x!=-1){POINT local=pt;ScreenToClient(list,&local);LVHITTESTINFO hit{};hit.pt=local;int i=ListView_HitTest(list,&hit);if(i<0)return 0;ListView_SetItemState(list,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}
            else {GetCursorPos(&pt);}
            if(ListView_GetNextItem(list,-1,LVNI_SELECTED)<0)return 0;
            HMENU menu=CreatePopupMenu();
            if(binView)AppendMenuW(menu,MF_STRING,RESTORE,L"Restore to original location");
            else {AppendMenuW(menu,MF_STRING,OPEN,L"Open / Edit\tEnter");AppendMenuW(menu,MF_STRING,RENAME,L"Rename\tF2");AppendMenuW(menu,MF_STRING,COPY,L"Copy...");AppendMenuW(menu,MF_STRING,MOVE,L"Move...");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,REMOVE,L"Move to recycle bin\tDelete");}
            int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,h,nullptr);DestroyMenu(menu);if(cmd)action(cmd);return 0;
        }
        if(msg==WM_CTLCOLORSTATIC){SetTextColor((HDC)w,RGB(20,44,73));SetBkColor((HDC)w,RGB(238,242,247));SetDCBrushColor((HDC)w,RGB(238,242,247));return (LRESULT)GetStockObject(DC_BRUSH);}
        if(msg==WM_CLOSE){if(busy){closing=true;SetWindowTextW(status,L"Finishing the current operation before closing...");return 0;}DestroyWindow(h);return 0;}
        if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    }catch(const std::exception& e){error(h,e);return 0;}
    return DefWindowProcW(h,msg,w,l);
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,PWSTR,int show){
    instance=inst;SetProcessDPIAware();INITCOMMONCONTROLSEX cc{sizeof cc,ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&cc);
    font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    titleFont=CreateFontW(-38,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    auto background=CreateSolidBrush(RGB(238,242,247));
    WNDCLASSW c{};c.hInstance=inst;c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.hbrBackground=background;c.lpfnWndProc=windowProc;c.lpszClassName=L"FileDeskMain";RegisterClassW(&c);
    c.lpfnWndProc=dialogProc;c.lpszClassName=L"FileDeskDialog";RegisterClassW(&c);
    try{wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);store=std::make_unique<FileStore>(fs::path(exe).parent_path()/L"storage");
        HWND h=CreateWindowW(L"FileDeskMain",L"FileDesk | C++ File Management System",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1180,800,nullptr,nullptr,inst,nullptr);
        if(!h)throw std::runtime_error("Cannot create the main window.");ShowWindow(h,show);UpdateWindow(h);
        MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){
            if(msg.message==WM_KEYDOWN && !busy){
                int command=0;
                if(msg.wParam==VK_F5)command=REFRESH;
                else if(msg.wParam=='F' && GetKeyState(VK_CONTROL)<0){SetFocus(searchBox);SendMessageW(searchBox,EM_SETSEL,0,-1);continue;}
                else if(msg.hwnd==searchBox && msg.wParam==VK_RETURN)command=FIND;
                else if(msg.hwnd==list){if(msg.wParam==VK_RETURN)command=binView?RESTORE:OPEN;if(msg.wParam==VK_F2)command=RENAME;if(msg.wParam==VK_DELETE)command=REMOVE;}
                if(command){SendMessageW(h,WM_COMMAND,command,0);continue;}
            }
            if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
    }catch(const std::exception& e){error(nullptr,e);return 1;}
    DeleteObject(font);DeleteObject(titleFont);DeleteObject(background);return 0;
}
