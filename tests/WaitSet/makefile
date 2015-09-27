include prorab.mk


this_name := tests


#compiler flags
this_cflags += -std=c++11
this_cflags += -Wall
this_cflags += -DDEBUG
this_cflags += -fstrict-aliasing #strict aliasing!!!

this_srcs += main.cpp tests.cpp

this_ldlibs += -lwait

ifeq ($(prorab_os),macosx)
    this_cflags += -stdlib=libc++ #this is needed to be able to use c++11 std lib
    this_ldlibs += -lc++
else ifeq ($(prorab_os),windows)
else
    this_cflags += -fPIC
    this_ldlibs += -lpthread
endif

this_ldflags += -L$(prorab_this_dir)../../src/

#add dependency on libwait.so
$(abspath $(prorab_this_dir)tests): $(abspath $(prorab_this_dir)../../src/libwait$(prorab_lib_extension))


$(eval $(prorab-build-app))

include $(prorab_this_dir)../test_target.mk


#include makefile for building ting
$(eval $(call prorab-include,$(prorab_this_dir)../../src/makefile))
