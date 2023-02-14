#!/usr/bin/env perl

use strict;
use warnings;

use Config;
use Cwd;
use Getopt::Long;
use Pod::Usage;
use File::Spec;

use lib '.';
use build::setup;
use build::probe;

# This allows us to run on ancient perls.
sub defined_or($$) {
    defined $_[0] ? $_[0] : $_[1]
}
# This allows us to run on perls without List::Util
sub uniq {
    my %s; grep { !$s{$_}++ } @_;
}


my $NAME    = 'moar';
my $GENLIST = 'build/gen.list';

# configuration logic

my $failed = 0;

my %args;
my %defaults;
my %config;
# In case a submodule folder needs to be deleted. We set this and print it
# out at the very end.
my $folder_to_delete = '';
my @args = @ARGV;

GetOptions(\%args, qw(
    help|?
    debug:s optimize:s coverage
    os=s shell=s toolchain=s compiler=s
    ar=s cc=s ld=s make=s has-sha has-libuv
    static has-libtommath has-libatomic_ops
    has-dyncall has-libffi pkgconfig=s
    build=s host=s big-endian jit! enable-jit
    prefix=s bindir=s libdir=s mastdir=s
    relocatable make-install asan ubsan tsan
    valgrind telemeh! dtrace show-autovect git-cache-dir=s
    show-autovect-failed:s mimalloc! has-mimalloc c11-atomics!),

    'no-optimize|nooptimize' => sub { $args{optimize} = 0 },
    'no-debug|nodebug' => sub { $args{debug} = 0 },
) or die "See --help for further information\n";


pod2usage(1) if $args{help};

print "Welcome to MoarVM!\n\n";

$config{prefix} = File::Spec->rel2abs(defined_or $args{prefix}, 'install');
# don't install to cwd, as this would clash with lib/MAST/*.nqp
if (-e 'README.markdown' && -e "$config{prefix}/README.markdown"
 && -s 'README.markdown' == -s "$config{prefix}/README.markdown") {
    die <<ENOTTOCWD;
Configuration FAIL. Installing to MoarVM root folder is not allowed.
Please specify another installation target by using --prefix=PATH.
ENOTTOCWD
}

# Override default target directories with command line arguments
my @target_dirs = qw{bindir libdir mastdir};
for my $target (@target_dirs) {
    $config{$target} = $args{$target} if $args{$target};
}

# Download / Update submodules
my $code = system($^X, 'tools/update-submodules.pl', Cwd::cwd(), @args);
exit 1 if $code >> 8 != 0;

# fiddle with flags
$args{optimize}     = 3 if not defined $args{optimize} or $args{optimize} eq "";
$args{debug}        = 3 if defined $args{debug} and $args{debug} eq "";

# Relocatability is not supported on AIX and OpenBSD.
if ( $args{relocatable} && ($^O eq 'aix' || $^O eq 'openbsd') ) {
    hardfail('Relocatability is not supported on ' . $^O .
    ".\n    Leave off the --relocatable flag to do a non-relocatable build.");
}

for (qw(coverage static big-endian has-libtommath has-sha has-libuv
        has-libatomic_ops has-mimalloc asan ubsan tsan valgrind dtrace show-vec)) {
    $args{$_} = 0 unless defined $args{$_};
}


# jit is default
$args{jit} = 1 unless defined $args{jit};

# fill in C<%defaults>
if (exists $args{build} || exists $args{host}) {
    setup_cross($args{build}, $args{host});
}
else {
    setup_native(defined_or $args{os}, $^O);
}

$config{name}   = $NAME;
$config{perl}   = $^X;
$config{config} = join ' ', map { / / ? "\"$_\"" : $_ } @args;
$config{osname} = $^O;
$config{osvers} = $Config{osvers};
$config{pkgconfig} = defined_or $args{pkgconfig}, '/usr/bin/pkg-config';



# set options that take priority over all others
my @keys = qw( ar cc ld make );
@config{@keys} = @args{@keys};

for (keys %defaults) {
    next if /^-/;
    $config{$_} = $defaults{$_} unless defined $config{$_};
}

my $VERSION = '0.0-0';
# get version
if (open(my $fh, '<', 'VERSION')) {
    $VERSION = <$fh>;
    close($fh);
}
# .git is a file and not a directory in submodule
if (-e '.git' && open(my $GIT, '-|', 'git describe --tags "--match=20*"')) {
    $VERSION = <$GIT>;
    close($GIT);
}
chomp $VERSION;
$config{version}      = $VERSION;
$config{versionmajor} = $VERSION =~ /^(\d+)/ ? $1 : 0;
$config{versionminor} = $VERSION =~ /^\d+\.(\d+)/ ? $1 : 0;
$config{versionpatch} = $VERSION =~ /^\d+\.\d+\-(\d+)/ ? $1 : 0;

# misc defaults
$config{exe}                      = '' unless defined $config{exe};
$config{defs}                     = [] unless defined $config{defs};
$config{syslibs}                  = [] unless defined $config{syslibs};
$config{usrlibs}                  = [] unless defined $config{usrlibs};
$config{platform}                 = '' unless defined $config{platform};
$config{crossconf}                = '' unless defined $config{crossconf};
$config{dllimport}                = '' unless defined $config{dllimport};
$config{dllexport}                = '' unless defined $config{dllexport};
$config{dlllocal}                 = '' unless defined $config{dlllocal};
$config{translate_newline_output} = 0  unless defined $config{translate_newline_output};
$config{vectorizerspecifier}      = '' unless defined $config{vectorizerspecifier};

# assume the compiler can be used as linker frontend
$config{ld}           = $config{cc} unless defined $config{ld};
$config{ldout}        = $config{ccout} unless defined $config{ldout};
$config{ldsys}        = $config{ldusr} unless defined $config{ldsys};
$config{ldoptiflags}  = $config{ccoptiflags} unless defined $config{ldoptiflags};
$config{lddebugflags} = $config{ccdebugflags} unless defined $config{lddebugflags};
$config{ldinstflags}  = $config{ccinstflags} unless defined $config{ldinstflags};

$config{as}           = $config{cc} unless defined $config{as};

