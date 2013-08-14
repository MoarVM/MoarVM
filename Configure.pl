#!perl

use 5.010;
use strict;
use warnings;

use Getopt::Long;
use Pod::Usage;

my $NAME    = 'moarvm';
my $GENLIST = 'build/gen.list';

# 3rdparty library configuration

my %APR = (
    name  => 'apr-1',
    path  => '3rdparty/apr/.libs',
    rule  => 'cd 3rdparty/apr && CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' ./configure --disable-shared @crossconf@ && $(MAKE)',
    clean => '-cd 3rdparty/apr && $(MAKE) distclean',
);

my %LAO = (
    name  => 'atomic_ops',
    path  => '3rdparty/libatomic_ops/src',
    rule  => 'cd 3rdparty/libatomic_ops && CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' ./configure @crossconf@ && $(MAKE)',
    clean => '-cd 3rdparty/libatomic_ops && $(MAKE) distclean',
);

my %SHA = (
    name => 'sha1',
    path => '3rdparty/sha1',
    src  => [ '3rdparty/sha1' ],
);

my %TOM = (
    name => 'tommath',
    path => '3rdparty/libtommath',
    src  => [ '3rdparty/libtommath' ],
);

my %LN = (
    name => 'linenoise',
    path => '3rdparty/linenoise',
    src  => [ '3rdparty/linenoise' ],
);

my %DC = (
    name  => 'dyncall_s',
    path  => '3rdparty/dyncall/dyncall',
    rule  => 'cd 3rdparty/dyncall &&  ./configure2 && CC=\'$(CC)\' CFLAGS=\'$(CFLAGS)\' $(MAKE) -f Makefile',
    clean => '-cd 3rdparty/dyncall && $(MAKE) -f Makefile clean',
);

my %DCB = (
    name  => 'dyncallback_s',
    path  => '3rdparty/dyncall/dyncallback',
    dummy => 1, # created as part of dyncall build
);

my %DL = (
    name  => 'dynload_s',
    path  => '3rdparty/dyncall/dynload',
    dummy => 1, # created as part of dyncall build
);

my %UVDUMMY = (
    name => 'uv',
    path => '3rdparty/libuv',
    # no default rule
    # building libuv is always OS-specific
);

my %UV = (
    %UVDUMMY,
    rule  => '$(AR) $(ARFLAGS) @arout@$@ $(UV_OBJECTS)',
    clean => '-$(RM) @uvlib@ $(UV_OBJECTS)',
    # actually insufficient to build libuv
    # the OS needs to provide a C<src> or C<objects> setting
);

my %THIRDPARTY = (
    apr => { %APR },
    lao => { %LAO },
    tom => { %TOM },
    sha => { %SHA },
    ln  => { %LN },
    dc  => { %DC },
    dcb => { %DCB },
    dl  => { %DL },
    uv  => { %UVDUMMY },
);

# shell configuration
# selected by C<--shell>

my %SHELLS = (
    posix => {
        sh  => 'sh',
        cat => 'cat',
        rm  => 'rm -f',
    },

    win32 => {
        sh  => 'cmd',
        cat => 'type',
        rm  => 'del',
    },
);

# toolchain configuration
# selected by C<--toolchain>

my %TOOLCHAINS = (
    gnu => {
        -compiler => 'gcc',

        make => 'make',
        ar   => 'ar',

        ccswitch => '-c',
        ccout    => '-o',
        ccinc    => '-I',
        ccdef    => '-D',

        ldout => undef,
        ldarg => '-l%s',

        arflags => 'rcs',
        arout   => '',

        obj => '.o',
        lib => 'lib%s.a',

        -auxfiles => [],
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
        ldarg => '%s.lib',

        arflags => '/nologo',
        arout   => '/out:',

        obj => '.obj',
        lib => '%s.lib',

        -auxfiles => [ qw( @name@.ilk @name@.pdb vc100.pdb ) ],

        -thirdparty => {
            apr => {
                %APR,
                rule  => 'cd 3rdparty/apr && $(MAKE) -f Makefile.win ARCH="Win32 Release" buildall',
                clean => '-cd 3rdparty/apr && $(MAKE) -f Makefile.win ARCH="Win32 Release" clean',
            },

            dc => {
                %DC,
                name  => 'libdyncall_s',
                rule  => 'cd 3rdparty/dyncall && configure.bat /target-x86 && $(MAKE) -f Nmakefile',
                clean => '-$(RM) 3rdparty/dyncall/ConfigVars @dclib@ @dcblib@ @dllib@ 3rdparty/dyncall/dyncall/*.obj 3rdparty/dyncall/dyncallback/*.obj 3rdparty/dyncall/dynload/*.obj',
            },

            dcb => { %DCB, name => 'libdyncallback_s' },
            dl  => { %DL, name => 'libdynload_s' },
        },
    },
);

