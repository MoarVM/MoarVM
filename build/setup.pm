package main;
use strict;
use warnings;

use File::Spec::Functions qw(devnull);
my $devnull = devnull();

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

our %TP_MT = (
    name => 'tinymt',
    path => '3rdparty/tinymt',
    src  => [ '3rdparty/tinymt' ],
);

our %TP_DC = (
    name  => 'dyncall_s',
    path  => '3rdparty/dyncall/dyncall',
    rule  => 'cd 3rdparty/dyncall &&  ./configure && CC=\'$(CC)\' CFLAGS=\'-fPIC\' $(MAKE) -f Makefile ',
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
    mt  => { %TP_MT },
    dc  => { %TP_DC },
    dcb => { %TP_DCB },
    dl  => { %TP_DL },
    uv  => { %TP_UVDUMMY },
);

# shell configuration
# selected by C<--shell>

our %SHELLS = (
    posix => {
        sh  => '/bin/sh',
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

    cppswitch => '-E',
    cppout    => '> ',

    asmswitch => '-S',
    asmout    => '-o ',

    ldout => undef,
    lddir => '-L',
    ldusr => '-l%s',
    ldsys => undef,
    ldimp => undef,

    ccshared   => '-fPIC',
    ldshared   => '-shared @ccshared@',
    moarshared => '',
    ldrpath    => '-Wl,-rpath,"/@libdir@"',

    arflags => 'rcs',
    arout   => '',

    mkflags => '',
    mknoisy => '',

    obj => '.o',
    lib => 'lib%s.a',
    dll => 'lib%s.so',
    asm => '.s',

    bindir    => '@prefix@/bin',
    libdir    => '@prefix@/lib',
    mastdir   => '@prefix@/share/nqp/lib/MAST',
    sharedlib => '',

    staticlib => '',

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

    cppswitch => '/P',
    cppout    => '/Fi',

    asmswitch => '/c /FAs',
    asmout    => '/Fa',

    ldout => '/out:',
    lddir => '/libpath:',
    ldusr => '%s.lib',
    ldsys => undef,
    ldimp => '%s.dll.lib',

    ccshared   => '',
    ldshared   => '/dll',
    moarshared => '/implib:@moardll@.lib',
    ldrpath    => '',

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
    asm => '.asm',

    bindir    => '@prefix@/bin',
    libdir    => '@bindir@',
    mastdir   => '@prefix@/share/nqp/lib/MAST',
    sharedlib => '@moardll@.lib',

    staticlib => '',

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

        ccmiscflags  => '-Werror=declaration-after-statement -Werror=pointer-arith',
        ccwarnflags  => '',
        ccoptiflags  => '-O%s -DNDEBUG',
        ccdebugflags => '-g%s',
        ccinstflags  => '-pg',

        ldmiscflags  => '',
        ldoptiflags  => undef,
        lddebugflags => undef,
        ldinstflags  => undef,

        noreturnspecifier => '',
        noreturnattribute => '__attribute__((noreturn))',
        formatattribute   => '__attribute__((format(X, Y, Z)))',
    },

    clang => {
        -toolchain => 'gnu',

        cc => 'clang',
        ld => undef,

        ccmiscflags  =>  '-fno-omit-frame-pointer -fno-optimize-sibling-calls',
        ccwarnflags  => '-Wno-logical-op-parentheses',
        ccoptiflags  => '-O%s -DNDEBUG',
        ccdebugflags => '-g%s',
        ccinstflags  => '-fsanitize=address',
        cc_covflags => '-fprofile-instr-generate -fcoverage-mapping',

        ldmiscflags  => '',
        ldoptiflags  => undef,
        lddebugflags => undef,
        ldinstflags  => undef,
        ld_covflags => '-fprofile-instr-generate -fcoverage-mapping',

        noreturnspecifier => '',
        noreturnattribute => '__attribute__((noreturn))',
        formatattribute   => '__attribute__((format(X, Y, Z)))',
    },

    cl => {
        -toolchain => 'msvc',

        cc => 'cl',
        ld => 'link',

        ccmiscflags  => '/nologo /MT',
        ccwarnflags  => '',
        ccoptiflags  => '/Ox /GL /DNDEBUG',
        ccdebugflags => '/Zi',
        ccinstflags  => '',

        ldmiscflags  => '/nologo',
        ldoptiflags  => '/LTCG',
        lddebugflags => '/debug /pdb:$@.pdb',
        ldinstflags  => '/Profile',

        noreturnspecifier => '__declspec(noreturn)',
        noreturnattribute => '',
        formatattribute   => '', # TODO
    },

    cc => {
        -toolchain => 'posix',

        cc => 'cc',
        ld => undef,

        ccmiscflags  => '',
        ccwarnflags  => '',
        ccoptiflags  => '-O -DNDEBUG',
        ccdebugflags => '-g',
        ccinstflags  => '',

        ldmiscflags  => '',
        ldoptiflags  => undef,
        lddebugflags => undef,
        ldinstflags  => undef,

        noreturnspecifier => '',
        noreturnattribute => '',
        formatattribute   => '',
    },
);

