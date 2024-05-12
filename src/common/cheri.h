#ifndef __CHERI_H__
#define __CHERI_H__

#include <stdint.h>
#include <ostream>
#include <sys/param.h>

namespace cheri 
{
#if defined(__CHERI_PURE_CAPABILITY__) && __CheriBSD_version <= 20220828
  inline std::ostream &operator<<(std::ostream &out, uintptr_t val)
  {
    return out << static_cast<size_t>(val);
  }

  inline std::ostream &operator<<(std::ostream &out, intptr_t val)
  {
    return out << static_cast<ssize_t>(val);
  }
#endif // __CHERI_PURE_CAPABILITY__ && __CheriBSD_version <= 20220828

  template<typename T>
  struct is_intcap {
    constexpr static bool value = std::is_same<intptr_t, T>::value || std::is_same<uintptr_t, T>::value;
  };
}

#endif