# If we're in macOS, let's verify that the toolchain is consistent.
if ($^O eq 'darwin') {
    my $gnu_count = 0;
    my $gnu_toolchain = (exists $args{toolchain}) && ($args{toolchain} eq 'gnu');

    unless ($gnu_toolchain) {
        # When XCode toolchain is used then force use of XCode's make if
        # available.
        $config{make} = '/usr/bin/make' if -x '/usr/bin/make'; 
    }

    # Here are the tools that seem to cause trouble.
    # If you see other ones, please add them to this list.
    my @check_tools = qw/ar cc ld/;
    for my $tool (map { `which $_` } @config{@check_tools}) {
        chomp $tool;
        system "grep -b 'gnu' '$tool'"; # Apple utilities don't match `gnu`
        if ($? == 0) {
            $gnu_count += 1;
        }
    }

    ## For a GNU toolchain, make sure that they're all GNU.
    if ($gnu_toolchain && $gnu_count != scalar @check_tools) {
        print "\nNot all tools in the toolchain are GNU. Please correct this and retry.\n"
            . "See README.markdown for more details.\n\n";
        exit -1;
    }

    ## Otherwise, make sure that none of them are GNU
    elsif (!$gnu_toolchain && $gnu_count != 0) {
        print "\nGNU tools detected, despite this not being a GNU-oriented build.\n"
            ." Please correct this and retry. See README.markdown for more details.\n\n";
        exit -1;
    }
}

# Probe the compiler.
build::probe::compiler_usability(\%config, \%defaults);

# Remove unsupported -Werror=* gcc flags if gcc doesn't support them.
build::probe::specific_werror(\%config, \%defaults);
if ($config{cc} eq 'gcc' && !$config{can_specific_werror}) {
    $config{ccmiscflags} =~ s/-Werror=[^ ]+//g;
    $config{ccmiscflags} =~ s/ +/ /g;
    $config{ccmiscflags} =~ s/^ +$//;
}

# Disable RETGUARD on OpenBSD, since it breaks the legojit.
if ($^O eq 'openbsd' && $args{jit} && $config{cc} eq 'clang') {
    $config{ccmiscflags} .= ' -fno-ret-protector';
}

# Set the remaining ldmiscflags. Do this after probing for gcc -Werror probe to not miss that change for the linker.
$config{ldmiscflags}  = $config{ccmiscflags} unless defined $config{ldmiscflags};

# Include paths that NQP/Rakudo are going to need in their build.
my @hllincludes = qw( moar );

if ($args{'has-sha'}) {
    $config{shaincludedir} = '/usr/include/sha';
    $defaults{-thirdparty}->{sha} = undef;
    unshift @{$config{usrlibs}}, 'sha';
}
else { $config{shaincludedir} = '3rdparty/sha1' }

# After upgrading from libuv from 0.11.18 to 0.11.29 we see very weird errors
# when the old libuv files are still around. Running a `make realclean` in
# case we spot an old file and the Makefile is already there.
if (-e '3rdparty/libuv/src/unix/threadpool' . $defaults{obj}
 && -e 'Makefile') {
    print("\nMaking realclean after libuv version upgrade.\n"
        . "Outdated files were detected.\n");
    system($defaults{make}, 'realclean')
}

# test whether pkg-config works
if (-e "$config{pkgconfig}") {
    print dots("    Testing pkgconfig");
    system("$config{pkgconfig}", "--version");
    if ( $? == 0 ) {
        $config{pkgconfig_works} = 1;
    } else {
        $config{pkgconfig_works} = 0;
    }
}

# conditionally set include dirs and install rules
$config{cincludes} = '' unless defined $config{cincludes};
$config{moar_cincludes} = '' unless defined $config{moar_cincludes};
$config{lincludes} = '' unless defined $config{lincludes};
$config{install}   = '' unless defined $config{install};
if ($args{'has-libuv'}) {
    $defaults{-thirdparty}->{uv} = undef;
    unshift @{$config{usrlibs}}, 'uv';
    setup_native_library('libuv') if $config{pkgconfig_works};
}
else {
    $config{moar_cincludes} .= ' ' . $defaults{ccinc} . '3rdparty/libuv/include'
                             . ' ' . $defaults{ccinc} . '3rdparty/libuv/src';
    $config{install}   .= "\t\$(MKPATH) \"\$(DESTDIR)\$(PREFIX)/include/libuv\"\n"
                        . "\t\$(MKPATH) \"\$(DESTDIR)\$(PREFIX)/include/libuv/uv\"\n"
                        . "\t\$(CP) 3rdparty/libuv/include/*.h \"\$(DESTDIR)\$(PREFIX)/include/libuv\"\n"
                        . "\t\$(CP) 3rdparty/libuv/include/uv/*.h \"\$(DESTDIR)\$(PREFIX)/include/libuv/uv\"\n";
    push @hllincludes, 'libuv';
}

# we just need a minimal ldlibs configured for the stdatomic probe,
# it will get overwritten later
$config{ldlibs} = join ' ',
    $config{lincludes},
    (map { sprintf $config{ldusr}, $_; } @{$config{usrlibs}}),
    (map { sprintf $config{ldsys}, $_; } @{$config{syslibs}});

# probe for working stdatomic.h, also used by mimalloc
build::probe::stdatomic(\%config, \%defaults);

$config{use_c11_atomics} = defined $args{'c11-atomics'}
    ? $args{'c11-atomics'}   ? 1 : 0
    : $config{has_stdatomic} ? 1 : 0; # default to on if available

