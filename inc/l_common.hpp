#pragma once

#include <cstddef>
#include <cstring>

namespace lain {
// -------------------------------------------------------
// please don't look at this file it's embarrasing
// -------------------------------------------------------

// who do you think you are?
constexpr static std::size_t CompileTimeStringLength(char const* str) {
  std::size_t len{0};
  while (*str++) {
    ++len;
  }
  return len;
}

constexpr static int fnv1a(char const* str, std::size_t const len) {
  int constexpr prime{0x01000193}; // FNV-1a 32-bit prime
  unsigned int hash{0x811c9dc5};   // FNV-1a 32-bit offset basis
  for (std::size_t i{0}; i < len; ++i) {
    hash ^= static_cast<unsigned int>(str[i]);
    hash *= prime;
  }
  return hash;
};

// your stupid c++ side has come to light
int constexpr gridAxisLineShaderId{fnv1a("GridAxisLine", CompileTimeStringLength("GridAxisLine"))};

int constexpr gridAxisCheckerboardShaderId{
    fnv1a("GridAxisCheckerboard", CompileTimeStringLength("GridAxisCheckerboard"))};

int constexpr levelEditorModelWithTextureShaderId{
    fnv1a("LevelEditorModelWithTexture", CompileTimeStringLength("LevelEditorModelWithTexture"))};

int constexpr levelEditorModelWithoutTextureShaderId{
    fnv1a("LevelEditorModelWithoutTextureShader",
          CompileTimeStringLength("LevelEditorModelWithoutTextureShader"))};

int constexpr cursorId{fnv1a("Cursor", CompileTimeStringLength("Cursor"))};

} // namespace lain
