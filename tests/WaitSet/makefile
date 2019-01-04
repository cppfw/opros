include prorab.mk


this_name := tests


#compiler flags
this_cxxflags += -std=c++11
this_cxxflags += -Wall
this_cxxflags += -DDEBUG
this_cxxflags += -fstrict-aliasing #strict aliasing!!!
this_cxxflags += -O3

this_srcs += main.cpp tests.cpp

this_ldlibs += -lpogodi -lstdc++

ifeq ($(os),macosx)
    this_cxxflags += -stdlib=libc++ #this is needed to be able to use c++11 std lib
    this_ldlibs += -lc++
else ifeq ($(os),windows)
else
    this_cxxflags += -fPIC
    this_ldlibs += -lpthread
endif

this_ldflags += -L$(d)../../src/

this_no_install := true

$(eval $(prorab-build-app))

#add dependency on libpogodi.so
$(prorab_this_name): $(abspath $(d)../../src/libpogodi$(soext))

include $(d)../test_target.mk


#include makefile for building pogodi
$(eval $(call prorab-include,$(d)../../src/makefile))
