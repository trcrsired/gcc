#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_INIT(package-unused, version-unused, libherbceptions)
AC_CONFIG_SRCDIR([include/herbceptions/error])

AM_ENABLE_MULTILIB(, ..)

AC_MSG_CHECKING([for --enable-version-specific-runtime-libs])
AC_ARG_ENABLE(version-specific-runtime-libs,
[  --enable-version-specific-runtime-libs    Specify that runtime libraries should be installed in a compiler-specific directory ],
[case "$enableval" in
 yes) version_specific_libs=yes ;;
 no)  version_specific_libs=no ;;
 *)   AC_MSG_ERROR([Unknown argument to enable/disable version-specific libs]);;
 esac],
[version_specific_libs=no])
AC_MSG_RESULT($version_specific_libs)

AC_USE_SYSTEM_EXTENSIONS

# Do not delete or change the following two lines.  For why, see
# http://gcc.gnu.org/ml/libstdc++/2003-07/msg00451.html
AC_CANONICAL_SYSTEM
target_alias=${target_alias-$host_alias}
AC_SUBST(target_alias)

GCC_LIBSTDCXX_RAW_CXX_FLAGS

AM_INIT_AUTOMAKE(foreign no-dist)
AM_MAINTAINER_MODE

GCC_WITH_TOOLEXECLIBDIR

# Calculate toolexeclibdir
# Also toolexecdir, though it's only used in toolexeclibdir
case ${version_specific_libs} in
  yes)
    # Need the gcc compiler version to know where to install libraries
    # and header files if --enable-version-specific-runtime-libs option
    # is selected.
    toolexecdir='$(libdir)/gcc/$(target_alias)'
    toolexeclibdir='$(toolexecdir)/$(gcc_version)$(MULTISUBDIR)'
    ;;
  no)
    if test -n "$with_cross_host" &&
       test x"$with_cross_host" != x"no"; then
      # Install a library built with a cross compiler in tooldir, not libdir.
      toolexecdir='$(exec_prefix)/$(target_alias)'
      case ${with_toolexeclibdir} in
	no)
	  toolexeclibdir='$(toolexecdir)/lib'
	  ;;
	*)
	  toolexeclibdir=${with_toolexeclibdir}
	  ;;
      esac
    else
      toolexecdir='$(libdir)/gcc-lib/$(target_alias)'
      toolexeclibdir='$(libdir)'
    fi
    multi_os_directory=`$CC -print-multi-os-directory`
    case $multi_os_directory in
      .) ;; # Avoid trailing /.
      *) toolexeclibdir=$toolexeclibdir/$multi_os_directory ;;
    esac
    ;;
esac
AC_SUBST(toolexecdir)
AC_SUBST(toolexeclibdir)

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AM_PROG_AS
AC_PROG_RANLIB

AC_LIBTOOL_DLOPEN
LT_INIT

AC_SUBST(enable_shared)
AC_SUBST(enable_static)

AM_CONDITIONAL([ENABLE_DARWIN_AT_RPATH], [test x$enable_darwin_at_rpath = xyes])

if test "${multilib}" = "yes"; then
  multilib_arg="--enable-multilib"
else
  multilib_arg=
fi

# The Itanium exception_ptr error domain is provided by the C++ runtime
# (libsupc++ / libc++abi), so libherbceptions never builds its own copy
# of src/itanium_exception_ptr.cpp.

# The library only uses ntdll on Windows.
case "${host}" in
  *-*-mingw* | *-*-cygwin*)
    herbceptions_win32=yes ;;
  *)
    herbceptions_win32=no ;;
esac
AM_CONDITIONAL(USING_WIN32, test x$herbceptions_win32 = xyes)

# Determine what GCC version number to use in filesystem paths.
GCC_BASE_VER

# Add CET specific flags if Intel CET is enabled.
GCC_CET_FLAGS(CET_FLAGS)

EXTRA_CXXFLAGS="$EXTRA_CXXFLAGS $CET_FLAGS"
EXTRA_ASFLAGS=$CET_FLAGS
AC_SUBST(EXTRA_ASFLAGS)
AC_SUBST(EXTRA_CXXFLAGS)

AC_ARG_ENABLE([werror],
  [AS_HELP_STRING([--disable-werror], [disable building with -Werror])])

ACX_PROG_CC_WARNING_OPTS([-Wextra -Wall -Wwrite-strings \
                          -Wmissing-format-attribute -Wcast-qual],
                          [WARN_FLAGS])
AS_IF([test "x$enable_werror" != "xno"],
  [WARN_FLAGS="$WARN_FLAGS -Werror"])
AC_SUBST([WARN_FLAGS])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
