// Implementação das bibliotecas stb (domínio público), compilada à parte e sem avisos de terceiros.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_WINDOWS_UTF8
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include "stb_image_write.h"
