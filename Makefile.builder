ifeq ($(PACKAGE_SET),dom0)
WIN_SOURCE_SUBDIRS := shared
WIN_COMPILER := WDK
WIN_PACKAGE_CMD := true
WIN_OUTPUT_LIBS := libs
WIN_OUTPUT_HEADERS := ../include
WIN_SIGN_CMD := true
endif
