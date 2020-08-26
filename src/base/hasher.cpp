//--------------------------------------------------------------------------------------------------
// Copyright (c) 2018 Marcus Geelnard
//
// This software is provided 'as-is', without any express or implied warranty. In no event will the
// authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose, including commercial
// applications, and to alter it and redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not claim that you wrote
//     the original software. If you use this software in a product, an acknowledgment in the
//     product documentation would be appreciated but is not required.
//
//  2. Altered source versions must be plainly marked as such, and must not be misrepresented as
//     being the original software.
//
//  3. This notice may not be removed or altered from any source distribution.
//--------------------------------------------------------------------------------------------------

#include <base/file_utils.hpp>
#include <base/hasher.hpp>

#include <cstring>

#include <algorithm>
#include <stdexcept>

#define XXH_INLINE_ALL
#include <xxHash/xxhash.h>

namespace bcache {
namespace {
bool is_ar_data(const std::string& data) {
  const char AR_SIGNATURE[] = {0x21, 0x3c, 0x61, 0x72, 0x63, 0x68, 0x3e, 0x0a};
  return (data.size() >= 8 && std::equal(data.cbegin(), data.cbegin() + 8, &AR_SIGNATURE[0]));
}
}  // namespace

const std::string hasher_t::hash_t::as_string() const {
  static const char digits[17] = "0123456789abcdef";
  std::string result(SIZE * 2, '0');
  for (size_t i = 0; i < SIZE; ++i) {
    result[i * 2] = digits[m_data[i] >> 4];
    result[i * 2 + 1] = digits[m_data[i] & 0x0fu];
  }
  return result;
}

hasher_t::hash_t hasher_t::final() {
  const auto digest = XXH3_128bits_digest(reinterpret_cast<XXH3_state_t*>(m_state));
  static_assert(sizeof(digest) == hash_t::SIZE, "Unexpected digest size.");
  hash_t result;
  std::memcpy(result.data(), &digest, hash_t::SIZE);
  return result;
}

hasher_t::hasher_t() {
  m_state = XXH3_createState();
  if (!m_state || XXH3_128bits_reset(reinterpret_cast<XXH3_state_t*>(m_state)) == XXH_ERROR) {
    throw std::runtime_error("Hash init failure.");
  }
}

hasher_t::hasher_t(const hasher_t& other) {
  m_state = XXH3_createState();
  if (!m_state) {
    throw std::runtime_error("Hash init failure.");
  }
  XXH3_copyState(reinterpret_cast<XXH3_state_t*>(m_state),
                 reinterpret_cast<XXH3_state_t*>(other.m_state));
}

hasher_t::~hasher_t() {
  XXH3_freeState(reinterpret_cast<XXH3_state_t*>(m_state));
}

void hasher_t::update(const void* data, const size_t size) {
  if (XXH3_128bits_update(reinterpret_cast<XXH3_state_t*>(m_state), data, size) == XXH_ERROR) {
    throw std::runtime_error("Hash update failure.");
  }
}

void hasher_t::update(const std::map<std::string, std::string>& data) {
  // Note: This is guaranteed by the C++ standard to iterate over the elements in ascending key
  // order, so the hash will always be the same for the same map contents.
  for (const auto& item : data) {
    update(item.first);
    update(item.second);
  }
}

void hasher_t::update_from_file(const std::string& path) {
  // TODO(m): Investigate if using buffered input gives better performance (at least it should use
  // less memory, and it should be nicer to the CPU caches).
  const auto file_data = file::read(path);
  update(file_data);
}

void hasher_t::update_from_file_deterministic(const std::string& path) {
  const auto file_data = file::read(path);
  if (is_ar_data(file_data)) {
    update_from_ar_data(file_data);
  } else {
    update(file_data);
  }
}

void hasher_t::update_from_ar_data(const std::string& data) {
  try {
    size_t pos = 8;
    while (pos < data.size()) {
      if ((pos + 60) > data.size()) {
        throw std::runtime_error("Invalid AR file header.");
      }

      // Hash all parts of the header except the timestamp.
      // See: https://en.wikipedia.org/wiki/Ar_(Unix)#File_header
      update(&data[pos], 16);
      update(&data[pos + 28], 32);

      // Hash the file data.
      const auto file_size = std::stoll(data.substr(pos + 48, 10));
      if (file_size < 0 || (pos + static_cast<size_t>(file_size)) > data.size()) {
        throw std::runtime_error("Invalid file size.");
      }
      update(&data[pos + 60], file_size);

      // Skip to the next file header.
      // Note: File data is padded to an even number of bytes.
      pos += 60 + file_size + (file_size & 1);
    }
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("Unable to parse an AR format file: ") +
                             std::string(e.what()));
  }
}

}  // namespace bcache
