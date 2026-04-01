cmake_minimum_required(VERSION 3.21)
project(LibraryBackendCPP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# PostgreSQL
set(PostgreSQL_ROOT "C:/Program Files/PostgreSQL/18" CACHE PATH "PostgreSQL installation path")
find_package(PostgreSQL REQUIRED)

# Исходные файлы
set(SOURCES
    src/main.cpp
    src/library_backend.cpp
)

set(CONSOLE_SOURCES
    src/console_menu.cpp
    src/library_backend.cpp
)

set(HEADERS
    src/library_backend.h
)

# Исполняемые файлы
add_executable(library_backend ${SOURCES} ${HEADERS})
add_executable(library_backend_console ${CONSOLE_SOURCES} ${HEADERS})

# Подключение заголовков
target_include_directories(library_backend PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${PostgreSQL_INCLUDE_DIRS}
)

target_include_directories(library_backend_console PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${PostgreSQL_INCLUDE_DIRS}
)

# Подключение библиотек
target_link_libraries(library_backend PRIVATE ${PostgreSQL_LIBRARIES})
target_link_libraries(library_backend_console PRIVATE ${PostgreSQL_LIBRARIES})

if(WIN32)
    target_link_libraries(library_backend PRIVATE ws2_32 wsock32)
    target_link_libraries(library_backend_console PRIVATE ws2_32 wsock32)
endif()

# Выходные директории
set_target_properties(library_backend PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
)

set_target_properties(library_backend_console PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
)

# Копирование DLL для Windows
if(WIN32)
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/copy_runtime_dlls_$<CONFIG>.cmake"
        CONTENT [=[
set(runtime_dlls "$<JOIN:$<TARGET_RUNTIME_DLLS:library_backend>,;>")
if(NOT runtime_dlls STREQUAL "")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${runtime_dlls} "$<TARGET_FILE_DIR:library_backend>"
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
]=]
    )
    add_custom_command(TARGET library_backend POST_BUILD
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/copy_runtime_dlls_$<CONFIG>.cmake"
    )
endif()
