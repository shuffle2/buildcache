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

#include <base/compressor.hpp>
#include <base/serializer_utils.hpp>
#include <cache/cache_entry.hpp>

#include <stdexcept>

namespace bcache {
namespace {
// The version of the entry file serialization data format.
const int32_t ENTRY_DATA_FORMAT_VERSION = 4;

std::vector<std::string> v2_files_to_vector(const std::map<std::string, std::string>& files) {
  std::vector<std::string> result;
  for (const auto& file : files) {
    result.emplace_back(file.first);
  }
  return result;
}

std::string from_string(const hasher_t::hash_t& x) {
  return std::string(reinterpret_cast<const char*>(x.data()), hasher_t::hash_t::SIZE);
}

hasher_t::hash_t to_string(const std::string& data, std::string::size_type& pos) {
  const auto size = hasher_t::hash_t::SIZE;
  if (pos + size > data.size()) {
    throw std::runtime_error("Premature end of serialized data stream.");
  }
  hasher_t::hash_t result;
  memcpy(result.data(), &data[pos], size);
  pos += size;
  return result;
}

std::string from_map(const dependency_records_t& x) {
  auto result = serialize::from_int(static_cast<int32_t>(x.size()));
  for (const auto& element : x) {
    result += (serialize::from_string(element.first) + from_string(element.second));
  }
  return result;
}

dependency_records_t to_map(const std::string& data, std::string::size_type& pos) {
  const auto size = serialize::to_int(data, pos);
  dependency_records_t result;
  for (int32_t i = 0; i < size; ++i) {
    const auto key = serialize::to_string(data, pos);
    const auto value = to_string(data, pos);
    result[key] = value;
  }
  return result;
}

}  // namespace

cache_entry_t::cache_entry_t() {
}

cache_entry_t::cache_entry_t(const std::vector<std::string>& file_ids,
                             const dependency_records_t& dependencies,
                             const cache_entry_t::comp_mode_t compression_mode,
                             const std::string& std_out,
                             const std::string& std_err,
                             const int return_code)
    : m_file_ids(file_ids),
      m_dependency_records(dependencies),
      m_compression_mode(compression_mode),
      m_std_out(std_out),
      m_std_err(std_err),
      m_return_code(return_code),
      m_valid(true) {
}

std::string cache_entry_t::serialize() const {
  std::string data = serialize::from_int(ENTRY_DATA_FORMAT_VERSION);
  data += serialize::from_int(static_cast<int>(m_compression_mode));
  data += serialize::from_vector(m_file_ids);
  if (m_compression_mode == comp_mode_t::ALL) {
    data += serialize::from_string(comp::compress(m_std_out));
    data += serialize::from_string(comp::compress(m_std_err));
  } else {
    data += serialize::from_string(m_std_out);
    data += serialize::from_string(m_std_err);
  }
  data += serialize::from_int(static_cast<int32_t>(m_return_code));
  data += from_map(m_dependency_records);
  return data;
}

cache_entry_t cache_entry_t::deserialize(const std::string& data) {
  std::string::size_type pos = 0;

  // Read and check the format version.
  int32_t format_version = serialize::to_int(data, pos);
  if (format_version > ENTRY_DATA_FORMAT_VERSION) {
    throw std::runtime_error("Unsupported serialization format version.");
  }

  // De-serialize the entry.
  const auto compression_mode = (format_version >= 2)
                                    ? static_cast<comp_mode_t>(serialize::to_int(data, pos))
                                    : comp_mode_t::NONE;
  const auto file_ids = (format_version >= 3) ? serialize::to_vector(data, pos)
                                              : v2_files_to_vector(serialize::to_map(data, pos));
  auto std_out = serialize::to_string(data, pos);
  auto std_err = serialize::to_string(data, pos);
  const auto return_code = static_cast<int>(serialize::to_int(data, pos));
  dependency_records_t dependencies;
  if (format_version >= 4) {
    dependencies = to_map(data, pos);
  }

  // Optionally decompress the program output.
  if (compression_mode == comp_mode_t::ALL) {
    std_out = comp::decompress(std_out);
    std_err = comp::decompress(std_err);
  }

  return cache_entry_t(file_ids, dependencies, compression_mode, std_out, std_err, return_code);
}

}  // namespace bcache
