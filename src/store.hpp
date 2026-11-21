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
namespace fs = std::filesystem;

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
    void transfer(const fs::path& relative,const fs::path& destination,const std::wstring& name,bool move) {
        auto src=path(relative),dst=fresh(destination,name);
        if(src==root_)throw std::runtime_error("The storage root cannot be moved or copied.");
        checkTree(src);
        if(fs::is_directory(src) && within(dst,src))throw std::runtime_error("Choose a destination outside the source folder.");
        if(move)fs::rename(src,dst);else fs::copy(src,dst,fs::copy_options::recursive);
    }
    void remove(const fs::path& relative) {
        auto p=path(relative);if(p==root_)throw std::runtime_error("The storage root cannot be deleted.");
        checkTree(p);fs::remove_all(p);
    }
    void backup(const fs::path& destination)const {
        auto dest=fs::absolute(destination).lexically_normal();
        if(within(dest,root_) || fs::exists(dest))throw std::runtime_error("Choose a new backup file outside Storage.");
        checkTree(root_);
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
                        auto count=in.gcount();written+=count;out.write(buffer,count);
                        for(std::streamsize i=0;i<count;++i){crc^=(unsigned char)buffer[i];for(int j=0;j<8;++j)crc=(crc>>1)^(0xedb88320u& (0u-(crc&1)));}
                    }
                    if(!in.eof() || written!=size)throw std::runtime_error("File changed or could not be read during backup.");
                }
                e.crc=crc^0xffffffff;u32(0x08074b50);u32(e.crc);u32(e.size);u32(e.size);entries.push_back(e);
            }
            uint32_t start=position();
            for(auto& e:entries){u32(0x02014b50);u16(20);u16(20);u16(0x808);u16(0);u16(0);u16(33);u32(e.crc);u32(e.size);u32(e.size);u16((uint16_t)e.name.size());u16(0);u16(0);u16(0);u16(0);u32(e.name.back()=='/'?16:0);u32(e.offset);out.write(e.name.data(),e.name.size());}
            uint32_t length=position()-start;u32(0x06054b50);u16(0);u16(0);u16((uint16_t)entries.size());u16((uint16_t)entries.size());u32(length);u32(start);u16(0);
            out.flush();if(!out)throw std::runtime_error("Backup write failed.");out.close();
        }catch(...){out.close();std::error_code ec;fs::remove(dest,ec);throw;}
    }
};
