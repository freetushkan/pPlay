#ifndef PPLAY_USBFS_H
#define PPLAY_USBFS_H

#ifdef __SWITCH__

#include <string>
#include <vector>
#include <usbhsfs.h>

void usbInit();
void usbExit();
bool usbWaitForDevice(int timeoutSeconds = 5);
std::string usbGetFirstMountName();
std::vector<std::string> usbGetMountNames();

#endif

#endif //PPLAY_USBFS_H
