cmake_minimum_required(VERSION 3.20)

set(name opros)
project(${name})

# !!! find_package must go after project() declaration !!!
# Otherwise VCPKG does not set the CMAKE_PREFIX_PATH to find packages.
find_package(myci CONFIG REQUIRED)

set(srcs)
myci_add_source_files(srcs
    DIRECTORY
        ../../src/${name}
    RECURSIVE
)

myci_declare_library(${name}
    SOURCES
        ${srcs}
    INSTALL_INCLUDE_DIRECTORIES
        ../../src/${name}
    PUBLIC_INCLUDE_DIRECTORIES
        ../../src
    DEPENDENCIES
        utki
)