# compiler configuration
# selected by C<--compiler>

my %COMPILERS = (
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
);

# OS configuration
# selected by C<--os> or taken from C<$^O>

my %WIN32 = (
    exe  => '.exe',
    defs => [ 'WIN32' ],
    libs => [ qw( shell32 ws2_32 mswsock rpcrt4 advapi32 psapi iphlpapi) ],

    -thirdparty => {
        # header only, no need to build anything
        lao => undef,

        uv => {
            %UV,
            src => [ qw( 3rdparty/libuv/src 3rdparty/libuv/src/win ) ],
        },
    },
);

my %LINUX = (
    -thirdparty => {
        uv => { %UV, objects => '$(UV_LINUX)' },
    },
);

my %OPENBSD = (
    -thirdparty => {
        uv => { %UV, objects => '$(UV_OPENBSD)' },
    },
);

my %NETBSD = (
    -thirdparty => {
        uv => { %UV, objects => '$(UV_NETBSD)' },
    },
);

my %FREEBSD = (
    -thirdparty => {
        uv => { %UV, objects => '$(UV_FREEBSD)' },
    },
);

my %DARWIN = (
    ldarg => '-framework %s',
    defs  => [ '_DARWIN_USE_64_BIT_INODE=1' ],
    libs  => [ qw( ApplicationServices CoreServices Foundation ) ],

    -thirdparty => {
        uv => { %UV, objects => '$(UV_DARWIN)' },
    },
);

my %SYSTEMS = (
    generic => [ qw( posix gnu gcc ), {} ],
    linux   => [ qw( posix gnu gcc ), { %LINUX } ],
    darwin  => [ qw( posix gnu clang ), { %DARWIN } ],
    openbsd => [ qw( posix gnu gcc ), { %OPENBSD} ],
    netbsd  => [ qw( posix gnu gcc ), { %NETBSD } ],
    freebsd => [ qw( posix gnu clang ), { %FREEBSD } ],
    cygwin  => [ qw( posix gnu gcc ), { exe => '.exe' } ],
    win32   => [ qw( win32 msvc cl ), { %WIN32 } ],
    mingw32 => [ qw( win32 gnu gcc ), { %WIN32 } ],
);

# configuration logic

my $failed = 0;

my %args;
my %defaults;
my %config;

GetOptions(\%args, qw(
    help|?
    debug! optimize! instrument!
    os=s shell=s toolchain=s compiler=s
    cc=s ld=s make=s
    build=s host=s
)) or die "See --help for further information\n";

pod2usage(1) if $args{help};

print "Welcome to MoarVM!\n\n";

# fiddle with flags
$args{debug}      //= 0 + !$args{optimize};
$args{optimize}   //= 0 + !$args{debug};
$args{instrument} //= 0;

# fill in C<%defaults>
if (exists $args{build} || exists $args{host}) {
    cross_setup($args{build}, $args{host});
}
else {
    native_setup($args{os} // {
        'MSWin32' => 'win32'
    }->{$^O} // $^O);
}

$config{name} = $NAME;

# set options that take priority over all others
my @keys = qw( cc ld make );
@config{@keys} = @args{@keys};

for (keys %defaults) {
    next if /^-/;
    $config{$_} //= $defaults{$_};
}

# misc defaults
$config{exe}       //= '';
$config{defs}      //= [];
$config{libs}      //= [ qw( m pthread ) ];
$config{crossconf} //= '';

