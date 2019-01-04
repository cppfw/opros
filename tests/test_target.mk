ifeq ($(os),windows)
    this_test_cmd := (cd $(d) && cmd /C 'set PATH=../../src;%PATH% && $$(notdir $$^)')
else ifeq ($(os),macosx)
    this_test_cmd := (cd $(d) && DYLD_LIBRARY_PATH=../../src ./$$(notdir $$^))
else ifeq ($(os),linux)
    this_test_cmd := (cd $(d) && LD_LIBRARY_PATH=../../src ./$$(notdir $$^))
else
    $(error "Unknown OS")
endif

this_dirs := $(subst /, ,$(d))
this_test := $(word $(words $(this_dirs)), $(this_dirs))

define this_rule
test:: $(prorab_this_name)
	@myci-running-test.sh $(this_test)
	$(prorab_echo)$(this_test_cmd) || myci-error.sh "test failed"
	@myci-passed.sh
endef
$(eval $(this_rule))



ifeq ($(os),windows)
    this_gdb_cmd := (cd $(d) && cmd /C 'set PATH=../../src;%PATH% && gdb $$(notdir $$^)')
else ifeq ($(os),macosx)
    this_gdb_cmd := (cd $(d) && DYLD_LIBRARY_PATH=../../src gdb ./$$(notdir $$^))
else ifeq ($(os),linux)
    this_gdb_cmd := (cd $(d) && LD_LIBRARY_PATH=../../src gdb ./$$(notdir $$^))
endif


define this_rule
gdb:: $(prorab_this_name)
	$(prorab_echo)$(this_gdb_cmd)
endef
$(eval $(this_rule))
