include $(config_dir)rel.mk

this_lint_cmd = $(prorab_lint_cmd_clang_tidy)

ifeq ($(os),macosx)
    this_cxxflags += -I /usr/include
	this_cxxflags += -I /usr/local/include
endif
