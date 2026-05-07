#pragma once

#if defined(__cpp_impl_reflection) && __cpp_impl_reflection >= 202506L
#include <meta>
#define CBOR_TAGS_HAS_STD_REFLECTION 1
#else
#define CBOR_TAGS_HAS_STD_REFLECTION 0
#endif
