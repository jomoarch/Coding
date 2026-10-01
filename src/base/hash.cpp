#include "base/hash.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <string>
#include <utility>

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

namespace hash {

namespace {

constexpr std::size_t kDigestBytes = 32;

void close(void *&state, void *&object, void *&algorithm) {
  if (state != nullptr) {
    ::BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(state));
    state = nullptr;
  }
  if (algorithm != nullptr) {
    ::BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(algorithm),
                                   0);
    algorithm = nullptr;
  }
  if (object != nullptr) {
    ::HeapFree(::GetProcessHeap(), 0, object);
    object = nullptr;
  }
}

} // namespace

Sha256::Sha256() {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (!NT_SUCCESS(::BCryptOpenAlgorithmProvider(
          &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
    return;

  DWORD object_size = 0;
  DWORD written = 0;
  if (!NT_SUCCESS(::BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                      reinterpret_cast<PUCHAR>(&object_size),
                                      sizeof(object_size), &written, 0)) ||
      object_size == 0) {
    ::BCryptCloseAlgorithmProvider(algorithm, 0);
    return;
  }

  void *object = ::HeapAlloc(::GetProcessHeap(), 0, object_size);
  if (object == nullptr) {
    ::BCryptCloseAlgorithmProvider(algorithm, 0);
    return;
  }

  BCRYPT_HASH_HANDLE handle = nullptr;
  if (!NT_SUCCESS(::BCryptCreateHash(algorithm, &handle,
                                     static_cast<PUCHAR>(object), object_size,
                                     nullptr, 0, 0))) {
    ::BCryptCloseAlgorithmProvider(algorithm, 0);
    ::HeapFree(::GetProcessHeap(), 0, object);
    return;
  }

  state_ = handle;
  object_ = object;
  algorithm_ = algorithm;
}

Sha256::~Sha256() { close(state_, object_, algorithm_); }

Sha256::Sha256(Sha256 &&other) noexcept
    : state_(other.state_), object_(other.object_),
      algorithm_(other.algorithm_) {
  other.state_ = nullptr;
  other.object_ = nullptr;
  other.algorithm_ = nullptr;
}

Sha256 &Sha256::operator=(Sha256 &&other) noexcept {
  if (this != &other) {
    close(state_, object_, algorithm_);
    state_ = other.state_;
    object_ = other.object_;
    algorithm_ = other.algorithm_;
    other.state_ = nullptr;
    other.object_ = nullptr;
    other.algorithm_ = nullptr;
  }
  return *this;
}

void Sha256::write(const void *data, std::size_t size) {
  if (state_ == nullptr || size == 0)
    return;
  if (!NT_SUCCESS(::BCryptHashData(
          state_, static_cast<PUCHAR>(const_cast<void *>(data)),
          static_cast<ULONG>(size), 0)))
    close(state_, object_, algorithm_);
}

void Sha256::write(std::string_view text) { write(text.data(), text.size()); }

void Sha256::write_u64(std::uint64_t value) {
  unsigned char bytes[8];
  for (int i = 0; i < 8; ++i)
    bytes[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xFFu);
  write(bytes, sizeof(bytes));
}

std::string Sha256::hex() {
  if (state_ == nullptr)
    return {};

  unsigned char digest[kDigestBytes] = {};
  const NTSTATUS finished =
      ::BCryptFinishHash(state_, digest, static_cast<ULONG>(sizeof(digest)), 0);
  close(state_, object_, algorithm_);
  if (!NT_SUCCESS(finished))
    return {};

  static const char *digits = "0123456789abcdef";
  std::string out;
  out.reserve(kDigestBytes * 2);
  for (unsigned char byte : digest) {
    out.push_back(digits[byte >> 4]);
    out.push_back(digits[byte & 0x0Fu]);
  }
  return out;
}

} // namespace hash