if ($config{use_c11_atomics}) {
    $defaults{-thirdparty}->{lao} = undef;
}
elsif ($args{'has-libatomic_ops'}) {
    $defaults{-thirdparty}->{lao} = undef;
    unshift @{$config{usrlibs}}, 'atomic_ops';
    setup_native_library('atomic_ops') if $config{pkgconfig_works};
}
else {
    $config{moar_cincludes} .= ' ' . $defaults{ccinc} . '3rdparty/libatomicops/src';
    my $lao             = '$(DESTDIR)$(PREFIX)/include/libatomic_ops';
    $config{install}   .= "\t\$(MKPATH) \"$lao/atomic_ops/sysdeps/armcc\"\n"
                        . "\t\$(MKPATH) \"$lao/atomic_ops/sysdeps/gcc\"\n"
                        . "\t\$(MKPATH) \"$lao/atomic_ops/sysdeps/hpc\"\n"
                        . "\t\$(MKPATH) \"$lao/atomic_ops/sysdeps/ibmc\"\n"
                        . "\t\$(MKPATH) \"$lao/atomic_ops/sysdeps/icc\"\n"
                        . "\t\$(MKPATH) \"$lao/atomic_ops/sysdeps/loadstore\"\n"
                        . "\t\$(MKPATH) \"$lao/atomic_ops/sysdeps/msftc\"\n"
                        . "\t\$(MKPATH) \"$lao/atomic_ops/sysdeps/sunc\"\n"
                        . "\t\$(CP) 3rdparty/libatomicops/src/*.h \"$lao\"\n"
                        . "\t\$(CP) 3rdparty/libatomicops/src/atomic_ops/*.h \"$lao/atomic_ops\"\n"
                        . "\t\$(CP) 3rdparty/libatomicops/src/atomic_ops/sysdeps/*.h \"$lao/atomic_ops/sysdeps\"\n"
                        . "\t\$(CP) 3rdparty/libatomicops/src/atomic_ops/sysdeps/armcc/*.h \"$lao/atomic_ops/sysdeps/armcc\"\n"
                        . "\t\$(CP) 3rdparty/libatomicops/src/atomic_ops/sysdeps/gcc/*.h \"$lao/atomic_ops/sysdeps/gcc\"\n"
                        . "\t\$(CP) 3rdparty/libatomicops/src/atomic_ops/sysdeps/hpc/*.h \"$lao/atomic_ops/sysdeps/hpc\"\n"
                        . "\t\$(CP) 3rdparty/libatomicops/src/atomic_ops/sysdeps/ibmc/*.h \"$lao/atomic_ops/sysdeps/ibmc\"\n"
                        . "\t\$(CP) 3rdparty/libatomicops/src/atomic_ops/sysdeps/icc/*.h \"$lao/atomic_ops/sysdeps/icc\"\n"
                        . "\t\$(CP) 3rdparty/libatomicops/src/atomic_ops/sysdeps/loadstore/*.h \"$lao/atomic_ops/sysdeps/loadstore\"\n"
                        . "\t\$(CP) 3rdparty/libatomicops/src/atomic_ops/sysdeps/msftc/*.h \"$lao/atomic_ops/sysdeps/msftc\"\n"
                        . "\t\$(CP) 3rdparty/libatomicops/src/atomic_ops/sysdeps/sunc/*.h \"$lao/atomic_ops/sysdeps/sunc\"\n";
    push @hllincludes, 'libatomic_ops';
}

if ($args{'has-libtommath'}) {
    $defaults{-thirdparty}->{tom} = undef;
    unshift @{$config{usrlibs}}, 'tommath';
    if (not $config{crossconf}) {
        if (index($config{cincludes}, '-I/usr/local/include') == -1) {
            $config{cincludes} = join(' ', $config{cincludes}, '-I/usr/local/include');
        }
        if (index($config{lincludes}, '-L/usr/local/lib') == -1) {
            $config{lincludes} = join(' ', $config{lincludes}, '-L/usr/local/lib');
        }
    }
}
else {
    $config{moar_cincludes} .= ' ' . $defaults{ccinc} . '3rdparty/libtommath';
    $config{install}   .= "\t\$(MKPATH) \"\$(DESTDIR)\$(PREFIX)/include/libtommath\"\n"
                        . "\t\$(CP) 3rdparty/libtommath/*.h \"\$(DESTDIR)\$(PREFIX)/include/libtommath\"\n";
    push @hllincludes, 'libtommath';
}

if ($args{'has-libffi'}) {
    $config{nativecall_backend} = 'libffi';
    unshift @{$config{usrlibs}}, 'ffi';
    push @{$config{defs}}, 'HAVE_LIBFFI';
    $defaults{-thirdparty}->{dc}  = undef;
    $defaults{-thirdparty}->{dcb} = undef;
    $defaults{-thirdparty}->{dl}  = undef;
    if ($config{pkgconfig_works}) {
        setup_native_library('libffi');
    }
    elsif ($^O eq 'solaris') {
        my ($first) = map { m,(.+)/ffi\.h$, && "/$1"  } grep { m,/ffi\.h$, } `pkg contents libffi`;
        if ($first) {
            $config{cincludes} .= " -I$first";
            print("Adding extra include for libffi: $first\n");
        }
        else {
            print("Unable to find ffi.h. Please install libffi by doing: 'sudo pkg install libffi'\n");
        }
    }
}
elsif ($args{'has-dyncall'}) {
    unshift @{$config{usrlibs}}, 'dyncall_s', 'dyncallback_s', 'dynload_s';
    $defaults{-thirdparty}->{dc}  = undef;
    $defaults{-thirdparty}->{dcb} = undef;
    $defaults{-thirdparty}->{dl}  = undef;
    $config{nativecall_backend} = 'dyncall';
    if (not $config{crossconf}) {
        if (index($config{cincludes}, '-I/usr/local/include') == -1) {
            $config{cincludes} = join(' ', $config{cincludes}, '-I/usr/local/include');
        }
        if (index($config{lincludes}, '-L/usr/local/lib') == -1) {
            $config{lincludes} = join(' ', $config{lincludes}, '-L/usr/local/lib');
        }
    }
}
else {
    $config{nativecall_backend} = 'dyncall';
    $config{moar_cincludes} .= ' ' . $defaults{ccinc} . '3rdparty/dyncall/dynload'
                             . ' ' . $defaults{ccinc} . '3rdparty/dyncall/dyncall'
                             . ' ' . $defaults{ccinc} . '3rdparty/dyncall/dyncallback';
    $config{install}   .= "\t\$(MKPATH) \"\$(DESTDIR)\$(PREFIX)/include/dyncall\"\n"
                        . "\t\$(CP) 3rdparty/dyncall/dynload/*.h \"\$(DESTDIR)\$(PREFIX)/include/dyncall\"\n"
                        . "\t\$(CP) 3rdparty/dyncall/dyncall/*.h \"\$(DESTDIR)\$(PREFIX)/include/dyncall\"\n"
                        . "\t\$(CP) 3rdparty/dyncall/dyncallback/*.h \"\$(DESTDIR)\$(PREFIX)/include/dyncall\"\n";
    push @hllincludes, 'dyncall';
}

# The ZSTD_CStream API is only exposed starting at version 1.0.0
# Ubuntu 16.04 packages version 0.5.1, so an "exists" check is not good enough.
if ($config{pkgconfig_works} && has_native_library('libzstd', '1.0.0')) {
    setup_native_library('libzstd');
    $config{heapsnapformat} = 3;
}
else {
    print "did not find libzstd; will not use heap snapshot format version 3\n";
    $config{heapsnapformat} = 2;
}

