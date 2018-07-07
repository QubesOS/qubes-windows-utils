ifeq ($(PACKAGE_SET),vm)
WIN_SOURCE_SUBDIRS := .
WIN_SLN_DIR := vs2017
WIN_COMPILER := msbuild
WIN_OUTPUT_LIBS := bin
WIN_OUTPUT_HEADERS := include
WIN_BUILD_DEPS := core-vchan-xen
WIN_PREBUILD_CMD = set_version.bat && powershell -executionpolicy bypass -File set_version.ps1 < nul
endif
