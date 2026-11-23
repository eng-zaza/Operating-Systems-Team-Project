#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <atomic>
#include <chrono>
namespace fs = std::filesystem;
using Progress = std::function<void(uint64_t,uint64_t)>;
inline void report(const Progress& progress,uint64_t done,uint64_t total){if(progress)progress(done,total);}
struct TrashItem {std::wstring id;fs::path original;std::wstring deleted;bool folder;};
struct SearchResult {std::vector<fs::path> paths;size_t skipped=0;bool truncated=false;};
struct PasteResult {size_t completed=0,skipped=0;std::vector<std::wstring> errors;};

inline std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
    if(!n) throw std::runtime_error("The editor accepts UTF-8 text only.");
    std::wstring out(n,0); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),out.data(),n); return out;
}
inline std::string utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    if(!n) throw std::runtime_error("Invalid Unicode text.");
    std::string out(n,0); WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),out.data(),n,nullptr,nullptr); return out;
}
inline std::wstring lower(std::wstring s) { for(auto& c:s)c=(wchar_t)towlower(c);return s; }

class FileStore {
    fs::path root_;
    static bool within(const fs::path& p,const fs::path& root) {
        auto a=p.begin(), b=root.begin();
        for(;b!=root.end();++b,++a) if(a==p.end() || lower(a->wstring())!=lower(b->wstring()))return false;
        return true;
    }
    void checkTree(const fs::path& p) const {
        path(p.lexically_relative(root_));
        if(fs::is_directory(p))for(auto& e:fs::recursive_directory_iterator(p))path(e.path().lexically_relative(root_));
    }
public:
    explicit FileStore(fs::path root):root_(fs::absolute(root).lexically_normal()) {fs::create_directories(root_);path(L".");}
    const fs::path& root()const{return root_;}
    bool contains(const fs::path& absolute)const{return within(fs::absolute(absolute).lexically_normal(),root_);}
    static void checkExternalPath(const fs::path& p){
        fs::path part;for(auto& component:p){part/=component;DWORD attr=GetFileAttributesW(part.c_str());
            if(attr==INVALID_FILE_ATTRIBUTES)throw std::runtime_error("A source file is unavailable.");
            if(attr&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("Linked files and folders cannot be copied.");}
    }
    fs::path path(const fs::path& relative=L".")const {
        if(relative.is_absolute() || relative.has_root_name())throw std::runtime_error("Use a path relative to Storage.");
        auto p=(root_/relative).lexically_normal();
        if(p.filename().empty() && p!=p.root_path())p=p.parent_path();
        if(!within(p,root_))throw std::runtime_error("The path must stay inside Storage.");
        // Reject reparse points, including junctions, before traversing them.
        fs::path part;
        for(auto& component:p) {
            part/=component;
            DWORD attr=GetFileAttributesW(part.c_str());
            if(attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked paths and junctions are not supported.");
        }
        return p;
    }
    fs::path fresh(const fs::path& parent,const std::wstring& name)const {
        if(name.empty() || name==L"." || name==L".." || iswspace(name.front()) || iswspace(name.back()) || name.back()==L'.' || name.find_first_of(L"<>:\"/\\|?*")!=std::wstring::npos)
            throw std::runtime_error("Enter a valid file or folder name without separators.");
        for(auto c:name)if(c<32)throw std::runtime_error("Invalid character in name.");
        auto stem=lower(name.substr(0,name.find(L'.')));
        if(stem==L"con" || stem==L"prn" || stem==L"aux" || stem==L"nul" || (stem.size()==4 && (stem.substr(0,3)==L"com" || stem.substr(0,3)==L"lpt") && stem[3]>=L'1' && stem[3]<=L'9'))throw std::runtime_error("Reserved Windows filename.");
        auto dir=path(parent);if(!fs::is_directory(dir))throw std::runtime_error("The destination folder must exist.");
        auto p=path(parent/name);if(fs::exists(p))throw std::runtime_error("That name already exists. Choose another name.");return p;
    }
    void create(const fs::path& parent,const std::wstring& name,bool folder) {
        auto p=fresh(parent,name);
        if(folder) {if(!fs::create_directory(p))throw std::runtime_error("Cannot create folder.");}
        else {HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create file.");CloseHandle(h);}
    }
    std::wstring read(const fs::path& relative)const {
        auto p=path(relative);if(fs::file_size(p)>2000000)throw std::runtime_error("The editor supports text files up to 2 MB.");
        std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("Cannot read file.");
        std::string data((std::istreambuf_iterator<char>(in)),{});
        if(data.find('\0')!=std::string::npos)throw std::runtime_error("Binary files cannot be edited.");
        return wide(data);
    }
    void write(const fs::path& relative,const std::wstring& text) {
        auto p=path(relative);auto data=utf8(text);
        if(data.size()>2000000)throw std::runtime_error("The editor supports text files up to 2 MB.");
        // Replace only after successfully writing a sibling temporary file.
        auto temp=p;temp+=L".filedesk-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
        HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create temporary save file.");
        DWORD count=0;bool ok=WriteFile(h,data.data(),(DWORD)data.size(),&count,nullptr) && count==data.size();
        if(ok)ok=FlushFileBuffers(h)!=0;CloseHandle(h);
        if(!ok || !MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {DeleteFileW(temp.c_str());throw std::runtime_error("Could not save. The previous file was preserved.");}
    }
    void transfer(const fs::path& relative,const fs::path& destination,const std::wstring& name,bool move,Progress progress={}) {
        transferAbsolute(path(relative),destination,name,move,progress);
    }
    void transferAbsolute(const fs::path& source,const fs::path& destination,const std::wstring& name,bool move,Progress progress={}) {
        auto src=fs::absolute(source).lexically_normal(),dst=fresh(destination,name);
        if(src==root_)throw std::runtime_error("The storage root cannot be moved or copied.");
        if(move && !contains(src))throw std::runtime_error("For files outside FileDesk, use Copy, then Paste here.");
        if(fs::is_directory(src) && within(dst,src))throw std::runtime_error("Choose a destination outside the source folder.");
        checkExternalPath(src);
        if(fs::is_directory(src))for(auto& e:fs::recursive_directory_iterator(src))checkExternalPath(e.path());
        if(move){report(progress,0,1);fs::rename(src,dst);report(progress,1,1);return;}
        std::vector<fs::path> files{src};
        if(fs::is_directory(src))for(auto& e:fs::recursive_directory_iterator(src))files.push_back(e.path());
        uint64_t total=0,done=0;for(auto& p:files)total+=fs::is_directory(p)?1:fs::file_size(p)+1;
        report(progress,0,total);
        // All destinations are new: a failed copy can remove only its own partial result.
        bool created=false;
        try{for(auto& p:files){
            auto target=p==src?dst:dst/p.lexically_relative(src);
            if(fs::is_directory(p)){fs::create_directory(target);created=true;report(progress,++done,total);continue;}
            std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("Cannot read source file.");
            std::ofstream out(target,std::ios::binary);if(!out)throw std::runtime_error("Cannot create destination file.");created=true;
            char buffer[65536];while(in.read(buffer,sizeof buffer)||in.gcount()) {out.write(buffer,in.gcount());done+=(uint64_t)in.gcount();report(progress,done,total);if(!out)throw std::runtime_error("Copy write failed.");}
            if(!in.eof())throw std::runtime_error("Copy read failed.");out.flush();if(!out)throw std::runtime_error("Copy write failed.");out.close();
            fs::last_write_time(target,fs::last_write_time(p));report(progress,++done,total);
        }}catch(...){if(created){std::error_code ec;fs::remove_all(dst,ec);}throw;}
    }
    PasteResult paste(const std::vector<fs::path>& sources,const fs::path& destination,bool move,Progress progress={}){
        if(!fs::is_directory(path(destination)))throw std::runtime_error("Open a destination folder before pasting.");
        // Do not let an external Cut become a destructive import or a silent Copy.
        if(move)for(auto& source:sources)if(!contains(source))throw std::runtime_error("For files outside FileDesk, use Copy (Ctrl+C), then Paste here. Outside files are never moved.");
        std::vector<fs::path> unique;
        for(auto& source:sources){auto p=fs::absolute(source).lexically_normal();if(std::none_of(unique.begin(),unique.end(),[&](auto& q){return lower(q.wstring())==lower(p.wstring());}))unique.push_back(p);}
        PasteResult result;
        for(size_t i=0;i<unique.size();++i){auto src=unique[i];
            // A selected folder already includes its selected children.
            if(std::any_of(unique.begin(),unique.end(),[&](auto& q){return q!=src && within(src,q);})){++result.skipped;continue;}
            try{
                auto dest=path(destination);bool folder=fs::is_directory(src);
                if(move && lower(src.parent_path().wstring())==lower(dest.wstring())){++result.skipped;continue;}
                std::wstring name=src.filename().wstring();
                if(!move && fs::exists(dest/name)){
                    auto stem=folder?name:src.stem().wstring();auto extension=folder?L"":src.extension().wstring();
                    unsigned n=1;do{name=stem+L" - Copy"+(n==1?L"":L" ("+std::to_wstring(n)+L")")+extension;++n;}while(fs::exists(dest/name));
                }
                transferAbsolute(src,destination,name,move,[&,i](uint64_t done,uint64_t total){if(!total)report(progress,0,0);else report(progress,i*1000+std::min<uint64_t>(1000,done*1000/total),unique.size()*1000);});
                ++result.completed;
            }catch(const std::exception& e){result.errors.push_back(src.filename().wstring()+L": "+wide(e.what()));}
            report(progress,(i+1)*1000,unique.size()*1000);
        }
        return result;
    }
    void remove(const fs::path& relative) {
        auto p=path(relative);if(p==root_)throw std::runtime_error("The storage root cannot be deleted.");
        checkTree(p);fs::remove_all(p);
    }
    fs::path trashRoot()const {
        auto root=root_.parent_path()/(root_.filename().wstring()+L".trash");
        DWORD attr=GetFileAttributesW(root.c_str());
        if(attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Recycle bin cannot be a linked folder.");
        fs::create_directories(root);return root;
    }
    std::wstring recycle(const fs::path& relative,Progress progress={}) {
        auto src=path(relative);if(src==root_)throw std::runtime_error("The storage root cannot be recycled.");checkTree(src);
        static std::atomic<unsigned> serial{0};
        auto id=std::to_wstring(std::chrono::system_clock::now().time_since_epoch().count())+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(serial++);
        auto record=trashRoot()/id;if(!fs::create_directory(record))throw std::runtime_error("Cannot create recycle record.");
        SYSTEMTIME t;GetLocalTime(&t);wchar_t date[40];swprintf_s(date,L"%04u-%02u-%02u %02u:%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute);
        try{
            std::ofstream meta(record/L"original.txt",std::ios::binary);meta<<utf8(src.lexically_relative(root_).wstring())<<'\n'<<utf8(date)<<'\n';meta.close();if(!meta)throw std::runtime_error("Cannot save recycle metadata.");
            report(progress,0,1);fs::rename(src,record/L"payload");
        }catch(...){std::error_code ec;fs::remove_all(record,ec);throw;}
        report(progress,1,1);return id;
    }
    std::vector<TrashItem> trashItems()const {
        std::vector<TrashItem> result;
        for(auto& e:fs::directory_iterator(trashRoot())){
            try{
                auto attr=GetFileAttributesW(e.path().c_str());if(attr==INVALID_FILE_ATTRIBUTES || (attr&FILE_ATTRIBUTE_REPARSE_POINT) || !e.is_directory())continue;
                for(auto leaf:{L"original.txt",L"payload"}){auto a=GetFileAttributesW((e.path()/leaf).c_str());if(a==INVALID_FILE_ATTRIBUTES || (a&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Invalid recycle record.");}
                if(fs::file_size(e.path()/L"original.txt")>65536)continue;
                std::ifstream meta(e.path()/L"original.txt",std::ios::binary);std::string original,deleted;std::getline(meta,original);std::getline(meta,deleted);
                if(!original.empty() && original.back()=='\r')original.pop_back();
                if(!deleted.empty() && deleted.back()=='\r')deleted.pop_back();
                if(original.empty() || std::any_of(original.begin(),original.end(),[](unsigned char c){return c<32;}))continue;
                auto rel=fs::path(wide(original));if(path(rel)==root_)continue;
                result.push_back({e.path().filename().wstring(),rel,wide(deleted),fs::is_directory(e.path()/L"payload")});
            }catch(const std::exception&){continue;}
        }
        std::sort(result.begin(),result.end(),[](auto& a,auto& b){return a.deleted>b.deleted;});return result;
    }
    fs::path restore(const std::wstring& id,Progress progress={}) {
        auto records=trashItems();auto it=std::find_if(records.begin(),records.end(),[&](auto& r){return r.id==id;});
        if(it==records.end())throw std::runtime_error("Recycle item is unavailable.");
        auto dest=path(it->original);if(fs::exists(dest))throw std::runtime_error("Restore would overwrite an existing item. Rename or move that item first.");
        auto record=trashRoot()/id;
        // Validate every payload child before moving a recycled directory back.
        FileStore bin(record);bin.checkTree(bin.path(L"payload"));
        report(progress,0,1);fs::create_directories(path(it->original.parent_path()));
        fs::rename(record/L"payload",dest);
        std::error_code ec;fs::remove(record/L"original.txt",ec);fs::remove(record,ec);
        report(progress,1,1);return it->original;
    }
    SearchResult search(const fs::path& start,const std::wstring& query,bool recursive,Progress progress={})const {
        SearchResult result;std::vector<fs::path> pending{path(start)};auto needle=lower(query);uint64_t scanned=0;
        while(!pending.empty()){
            auto folder=pending.back();pending.pop_back();
            std::error_code ec;fs::directory_iterator it(folder,ec),end;if(ec){++result.skipped;continue;}
            while(it!=end){
                auto p=it->path();
                try{
                    auto relative=p.lexically_relative(root_);path(relative);
                    if(recursive && fs::is_directory(p))pending.push_back(p);
                    if(lower(p.filename().wstring()).find(needle)!=std::wstring::npos){
                        if(result.paths.size()==5000){result.truncated=true;return result;}
                        result.paths.push_back(relative);
                    }
                }catch(const std::exception&){++result.skipped;}
                report(progress,++scanned,0);it.increment(ec);if(ec){++result.skipped;break;}
            }
        }
        return result;
    }
    void backup(const fs::path& destination,Progress progress={})const {
        auto dest=fs::absolute(destination).lexically_normal();
        if(within(dest,root_) || fs::exists(dest))throw std::runtime_error("Choose a new backup file outside Storage.");
        checkTree(root_);
        uint64_t total=0,done=0;for(auto& e:fs::recursive_directory_iterator(root_))total+=e.is_directory()?1:fs::file_size(e.path())+1;
        report(progress,0,total);
        struct Entry{std::string name;uint32_t crc,size,offset;};std::vector<Entry> entries;
        std::ofstream out(dest,std::ios::binary);if(!out)throw std::runtime_error("Cannot create backup.");
        auto u16=[&](uint16_t v){out.put(char(v));out.put(char(v>>8));};
        auto u32=[&](uint32_t v){u16(uint16_t(v));u16(uint16_t(v>>16));};
        auto position=[&](){auto n=out.tellp();if(n<0 || n>0xffffffffLL)throw std::runtime_error("ZIP exceeds the 4 GB limit.");return (uint32_t)n;};
        try {
            for(auto& item:fs::recursive_directory_iterator(root_)) {
                if(entries.size()>=65535)throw std::runtime_error("ZIP supports at most 65,535 entries.");
                auto name=item.path().lexically_relative(root_).generic_u8string();
                bool dir=item.is_directory();if(dir)name+='/';
                if(name.size()>65535)throw std::runtime_error("ZIP filename is too long.");
                uint64_t size=dir?0:fs::file_size(item.path());if(size>0xffffffffULL)throw std::runtime_error("ZIP file exceeds 4 GB.");
                Entry e{name,0,(uint32_t)size,position()};
                u32(0x04034b50);u16(20);u16(0x808);u16(0);u16(0);u16(33);u32(0);u32(0);u32(0);u16((uint16_t)name.size());u16(0);out.write(name.data(),name.size());
                uint32_t crc=0xffffffff;uint64_t written=0;
                if(!dir) {
                    std::ifstream in(item.path(),std::ios::binary);if(!in)throw std::runtime_error("Cannot read a file during backup.");
                    char buffer[65536];while(in.read(buffer,sizeof buffer) || in.gcount()) {
                        auto count=in.gcount();written+=count;out.write(buffer,count);done+=(uint64_t)count;report(progress,done,total);
                        for(std::streamsize i=0;i<count;++i){crc^=(unsigned char)buffer[i];for(int j=0;j<8;++j)crc=(crc>>1)^(0xedb88320u& (0u-(crc&1)));}
                    }
                    if(!in.eof() || written!=size)throw std::runtime_error("File changed or could not be read during backup.");
                }
                e.crc=crc^0xffffffff;u32(0x08074b50);u32(e.crc);u32(e.size);u32(e.size);entries.push_back(e);report(progress,++done,total);
            }
            uint32_t start=position();
            for(auto& e:entries){u32(0x02014b50);u16(20);u16(20);u16(0x808);u16(0);u16(0);u16(33);u32(e.crc);u32(e.size);u32(e.size);u16((uint16_t)e.name.size());u16(0);u16(0);u16(0);u16(0);u32(e.name.back()=='/'?16:0);u32(e.offset);out.write(e.name.data(),e.name.size());}
            uint32_t length=position()-start;u32(0x06054b50);u16(0);u16(0);u16((uint16_t)entries.size());u16((uint16_t)entries.size());u32(length);u32(start);u16(0);
            out.flush();if(!out)throw std::runtime_error("Backup write failed.");out.close();
        }catch(...){out.close();std::error_code ec;fs::remove(dest,ec);throw;}
    }
};
