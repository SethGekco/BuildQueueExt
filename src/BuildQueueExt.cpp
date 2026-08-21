#include "BuildQueueExt.h"

#include <Phobos.h>
#include <Syringe.h>
#include <Utilities/Patch.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

HANDLE BuildQueueExtDLL::hInstance = nullptr;

char BuildQueueExtDLL::readBuffer[BuildQueueExtDLL::readLength];
wchar_t BuildQueueExtDLL::wideBuffer[BuildQueueExtDLL::readLength];

void BuildQueueExtDLL::ExeRun()
{
    Patch::ApplyStatic();
}

bool __stdcall DllMain(HANDLE hInstance, DWORD dwReason, LPVOID)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        BuildQueueExtDLL::hInstance = hInstance;
        Phobos::hInstance = hInstance; // needed by Patch::ApplyStatic
    }
    return true;
}

SYRINGE_HANDSHAKE(pInfo)
{
    pInfo->Message = const_cast<char*>("BuildQueueExt");
    return S_OK;
}

// Hook into the game's main loop start so our patches apply at the right time
DEFINE_HOOK(0x7CD810, BQExt_ExeRun, 0x9)
{
    BuildQueueExtDLL::ExeRun();
    return 0;
}

// Trigger deferred debug log flush after command line parse
DEFINE_HOOK(0x52F639, BQExt_CmdLineParse, 0x5)
{
    Debug::LogDeferredFinalize();
    return 0;
}