$config{use_mimalloc} = $args{mimalloc};
if (!defined $config{use_mimalloc}) {
    if ($config{has_stdatomic}) {
        print "Defaulting to mimalloc because you have <stdatomic.h>\n";
        $config{use_mimalloc} = 1;
    }
    elsif ($config{cc} eq 'cl') {
        print "Defaulting to mimalloc because you are using MSVC\n";
        $config{use_mimalloc} = 1;
    }
    else {
        print "Defaulting to libc malloc because <stdatomic.h> was not found.\n";
        $config{use_mimalloc} = 0;
    }
}

if ($config{use_mimalloc}) {
    $config{cflags} .= ' -DMI_SKIP_COLLECT_ON_EXIT';
    if ($args{'has-mimalloc'}) {
        $config{mimalloc_include} = "";
        $config{mimalloc_object} = "";
        $defaults{-thirdparty}->{mimalloc} = undef;
        unshift @{$config{usrlibs}}, 'mimalloc';
        setup_native_library('mimalloc') if $config{pkgconfig_works};
        if (not $config{crossconf}) {
            if (index($config{cincludes}, '-I/usr/local/include') == -1) {
                $config{cincludes} = join(' ', $config{cincludes}, '-I/usr/local/include');
            }
            if (index($config{lincludes}, '-L/usr/local/lib') == -1) {
                $config{lincludes} = join(' ', $config{lincludes}, '-L/usr/local/lib');
            }
        }
    }
    else {
        $config{moar_cincludes} .= ' ' . $defaults{ccinc} . '3rdparty/mimalloc/include'
                                 . ' ' . $defaults{ccinc} . '3rdparty/mimalloc/src';
        $config{install}   .= "\t\$(MKPATH) \"\$(DESTDIR)\$(PREFIX)/include/mimalloc\"\n"
                           . "\t\$(CP) 3rdparty/mimalloc/include/*.h \"\$(DESTDIR)\$(PREFIX)/include/mimalloc\"\n";
        push @hllincludes, 'mimalloc';
        $config{mimalloc_include} = '@ccincsystem@3rdparty/mimalloc';
        $config{mimalloc_object} = '3rdparty/mimalloc/src/static@obj@';
    }
}
else {
    $config{mimalloc_include} = "";
    $config{mimalloc_object} = "";
}

# mangle library names
$config{ldlibs} = join ' ',
    $config{lincludes},
    (map { sprintf $config{ldusr}, $_; } @{$config{usrlibs}}),
    (map { sprintf $config{ldsys}, $_; } @{$config{syslibs}});
$config{ldlibs} = ' -lasan ' . $config{ldlibs} if $args{asan} && $^O ne 'darwin' && $config{cc} ne 'clang';
$config{ldlibs} = ' -lubsan ' . $config{ldlibs} if $args{ubsan} and $^O ne 'darwin';
$config{ldlibs} = ' -ltsan ' . $config{ldlibs} if $args{tsan} and $^O ne 'darwin';
$config{ldlibs} = $config{ldlibs} . ' -lzstd ' if $config{heapsnapformat} == 3;
# macro defs
$config{ccdefflags} = join ' ', map { $config{ccdef} . $_ } @{$config{defs}};

$config{ccoptiflags}  = sprintf $config{ccoptiflags},  defined_or $args{optimize}, 1 if $config{ccoptiflags}  =~ /%s/;
$config{ccdebugflags} = sprintf $config{ccdebugflags}, defined_or $args{debug},    3 if $config{ccdebugflags} =~ /%s/;
$config{ldoptiflags}  = sprintf $config{ldoptiflags},  defined_or $args{optimize}, 1 if $config{ldoptiflags}  =~ /%s/;
$config{lddebugflags} = sprintf $config{lddebugflags}, defined_or $args{debug},    3 if $config{lddebugflags} =~ /%s/;


# generate CFLAGS
my @cflags;
push @cflags, '-std=c99' if $defaults{os} eq 'mingw32';
push @cflags, $config{ccmiscflags};
push @cflags, $config{ccoptiflags}  if $args{optimize};
push @cflags, $config{ccdebugflags} if $args{debug};
push @cflags, $config{cc_covflags}  if $args{coverage};
push @cflags, $config{ccwarnflags};
push @cflags, $config{ccdefflags};
push @cflags, $config{ccshared}     unless $args{static};
push @cflags, '-gdwarf-4'           if $config{cc} eq 'clang';
push @cflags,
$config{cc} eq 'clang'
    ? '-Rpass=loop-vectorize'
: $config{cc} eq 'gcc'
    ? '-fopt-info-vec-optimized'
    : die if $args{'show-autovect'};
push @cflags, $config{ccjitflags} if $args{jit};

if (exists $args{'show-autovect-failed'}) {
    push @cflags, '-Rpass-missed=loop-vectorize' if $config{cc} eq 'clang';
    push @cflags, ("-ftree-vectorizer-verbose=" . ($args{'show-autovect-failed'} || 1), "-fopt-info-vec-missed")
        if $config{cc} eq 'gcc';
}
if ($args{'show-autovect-failed'}) {
    push @cflags, '-Rpass-analysis=loop-vectorize' if 2 <= $args{'show-autovect-failed'} && $config{cc} eq 'clang';
    push @cflags, '-fsave-optimization-record '    if 3 <= $args{'show-autovect-failed'} && $config{cc} eq 'clang';
}
push @cflags, '-fno-omit-frame-pointer' if $args{asan} or $args{ubsan} or $args{tsan};
push @cflags, '-fsanitize=address' if $args{asan};
push @cflags, '-fsanitize=undefined' if $args{ubsan};
push @cflags, '-fsanitize=thread' if $args{tsan};
push @cflags, '-DWSL_BASH_ON_WIN' if wsl_bash_on_win();
push @cflags, '-DDEBUG_HELPERS' if $args{debug};
push @cflags, '-DMVM_VALGRIND_SUPPORT' if $args{valgrind};
push @cflags, '-DMVM_DTRACE_SUPPORT' if $args{dtrace};
push @cflags, '-DHAVE_TELEMEH' if $args{telemeh};
push @cflags, '-DWORDS_BIGENDIAN' if $config{be}; # 3rdparty/sha1 needs it and it isnt set on mips;
push @cflags, '-DMVM_HEAPSNAPSHOT_FORMAT=' . $config{heapsnapformat};
push @cflags, $ENV{CFLAGS} if $ENV{CFLAGS};
push @cflags, $ENV{CPPFLAGS} if $ENV{CPPFLAGS};

