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
	-fherbceptions \
	-fexceptions \
	$(LIBSTDCXX_RAW_CXX_CXXFLAGS) \
	$(WARN_FLAGS) \
	$(EXTRA_CXXFLAGS)

toolexeclib_LTLIBRARIES = libherbceptions.la

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

# src/itanium_exception_ptr.cpp is deliberately not built: the Itanium
# exception_ptr error domain is supplied by the C++ runtime (libsupc++ /
# libc++abi).  The file is kept in the tree for reference and for
# freestanding builds of libherbceptions outside of GCC.
libherbceptions_la_SOURCES = $(herbceptions_sources)

if USING_WIN32
libherbceptions_la_LIBADD = -lntdll
endif

herbceptionsincludedir = $(includedir)/herbceptions
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
