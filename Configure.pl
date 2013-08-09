#!perl

use 5.010;
use strict;
use warnings;

use Getopt::Long;
use Pod::Usage;

my %SHELLS = (
    posix => {
        cat => 'cat',
        rm  => 'rm -f',
        cp  => 'cp',
    },

    win32 => {
        cat => 'type',
        rm  => 'del',
        cp  => 'copy',
    },
);

my %TOOLCHAINS = (
    gnu => {
        -compiler => 'gcc',

        make  => 'make',
        ccout => '-o',
        ccinc => '-I',
        ccdef => '-D',
        cclib => '-l%s',
        ldout => undef,
        obj   => '.o',
        lib   => '.a',
    },

    msvc => {
        -compiler => 'cl',

        make  => 'nmake',
        ccout => '/Fo',
        ccinc => '/I',
        ccdef => '/D',
        cclib => '%s.lib',
        ldout => '/out:',
        obj   => '.obj',
        lib   => '.lib',
    },
);

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
        ccwarnflags  => '-Weverything',
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

        ccmiscflags  => '/nologo',
        ccwarnflags  => '',
        ccoptiflags  => '/Ox /GL',
        ccdebugflags => '/Zi',
        ccinstflags  => '',

        ldmiscflags => '/nologo /MT',
        ldoptiflags  => '/LTCG',
        lddebugflags => '/debug',
        ldinstflags  => '/Profile',

        noreturnspecifier => '__declspec(noreturn)',
        noreturnattribute => '',
    },
);

my %SYSTEMS = (
    generic => [ qw( posix gnu gcc ), {} ],
    linux   => [ qw( posix gnu gcc ), {} ],
    darwin  => [ qw( posix gnu clang ), {} ],
    freebsd => [ qw( posix gnu clang ), {} ],

    cygwin  => [ qw( posix gnu gcc ), {
        exe => '.exe',
    } ],

    win32 => [ qw( win32 msvc cl ), {
        exe  => '.exe',
        defs => [ 'WIN32' ],
        libs => [ qw( ws2_32 mswsock rpcrt4 ) ],
        thirdparty => [ '3rdparty/apr/apr-1' ],
    } ],

    mingw32 => [ qw( win32 gnu gcc ), {
        exe  => '.exe',
        defs => [ 'WIN32' ],
        libs => [ qw( ws2_32 mswsock rpcrt4 ) ],
        make => 'gmake',
    } ],
);

my %args;
my %defaults;
my %config;

sub dots {
    my $message = shift;
    my $length = shift || 55;

    return "$message ". '.' x ($length - length $message) . ' ';
}

sub cross_setup {
    my ($build, $host) = @_;

    print dots "Configuring cross build environment";

    unless (defined $build && defined $host) {
        print "FAIL\n";
        die "    both --build and --host need to be specified\n";
    }

    my $cc        = "$host-gcc";
    my $crossconf = "--build=$build --host=$host";

    for (\$build, \$host) {
        if ($$_ =~ /-(\w+)$/) {
            $$_ = $1;
            if (!exists $SYSTEMS{$1}) {
                print "FAIL\n";
                warn "    unknown OS '$1'\n";
                print dots "    assuming GNU userland";
                $$_ = 'generic';
            }
        }
        else {
            print "FAIL\n";
            die "    failed to parse triple '$$_'\n"
        }
    }

    $build = $SYSTEMS{$build};
    $host  = $SYSTEMS{$host};

    my $shell     = $SHELLS{ $build->[0] };
    my $toolchain = $TOOLCHAINS{gnu};
    my $compiler  = $COMPILERS{gcc};

    @defaults{ keys %$shell }     = values %$shell;
    @defaults{ keys %$toolchain } = values %$toolchain;
    @defaults{ keys %$compiler }  = values %$compiler;

    $defaults{cc}        = $cc;
    $defaults{exe}       = $host->[3]->{exe};
    $defaults{defs}      = $host->[3]->{defs};
    $defaults{libs}      = $host->[3]->{libs};
    $defaults{crossconf} = $crossconf;
}

sub native_setup {
    my ($os) = @_;

    print dots "Configuring native build environment";

    if (!exists $SYSTEMS{$os}) {
        print "FAIL\n";
        warn "    unknown OS '$os'\n";
        print dots "    assuming GNU userland";
        $os = 'generic';
    }

    my ($shell, $toolchain, $compiler, $overrides) = @{$SYSTEMS{$os}};

    $shell     = $SHELLS{$shell};
    $toolchain = $TOOLCHAINS{$toolchain};
    $compiler  = $COMPILERS{$compiler};

    @defaults{ keys %$shell }     = values %$shell;
    @defaults{ keys %$toolchain } = values %$toolchain;
    @defaults{ keys %$compiler }  = values %$compiler;
    @defaults{ keys %$overrides } = values %$overrides;

    if (exists $args{shell}) {
        $shell = $args{shell};
        die "    unsupported shell $shell\n"
            unless exists $SHELLS{$shell};

        $shell = $SHELLS{$shell};
        @defaults{ keys %$shell } = values %$shell;
    }

    if (exists $args{toolchain}) {
        $toolchain = $args{toolchain};
        die "    unsupported toolchain $toolchain\n"
            unless exists $TOOLCHAINS{$toolchain};

        $toolchain = $TOOLCHAINS{$toolchain};
        $compiler  = $COMPILERS{ $toolchain->{-compiler} };

        @defaults{ keys %$toolchain } = values %$toolchain;
        @defaults{ keys %$compiler }  = values %$compiler;
    }

    if (exists $args{compiler}) {
        $compiler = $args{compiler};
        die "    unsupported compiler $compiler\n"
            unless exists $COMPILERS{$compiler};

        $compiler  = $COMPILERS{$compiler};

        unless (exists $args{toolchain}) {
            $toolchain = $TOOLCHAINS{ $compiler->{-toolchain} };
            @defaults{ keys %$toolchain } = values %$toolchain;
        }

        @defaults{ keys %$compiler }  = values %$compiler;
    }
}