# Define _GNU_SOURCE for libuv to quell warnings with gcc.
# According to the libuv developers, they define _GNU_SOURCE for
# Linux use. Our code was showing warnings from libuv
# because we did not. Adding -D_GNU_SOURCE conditionally
# to our build then resulted in redefinition warnings in
# our code for three of our files that already have that definition.
# The fix was to bracket those definitions with
# #ifdef _GNU_SOURCE/#endif.
push @cflags, '-D_GNU_SOURCE' unless $args{'has-libuv'};

$config{cflags} = join ' ', uniq(@cflags);

# generate LDFLAGS
my @ldflags = ($config{ldmiscflags});
push @ldflags, $config{ldoptiflags}  if $args{optimize};
push @ldflags, $config{lddebugflags} if $args{debug};
push @ldflags, $config{ld_covflags}  if $args{coverage};
if (not $args{static} and $config{prefix} ne '/usr') {
    push @ldflags, $config{ldrpath_relocatable} if  $args{relocatable};
    push @ldflags, $config{ldrpath}             if !$args{relocatable};
}
push @ldflags, '-fsanitize=address'  if $args{asan};
push @ldflags, '-fsanitize=thread'  if $args{tsan};
push @ldflags, $ENV{LDFLAGS}         if $ENV{LDFLAGS};
$config{ldflags} = join ' ', @ldflags;

$config{moarshared} = '';
# Switch shared lib compiler flags in relocatable case.
if (not $args{static} and $config{prefix} ne '/usr') {
    $config{moarshared} = $config{moarshared_relocatable}   if  $args{relocatable};
    $config{moarshared} = $config{moarshared_norelocatable} if !$args{relocatable};
}

# setup library names
$config{moarlib} = sprintf $config{lib}, $NAME;
$config{moardll} = sprintf $config{dll}, $NAME;

# setup flags for shared builds
unless ($args{static}) {
    $config{objflags}  = '@ccdef@MVM_BUILD_SHARED @ccshared@';
    $config{mainflags} = '@ccdef@MVM_SHARED';
    $config{moar}      = '@moardll@';
    $config{impinst}   = $config{sharedlib},
    $config{mainlibs}  = '@lddir@. ' .
        sprintf(defined_or($config{ldimp}, $config{ldusr}), $NAME);
}
else {
    $config{objflags}  = '';
    $config{mainflags} = '';
    $config{moar}      = '@moarlib@';
    $config{impinst}   = $config{staticlib};
    $config{mainlibs}  = '@moarlib@ @thirdpartylibs@ $(LDLIBS)';
    # Install static library in default location
    $config{libdir}    = '@prefix@/lib' if ! $args{libdir};
}

$config{mainlibs} = '-lubsan ' . $config{mainlibs} if $args{ubsan};

# some toolchains generate garbage
my @auxfiles = @{ $defaults{-auxfiles} };
$config{auxclean} = @auxfiles ? '$(RM) ' . join ' ', @auxfiles : '@:';

print "OK\n\n";

