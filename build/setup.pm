package main;
use strict;
use warnings;

# 3rdparty library configuration

our %TP_APR = (
    name  => 'apr-1',
    path  => '3rdparty/apr/.libs',
    rule  => 'cd 3rdparty/apr && CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' ./configure --disable-shared @crossconf@ && $(MAKE)',
    clean => 'cd 3rdparty/apr && $(MAKE) distclean',
);

our %TP_LAO = (
    name  => 'atomic_ops',
    path  => '3rdparty/libatomic_ops/src',
    rule  => 'cd 3rdparty/libatomic_ops && CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' ./configure @crossconf@ && cd src && $(MAKE) && cd ..',
    clean => 'cd 3rdparty/libatomic_ops/src && $(MAKE) distclean',
);

our %TP_SHA = (
    name => 'sha1',
    path => '3rdparty/sha1',
    src  => [ '3rdparty/sha1' ],
);

our %TP_TOM = (
    name => 'tommath',
    path => '3rdparty/libtommath',
    src  => [ '3rdparty/libtommath' ],
);

our %TP_LN = (
    name => 'linenoise',
    path => '3rdparty/linenoise',
    src  => [ '3rdparty/linenoise' ],
);

our %TP_DC = (
    name  => 'dyncall_s',
    path  => '3rdparty/dyncall/dyncall',
    rule  => 'cd 3rdparty/dyncall &&  ./configure2 && CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' $(MAKE) -f Makefile',
    clean => 'cd 3rdparty/dyncall && $(MAKE) -f Makefile clean',
);

our %TP_DCB = (
    name  => 'dyncallback_s',
    path  => '3rdparty/dyncall/dyncallback',
    dummy => 1, # created as part of dyncall build
);

our %TP_DL = (
    name  => 'dynload_s',
    path  => '3rdparty/dyncall/dynload',
    dummy => 1, # created as part of dyncall build
);

our %TP_UVDUMMY = (
    name => 'uv',
    path => '3rdparty/libuv',
    # no default rule
    # building libuv is always OS-specific
);

our %TP_UV = (
    %TP_UVDUMMY,
    rule  => '$(AR) $(ARFLAGS) @arout@$@ $(UV_OBJECTS)',
    clean => '$(RM) @uvlib@ $(UV_OBJECTS)',
    # actually insufficient to build libuv
    # the OS needs to provide a C<src> or C<objects> setting
);

our %THIRDPARTY = (
    apr => { %TP_APR },
    lao => { %TP_LAO },
    tom => { %TP_TOM },
    sha => { %TP_SHA },
    ln  => { %TP_LN },
#    dc  => { %TP_DC },
#    dcb => { %TP_DCB },
#    dl  => { %TP_DL },
#    uv  => { %TP_UVDUMMY },
);

# shell configuration
# selected by C<--shell>

our %SHELLS = (
    posix => {
        sh  => 'sh',
        cat => 'cat',
        rm  => 'rm -f',
        nul => '/dev/null',
    },

    win32 => {
        sh  => 'cmd',
        cat => 'type',
        rm  => 'del',
        nul => 'NUL',
    },
);

# toolchain configuration
# selected by C<--toolchain>

our %TC_POSIX = (
    -compiler => 'cc',

    make => 'make',
    ar   => 'ar',

    ccswitch => '-c',
    ccout    => '-o ',
    ccinc    => '-I',
    ccdef    => '-D',

    ldout => undef,
    ldusr => '-l%s',
    ldsys => undef,

    arflags => 'rcs',
    arout   => '',

    mkflags => '',
    mknoisy => '',

    obj => '.o',
    lib => 'lib%s.a',

    -auxfiles => [],
);

