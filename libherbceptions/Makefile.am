ACLOCAL_AMFLAGS = -I .. -I ../config

# May be used by toolexeclibdir.
gcc_version := $(shell @get_gcc_base_ver@ $(top_srcdir)/../gcc/BASE-VER)

# libherbceptions is built with the target's raw C++ compiler and does not
# link the C++ runtime: the Itanium exception_ptr error domain lives in
# libsupc++/libc++abi, and the remaining domains are self-contained.
AM_CPPFLAGS = \
	-I$(srcdir)/include \
	-I$(srcdir)/src \
	-D_HERBCEPTIONS_BUILDING_RUNTIME=1

AM_CXXFLAGS = \
	-std=c++23 \
	$(LIBSTDCXX_RAW_CXX_CXXFLAGS) \
	$(WARN_FLAGS) \
	$(EXTRA_CXXFLAGS) \
	-fno-exceptions -fno-rtti -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -fno-ident

# Rethrowing a Microsoft-ABI legacy exception calls RaiseException; the
# frames between the raise and the caller must be unwindable, so this one
# translation unit needs unwind tables despite the flags above.
msvc_exception_ptr.lo msvc_exception_ptr.o: CXXFLAGS += -funwind-tables

if USING_MINGW
toolexeclib_LTLIBRARIES = herbceptions.la
else
toolexeclib_LTLIBRARIES = libherbceptions.la
endif

herbceptions_sources = \
	src/posix.cpp \
	src/parse.cpp \
	src/cmath_errc.cpp \
	src/http_errc.cpp \
	src/tls_alert.cpp \
	src/query_information.cpp \
	src/win32.cpp \
	src/nt.cpp \
	src/com.cpp \
	src/wine.cpp \
	src/msvc_exception_ptr.cpp

# src/query_information.cpp implements the legacy-box buffer ABI and is
# hosted-only: the file compiles to nothing when __STDC_HOSTED__ is 0.
# src/itanium_exception_ptr.cpp is deliberately not built: the Itanium
# exception_ptr error domain is supplied by the C++ runtime (libsupc++ /
# libc++abi).  The file is kept in the tree for reference and for
# freestanding builds of libherbceptions outside of GCC.
if USING_MINGW
herbceptions_la_SOURCES = $(herbceptions_sources)
herbceptions_la_LIBADD = -lntdll
# -module drops libtool's "lib" prefix and -avoid-version drops the "-0"
# suffix, so the output is herbceptions.dll: the same file name the MSVC
# build produces, letting one DLL serve both ABIs.
herbceptions_la_LDFLAGS = $(lt_host_flags) -module -avoid-version
else
libherbceptions_la_SOURCES = $(herbceptions_sources)
if USING_WIN32
# Cygwin still uses ntdll for the NT heap and the win32 domains.
libherbceptions_la_LIBADD = -lntdll
endif
libherbceptions_la_LDFLAGS = $(lt_host_flags)
endif

herbceptionsincludedir = @herbceptionsincludedir@
herbceptionsinclude_HEADERS = include/herbceptions/error

herbceptions_detailsincludedir = $(herbceptionsincludedir)/__details
herbceptions_detailsinclude_HEADERS = \
	include/herbceptions/__details/cmath_errc.h \
	include/herbceptions/__details/com.h \
	include/herbceptions/__details/exception_ptr.h \
	include/herbceptions/__details/http.h \
	include/herbceptions/__details/legacy_exception.h \
	include/herbceptions/__details/macros_guard.h \
	include/herbceptions/__details/nt.h \
	include/herbceptions/__details/parse.h \
	include/herbceptions/__details/posix.h \
	include/herbceptions/__details/tls.h \
	include/herbceptions/__details/win32.h \
	include/herbceptions/__details/wine.h

include $(top_srcdir)/../multilib.am
