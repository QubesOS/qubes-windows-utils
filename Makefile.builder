ifeq ($(PACKAGE_SET),vm)
WIN_SOURCE_SUBDIRS := .
WIN_COMPILER := WDK
WIN_OUTPUT_LIBS := shared/bin
WIN_OUTPUT_HEADERS := include
WIN_PREBUILD_CMD = set_version.bat && powershell -executionpolicy bypass set_version.ps1
endif
