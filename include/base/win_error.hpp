#ifndef WIN_ERROR_HPP
#define WIN_ERROR_HPP

#include <windows.h>

#include <string>
namespace coding {

namespace win {

std::string error_string(DWORD code);

std::string last_error_string();

} // namespace win

} // namespace coding

#endif // WIN_ERROR_HPP