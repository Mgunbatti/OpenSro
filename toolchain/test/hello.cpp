#include <windows.h>
#include <map>
#include <string>
#include <cstdio>

int main()
{
    std::map<int, std::string> m;
    m[1] = "vc8";
    std::printf("%s _MSC_FULL_VER=%d sizeof(std::map)=%u\n", m[1].c_str(), _MSC_FULL_VER, (unsigned)sizeof(m));
    return GetCurrentProcessId() ? 0 : 1;
}
