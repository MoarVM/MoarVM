package main;
use strict;
use warnings;

# 3rdparty library configuration

our %APR = (
    name  => 'apr-1',
    path  => '3rdparty/apr/.libs',
    rule  => 'cd 3rdparty/apr && CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' ./configure --disable-shared @crossconf@ && $(MAKE)',
    clean => 'cd 3rdparty/apr && $(MAKE) distclean',
);

our %LAO = (
    name  => 'atomic_ops',
    path  => '3rdparty/libatomic_ops/src',
    rule  => 'cd 3rdparty/libatomic_ops && CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' ./configure @crossconf@ && cd src && $(MAKE) && cd ..',
    clean => 'cd 3rdparty/libatomic_ops/src && $(MAKE) distclean',
);

our %SHA = (
    name => 'sha1',
    path => '3rdparty/sha1',
    src  => [ '3rdparty/sha1' ],
);

our %TOM = (
    name => 'tommath',
    path => '3rdparty/libtommath',
    src  => [ '3rdparty/libtommath' ],
);

our %LN = (
    name => 'linenoise',
    path => '3rdparty/linenoise',
    src  => [ '3rdparty/linenoise' ],
);

our %DC = (
    name  => 'dyncall_s',
    path  => '3rdparty/dyncall/dyncall',
    rule  => 'cd 3rdparty/dyncall &&  ./configure2 && CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' $(MAKE) -f Makefile',
    clean => 'cd 3rdparty/dyncall && $(MAKE) -f Makefile clean',
);

our %DCB = (
    name  => 'dyncallback_s',
    path  => '3rdparty/dyncall/dyncallback',
    dummy => 1, # created as part of dyncall build
);

our %DL = (
    name  => 'dynload_s',
    path  => '3rdparty/dyncall/dynload',
    dummy => 1, # created as part of dyncall build
);

our %UVDUMMY = (
    name => 'uv',
    path => '3rdparty/libuv',
    # no default rule
    # building libuv is always OS-specific
);

our %UV = (
    %UVDUMMY,
    rule  => '$(AR) $(ARFLAGS) @arout@$@ $(UV_OBJECTS)',
    clean => '$(RM) @uvlib@ $(UV_OBJECTS)',
    # actually insufficient to build libuv
    # the OS needs to provide a C<src> or C<objects> setting
);

our %THIRDPARTY = (
    apr => { %APR },
    lao => { %LAO },
    tom => { %TOM },
    sha => { %SHA },
    ln  => { %LN },
#    dc  => { %DC },
#    dcb => { %DCB },
#    dl  => { %DL },
#    uv  => { %UVDUMMY },
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

our %GNU = (
    -compiler => 'gcc',

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
    mknoisy => <<'TERM',
ifneq ($(NOISY), 1)
MSG = @echo
CMD = @
NOOUT = > @nul@
NOERR = 2> @nul@
endif
TERM

    obj => '.o',
    lib => 'lib%s.a',

    -auxfiles => [],
);

our %TOOLCHAINS = (
    gnu => { %GNU },

    # FIXME: support BSD makefile syntax
    bsd => {
        %GNU,

        mknoisy => <<'TERM',
.if $(NOISY) != 1
MSG = @echo
CMD = @
NOOUT = > @nul@
NOERR = 2> @nul@
.endif
TERM
    },

    msvc => {
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
                %APR,
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
    },
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
        -toolchain => 'bsd',

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

our %WIN32 = (
    exe      => '.exe',
    defs     => [ 'WIN32', 'AO_ASSUME_WINDOWS98' ],
    syslibs  => [ qw( shell32 ws2_32 mswsock rpcrt4 advapi32 psapi iphlpapi ) ],
    platform => '$(PLATFORM_WIN32)',

    -thirdparty => {
        # header only, no need to build anything
        lao => undef,

#        uv => {
#            %UV,
#            src => [ qw( 3rdparty/libuv/src 3rdparty/libuv/src/win ) ],
#        },
    },
);

our %LINUX = (
    syslibs => [ qw( m pthread uuid rt ) ],

    -thirdparty => {
#        uv => { %UV, objects => '$(UV_LINUX)' },
    },
);

our %OPENBSD = (
    -thirdparty => {
#        uv => { %UV, objects => '$(UV_OPENBSD)' },
    },
);

our %NETBSD = (
    -thirdparty => {
#        uv => { %UV, objects => '$(UV_NETBSD)' },
    },
);

our %FREEBSD = (
    -thirdparty => {
#        uv => { %UV, objects => '$(UV_FREEBSD)' },
    },
);

our %SOLARIS = (
    syslibs => [ qw( socket sendfile nsl uuid pthread m rt ) ],
    mknoisy => '',

    -thirdparty => {
#        uv => { %UV, objects => '$(UV_SOLARIS)' },
    },
);

our %DARWIN = (
    ldsys    => '-framework %s',
    defs     => [ '_DARWIN_USE_64_BIT_INODE=1' ],
    syslibs  => [ qw( ApplicationServices CoreServices Foundation ) ],

    -thirdparty => {
#        uv => { %UV, objects => '$(UV_DARWIN)' },
    },
);

our %SYSTEMS = (
    generic => [ qw( posix gnu gcc ), {} ],
    linux   => [ qw( posix gnu gcc ), { %LINUX } ],
    darwin  => [ qw( posix gnu clang ), { %DARWIN } ],
    openbsd => [ qw( posix bsd gcc ), { %OPENBSD} ],
    netbsd  => [ qw( posix bsd gcc ), { %NETBSD } ],
    freebsd => [ qw( posix bsd clang ), { %FREEBSD } ],
    solaris => [ qw( posix bsd cc ),  { %SOLARIS } ],
    cygwin  => [ qw( posix gnu gcc ), { exe => '.exe' } ],
    win32   => [ qw( win32 msvc cl ), { %WIN32 } ],
    mingw32 => [ qw( win32 gnu gcc ), { %WIN32 } ],
);

42;