sub generate {
    my ($dest, $src) = @_;

    print dots "Generating $dest";

    open my $srcfile, '<', $src or die $!;
    open my $destfile, '>', $dest or die $!;

    while (<$srcfile>) {
        while (/@(\w+)@/) {
            my $key = $1;
            unless (exists $config{$key}) {
                print "FAIL\n";
                die "    unknown configuration key '$key'\n";
            }

            s/@\Q$key\E@/$config{$key}/;
        }

        print $destfile $_;
    }

    close $srcfile;
    close $destfile;

    print "OK\n";
}

my $fail = !GetOptions(\%args, qw(
    help|?
    debug! optimize! instrument!
    os=s shell=s toolchain=s compiler=s
    cc=s ld=s make=s
    build=s host=s
));

die "See --help for further information\n"
    if $fail;

pod2usage(1)
    if $args{help};

print "Welcome to MoarVM!\n\n";

$args{debug}      //= 0 + !$args{optimize};
$args{optimize}   //= 0 + !$args{debug};
$args{instrument} //= 0;

if (exists $args{build} || exists $args{host}) {
    cross_setup $args{build}, $args{host};
}
else {
    native_setup $args{os} // {
        'MSWin32' => 'win32'
    }->{$^O} // $^O;
}

my @keys = qw( cc ld make );
@config{@keys} = @args{@keys};

for (keys %defaults) {
    next if /^-/;
    $config{$_} //= $defaults{$_};
}

$config{exe}       //= '';
$config{defs}      //= [];
$config{libs}      //= [ qw( m pthread ) ];
$config{crossconf} //= '';

$config{thirdparty} //= [ qw(
    3rdparty/libatomic_ops/src/libatomic_ops
    3rdparty/apr/.libs/libapr-1
) ];

$config{thirdpartylibs} //= join ' ',
    "3rdparty/sha1/sha1$config{obj}",
    map { "$_$config{lib}" } @{$config{thirdparty}};

$config{ld}           //= $config{cc};
$config{ldout}        //= $config{ccout};
$config{ldmiscflags}  //= $config{ccmiscflags};
$config{ldoptiflags}  //= $config{ccoptiflags};
$config{lddebugflags} //= $config{ccdebugflags};
$config{ldinstflags}  //= $config{ccinstflags};

$config{ldlibs} = join ' ', map {
    sprintf $config{cclib}, $_;
} @{$config{libs}};

my @cflags = ($config{ccmiscflags});
push @cflags, $config{ccoptiflags}  if $args{optimize};
push @cflags, $config{ccdebugflags} if $args{debug};
push @cflags, $config{ccinstflags}  if $args{instrument};
push @cflags, $config{ccwarnflags};
push @cflags, map { "$config{ccdef}$_" } @{$config{defs}};
$config{cflags} //= join ' ', @cflags;

my @ldflags = ($config{ldmiscflags});
push @ldflags, $config{ldoptiflags}  if $args{optimize};
push @ldflags, $config{lddebugflags} if $args{debug};
push @ldflags, $config{ldinstflags}  if $args{instrument};
$config{ldflags} //= join ' ', @ldflags;

print "OK\n",
    "        compile: $config{cc} $config{cflags}\n",
    "           link: $config{ld} $config{ldflags}\n",
    "           libs: $config{ldlibs}\n",
    "       3rdparty: " . join("\n". ' ' x 17,
        split / /, $config{thirdpartylibs}) . "\n",
    "           make: $config{make}\n";

open my $listfile, '<', 'gen.list'
    or die $!;

my $target;
while (<$listfile>) {
    s/^\s+|\s+$//;
    next if /^#|^$/;

    $target = $_, next
        unless defined $target;

    generate $target, $_;
    $target = undef;
}

close $listfile;

print "\nConfiguration successful. Type '$config{'make'}' to build.\n";

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

Except for C<-?|--help>, any option can be explicitly turned off by
preceding it with C<no->, as in C<--no-optimize>.

=item -?|--help

Show this help information.

=item --debug

Turn on debugging flags during compile and link.  If C<--optimize> is not
explicitly set, debug defaults to on, and optimize defaults to off.

=item --optimize

Turn on optimization flags during compile and link.  If C<--debug> is not
explicitly set, turning this on defaults debugging off; otherwise this
defaults to the opposite of C<--debug>.

=item --instrument

Turn on extra instrumentation flags during compile and link; for example,
turns on Address Sanitizer when compiling with F<clang>.  Defaults to off.

=item TODO

=back
