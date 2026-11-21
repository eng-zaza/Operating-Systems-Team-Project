#include "../src/store.hpp"
#include <iostream>
#include <functional>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void rejects(std::function<void()> f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,"Expected operation to be rejected");}
int main(){
    auto base=fs::temp_directory_path()/(L"FileDesk-tests-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        FileStore s(base/L"storage");
        s.create(L".",L"notes.txt",false);s.write(L"notes.txt",L"OS project\r\nمرحبا");require(FileStore(s.root()).read(L"notes.txt")==L"OS project\r\nمرحبا","Persistence/Unicode failed");
        s.create(L".",L"Documents",true);s.transfer(L"notes.txt",L"Documents",L"copy.txt",false);s.transfer(L"notes.txt",L".",L"renamed.txt",true);require(s.read(L"Documents/copy.txt")==s.read(L"renamed.txt"),"Copy failed");require(!fs::exists(s.path(L"notes.txt")),"Move left source");
        rejects([&]{s.create(L".",L"renamed.txt",false);});require(!s.read(L"renamed.txt").empty(),"Duplicate destroyed data");
        for(auto name:{L"../escape",L"CON",L"a/b",L"bad.",L""})rejects([&]{s.create(L".",name,false);});
        rejects([&]{s.path(L"../outside");});rejects([&]{s.path(L"C:\\outside");});rejects([&]{s.remove(L".");});
        rejects([&]{s.transfer(L"Documents",L"Documents",L"child",false);});
        s.create(L".",L"empty",true);s.backup(base/L"backup.zip");require(fs::file_size(base/L"backup.zip")>100,"Backup empty");
        rejects([&]{s.backup(base/L"backup.zip");});rejects([&]{s.backup(s.root()/L"bad.zip");});
        s.create(L".",L"binary.bin",false);std::ofstream(s.path(L"binary.bin"),std::ios::binary).put('\xff');rejects([&]{s.read(L"binary.bin");});
        s.remove(L"Documents");require(!fs::exists(s.path(L"Documents")),"Directory deletion failed");
        // Leave a copy for an independent ZIP-format validator.
        fs::copy_file(base/L"backup.zip",L"build/test-backup.zip",fs::copy_options::overwrite_existing);
        fs::remove_all(base);std::cout<<"PASS: lifecycle, Unicode persistence, duplicate protection, invalid names, path boundaries, root protection, recursive copy rejection, ZIP backup, binary rejection, deletion.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";std::error_code ec;fs::remove_all(base,ec);return 1;}
}
