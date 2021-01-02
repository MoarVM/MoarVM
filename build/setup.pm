package main;
use strict;
use warnings;

use File::Spec::Functions qw(devnull);
my $devnull = devnull();

# 3rdparty library configuration

my %TP_LAO = (
    name  => 'atomic_ops',
    path  => '3rdparty/libatomicops/src',
    rule  => 'cd 3rdparty/libatomicops && CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' MAKE=\'$(MAKE)\' ./configure @crossconf@ && cd src && $(MAKE) && cd ..',
    clean => 'cd 3rdparty/libatomicops/src && $(MAKE) distclean',
);

my %TP_SHA = (
    name => 'sha1',
    path => '3rdparty/sha1',
    src  => [ '3rdparty/sha1' ],
);

my %TP_TOM = (
    name => 'tommath',
    path => '3rdparty/libtommath',
    src  => [ '3rdparty/libtommath' ],
);

my %TP_MT = (
    name => 'tinymt',
    path => '3rdparty/tinymt',
    src  => [ '3rdparty/tinymt' ],
);

my %TP_DC = (
    name  => 'dyncall_s',
    path  => '3rdparty/dyncall/dyncall',
    rule  => 'cd 3rdparty/dyncall &&  ./configure && CC=\'$(CC)\' CFLAGS=\'-fPIC\' $(MAKE) -f Makefile ',
    clean => 'cd 3rdparty/dyncall && $(MAKE) -f Makefile clean',
);

my %TP_DCB = (
    name  => 'dyncallback_s',
    path  => '3rdparty/dyncall/dyncallback',
    dummy => 1, # created as part of dyncall build
);

my %TP_DL = (
    name  => 'dynload_s',
    path  => '3rdparty/dyncall/dynload',
    dummy => 1, # created as part of dyncall build
);

my %TP_CMP = (
    name => 'cmp',
    path => '3rdparty/cmp',
    src  => [ '3rdparty/cmp' ],
    clean => 'cd 3rdparty/cmp && $(RM) libcmp.a && $(RM) cmp.lib && $(RM) cmp.obj && $(MAKE) clean'
);

my %TP_UVDUMMY = (
    name => 'uv',
    path => '3rdparty/libuv',
    # no default rule
    # building libuv is always OS-specific
);