# assume the compiler can be used as linker frontend
$config{ld}           //= $config{cc};
$config{ldout}        //= $config{ccout};
$config{ldmiscflags}  //= $config{ccmiscflags};
$config{ldoptiflags}  //= $config{ccoptiflags};
$config{lddebugflags} //= $config{ccdebugflags};
$config{ldinstflags}  //= $config{ccinstflags};

# mangle OS library names
$config{ldlibs} = join ' ', map {
    sprintf $config{ldarg}, $_;
} @{$config{libs}};

# generate CFLAGS
my @cflags = ($config{ccmiscflags});
push @cflags, $config{ccoptiflags}  if $args{optimize};
push @cflags, $config{ccdebugflags} if $args{debug};
push @cflags, $config{ccinstflags}  if $args{instrument};
push @cflags, $config{ccwarnflags};
push @cflags, map { "$config{ccdef}$_" } @{$config{defs}};
$config{cflags} = join ' ', @cflags;

# generate LDFLAGS
my @ldflags = ($config{ldmiscflags});
push @ldflags, $config{ldoptiflags}  if $args{optimize};
push @ldflags, $config{lddebugflags} if $args{debug};
push @ldflags, $config{ldinstflags}  if $args{instrument};
$config{ldflags} = join ' ', @ldflags;

# some toolchains generate garbage
my @auxfiles = @{ $defaults{-auxfiles} };
$config{clean} = @auxfiles ? '-$(RM) ' . join ' ', @auxfiles : '@:';

print "OK\n";

# detect x64 on Windows so we can build the correct APR version
# something of a hack, but works for now
if ($config{cc} eq 'cl') {
    print dots('    auto-detecting x64 toolchain');
    my $msg = `cl 2>&1`;
    if (defined $msg) {
        if ($msg =~ /x64/) {
            print "YES\n";
            $defaults{-thirdparty}->{apr} = {
                %APR,
                path  => '3rdparty/apr/x64/LibR',
                rule  => 'cd 3rdparty/apr && $(MAKE) -f Makefile.win ARCH="x64 Release" buildall',
                clean => '-cd 3rdparty/apr && $(MAKE) -f Makefile.win ARCH="x64 Release" clean',
            };

            $defaults{-thirdparty}->{dc}->{rule} =
                'cd 3rdparty/dyncall && configure.bat /target-x64 && $(MAKE) -f Nmakefile';
        }
        else { print "NO\n" }
    }
    else {
        softfail("could not run 'cl'");
        print dots('    assuming x86'), "OK\n";
    }
}

# dump configuration
print "\n", <<TERM, "\n";
      make: $config{make}
   compile: $config{cc} $config{cflags}
      link: $config{ld} $config{ldflags}
      libs: $config{ldlibs}
TERM

print dots('Configuring 3rdparty libs');

my @thirdpartylibs;
my $thirdparty = $defaults{-thirdparty};

