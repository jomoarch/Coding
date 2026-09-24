#ifndef WIN_ERROR_HPP
#define WIN_ERROR_HPP

#include <windows.h>

#include <string>

namespace win {

std::string error_string(DWORD code);

std::string last_error_string();

} // namespace win

#endif // WIN_ERROR_HPP