my %TP_UV = (
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
    cmp => { %TP_CMP },
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

my %TC_POSIX = (
    -compiler => 'cc',

    make => 'make',
    ar   => 'ar',


    ccswitch => '-c',
    ccout    => '-o ',
    ccinc    => '-I',
    ccincsystem => '-isystem',
    ccdef    => '-D',

    cppswitch => '-E',
    cppout    => '> ',

    asmswitch => '-S',
    asmout    => '-o ',
    objout    => '-o ',

    ldout => undef,
    lddir => '-L',
    ldusr => '-l%s',
    ldsys => undef,
    ldimp => undef,

    ccshared                 => '-fPIC',
    ldshared                 => '-shared @ccshared@',
    moarshared_norelocatable => '',
    moarshared_relocatable   => '',
    ldrpath                  => '-Wl,-rpath,"/@libdir@"',
    ldrpath_relocatable      => '-Wl,-z,origin,-rpath,\'$$ORIGIN/../lib\'',

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

my %TC_GNU = (
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

my %TC_BSD = (
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

my %TC_MSVC = (
    -compiler => 'cl',

    make => 'nmake',
    ar   => 'lib',

    ccswitch => '/c',
    ccout    => '/Fo',
    ccinc    => '/I',
    ccincsystem => '/I',
    ccdef    => '/D',

    cppswitch => '/P',
    cppout    => '/Fi',

    asmswitch => '/c /FAs',
    asmout    => '/Fa',
    objout    => '/Fo',

    ldout => '/out:',
    lddir => '/libpath:',
    ldusr => '%s.lib',
    ldsys => undef,
    ldimp => '%s.dll.lib',

    ccshared                 => '',
    ldshared                 => '/dll',
    moarshared_norelocatable => '/implib:@moardll@.lib',
    moarshared_relocatable   => '/implib:@moardll@.lib',
    ldrpath                  => '',
    ldrpath_relocatable      => '',

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

my %TC_MINGW32 = (
    %TC_GNU,

    make => 'gmake',

    dll   => '%s.dll',
    ldimp => '-l%s.dll',

    libdir                   => '@bindir@',
    ccshared                 => '',
    ldshared                 => '-shared -Wl,--out-implib,lib$(notdir $@).a',
    moarshared_norelocatable => '',
    moarshared_relocatable   => '',
    ldrpath                  => '',
    ldrpath_relocatable      => '',
    sharedlib                => 'lib@moardll@.a',

    -thirdparty => {
        dc => {
            %TP_DC,
            rule  => 'cd 3rdparty/dyncall && ./configure.bat /target-x86 /tool-gcc && $(MAKE) -f Makefile.embedded mingw32',
            clean => $TC_MSVC{-thirdparty}->{dc}->{clean},
        },
    },
);

our %TOOLCHAINS = (
    posix => { %TC_POSIX },
    gnu   => { %TC_GNU },
    bsd   => { %TC_BSD },
    msvc  => { %TC_MSVC },
    mingw32 => { %TC_MINGW32 },
);

# compiler configuration
# selected by C<--compiler>

our %COMPILERS = (
    gcc => {
        -toolchain => 'gnu',

        cc => 'gcc',
        ld => undef,
        as => 'as',

        ccmiscflags  => '-std=gnu99 -Wextra -Wall -Wno-unused-parameter -Wno-unused-function -Wno-missing-braces -Werror=pointer-arith',
        ccwarnflags  => '',
        ccoptiflags  => '-O%s -DNDEBUG',
        ccdebugflags => '-g%s',
        ccjitflags   => '',

        ldmiscflags  => '',
        ldoptiflags  => undef,
        lddebugflags => undef,

        noreturnspecifier => '',
        noreturnattribute => '__attribute__((noreturn))',
        fallthrough => '__attribute__ ((fallthrough));',
        formatattribute   => '__attribute__((format(X, Y, Z)))',
        expect_likely => '__builtin_expect(!!(condition), 1)',
        expect_unlikely => '__builtin_expect(!!(condition), 0)',
        expect_condition => '__builtin_expect((condition), (expection))'
    },
    # This is a copy of gcc except with -wd858 which supresses a warning that
    # const on a return type is meaningless (warning doesn't show up on other
    # compilers). Also we use -fp-model precise -fp-model source to make sure
    # that denormals are handled properly.
    icc => {
        -toolchain => 'gnu',

        cc => 'icc',
        ld => undef,
        as => 'as',

        ccmiscflags  => '-Werror=declaration-after-statement -Werror=pointer-arith -wd858 -fp-model precise -fp-model source',
        ccwarnflags  => '',
        ccoptiflags  => '-O%s -DNDEBUG',
        ccdebugflags => '-g%s',
        ccjitflags   => '',

        ldmiscflags  => '',
        ldoptiflags  => undef,
        lddebugflags => undef,

        noreturnspecifier => '',
        noreturnattribute => '__attribute__((noreturn))',
        fallthrough => '',
        formatattribute   => '__attribute__((format(X, Y, Z)))',
        expect_likely => '__builtin_expect(!!(condition), 1)',
        expect_unlikely => '__builtin_expect(!!(condition), 0)',
        expect_condition => '__builtin_expect((condition), (expection))'
    },

    clang => {
        -toolchain => 'gnu',

        cc => 'clang',
        ld => undef,
        as => 'as',

        ccmiscflags  =>  '-fno-omit-frame-pointer -fno-optimize-sibling-calls',
        ccwarnflags  => '-Wno-logical-op-parentheses',
        ccoptiflags  => '-O%s -DNDEBUG',
        ccdebugflags => '-g%s',
        cc_covflags => '-fprofile-instr-generate -fcoverage-mapping',
        ccjitflags   => '',

        ldmiscflags  => '',
        ldoptiflags  => undef,
        lddebugflags => undef,
        ld_covflags => '-fprofile-instr-generate -fcoverage-mapping',

        noreturnspecifier => '',
        noreturnattribute => '__attribute__((noreturn))',
        fallthrough => '',
        formatattribute   => '__attribute__((format(X, Y, Z)))',
        vectorizerspecifier => '_Pragma ("clang loop vectorize(enable)")',
        expect_likely => '__builtin_expect(!!(condition), 1)',
        expect_unlikely => '__builtin_expect(!!(condition), 0)',
        expect_condition => '__builtin_expect((condition), (expection))'
    },

    cl => {
        -toolchain => 'msvc',

        cc => 'cl',
        ld => 'link',
        as => 'ml64',

        ccmiscflags  => '/nologo /MT /std:c++latest',
        ccwarnflags  => '',
        ccoptiflags  => '/Ox /GL /DNDEBUG',
        ccdebugflags => '/Zi',
        ccjitflags   => '',

        ldmiscflags  => '/nologo',
        ldoptiflags  => '/LTCG',
        lddebugflags => '/debug /pdb:$@.pdb',

        noreturnspecifier => '__declspec(noreturn)',
        noreturnattribute => '',
        fallthrough => '',
        formatattribute   => '', # TODO
        expect_likely => '(condition)',
        expect_unlikely => '(condition)',
        expect_condition => '(condition)'

    },

    cc => {
        -toolchain => 'posix',

        cc => 'cc',
        ld => undef,
        as => 'as',

        ccmiscflags  => '',
        ccwarnflags  => '',
        ccoptiflags  => '-O -DNDEBUG',
        ccdebugflags => '-g',
        ccjitflags   => '',

        ldmiscflags  => '',
        ldoptiflags  => undef,
        lddebugflags => undef,

        noreturnspecifier => '',
        noreturnattribute => '',
        fallthrough => '',
        formatattribute   => '',
        expect_likely => '(condition)',
        expect_unlikely => '(condition)',
        expect_condition => '(condition)'
    },
);

# OS configuration
# selected by C<--os> or taken from C<$^O>

my %OS_WIN32 = (
    exe      => '.exe',
    defs     => [ qw( WIN32 AO_ASSUME_WINDOWS98 ) ],
    syslibs  => [ qw( shell32 ws2_32 mswsock rpcrt4 advapi32 psapi iphlpapi userenv user32 ) ],
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

# FIXME - This 'defs' is in the "wrong" place, as currently we assume that
# 'defs' needs to be in the OS. Hence a phony OS just to hack this in.
# Our current system also only permits *one* OS-specific override, even though
# what we need might vary by compiler, meaning that realistically we can't
# really support more than one compiler per OS.  The proper fix feels like we
# ought to nest OS, toolchain and compiler in some order, to permit 'defs' and
# similar to alternate and stack/merge.

my %OS_MINGW32 = (
    %OS_WIN32,

    defs => [ @{$OS_WIN32{defs}}, qw( _WIN32_WINNT=0x0600 ) ],
);

my %OS_POSIX = (
    defs     => [ qw( _REENTRANT _FILE_OFFSET_BITS=64 ) ],
    syslibs  => [ qw( m pthread ) ],
    platform => '$(PLATFORM_POSIX)',
);

my %OS_AIX = (
    %OS_POSIX,

    defs                => [ qw( _ALL_SOURCE _XOPEN_SOURCE=500 _LINUX_SOURCE_COMPAT ) ],
    syslibs             => [ @{$OS_POSIX{syslibs}}, qw( rt dl perfstat ) ],
    ldmiscflags         => '-Wl,-brtl',
    ldrpath             => '-L"/@libdir@"',
    ldrpath_relocatable => '-L"/@libdir@"',

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_AIX)' },
    },
);

my %OS_LINUX = (
    %OS_POSIX,

    syslibs => [ @{$OS_POSIX{syslibs}}, qw( rt dl ) ],

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_LINUX)' },
    },
);

my %OS_OPENBSD = (
    %OS_POSIX,

    syslibs     => [ @{$OS_POSIX{syslibs}}, qw( kvm ) ],

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_OPENBSD)' },
    },
);