our %TC_GNU = (
    %TC_POSIX,

    mknoisy => <<'TERM',
ifneq ($(NOISY), 1)
MSG = @echo
CMD = @
CMDOUT = > @nul@
NOERR = 2> @nul@
endif
TERM

    dllimport => '__attribute__ ((visibility ("default")))',
    dllexport => '__attribute__ ((visibility ("default")))',
    dlllocal  => '__attribute__ ((visibility ("hidden")))',
);

our %TC_BSD = (
    %TC_POSIX,

    mknoisy => <<'TERM',
.if $(NOISY) != 1
MSG = @echo
CMD = @
NOOUT = > @nul@
NOERR = 2> @nul@
.endif
TERM
);

our %TC_MSVC = (
    -compiler => 'cl',

    make => 'nmake',
    ar   => 'lib',

    ccswitch => '/c',
    ccout    => '/Fo',
    ccinc    => '/I',
    ccdef    => '/D',

    ldout => '/out:',
    ldusr => '%s.lib',
    ldsys => undef,

    arflags => '/nologo',
    arout   => '/out:',

    mkflags => '/nologo',
    mknoisy => <<'TERM',
!IF $(NOISY) != 1
MSG = @echo
CMD = @
NOOUT = > @nul@
NOERR = 2> @nul@
!ENDIF
TERM

    obj => '.obj',
    lib => '%s.lib',

    -auxfiles => [ qw( @name@.ilk @name@.pdb vc100.pdb ) ],

    -thirdparty => {
        apr => {
            %TP_APR,
            path  => '3rdparty/apr/LibR',
            rule  => 'cd 3rdparty/apr && $(MAKE) -f Makefile.win ARCH="Win32 Release" buildall',
            clean => 'cd 3rdparty/apr && $(MAKE) -f Makefile.win ARCH="Win32 Release" clean',
        },

#            dc => {
#                %DC,
#                name  => 'libdyncall_s',
#                rule  => 'cd 3rdparty/dyncall && configure.bat /target-x86 && $(MAKE) -f Nmakefile',
#                clean => '$(RM) 3rdparty/dyncall/ConfigVars @dclib@ @dcblib@ @dllib@ 3rdparty/dyncall/dyncall/*.obj 3rdparty/dyncall/dyncallback/*.obj 3rdparty/dyncall/dynload/*.obj',
#            },
#
#            dcb => { %DCB, name => 'libdyncallback_s' },
#            dl  => { %DL, name => 'libdynload_s' },
    },
);

our %TOOLCHAINS = (
    posix => { %TC_POSIX },
    gnu   => { %TC_GNU },
    bsd   => { %TC_BSD },
    msvc  => { %TC_MSVC },
);

# compiler configuration
# selected by C<--compiler>

our %COMPILERS = (
    gcc => {
        -toolchain => 'gnu',

        cc => 'gcc',
        ld => undef,

        ccmiscflags  => '-D_REENTRANT -D_LARGEFILE64_SOURCE',
        ccwarnflags  => '-Wall -Wextra',
        ccoptiflags  => '-O3',
        ccdebugflags => '-g',
        ccinstflags  => '',

        ldmiscflags  => '',
        ldoptiflags  => undef,
        lddebugflags => undef,
        ldinstflags  => undef,

        noreturnspecifier => '',
        noreturnattribute => '__attribute__((noreturn))',
    },

    clang => {
        -toolchain => 'gnu',

        cc => 'clang',
        ld => undef,

        ccmiscflags  =>  '-fno-omit-frame-pointer -fno-optimize-sibling-calls',
        ccwarnflags  => '', #'-Weverything',
        ccoptiflags  =>  '-O3',
        ccdebugflags => '-g',
        ccinstflags  => '-fsanitize=address',

        ldmiscflags  => '',
        ldoptiflags  => undef,
        lddebugflags => undef,
        ldinstflags  => undef,

        noreturnspecifier => '',
        noreturnattribute => '__attribute__((noreturn))',
    },

    cl => {
        -toolchain => 'msvc',

        cc => 'cl',
        ld => 'link',

        ccmiscflags  => '/nologo /MT',
        ccwarnflags  => '',
        ccoptiflags  => '/Ox /GL',
        ccdebugflags => '/Zi',
        ccinstflags  => '',

        ldmiscflags  => '/nologo',
        ldoptiflags  => '/LTCG',
        lddebugflags => '/debug',
        ldinstflags  => '/Profile',

        noreturnspecifier => '__declspec(noreturn)',
        noreturnattribute => '',
    },

    cc => {
        -toolchain => 'posix',

        cc => 'cc',
        ld => undef,

        ccmiscflags  => '-D_REENTRANT -D_LARGEFILE64_SOURCE',
        ccwarnflags  => '',
        ccoptiflags  => '-O',
        ccdebugflags => '-g',
        ccinstflags  => '',

        ldmiscflags  => '',
        ldoptiflags  => undef,
        lddebugflags => undef,
        ldinstflags  => undef,

        noreturnspecifier => '',
        noreturnattribute => '',
    },
);