unless ($config{crossconf}) {
    # detect x64 on Windows so we can build the correct dyncall version
    if ($config{cc} eq 'cl') {
        print dots('    auto-detecting x64 toolchain');
        my $msg = `cl 2>&1`;
        if (defined $msg) {
            if ($msg =~ /x64/) {
                print "YES\n";
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
    elsif ($defaults{os} eq 'mingw32' && $defaults{-toolchain} eq 'gnu') {
        print dots('    auto-detecting x64 toolchain');
        my $cc = $config{cc};
        my $msg = `$cc -dumpmachine 2>&1`;
        if (defined $msg) {
            if ($msg =~ /x86_64/) {
                print "YES\n";

                $defaults{-thirdparty}->{dc}->{rule} =
                    'cd 3rdparty/dyncall && ./configure.bat /target-x64 /tool-gcc && $(MAKE) COMPILE.C=$$(COMPILE.c) -f Makefile.embedded mingw32';
            }
            else { print "NO\n" }
        }
        else {
            softfail("could not run 'cl'");
            print dots('    assuming x86'), "OK\n";
        }
    }
}

build::probe::static_inline(\%config, \%defaults);
build::probe::thread_local(\%config, \%defaults);
build::probe::substandard_pow(\%config, \%defaults);
build::probe::substandard_log(\%config, \%defaults);
build::probe::substandard_trig(\%config, \%defaults);
build::probe::has_isinf_and_isnan(\%config, \%defaults);
build::probe::unaligned_access(\%config, \%defaults);
build::probe::ptr_size(\%config, \%defaults);

my $archname = $Config{archname};
if ($args{'jit'}) {
    if ($config{ptr_size} != 8) {
        print "JIT isn't supported on platforms with $config{ptr_size} byte pointers.\n";
    } elsif ($archname =~ m/^x86_64|^amd64/) {
        $config{jit_obj}      = '$(JIT_OBJECTS) $(JIT_ARCH_X64)';
        $config{dasm_flags}   = '-D POSIX=1';
        $config{jit_arch}     = 'MVM_JIT_ARCH_X64';
        $config{jit_platform} = 'MVM_JIT_PLATFORM_POSIX';
    } elsif ($archname =~ m/^darwin(-thread)?(-multi)?-2level/) {
        hardfail("Missing /usr/bin/arch") if !-x '/usr/bin/arch';
        my $arch = `/usr/bin/arch`;
        chomp $arch;
        if ($arch ne 'arm64') {
            $config{jit_obj}      = '$(JIT_OBJECTS) $(JIT_ARCH_X64)';
            $config{dasm_flags}   = '-D POSIX=1';
            $config{jit_arch}     = 'MVM_JIT_ARCH_X64';
            $config{jit_platform} = 'MVM_JIT_PLATFORM_POSIX';
        } else {
            print "JIT isn't supported on $Config{archname} ARM64 yet.\n";
# future support of ARM64 JITting
#            $config{jit_obj}      = '$(JIT_OBJECTS) $(JIT_ARCH_)';
#            $config{dasm_flags}   = '-D POSIX=1';
#            $config{jit_arch}     = 'MVM_JIT_ARCH_ARM64';
#            $config{jit_platform} = 'MVM_JIT_PLATFORM_POSIX';
        }

    } elsif ($archname =~ /^MSWin32-x64/) {
        $config{jit_obj}      = '$(JIT_OBJECTS) $(JIT_ARCH_X64)';
        $config{dasm_flags}   = '-D WIN32=1';
        $config{jit_arch}     = 'MVM_JIT_ARCH_X64';
        $config{jit_platform} = 'MVM_JIT_PLATFORM_WIN32';
    } else {
        print "JIT isn't supported on $Config{archname} yet.\n";
    }
}
# fallback
unless (defined $config{jit_obj}) {
    $config{jit_obj}      = '$(JIT_STUB)';
    $config{jit_arch}     = 'MVM_JIT_ARCH_NONE';
    $config{jit_platform} = 'MVM_JIT_PLATFORM_NONE';
    $config{dasm_flags}   = '';
}

if ($^O eq 'aix' && $config{ptr_size} == 4) {
    $config{ldflags} = join(',', $config{ldflags}, '-bmaxdata:0x80000000');
}

build::probe::C_type_bool(\%config, \%defaults);
build::probe::computed_goto(\%config, \%defaults);
build::probe::pthread_yield(\%config, \%defaults);
build::probe::pthread_setname_np(\%config, \%defaults);
build::probe::check_fn_malloc_trim(\%config, \%defaults);
build::probe::rdtscp(\%config, \%defaults);

my $order = $config{be} ? 'big endian' : 'little endian';

# dump configuration
print "\n", <<TERM, "\n";
        make: $config{make}
     compile: $config{cc} $config{cflags}
    includes: $config{cincludes} $config{moar_cincludes}
        link: $config{ld} $config{ldflags}
        libs: $config{ldlibs}

  byte order: $order
TERM

print dots('Configuring 3rdparty libs');

my @thirdpartylibs;
my $thirdparty = $defaults{-thirdparty};

for (sort keys %$thirdparty) {
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
        $clean = sprintf '$(RM) %s', $lib unless defined $clean;
    }

    # use explicit object list
    elsif (exists $current->{objects}) {
        $objects = $current->{objects};
        $rule    = sprintf '$(AR) $(ARFLAGS) @arout@$@ @%sobjects@', $_  unless defined $rule;
        $clean   = sprintf '$(RM) @%slib@ @%sobjects@', $_, $_ unless defined $clean;
    }

    # find *.c files and build objects for those
    elsif (exists $current->{src}) {
        my @sources = map { glob "$_/*.c" } @{ $current->{src} };
        my $globs   = join ' ', map { $_ . '/*@obj@' } @{ $current->{src} };

        $objects = join ' ', map { s/\.c$/\@obj\@/; $_ } @sources;
        $rule    = sprintf '$(AR) $(ARFLAGS) @arout@$@ %s', $globs unless defined $rule;
        $clean   = sprintf '$(RM) %s %s', $lib, $globs unless defined $clean;
    }

    # use an explicit rule (which has already been set)
    elsif (exists $current->{rule}) {}

    # give up
    else {
        softfail("no idea how to build '$lib'");
        print dots('    continuing anyway');
    }

    @config{@keys} = ($lib, defined_or($objects, ''), defined_or($rule, '@:'), defined_or($clean, '@:'));

    push @thirdpartylibs, $config{"${_}lib"};
}

$config{thirdpartylibs} = join ' ', @thirdpartylibs;
my $thirdpartylibs = join "\n" . ' ' x 12, sort @thirdpartylibs;

print "OK\n";

# the parser for config values used in the NQP/Rakudo build doesn't understand arrays,
# so just join into a single string
$config{hllincludes} = join " ", @hllincludes;

write_backend_config();

# dump 3rdparty libs we need to build
print "\n", <<TERM, "\n";
  3rdparty: $thirdpartylibs
TERM

# make sure to link with the correct entry point */
$config{mingw_unicode} = '';
if ($config{os} eq 'mingw32') {
    $config{mingw_unicode} = '-municode';
}

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

if ($args{'enable-jit'}) {
    print("\nThe --enable-jit flag is obsolete, as jit is enabled by default.\n");
    print("You can use --no-jit to build without jit.");
}

print "\n", $failed ? <<TERM1 : <<TERM2;
Configuration FAIL. You can try to salvage the generated Makefile.
TERM1
Configuration SUCCESS.

Type '$config{'make'}' to build and '$config{'make'} help' to see a list of
available make targets.
TERM2

if (!$failed && $args{'make-install'}) {
    $failed = system($config{make}, 'install') >> 8;
}
print $folder_to_delete if $folder_to_delete;
exit $failed;

# helper functions

# fill in defaults for native builds
sub setup_native {
    my ($os) = @_;

    print dots("Configuring native build environment");
    print "\n";

    if ($os eq 'MSWin32') {
        my $has_nmake = 0 == system('nmake /? >NUL 2>&1');
        my $has_cl    = `cl 2>&1` =~ /Microsoft Corporation/;
        my $has_gmake = 0 == system('gmake --version >NUL 2>&1');
        my $has_gcc   = 0 == system('gcc --version >NUL 2>&1');
        if ($has_nmake && $has_cl) {
            $os = 'win32';
        }
        elsif ($has_gmake && $has_gcc) {
            $os = 'mingw32';
        } else {
            $os = "";
        }
    }

    if (!exists $::SYSTEMS{$os}) {
        softfail("unknown OS '$os'");
        print dots("    assuming POSIX userland");
        $os = 'posix';
    }

    $defaults{os} = $os;

    my ($shell, $toolchain, $compiler, $overrides) = @{$::SYSTEMS{$os}};
    $shell     = $::SHELLS{$shell};
    $toolchain = $::TOOLCHAINS{$toolchain};
    $compiler  = $::COMPILERS{$compiler};
    set_defaults($shell, $toolchain, $compiler, $overrides);

    if (exists $args{shell}) {
        $shell = $args{shell};
        hardfail("unsupported shell '$shell'")
            unless exists $::SHELLS{$shell};

        $shell = $::SHELLS{$shell};
        set_defaults($shell);
    }

    if (exists $args{toolchain}) {
        $toolchain = $args{toolchain};
        hardfail("unsupported toolchain '$toolchain'")
            unless exists $::TOOLCHAINS{$toolchain};

        $toolchain = $::TOOLCHAINS{$toolchain};
        $compiler  = $::COMPILERS{ $toolchain->{-compiler} };
        set_defaults($toolchain, $compiler);
    }

    if (exists $args{compiler}) {
        $compiler = $args{compiler};
        hardfail("unsupported compiler '$compiler'")
            unless exists $::COMPILERS{$compiler};

        $compiler  = $::COMPILERS{$compiler};

        unless (exists $args{toolchain}) {
            $toolchain = $::TOOLCHAINS{ $compiler->{-toolchain} };
            set_defaults($toolchain);
        }

        set_defaults($compiler);
    }

    my $order = $Config{byteorder};
    if ($order eq '1234' || $order eq '12345678') {
        $defaults{be} = 0;
    }
    elsif ($order eq '4321' || $order eq '87654321') {
        $defaults{be} = 1;
    }
    else {
        ::hardfail("unsupported byte order $order");
    }
}

# fill in defaults for cross builds
sub setup_cross {
    my ($build, $host) = @_;

    print dots("Configuring cross build environment");

    hardfail("both --build and --host need to be specified")
        unless defined $build && defined $host;

    my $cc        = "$host-gcc";
    my $ar        = "$host-ar";
    my $crossconf = "--build=$build --host=$host";

    for (\$build, \$host) {
        if ($$_ =~ /-(\w+)-\w+$/) {
            $$_ = $1;
            if (!exists $::SYSTEMS{$1}) {
                softfail("unknown OS '$1'");
                print dots("    assuming GNU userland");
                $$_ = 'posix';
            }
        }
        else { hardfail("failed to parse triple '$$_'") }
    }

    $defaults{os} = $host;

    $build = $::SYSTEMS{$build};
    $host  = $::SYSTEMS{$host};

    my $shell     = $::SHELLS{ $build->[0] };
    my $toolchain = $::TOOLCHAINS{gnu};
    my $compiler  = $::COMPILERS{gcc};
    my $overrides = $host->[3];

    set_defaults($shell, $toolchain, $compiler, $overrides);

    $defaults{cc}        = $cc;
    $defaults{ar}        = $ar;
    $defaults{crossconf} = $crossconf;
    $defaults{be}        = $args{'big-endian'};
}

# sets C<%defaults> from C<@_>
sub set_defaults {
    # getting the correct 3rdparty information is somewhat tricky
    my $thirdparty = defined_or $defaults{-thirdparty}, \%::THIRDPARTY;
    @defaults{ keys %$_ } = values %$_ for @_;
    $defaults{-thirdparty} = {
        %$thirdparty, map{ %{ defined_or $_->{-thirdparty}, {} } } @_
    };
}

# fill in config values
sub configure {
    my ($template) = @_;

    while ($template =~ /@(\w+)@/) {
        my $key = $1;
        unless (exists $config{$key}) {
            return (undef, "unknown configuration key '$key'\n    known keys: " .
                join(', ', sort keys %config));
        }

        my $val = $config{$key};
        if ($template =~ /'[^']*@\Q$key\E[^']*'/) {
            # escape \ and '
            $val =~ s/\\/\\\\/g;
            $val =~ s/\'/\\\'/g;
        } elsif ($template =~ /"[^"]*@\Q$key\E[^"]*"/) {
            # escape \ and "
            $val =~ s/\\/\\\\/g;
            $val =~ s/\"/\\\"/g;
        }

        $template =~ s/@\Q$key\E@/$val/;
    }

    return $template;
}

# generate files
sub generate {
    my ($dest, $src) = @_;

    print dots("Generating $dest");

    open my $srcfile, '<', $src or hardfail($!);
    open my $destfile, '>', $dest or hardfail($!);

    while (<$srcfile>) {
        my ($line, $error) = configure($_);
        hardfail($error)
            unless defined $line;

        if ($config{sh} eq 'cmd' && $dest =~ /Makefile|config\.c/) {
            # In-between slashes in makefiles need to be backslashes on Windows.
            # Double backslashes in config.c, beause these are in qq-strings.
            my $bs = $dest =~ /Makefile/ ? '\\' : '\\\\';
            $line =~ s/(\w|\.|\w\:|\$\(PREFIX\))\/(?=\w|\.|\*)/$1$bs/g;
            $line =~ s/(\w|\.|\w\:|\$\(PREFIX\))\\(?=\w|\.|\*)/$1$bs/g if $bs eq '\\\\';

            # gmake doesn't like \*
            $line =~ s/(\w|\.|\w\:|\$\(PREFIX\))\\\*/$1\\\\\*/g
                if $config{make} eq 'gmake';
        }

        print $destfile $line;
    }

    close $srcfile;
    close $destfile;

    print "OK\n";
}

# some dots
sub dots {
    my $message = shift;
    my $length = shift || 55;
    my $dot_count = $length - length $message;
    $dot_count = 0 if $dot_count < 0;
    return "$message " . '.' x $dot_count . ' ';
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

sub write_backend_config {
    $config{backendconfig} = '';
    for my $k (sort keys %config) {
        next if $k eq 'backendconfig';
        my $v = $config{$k};

        if (ref($v) eq 'ARRAY') {
            my $i = 0;
            for (@$v) {
                $config{backendconfig} .= qq/        add_entry(tc, config, "$k\[$i]", "$_");\n/;
                $i++;
            }
        }
        elsif (ref($v) eq 'HASH') {
            # should not be there
        }
        else {
            $v   = '' unless defined $v;
            $v   =~ s/"/\\"/g;
            $v   =~ s/\n/\\\n/g;
            $config{backendconfig} .= qq/        add_entry(tc, config, "$k", "$v");\n/;
        }
    }
}

sub wsl_bash_on_win {
    open my $fh, '<', '/proc/sys/kernel/osrelease' or return 0;
    return ((readline $fh) =~ /\A\d\.\d\.\d-\d+-Microsoft\s*\z/) ? 1 : 0;
}

# set up cincludes and lincludes flags for a native library
sub setup_native_library {
    my $library = shift;
    my $result_cflags = `$config{pkgconfig} --cflags $library`;
    if ( $? == 0 ) {
        $result_cflags =~ s/\n//g;
        if (index($config{cincludes}, $result_cflags) == -1) {
            $config{cincludes} = join(' ', $config{cincludes}, $result_cflags);
            print("Adding extra include for $library: $result_cflags\n");
        }
    }
    else {
        print("Error occured when running $config{pkgconfig} --cflags $library.\n");
    }
    my $result_libs = `$config{pkgconfig} --libs-only-L $library`;
    if ( $? == 0 ) {
        $result_libs =~ s/\n//g;
        if (index($config{lincludes}, $result_libs) == -1) {
            $config{lincludes} = join(' ', $config{lincludes}, $result_libs);
            print("Adding extra libs for $library: $result_libs\n");
        }
    }
    else {
        print("Error occured when running $config{pkgconfig} --libs-only-L $library.\n");
    }
}

sub has_native_library {
    my ($library, $version) = @_;
    if (defined $version) {
        return !system "$config{pkgconfig} --atleast-version=$version $library";
    }
    return !system "$config{pkgconfig} --exists $library"
}

__END__

=head1 SYNOPSIS

    ./Configure.pl -?|--help

    ./Configure.pl [--os <os>] [--shell <shell>]
                   [--toolchain <toolchain>] [--compiler <compiler>]
                   [--ar <ar>] [--cc <cc>] [--ld <ld>] [--make <make>]
                   [--debug] [--optimize]
                   [--static] [--prefix <path>] [--relocatable]
                   [--has-libtommath] [--has-sha] [--has-libuv]
                   [--has-libatomic_ops]
                   [--asan] [--ubsan] [--tsan] [--no-jit]
                   [--telemeh] [--git-cache-dir <path>]

    ./Configure.pl --build <build-triple> --host <host-triple>
                   [--ar <ar>] [--cc <cc>] [--ld <ld>] [--make <make>]
                   [--debug] [--optimize]
                   [--static] [--prefix <path>] [--relocatable]
                   [--big-endian] [--make-install]

=head2 Use of environment variables

Compiler and linker flags can be extended with environment variables.

CFLAGS="..." LDFLAGS="..." ./Configure.pl

=head1 OPTIONS

=over 4

=item -?

=item --help

Show this help information.

=item --debug

=item --no-debug

Toggle debugging flags during compile and link. Debugging is off by
default.

=item --optimize

=item --no-optimize

Toggle optimization and debug flags during compile and link. If nothing
is specified the default is to optimize.

=item --mimalloc

=item --no-mimalloc

Control whether we build with mimalloc or use the C library's malloc.

mimalloc requires either MSVC or working C11 C<< <stdatomic> >>. We probe for
this, and if found or using MSVC we default to use mimalloc. Otherwise we
default to the C library's malloc. Specify C<--no-mimalloc> to force use of the
C library's malloc always. Specify C<--mimalloc> to force use of mimalloc, even
if the probing thinks that it won't build.

=item --c11-atomics

=item --no-c11-atomics

Use C11 atomics instead of libatomic_ops for atomic operations. The default
is currently C<--no-c11-atomics> - ie use libatomic_ops. If you set
C<--c11-atomics> and your compiler does not support C11 atomics, your build
will fail.

=item --os <os>

Set the operating system name which you are compiling to.

Currently supported operating systems are C<posix>, C<linux>, C<darwin>,
C<openbsd>, C<netbsd>, C<freebsd>, C<solaris>, C<win32>, and C<mingw32>.

If not explicitly set, the option will be provided by the Perl runtime.
In case of unknown operating systems, a POSIX userland is assumed.

=item --shell <shell>

Currently supported shells are C<posix> and C<win32>.

=item --toolchain <toolchain>

Currently supported toolchains are C<posix>, C<gnu>, C<bsd> and C<msvc>.

=item --compiler <compiler>

Currently supported compilers are C<gcc>, C<clang> and C<cl>.

=item --ar <ar>

Explicitly set the archiver without affecting other configuration
options.

=item --cc <cc>

Explicitly set the compiler without affecting other configuration
options.

=item --show-autovect

Prints debug messages when compiling showing which loops were auto vectorized
to SIMD instructions during build. Option is supported for Clang and GCC only.

=item --show-autovect-failed

Prints debug messages which hopefully reveal why autovectorization has failed
for a loop. Verbosity level is 1-3 for clang, for GCC it is likely 1-2.
If you are trying to vectorize code, it's *highly* recommended to try using clang
first as it's smarter and has more useful messages. Then once it is working,
try to get it working on gcc.

=item --asan

Build with AddressSanitizer (ASAN) support. Requires clang and LLVM 3.1 or newer.
See L<https://code.google.com/p/address-sanitizer/wiki/AddressSanitizer>

You can use C<ASAN_OPTIONS> to configure ASAN at runtime; for example, to disable
memory leak checking (which can make Rakudo fail to build), you can set the following:

    export ASAN_OPTIONS=detect_leaks=0

A full list of options is displayed if you set C<ASAN_OPTIONS> to C<help=1>.

=item --ubsan

Build with Undefined Behaviour sanitizer support.

=item --tsan

Build with ThreadSanitizer (TSAN) support. Requires clang and LLVM 3.2 or newer
alternatively gcc 4.8 or newer. You can use C<TSAN_OPTIONS> to configure TSAN
at runtime, e.g.

    export TSAN_OPTIONS=report_thread_leaks=0

A full list of options is displayed if you set C<TSAN_OPTIONS> to C<help=1>.

=item --valgrind

Include Valgrind Client Requests for moarvm's own memory allocators.

=item --dtrace

Include DTrace trace points in various places.

=item --ld <ld>

Explicitly set the linker without affecting other configuration
options.

=item --make <make>

Explicitly set the make tool without affecting other configuration
options.

=item --static

Build MoarVM as a static library instead of a shared one.

=item --build <build-triple> --host <host-triple>

Set up cross-compilation.

=item --big-endian

Set byte order of host system in case of cross compilation. With native
builds, the byte order is auto-detected.

=item --prefix

Install files in subdirectory /bin, /lib and /include of the supplied path.
The default prefix is "install" if this option is not passed.

=item --relocatable

Search for the libmoar library relative to the executable path.
(On AIX and OpenBSD MoarVM can not be built relocatable, since both OS'
miss the necessary mechanism to make this work.)

=item --bindir

Install executable files in the supplied path.  The default is
"@prefix@/bin" if this option is not passed.

=item --libdir

Install library in the supplied path.  The default is "@prefix@/lib"
for POSIX toolchain and "@bindir@" for MSVC if this option is not
passed.

=item --mastdir

Install NQP libraries in the supplied path.  The default is
"@prefix@/share/nqp/lib/MAST" if this option is not passed.

=item --make-install

Build and install MoarVM in addition to configuring it.

=item --has-libtommath

=item --has-sha

=item --has-libuv

=item --has-libatomic_ops

=item --has-dyncall

=item --has-libffi

=item --has-mimalloc

=item --pkgconfig=/path/to/pkgconfig/executable

Provide path to the pkgconfig executable. Default: /usr/bin/pkg-config

=item --no-jit

Disable JIT compiler, which is enabled by default to JIT-compile hot frames.

=item --telemeh

Build support for the fine-grained internal event logger.

=item --git-cache-dir <path>

Use the given path as a git repository cache.
For example: --git-cache-dir=/home/user/repo/git_cache
Each repository ('MoarVM' and its submodules) will use a separate subfolder.
If the subfolder does not exist, it will be cloned. If it exists the
contained repository will be updated.


=back
