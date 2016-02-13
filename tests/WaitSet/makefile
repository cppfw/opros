include prorab.mk


this_name := tests


#compiler flags
this_cxxflags += -std=c++11
this_cxxflags += -Wall
this_cxxflags += -DDEBUG
this_cxxflags += -fstrict-aliasing #strict aliasing!!!

this_srcs += main.cpp tests.cpp

this_ldlibs += -lpogodi

ifeq ($(prorab_os),macosx)
    this_cxxflags += -stdlib=libc++ #this is needed to be able to use c++11 std lib
    this_ldlibs += -lc++
else ifeq ($(prorab_os),windows)
    this_cxxflags += -I/usr/include -I/usr/local/include
else
    this_cxxflags += -fPIC
    this_ldlibs += -lpthread
endif

this_ldflags += -L$(prorab_this_dir)../../src/

#add dependency on libpogodi.so
$(abspath $(prorab_this_dir)tests): $(abspath $(prorab_this_dir)../../src/libpogodi$(prorab_lib_extension))


$(eval $(prorab-build-app))

include $(prorab_this_dir)../test_target.mk


#include makefile for building pogodi
$(eval $(call prorab-include,$(prorab_this_dir)../../src/makefile))
