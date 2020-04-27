
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>

static BOOL signal_handler(DWORD) {
  ExitProcess(0);
  return TRUE;
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR commandline, int) {
  auto pid = static_cast<DWORD>(std::atoi(commandline));
  if (!pid)
    return 1;

  if (AttachConsole(pid) == FALSE)
    return 2;

  if (SetConsoleCtrlHandler(signal_handler, TRUE) == FALSE)
    return 3;

  if (GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0) == FALSE)
    return 4;

  Sleep(1000);

  return 5;
}
