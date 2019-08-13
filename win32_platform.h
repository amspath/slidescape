#pragma once


// Types and exposed only to the win32 platform

void win32_diagnostic(const char* prefix);
void win32_message_box(HWND window_handle, const char* message);
