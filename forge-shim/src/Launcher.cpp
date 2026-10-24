#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <cstdio>
#include <filesystem>
#include <dbghelp.h>
#pragma comment(lib,"dbghelp.lib")
static uintptr_t Module(DWORD pid,const std::wstring& path){auto s=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);MODULEENTRY32W m{sizeof(m)};uintptr_t result=0;if(Module32FirstW(s,&m))do{if(_wcsicmp(m.szExePath,path.c_str())==0){result=reinterpret_cast<uintptr_t>(m.modBaseAddr);break;}}while(Module32NextW(s,&m));CloseHandle(s);return result;}
int wmain(int argc,wchar_t** argv){
 setvbuf(stdout,nullptr,_IONBF,0);
 if(argc!=3){fwprintf(stderr,L"Usage: ForgeStandalone.exe <dev-folder> <shim-dll>\n");return 2;}
 std::wstring root=argv[1],shim=argv[2],exe=root+L"\\halo5forge.exe",cmd=L"\""+exe+L"\"";
 root=std::filesystem::canonical(root).wstring();shim=std::filesystem::canonical(shim).wstring();
 if(_wcsicmp(root.c_str(),L"C:\\Games\\H5Forge")==0){fputs("Refusing original/Guardians folder\n",stderr);return 2;}
 PROCESS_INFORMATION pi{};STARTUPINFOW si{sizeof(si)};
 puts("creating isolated Forge process");
 if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS,nullptr,root.c_str(),&si,&pi)){printf("CreateProcess error=%lu\n",GetLastError());return 3;}
 printf("child pid=%lu\n",pi.dwProcessId);
 HANDLE remoteThread=nullptr;DWORD injectTid=0;int stage=0;bool alive=true;void* remote=nullptr;ULONGLONG start=GetTickCount64();uintptr_t entryAddress=0;BYTE entryByte=0;
 bool snapshotSaved=false;
 while(alive && GetTickCount64()-start<180000){
  if(!snapshotSaved && GetTickCount64()-start>20000){wchar_t path[32768];if(GetEnvironmentVariableW(L"FORGE_WAIT_DUMP",path,32768)){HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file!=INVALID_HANDLE_VALUE){BOOL ok=MiniDumpWriteDump(pi.hProcess,pi.dwProcessId,file,MiniDumpWithThreadInfo,nullptr,nullptr,nullptr);printf("waiting snapshot saved=%d error=%lu\n",ok,GetLastError());CloseHandle(file);}}snapshotSaved=true;}
  DEBUG_EVENT e{};if(!WaitForDebugEvent(&e,500))continue;DWORD status=DBG_CONTINUE;
  if(e.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){
   entryAddress=reinterpret_cast<uintptr_t>(e.u.CreateProcessInfo.lpStartAddress);SIZE_T count;BYTE breakpoint=0xcc;
   ReadProcessMemory(pi.hProcess,reinterpret_cast<void*>(entryAddress),&entryByte,1,&count);
   WriteProcessMemory(pi.hProcess,reinterpret_cast<void*>(entryAddress),&breakpoint,1,&count);FlushInstructionCache(pi.hProcess,reinterpret_cast<void*>(entryAddress),1);
   if(e.u.CreateProcessInfo.hFile)CloseHandle(e.u.CreateProcessInfo.hFile);
  }
  if(e.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT && e.u.LoadDll.hFile)CloseHandle(e.u.LoadDll.hFile);
  if(e.dwDebugEventCode==OUTPUT_DEBUG_STRING_EVENT){auto d=e.u.DebugString;SIZE_T got=0;if(d.fUnicode){std::wstring b(d.nDebugStringLength,L'\0');ReadProcessMemory(pi.hProcess,d.lpDebugStringData,b.data(),b.size()*2,&got);wprintf(L"%s",b.c_str());}else{std::string b(d.nDebugStringLength,'\0');ReadProcessMemory(pi.hProcess,d.lpDebugStringData,b.data(),b.size(),&got);printf("%s",b.c_str());}fflush(stdout);}
  if(e.dwDebugEventCode==EXCEPTION_DEBUG_EVENT){auto x=e.u.Exception.ExceptionRecord;printf("exception=%08lX first=%lu address=%p\n",x.ExceptionCode,e.u.Exception.dwFirstChance,x.ExceptionAddress);
   if(x.ExceptionCode==0xe06d7363 || !e.u.Exception.dwFirstChance){wchar_t path[32768];if(GetEnvironmentVariableW(L"FORGE_DUMP_PATH",path,32768)){HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,e.dwThreadId);CONTEXT ctx{};ctx.ContextFlags=CONTEXT_ALL;if(thread&&GetThreadContext(thread,&ctx)){EXCEPTION_POINTERS pointers{&x,&ctx};MINIDUMP_EXCEPTION_INFORMATION info{e.dwThreadId,&pointers,FALSE};HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file!=INVALID_HANDLE_VALUE){MiniDumpWriteDump(pi.hProcess,pi.dwProcessId,file,MiniDumpNormal,&info,nullptr,nullptr);CloseHandle(file);}}if(thread)CloseHandle(thread);}}
   if(x.ExceptionCode==EXCEPTION_BREAKPOINT && stage==0 && reinterpret_cast<uintptr_t>(x.ExceptionAddress)==entryAddress){
    SIZE_T count;WriteProcessMemory(pi.hProcess,reinterpret_cast<void*>(entryAddress),&entryByte,1,&count);FlushInstructionCache(pi.hProcess,reinterpret_cast<void*>(entryAddress),1);
    CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;GetThreadContext(pi.hThread,&context);context.Rip=entryAddress;SetThreadContext(pi.hThread,&context);
    SuspendThread(pi.hThread);remote=VirtualAllocEx(pi.hProcess,nullptr,(shim.size()+1)*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);SIZE_T written=0;
    if(!remote||!WriteProcessMemory(pi.hProcess,remote,shim.c_str(),(shim.size()+1)*2,&written)){puts("injection allocation failed");alive=false;}
    else{auto load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");HMODULE owner;GetModuleHandleExW(6,reinterpret_cast<LPCWSTR>(load),&owner);wchar_t ownerPath[MAX_PATH];GetModuleFileNameW(owner,ownerPath,MAX_PATH);auto remoteOwner=Module(pi.dwProcessId,ownerPath);
     auto address=remoteOwner+(reinterpret_cast<uintptr_t>(load)-reinterpret_cast<uintptr_t>(owner));remoteThread=CreateRemoteThread(pi.hProcess,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(address),remote,0,&injectTid);stage=1;if(!remoteThread){printf("LoadLibrary thread error=%lu\n",GetLastError());alive=false;}}
   }else if(x.ExceptionCode==EXCEPTION_BREAKPOINT && stage==0)status=DBG_CONTINUE;
   else status=DBG_EXCEPTION_NOT_HANDLED;
  }
  if(e.dwDebugEventCode==EXIT_THREAD_DEBUG_EVENT && e.dwThreadId==injectTid && stage==1){
   printf("LoadLibrary thread exit=%08lX\n",e.u.ExitThread.dwExitCode);CloseHandle(remoteThread);remoteThread=nullptr;VirtualFreeEx(pi.hProcess,remote,0,MEM_RELEASE);remote=nullptr;
   auto remoteModule=Module(pi.dwProcessId,shim);HMODULE local=LoadLibraryExW(shim.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
   auto init=local?GetProcAddress(local,"InitializeShim"):nullptr;
   if(!remoteModule||!init){puts("shim load/export missing");alive=false;}else{auto address=remoteModule+(reinterpret_cast<uintptr_t>(init)-reinterpret_cast<uintptr_t>(local));remoteThread=CreateRemoteThread(pi.hProcess,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(address),nullptr,0,&injectTid);stage=2;if(!remoteThread)alive=false;}if(local)FreeLibrary(local);
  }else if(e.dwDebugEventCode==EXIT_THREAD_DEBUG_EVENT && e.dwThreadId==injectTid && stage==2){printf("InitializeShim exit=%08lX\n",e.u.ExitThread.dwExitCode);CloseHandle(remoteThread);remoteThread=nullptr;if(e.u.ExitThread.dwExitCode){alive=false;}else{stage=3;ResumeThread(pi.hThread);}}
  if(e.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT){printf("process exit=%08lX\n",e.u.ExitProcess.dwExitCode);alive=false;}
  ContinueDebugEvent(e.dwProcessId,e.dwThreadId,status);
 }
 // This experiment launcher owns its child; terminate only that child on the diagnostic time limit.
 DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);if(code==STILL_ACTIVE){puts("diagnostic run ended; terminating owned child");TerminateProcess(pi.hProcess,0xdead);code=0xdead;}
 if(remoteThread)CloseHandle(remoteThread);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return stage==3 && code==0?0:4;
}