# OS configuration
# selected by C<--os> or taken from C<$^O>

our %OS_WIN32 = (
    exe      => '.exe',
    defs     => [ 'WIN32', 'AO_ASSUME_WINDOWS98' ],
    syslibs  => [ qw( shell32 ws2_32 mswsock rpcrt4 advapi32 psapi iphlpapi ) ],
    platform => '$(PLATFORM_WIN32)',

    dllimport => '__declspec(dllimport)',
    dllexport => '__declspec(dllexport)',
    dlllocal  => '',

    -thirdparty => {
        # header only, no need to build anything
        lao => undef,

#        uv => {
#            %UV,
#            src => [ qw( 3rdparty/libuv/src 3rdparty/libuv/src/win ) ],
#        },
    },
);

our %OS_LINUX = (
    syslibs => [ qw( m pthread uuid rt ) ],

    -thirdparty => {
#        uv => { %UV, objects => '$(UV_LINUX)' },
    },
);

our %OS_OPENBSD = (
    -thirdparty => {
#        uv => { %UV, objects => '$(UV_OPENBSD)' },
    },
);

our %OS_NETBSD = (
    -thirdparty => {
#        uv => { %UV, objects => '$(UV_NETBSD)' },
    },
);

our %OS_FREEBSD = (
    -thirdparty => {
#        uv => { %UV, objects => '$(UV_FREEBSD)' },
    },
);

our %OS_SOLARIS = (
    syslibs => [ qw( socket sendfile nsl uuid pthread m rt ) ],
    mknoisy => '',

    -thirdparty => {
#        uv => { %UV, objects => '$(UV_SOLARIS)' },
    },
);

our %OS_DARWIN = (
    ldsys    => '-framework %s',
    defs     => [ '_DARWIN_USE_64_BIT_INODE=1' ],
    syslibs  => [ qw( ApplicationServices CoreServices Foundation ) ],

    -thirdparty => {
#        uv => { %UV, objects => '$(UV_DARWIN)' },
    },
);

our %SYSTEMS = (
    posix   => [ qw( posix posix cc ), {} ],
    linux   => [ qw( posix gnu gcc ), { %OS_LINUX } ],
    darwin  => [ qw( posix gnu clang ), { %OS_DARWIN } ],
    openbsd => [ qw( posix bsd gcc ), { %OS_OPENBSD} ],
    netbsd  => [ qw( posix bsd gcc ), { %OS_NETBSD } ],
    freebsd => [ qw( posix bsd clang ), { %OS_FREEBSD } ],
    solaris => [ qw( posix posix cc ),  { %OS_SOLARIS } ],
    win32   => [ qw( win32 msvc cl ), { %OS_WIN32 } ],
    cygwin  => [ qw( win32 gnu gcc ), { %OS_WIN32 } ],
    mingw32 => [ qw( win32 gnu gcc ), { %OS_WIN32 } ],
);

42;
