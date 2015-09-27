include prorab.mk


debug := false


this_name := pogodi

this_soname := $(shell cat $(prorab_this_dir)soname.txt)


#Sources
this_srcs :=
this_srcs += pogodi/WaitSet.cpp
this_srcs += pogodi/Waitable.cpp



this_cflags := 

ifeq ($(debug),true)
    this_cflags += -DDEBUG
endif




this_cflags += -Wall #enable all warnings
this_cflags += -Wno-comment #no warnings on nested comments
this_cflags += -funsigned-char #the 'char' type is unsigned
this_cflags += -Wnon-virtual-dtor #warn if base class has non-virtual destructor
this_cflags += -fno-operator-names #do not treat 'and', 'bitand','bitor','compl','not','or','xor' as keywords
this_cflags += -Werror #treat warnings as errors
this_cflags += -Wfatal-errors # stop on first error encountered
this_cflags += -fstrict-aliasing #strict aliasing (in order to comply with the c++ standard more strictly)
this_cflags += -std=c++11
this_cflags += -g #include debugging symbols
                #-P -E #-Wreorder
                #-O3 -funroll-loops -fomit-frame-pointer



ifeq ($(prorab_os),linux)
    this_cflags += -fPIC # Since we are building shared library, we need Position-Independend Code
#    this_ldlibs := -lpthread -lrt
else ifeq ($(prorab_os),windows)
 #   this_ldlibs := -lws2_32
else ifeq ($(prorab_os),macosx)
    this_cflags += -stdlib=libc++ #this is needed to be able to use c++11 std lib
endif



$(eval $(prorab-build-lib))