for (keys %$thirdparty) {
    my $current = $thirdparty->{$_};
    my @keys = ( "${_}lib", "${_}objects", "${_}rule", "${_}clean");

    # don't build the library (libatomic_ops can be header-only)
    unless (defined $current) {
        @config{@keys} = ("__${_}__", '', '@:', '@:');
        next;
    }

    my ($lib, $objects, $rule, $clean);

    $lib = sprintf "%s/$config{lib}",
        $current->{path},
        $current->{name};

    # C<rule> and C<build> can be used to augment all build types
    $rule  = $current->{rule};
    $clean = $current->{clean};

    # select type of build

     # dummy build - nothing to do
    if (exists $current->{dummy}) {
        $clean //= sprintf '-$(RM) %s', $lib;
    }

    # use explicit object list
    elsif (exists $current->{objects}) {
        $objects = $current->{objects};
        $rule  //= sprintf '$(AR) $(ARFLAGS) @arout@$@ @%sobjects@', $_;
        $clean //= sprintf '-$(RM) @%slib@ @%sobjects@', $_, $_;
    }

    # find *.c files and build objects for those
    elsif (exists $current->{src}) {
        my @sources = map { glob "$_/*.c" } @{ $current->{src} };
        my $globs   = join ' ', map { $_ . '/*@obj@' } @{ $current->{src} };

        $objects = join ' ', map { s/\.c$/\@obj\@/; $_ } @sources;
        $rule  //= sprintf '$(AR) $(ARFLAGS) @arout@$@ %s', $globs;
        $clean //= sprintf '-$(RM) %s %s', $lib, $globs;
    }

    # use an explicit rule (which has already been set)
    elsif (exists $current->{rule}) {}

    # give up
    else {
        softfail("no idea how to build '$lib'");
        print dots('    continuing anyway');
    }

    @config{@keys} = ($lib, $objects // '', $rule // '@:', $clean // '@:');

    push @thirdpartylibs, $config{"${_}lib"};
}

$config{thirdpartylibs} = join ' ', @thirdpartylibs;
my $thirdpartylibs = join "\n" . ' ' x 12, sort @thirdpartylibs;

print "OK\n";

# dump 3rdparty libs we need to build
print "\n", <<TERM, "\n";
  3rdparty: $thirdpartylibs
TERM

# read list of files to generate

open my $listfile, '<', $GENLIST
    or die "$GENLIST: $!\n";

my $target;
while (<$listfile>) {
    s/^\s+|\s+$//;
    next if /^#|^$/;

    $target = $_, next
        unless defined $target;

    generate($target, $_);
    $target = undef;
}

close $listfile;

# configuration completed

print "\n", $failed ? <<TERM1 : <<TERM2;
Configuration FAIL. You can try to salvage the generated Makefile.
TERM1
Configuration SUCCESS. Type '$config{'make'}' to build.
TERM2

exit $failed;

# helper functions

# fill in defaults for native builds
sub native_setup {
    my ($os) = @_;

    print dots("Configuring native build environment");

    if (!exists $SYSTEMS{$os}) {
        softfail("unknown OS '$os'");
        print dots("    assuming GNU userland");
        $os = 'generic';
    }

    my ($shell, $toolchain, $compiler, $overrides) = @{$SYSTEMS{$os}};
    $shell     = $SHELLS{$shell};
    $toolchain = $TOOLCHAINS{$toolchain};
    $compiler  = $COMPILERS{$compiler};
    set_defaults($shell, $toolchain, $compiler, $overrides);

    if (exists $args{shell}) {
        $shell = $args{shell};
        hardfail("unsupported shell '$shell'")
            unless exists $SHELLS{$shell};

        $shell = $SHELLS{$shell};
        set_defaults($shell);
    }

    if (exists $args{toolchain}) {
        $toolchain = $args{toolchain};
        hardfail("unsupported toolchain '$toolchain'")
            unless exists $TOOLCHAINS{$toolchain};

        $toolchain = $TOOLCHAINS{$toolchain};
        $compiler  = $COMPILERS{ $toolchain->{-compiler} };
        set_defaults($toolchain, $compiler);
    }

    if (exists $args{compiler}) {
        $compiler = $args{compiler};
        hardfail("unsupported compiler '$compiler'")
            unless exists $COMPILERS{$compiler};

        $compiler  = $COMPILERS{$compiler};

        unless (exists $args{toolchain}) {
            $toolchain = $TOOLCHAINS{ $compiler->{-toolchain} };
            set_defaults($toolchain);
        }

        set_defaults($compiler);
    }
}

# fill in defaults for cross builds
sub cross_setup {
    my ($build, $host) = @_;

    print dots("Configuring cross build environment");

    hardfail("both --build and --host need to be specified")
        unless defined $build && defined $host;

    my $cc        = "$host-gcc";
    my $ar        = "$host-ar";
    my $crossconf = "--build=$build --host=$host";

    for (\$build, \$host) {
        if ($$_ =~ /-(\w+)$/) {
            $$_ = $1;
            if (!exists $SYSTEMS{$1}) {
                softfail("unknown OS '$1'");
                print dots("    assuming GNU userland");
                $$_ = 'generic';
            }
        }
        else { hardfail("failed to parse triple '$$_'") }
    }

    $build = $SYSTEMS{$build};
    $host  = $SYSTEMS{$host};

    my $shell     = $SHELLS{ $build->[0] };
    my $toolchain = $TOOLCHAINS{gnu};
    my $compiler  = $COMPILERS{gcc};
    my $overrides = $host->[3];

    set_defaults($shell, $toolchain, $compiler, $overrides);

    $defaults{cc}        = $cc;
    $defaults{ar}        = $ar;
    $defaults{crossconf} = $crossconf;
}

# sets C<%defaults> from C<@_>
sub set_defaults {
    # getting the correct 3rdparty information is somewhat tricky
    my $thirdparty = $defaults{-thirdparty} // \%THIRDPARTY;
    @defaults{ keys %$_ } = values %$_ for @_;
    $defaults{-thirdparty} = {
        %$thirdparty, map{ %{ $_->{-thirdparty} // {} } } @_
    };
}

# fill in config values
sub configure {
    my ($_) = @_;

    while (/@(\w+)@/) {
        my $key = $1;
        return (undef, "unknown configuration key '$key'")
            unless exists $config{$key};

        s/@\Q$key\E@/$config{$key}/;
    }

    return $_;
}

# generate files
sub generate {
    my ($dest, $src) = @_;

    print dots("Generating $dest");

    open my $srcfile, '<', $src or hardfail($!);
    open my $destfile, '>', $dest or hardfail($!);

    while (<$srcfile>) {
        my ($_, $error) = configure($_);
        hardfail($error)
            unless defined $_;

        # in-between slashes in makefiles need to be backslashes on Windows
        s/(\w|\.)\/(\w|\.|\*)/$1\\$2/g
            if $dest =~ /Makefile/ && $config{sh} eq 'cmd';

        print $destfile $_;
    }

    close $srcfile;
    close $destfile;

    print "OK\n";
}

# some dots
sub dots {
    my $message = shift;
    my $length = shift || 55;

    return "$message ". '.' x ($length - length $message) . ' ';
}

# fail but continue
sub softfail {
    my ($msg) = @_;
    $failed = 1;
    print "FAIL\n";
    warn "    $msg\n";
}


# fail and don't continue
sub hardfail {
    softfail(@_);
    die "\nConfiguration PANIC. A Makefile could not be generated.\n";
}


__END__

=head1 SYNOPSIS

    ./Configure.pl -?|--help

    ./Configure.pl [--os <os>] [--shell <shell>]
                   [--toolchain <toolchain>] [--compiler <compiler>]
                   [--cc <cc>] [--ld <ld>] [--make <make>]
                   [--debug] [--optimize] [--instrument]

    ./Configure.pl --build <build-triple> --host <host-triple>
                   [--cc <cc>] [--ld <ld>] [--make <make>]
                   [--debug] [--optimize] [--instrument]

=head1 OPTIONS

=over 4

=item -?

=item --help

Show this help information.

=item --debug

=item --no-debug

Toggle debugging flags during compile and link.  If C<--optimize> is not
explicitly set, debug defaults to on, and optimize defaults to off.

=item --optimize

=item --no-optimize

Toggle optimization flags during compile and link.  If C<--debug> is not
explicitly set, turning this on defaults debugging off; otherwise this
defaults to the opposite of C<--debug>.

=item --instrument

=item --no-instrument

Toggle extra instrumentation flags during compile and link; for example,
turns on Address Sanitizer when compiling with C<clang>.  Defaults to off.

=item --os <os>

If not explicitly set, the operating system is provided by the Perl
runtime.  In case of unknown operating systems, a GNU userland is assumed.

=item --shell <shell>

Currently supported shells are C<posix> and C<win32>.

=item --toolchain <toolchain>

Currently supported toolchains are C<gnu> and C<msvc>.

=item --compiler <compiler>

Currently supported compilers are C<gcc>, C<clang> and C<cl>.

=item --cc <cc>

Explicitly set the compiler without affecting other configuration
options.

=item --ld <ld>

Explicitly set the linker without affecting other configuration
options.

=item --make <make>

Explicitly set the make tool without affecting other configuration
options.

=item --build <build-triple> --host <host-triple>

Set up cross-compilation.

=back
