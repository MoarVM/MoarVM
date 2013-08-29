package main;
use strict;
use warnings;

# 3rdparty library configuration

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
    lao => { %TP_LAO },
    tom => { %TP_TOM },
    sha => { %TP_SHA },
    ln  => { %TP_LN },

    dc  => { %TP_DC },
    dcb => { %TP_DCB },
    dl  => { %TP_DL },
    uv  => { %TP_UVDUMMY },
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
    lddir => '-L',
    ldusr => '-l%s',
    ldsys => undef,
    ldimp => undef,

    ccshared => '-fPIC',
    ldshared => '-shared @ccshared@',

    arflags => 'rcs',
    arout   => '',

    mkflags => '',
    mknoisy => '',

    obj => '.o',
    lib => 'lib%s.a',
    dll => 'lib%s.so',

    -auxfiles => [],
);

our %TC_GNU = (
    %TC_POSIX,

    -compiler => 'gcc',

    mknoisy => <<'TERM',
ifneq ($(NOISY), 1)
MSG = @echo
CMD = @
NOOUT = > @nul@
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
    lddir => '/libpath:',
    ldusr => '%s.lib',
    ldsys => undef,
    ldimp => '%s.dll.lib',

    ccshared => '',
    ldshared => '/dll /implib:@moardll@.lib',

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
    dll => '%s.dll',

    -auxfiles => [ qw( @name@.ilk @name@.pdb @moardll@.lib @moardll@.exp vc100.pdb ) ],

    -thirdparty => {
        dc => {
            %TP_DC,
            name  => 'libdyncall_s',
            rule  => 'cd 3rdparty/dyncall && configure.bat /target-x86 && $(MAKE) -f Nmakefile',
            clean => '$(RM) 3rdparty/dyncall/ConfigVars @dclib@ @dcblib@ @dllib@ 3rdparty/dyncall/dyncall/*@obj@ 3rdparty/dyncall/dyncallback/*@obj@ 3rdparty/dyncall/dynload/*@obj@',
        },

        dcb => { %TP_DCB, name => 'libdyncallback_s' },
        dl  => { %TP_DL, name => 'libdynload_s' },
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

        ccmiscflags  => '',
        ccwarnflags  => '',
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
        ccwarnflags  => '',
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

        ccmiscflags  => '',
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
    defs     => [ qw( WIN32 AO_ASSUME_WINDOWS98 ) ],
    syslibs  => [ qw( shell32 ws2_32 mswsock rpcrt4 advapi32 psapi iphlpapi ) ],
    platform => '$(PLATFORM_WIN32)',

    dllimport => '__declspec(dllimport)',
    dllexport => '__declspec(dllexport)',
    dlllocal  => '',

    -thirdparty => {
        # header only, no need to build anything
        lao => undef,

        uv => {
            %TP_UVDUMMY,
            src => [ qw( 3rdparty/libuv/src 3rdparty/libuv/src/win ) ],
        },
    },
);

our %OS_MINGW32 = (
    %OS_WIN32,

    make => 'gmake',
    defs => [ @{$OS_WIN32{defs}}, qw( _WIN32_WINNT=0x0600 ) ],

    dll   => '%s.dll',
    ldimp => '-l%s.dll',

    ccshared => '',
    ldshared => '-shared -Wl,--out-implib,lib$@.a',

    -thirdparty => {
        %{$OS_WIN32{-thirdparty}},

        dc => {
            %TP_DC,
            rule  => 'cd 3rdparty/dyncall && ./configure.bat /target-x86 /tool-gcc && $(MAKE) -f Makefile.embedded mingw32',
            clean => $TC_MSVC{-thirdparty}->{dc}->{clean},
        },
    },
);

our %OS_POSIX = (
    defs     => [ qw( _REENTRANT _FILE_OFFSET_BITS=64 ) ],
    syslibs  => [ qw( m pthread ) ],
    platform => '$(PLATFORM_POSIX)',
);

our %OS_LINUX = (
    %OS_POSIX,

    syslibs => [ @{$OS_POSIX{syslibs}}, qw( rt ) ],

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_LINUX)' },
    },
);

our %OS_OPENBSD = (
    %OS_POSIX,

    syslibs => [ @{$OS_POSIX{syslibs}}, qw( kvm ) ],

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_OPENBSD)' },
    },
);

our %OS_NETBSD = (
    %OS_POSIX,

    syslibs => [ @{$OS_POSIX{syslibs}}, qw( kvm ) ],

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_NETBSD)' },
    },
);

our %OS_FREEBSD = (
    %OS_POSIX,

    syslibs => [ @{$OS_POSIX{syslibs}}, qw( kvm ) ],

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_FREEBSD)' },
    },
);

our %OS_SOLARIS = (
    %OS_POSIX,

    syslibs => [ qw( socket sendfile nsl pthread m rt ) ],
    mknoisy => '',

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_SOLARIS)' },
    },
);

our %OS_DARWIN = (
    %OS_POSIX,

    defs     => [ qw( _DARWIN_USE_64_BIT_INODE=1 ) ],
    syslibs  => [],
    usrlibs  => [ qw( pthread ) ],

    dll => 'lib%s.dylib',

    ccshared => '',
    ldshared => '-dynamiclib',

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_DARWIN)' },
    },
);

our %SYSTEMS = (
    posix   => [ qw( posix posix cc ), { %OS_POSIX } ],
    linux   => [ qw( posix gnu gcc ), { %OS_LINUX } ],
    darwin  => [ qw( posix gnu clang ), { %OS_DARWIN } ],
    openbsd => [ qw( posix bsd gcc ), { %OS_OPENBSD} ],
    netbsd  => [ qw( posix bsd gcc ), { %OS_NETBSD } ],
    freebsd => [ qw( posix bsd clang ), { %OS_FREEBSD } ],
    solaris => [ qw( posix posix cc ),  { %OS_SOLARIS } ],
    win32   => [ qw( win32 msvc cl ), { %OS_WIN32 } ],
    cygwin  => [ qw( posix gnu gcc ), { %OS_WIN32 } ],
    mingw32 => [ qw( win32 gnu gcc ), { %OS_MINGW32 } ],
);

42;
