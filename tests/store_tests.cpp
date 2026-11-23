#include "../src/store.hpp"
#include "../src/clipboard.hpp"
#pragma comment(lib,"shell32.lib")
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
        // Recursive name search distinguishes nested matches from a local listing.
        s.create(L".",L"Projects",true);s.create(L"Projects",L"Nested",true);
        s.create(L"Projects/Nested",L"Meeting-NOTES.txt",false);s.write(L"Projects/Nested/Meeting-NOTES.txt",L"recover me");
        auto local=s.search(L"Projects",L"notes",false);require(local.paths.empty(),"Local search crossed folders");
        auto nested=s.search(L"Projects",L"notes",true);require(nested.paths.size()==1 && nested.paths[0]==fs::path(L"Projects/Nested/Meeting-NOTES.txt"),"Recursive search missed nested case-insensitive match");
        require(s.search(L"Projects",L"not-present",true).paths.empty(),"Unexpected search match");
        // Recycle records persist across instances; active search excludes them.
        auto first=s.recycle(L"Projects/Nested/Meeting-NOTES.txt");
        require(!fs::exists(s.path(L"Projects/Nested/Meeting-NOTES.txt")),"Recycle left active source");
        require(s.search(L".",L"Meeting",true).paths.empty(),"Search included recycled data");
        FileStore reopened(s.root());require(reopened.trashItems().size()==1,"Recycle metadata did not persist");
        // Restoring a child recreates missing parents and preserves the payload.
        s.remove(L"Projects");reopened.restore(first);
        require(s.read(L"Projects/Nested/Meeting-NOTES.txt")==L"recover me","Restore lost contents");
        require(reopened.trashItems().empty(),"Restore left a live recycle record");
        // Conflicts never replace an active file or consume the recycled item.
        auto conflict=s.recycle(L"Projects/Nested/Meeting-NOTES.txt");
        s.create(L"Projects/Nested",L"Meeting-NOTES.txt",false);s.write(L"Projects/Nested/Meeting-NOTES.txt",L"keep active");
        rejects([&]{s.restore(conflict);});require(s.read(L"Projects/Nested/Meeting-NOTES.txt")==L"keep active","Restore overwrote conflict");
        require(s.trashItems().size()==1,"Failed restore consumed payload");
        auto second=s.recycle(L"Projects/Nested/Meeting-NOTES.txt");require(second!=conflict,"Recycle IDs collided");
        s.restore(conflict);require(s.read(L"Projects/Nested/Meeting-NOTES.txt")==L"recover me","Wrong recycled version restored");
        // Entire folders restore with empty children and nested file contents.
        s.create(L"Projects",L"Empty",true);auto folder=s.recycle(L"Projects");s.restore(folder);
        require(fs::is_directory(s.path(L"Projects/Empty")),"Empty folder missing after restore");
        rejects([&]{s.recycle(L".");});rejects([&]{s.restore(L"../outside");});
        // Corrupt/hostile metadata is ignored, not treated as a restore path.
        auto corrupt=s.trashRoot()/L"corrupt";fs::create_directory(corrupt);
        std::ofstream(corrupt/L"original.txt")<<"../outside\nunknown\n";std::ofstream(corrupt/L"payload")<<"bad";
        auto records=s.trashItems();require(records.size()==1 && records[0].id==second,"Invalid metadata accepted");
        auto crlf=s.trashRoot()/L"windows-newlines";fs::create_directory(crlf);
        {std::ofstream m(crlf/L"original.txt",std::ios::binary);m<<"crlf-restored.txt\r\n2026-11-21 18:00\r\n";}
        std::ofstream(crlf/L"payload")<<"CRLF record";s.restore(L"windows-newlines");require(s.read(L"crlf-restored.txt")==L"CRLF record","CRLF metadata restore failed");
        // Chunk progress is monotonic and failed operations remove partial output.
        s.create(L".",L"large.bin",false);{std::ofstream out(s.path(L"large.bin"),std::ios::binary);std::string block(65536,'x');for(int i=0;i<12;++i)out.write(block.data(),block.size());}
        uint64_t last=0,totalSeen=0;int updates=0;
        s.transfer(L"large.bin",L".",L"large-copy.bin",false,[&](uint64_t done,uint64_t total){require(done>=last && done<=total,"Copy progress invalid");last=done;totalSeen=total;++updates;});
        require(updates>3 && last==totalSeen,"Copy did not report chunk progress");
        require(fs::file_size(s.path(L"large-copy.bin"))==12*65536,"Large copy truncated");
        rejects([&]{s.transfer(L"large.bin",L".",L"partial.bin",false,[](uint64_t d,uint64_t){if(d>0)throw std::runtime_error("injected failure");});});
        require(!fs::exists(s.path(L"partial.bin")),"Failed copy left partial file");
        rejects([&]{s.backup(base/L"partial.zip",[](uint64_t d,uint64_t){if(d>0)throw std::runtime_error("injected failure");});});
        require(!fs::exists(base/L"partial.zip"),"Failed backup left partial archive");
        last=0;totalSeen=0;s.backup(base/L"enhanced.zip",[&](uint64_t d,uint64_t t){require(d>=last && d<=t,"ZIP progress invalid");last=d;totalSeen=t;});require(last==totalSeen,"ZIP progress incomplete");
        fs::copy_file(base/L"enhanced.zip",L"build/enhanced-backup.zip",fs::copy_options::overwrite_existing);
        // Explorer-style paste never overwrites an existing destination.
        s.create(L".",L"Inbox",true);s.create(L".",L"Outbox",true);
        s.create(L"Inbox",L"one.txt",false);s.write(L"Inbox/one.txt",L"one");
        s.create(L"Inbox",L"two.txt",false);s.write(L"Inbox/two.txt",L"two");
        auto copied=s.paste({s.path(L"Inbox/one.txt"),s.path(L"Inbox/two.txt")},L"Outbox",false);
        require(copied.completed==2 && copied.errors.empty(),"Multi-file copy failed");
        s.paste({s.path(L"Inbox/one.txt")},L"Outbox",false);
        require(s.read(L"Outbox/one - Copy.txt")==L"one" && s.read(L"Outbox/one.txt")==L"one","Copy collision replaced original");
        s.paste({s.path(L"Inbox/one.txt")},L"Outbox",false);require(fs::exists(s.path(L"Outbox/one - Copy (2).txt")),"Repeated copy naming failed");
        auto blockedMove=s.paste({s.path(L"Inbox/one.txt")},L"Outbox",true);
        require(blockedMove.completed==0 && blockedMove.errors.size()==1 && fs::exists(s.path(L"Inbox/one.txt")),"Move conflict lost source");
        s.create(L".",L"Moved",true);auto moved=s.paste({s.path(L"Inbox/one.txt"),s.path(L"Inbox/two.txt")},L"Moved",true);
        require(moved.completed==2 && !fs::exists(s.path(L"Inbox/one.txt")) && s.read(L"Moved/one.txt")==L"one","Cut/paste failed");
        auto noOp=s.paste({s.path(L"Moved/one.txt")},L"Moved",true);require(noOp.skipped==1 && noOp.errors.empty(),"Same-folder cut should be harmless");
        auto nestedSelection=s.paste({s.path(L"Moved"),s.path(L"Moved/one.txt")},L"Inbox",false);
        require(nestedSelection.completed==1 && nestedSelection.skipped==1 && s.read(L"Inbox/Moved/one.txt")==L"one","Parent/child selection duplicated contents");
        auto selfCopy=s.paste({s.path(L"Moved")},L"Moved",false);require(selfCopy.completed==0 && !selfCopy.errors.empty(),"Folder copied into itself");
        // Import from outside Storage copies only, even when moving was requested.
        fs::create_directory(base/L"external");std::ofstream(base/L"external/import.txt")<<"outside";
        auto imported=s.paste({base/L"external/import.txt"},L"Inbox",false);require(imported.completed==1 && fs::exists(base/L"external/import.txt") && s.read(L"Inbox/import.txt")==L"outside","External import failed");
        rejects([&]{s.paste({base/L"external/import.txt"},L"Outbox",true);});require(fs::exists(base/L"external/import.txt"),"External cut removed original");
        auto mixed=s.paste({base/L"external/missing.txt",s.path(L"Moved/two.txt")},L"Inbox",false);require(mixed.completed==1 && mixed.errors.size()==1,"Partial batch did not report success/failure separately");
        // Exercise CF_HDROP encoding without replacing the user's system clipboard.
        std::vector<fs::path> paths={s.path(L"Moved/one.txt"),s.path(L"Moved/two.txt"),base/L"مرحبا.txt"};
        auto drop=fileDropMemory(paths);auto decoded=readFileDrop((HDROP)drop);GlobalFree(drop);require(decoded==paths,"Explorer file-list encoding failed");
        // Leave a copy for an independent ZIP-format validator.
        fs::copy_file(base/L"backup.zip",L"build/test-backup.zip",fs::copy_options::overwrite_existing);
        fs::remove_all(base);std::cout<<"PASS: storage, search, recycle recovery, progress, failure cleanup; multi-file Copy/Cut/Paste; copy-name collisions; move conflicts; external import; CF_HDROP round-trip.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";std::error_code ec;fs::remove_all(base,ec);return 1;}
}