my %OS_NETBSD = (
    %OS_POSIX,

    syslibs => [ @{$OS_POSIX{syslibs}}, qw( kvm ) ],

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_NETBSD)' },
    },
);

my %OS_FREEBSD = (
    %OS_POSIX,

    cc => (qx!cc -v 2>&1 >$devnull! !~ 'clang') ? 'gcc' : 'clang',

    syslibs => [ @{$OS_POSIX{syslibs}}, qw( kvm ) ],

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_FREEBSD)' },
    },
);

my %OS_DRAGONFLY = (
    %OS_POSIX,

    syslibs => [ @{$OS_POSIX{syslibs}}, qw( kvm ) ],

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_DRAGONFLYBSD)' },
    },
);

my %OS_GNUKFREEBSD = (
    %OS_FREEBSD,

    syslibs => [ @{$OS_FREEBSD{syslibs}}, qw( rt dl ) ],
);

my %OS_SOLARIS = (
    %OS_POSIX,

    defs     => [ qw( _XOPEN_SOURCE=500 _XOPEN_SOURCE_EXTENDED=1  __EXTENSIONS__=1 _POSIX_PTHREAD_SEMANTICS _REENTRANT ) ],
    syslibs => [ qw( socket sendfile nsl pthread kstat m rt ) ],
    mknoisy => '',

    -thirdparty => {
        dc => { %TP_DC,
            rule  => 'cd 3rdparty/dyncall &&  CC=\'$(CC)\' CFLAGS=\'$(CFLAGS) -U_FILE_OFFSET_BITS\' $(MAKE) -f Makefile.embedded sun',
            clean => 'cd 3rdparty/dyncall &&  CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' $(MAKE) -f Makefile.embedded clean',
        },
        uv => { %TP_UVDUMMY, objects => '$(UV_SOLARIS)' },
    },
);

