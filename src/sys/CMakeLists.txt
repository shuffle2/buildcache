#---------------------------------------------------------------------------------------------------
# Copyright (c) 2018 Marcus Geelnard
#
# This software is provided 'as-is', without any express or implied warranty. In no event will the
# authors be held liable for any damages arising from the use of this software.
#
# Permission is granted to anyone to use this software for any purpose, including commercial
# applications, and to alter it and redistribute it freely, subject to the following restrictions:
#
#  1. The origin of this software must not be misrepresented; you must not claim that you wrote
#     the original software. If you use this software in a product, an acknowledgment in the
#     product documentation would be appreciated but is not required.
#
#  2. Altered source versions must be plainly marked as such, and must not be misrepresented as
#     being the original software.
#
#  3. This notice may not be removed or altered from any source distribution.
#---------------------------------------------------------------------------------------------------

set(SYS_SRCS
  filetracker.hpp
  filetracker.cpp
  perf_utils.cpp
  perf_utils.hpp
  sys_utils.cpp
  sys_utils.hpp
  )

add_library(sys ${SYS_SRCS})
target_link_libraries(sys base config)

if(MINGW)
  target_link_libraries(sys mingw_stdthreads)
  target_compile_definitions(sys PUBLIC USE_MINGW_THREADS)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(sys Threads::Threads)
endif()
