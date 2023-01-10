/*
*   RT64VK
*/

#include "rt64_common.h"


namespace RT64 {
	FILE* GlobalLogFile = nullptr;
	std::string GlobalLastError = "";
};

DLEXPORT const char* RT64_GetLastError() {
	return RT64::GlobalLastError.c_str();
}