my %OS_DARWIN = (
    %OS_POSIX,

    defs     => [ qw( _DARWIN_USE_64_BIT_INODE=1 ) ],
    syslibs  => [],
    usrlibs  => [ qw( pthread ) ],

    dll => 'lib%s.dylib',

    sharedlib                => 'libmoar.dylib',
    ccshared                 => '',
    ldshared                 => '-dynamiclib',
    moarshared_norelocatable => '-install_name "@prefix@/lib/libmoar.dylib"',
    moarshared_relocatable   => '-install_name @rpath/libmoar.dylib',
    ldrpath_relocatable      => '-Wl,-rpath,@executable_path/../lib',

    -thirdparty => {
        uv => { %TP_UVDUMMY, objects => '$(UV_DARWIN)' },
    },
);

our %SYSTEMS = (
    posix       => [ qw( posix posix cc ),    { %OS_POSIX } ],
    linux       => [ qw( posix gnu   gcc ),   { %OS_LINUX } ],
    aix         => [ qw( posix gnu   gcc ),   { %OS_AIX } ],
    darwin      => [ qw( posix gnu   clang ), { %OS_DARWIN } ],
    openbsd     => [ qw( posix bsd   clang ),   { %OS_OPENBSD} ],
    netbsd      => [ qw( posix bsd   gcc ),   { %OS_NETBSD } ],
    dragonfly   => [ qw( posix bsd   gcc ),   { %OS_DRAGONFLY } ],
    freebsd     => [ qw( posix bsd), $OS_FREEBSD{cc} , { %OS_FREEBSD } ],
    gnukfreebsd => [ qw( posix gnu   gcc ),   { %OS_GNUKFREEBSD } ],
    solaris     => [ qw( posix posix gcc ),   { %OS_SOLARIS } ],
    win32       => [ qw( win32 msvc  cl ),    { %OS_WIN32 } ],
    cygwin      => [ qw( posix gnu   gcc ),   { %OS_WIN32 } ],
    mingw32     => [ qw( win32 mingw32 gcc ), { %OS_MINGW32 } ],
);

42;