# OS configuration
# selected by C<--os> or taken from C<$^O>

our %OS_WIN32 = (
    exe      => '.exe',
    defs     => [ qw( WIN32 AO_ASSUME_WINDOWS98 ) ],
    syslibs  => [ qw( shell32 ws2_32 mswsock rpcrt4 advapi32 psapi iphlpapi userenv ) ],
    platform => '$(PLATFORM_WIN32)',

    dllimport => '__declspec(dllimport)',
    dllexport => '__declspec(dllexport)',
    dlllocal  => '',

    translate_newline_output => 1,

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

    libdir     => '@bindir@',
    ccshared   => '',
    ldshared   => '-shared -Wl,--out-implib,lib$(notdir $@).a',
    moarshared => '',
    ldrpath    => '',
    sharedlib  => 'lib@moardll@.a',

    translate_newline_output => 1,

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

our %OS_AIX = (
    %OS_POSIX,

    defs        => [ qw( _ALL_SOURCE _XOPEN_SOURCE=500 _LINUX_SOURCE_COMPAT ) ],
    syslibs     => [ @{$OS_POSIX{syslibs}}, qw( rt dl perfstat ) ],
    ldmiscflags => '-Wl,-brtl',
    ldrpath     => '-L"/@libdir@"',

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_AIX)' },
    },
);

our %OS_LINUX = (
    %OS_POSIX,

    syslibs => [ @{$OS_POSIX{syslibs}}, qw( rt dl ) ],

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

    cc => (qx!cc -v 2>&1 >$devnull! !~ 'clang') ? 'gcc' : 'clang',

    syslibs => [ @{$OS_POSIX{syslibs}}, qw( kvm ) ],

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_FREEBSD)' },
    },
);

our %OS_DRAGONFLY = (
    %OS_POSIX,

    syslibs => [ @{$OS_POSIX{syslibs}}, qw( kvm ) ],

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_FREEBSD)' },
    },
);

our %OS_GNUKFREEBSD = (
    %OS_FREEBSD,

    syslibs => [ @{$OS_FREEBSD{syslibs}}, qw( rt dl ) ],
);

our %OS_SOLARIS = (
    %OS_POSIX,

    defs     => [ qw( _XOPEN_SOURCE=500 _XOPEN_SOURCE_EXTENDED=1  __EXTENSIONS__=1 _POSIX_PTHREAD_SEMANTICS _REENTRANT _FILE_OFFSET_BITS=64 ) ],
    syslibs => [ qw( socket sendfile nsl pthread kstat m rt ) ],
    mknoisy => '',

    -thirdparty => {
        dc => { %TP_DC,
	        rule  => 'cd 3rdparty/dyncall &&  CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' $(MAKE) -f Makefile.embedded sun',
	        clean => 'cd 3rdparty/dyncall &&  CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' $(MAKE) -f Makefile.embedded clean',
	    },
        uv => { %TP_UVDUMMY, objects => '$(UV_SOLARIS)' },
    },
);

our %OS_DARWIN = (
    %OS_POSIX,

    defs     => [ qw( _DARWIN_USE_64_BIT_INODE=1 ) ],
    syslibs  => [],
    usrlibs  => [ qw( pthread ) ],

    dll => 'lib%s.dylib',

    ccshared   => '',
    ldshared   => '-dynamiclib',
    moarshared => '-install_name "@prefix@/lib/libmoar.dylib"',
    sharedlib  => 'libmoar.dylib',

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_DARWIN)' },
    },
);

our %SYSTEMS = (
    posix       => [ qw( posix posix cc ),    { %OS_POSIX } ],
    linux       => [ qw( posix gnu   gcc ),   { %OS_LINUX } ],
    aix         => [ qw( posix gnu   gcc ),   { %OS_AIX } ],
    darwin      => [ qw( posix gnu   clang ), { %OS_DARWIN } ],
    openbsd     => [ qw( posix bsd   gcc ),   { %OS_OPENBSD} ],
    netbsd      => [ qw( posix bsd   gcc ),   { %OS_NETBSD } ],
    dragonfly   => [ qw( posix bsd   gcc ),   { %OS_DRAGONFLY } ],
    freebsd     => [ qw( posix bsd), $OS_FREEBSD{cc} , { %OS_FREEBSD } ],
    gnukfreebsd => [ qw( posix gnu   gcc ),   { %OS_GNUKFREEBSD } ],
    solaris     => [ qw( posix posix gcc ),   { %OS_SOLARIS } ],
    win32       => [ qw( win32 msvc  cl ),    { %OS_WIN32 } ],
    cygwin      => [ qw( posix gnu   gcc ),   { %OS_WIN32 } ],
    mingw32     => [ qw( win32 gnu   gcc ),   { %OS_MINGW32 } ],
);

42;
