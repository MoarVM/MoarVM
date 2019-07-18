package build::probe;
use strict;
use warnings;

use File::Path qw(mkpath rmtree);
use File::Spec::Functions qw(curdir catdir rel2abs devnull);

my $devnull = devnull();

my $probe_dir;

END {
    rmtree($probe_dir)
        if defined $probe_dir;
}

{
    package build::probe::restore_cwd;
    use Cwd;

    sub new {
        my $cwd = getcwd;
        die "Can't getcwd: $!"
            unless defined $cwd && length $cwd;
        bless \$cwd;
    }

    sub DESTROY {
        chdir ${$_[0]}
            or warn "Can't restore cwd to ${$_[0]}: $!";
    }
}

sub _to_probe_dir {
    unless (defined $probe_dir) {
        $probe_dir = rel2abs(catdir(curdir(), 'probe'));
        mkpath($probe_dir);
    }
    my $restore = build::probe::restore_cwd->new();
    chdir $probe_dir
        or die "Can't chir $probe_dir: $!";
    return $restore;
}

sub compile {
    my ($config, $leaf, $defines, $files) = @_;
    my $restore = _to_probe_dir();

    my $cl_define = join ' ', map {$config->{ccdef} . $_} @$defines;

    my @objs;
    foreach my $file ("$leaf.c", @$files) {
        (my $obj = $file) =~ s/\.c/$config->{obj}/;
        my $command = "$config->{cc} $ENV{CFLAGS} $cl_define $config->{ccout}$obj $config->{ccswitch} $file >$devnull 2>&1";
        system $command
            and return;
        push @objs, $obj;
    }

    my $command = "$config->{ld} $ENV{LDFLAGS} $config->{ldout}$leaf @objs $config->{ldlibs} >$devnull 2>&1";
    system $command
        and return;
    return 1;
}

sub _spew {
    my ($filename, $content) = @_;
    open my $fh, '>', $filename
        or die "Can't open $filename: $!";
    print $fh $content
        or die "Can't write to $filename: $!";
    close $fh
        or die "Can't close $filename: $!";
}

sub compiler_usability {
    my ($config) = @_;
    my $restore  = _to_probe_dir();
    my $leaf     = 'try';
    my $file     = "$leaf.c";

    _spew('try.c', <<'EOT');
#include <stdlib.h>

int main(int argc, char **argv) {
     return EXIT_SUCCESS;
}
EOT

    print ::dots('    trying to compile a simple C program');

    my ($can_compile, $can_link, $command_errored, $error_message);
    (my $obj = $file) =~ s/\.c/$config->{obj}/;
    $ENV{CFLAGS} //= '';
    my $command = "$config->{cc} $ENV{CFLAGS} $config->{ccout}$obj $config->{ccswitch} $file 2>&1";
    my $output  = `$command` || $!;
    if ($? >> 8 == 0) {
        $can_compile = 1;
    }
    else {
        $command_errored = $command;
        $error_message   = $output;
    }

    if ($can_compile) {
    $ENV{LDFLAGS} //= '';
        $command = "$config->{ld} $ENV{LDFLAGS} $config->{ldout}$leaf $obj 2>&1";
        $output  = `$command` || $!;
        if ($? >> 8 == 0) {
            $can_link = 1;
        }
        else {
            $command_errored = $command;
            $error_message   = $output;
        }
    }

    if (!$can_compile || !$can_link) {
        die "ERROR\n\n" .
            "    Can't " . ($can_compile ? 'link' : 'compile') . " simple C program.\n" .
            "    Failing command: $command_errored\n" .
            "    Error: $error_message\n\n" .
            "Cannot continue after this error.\n" .
            "On linux, maybe you need something like 'sudo apt-get install build-essential'.\n" .
            "On macOS, maybe you need to install XCode and accept the XCode EULA.\n";
    }

    print "YES\n";
}

sub static_inline_native {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    _spew('try.c', <<'EOT');
#include <stdlib.h>

int main(int argc, char **argv) {
#ifdef __GNUC__
     return EXIT_SUCCESS;
#else
     return EXIT_FAILURE;
#endif
}
EOT

    print ::dots('    probing whether your compiler thinks that it is gcc');
    compile($config, 'try')
        or die "Can't compile simple gcc probe, so something is badly wrong";
    my $gcc = !system './try';
    print $gcc ? "YES\n": "NO\n";

    print ::dots('    probing how your compiler does static inline');

    _spew('inline.c', <<'EOCP');
#include <stdlib.h>
extern int f_via_a(int x);
extern int f_via_b(int x);
int main(int argc, char **argv)
{
    int y;

    y = f_via_a(0);
#ifdef USE_B
    y = f_via_b(0);
#endif
    if (y == 42) {
        return EXIT_SUCCESS;
    }
    else {
        return EXIT_FAILURE;
    }
}
EOCP

_spew('a.c', <<'EOCP');
static INLINE int f(int x) {
    int y;
    y = x + 42;
    return y;
}

int f_via_a(int x)
{
    return f(x);
}
EOCP
_spew('b.c', <<'EOCP');
extern int f(int x);

int f_via_b(int x)
{
    return f(x);
}
EOCP

    # For gcc, prefer __inline__, which permits the cflags to add -ansi
    my @try = $gcc ? qw(__inline__ inline __inline _inline)
        : qw(inline __inline__ __inline _inline);

    my $s_i;
    while (my $try = shift @try) {
        next unless compile($config, 'inline', ["INLINE=$try"], ['a.c']);
        next if system "./inline";
        # Now make sure there is no external linkage of static functions
        if(!compile($config, 'inline', ["INLINE=$try", "USE_B"], ['a.c', 'b.c'])
           || system "./inline") {
            $s_i = "static $try";
            last;
        }
    }

    if ($s_i) {
        print "$s_i\n";
    } else {
        print "none, so falling back to static\n";
        $s_i = 'static';
    }
    $config->{static_inline} = $s_i;
}

sub static_inline_cross {
    my ($config) = @_;
    # FIXME. Needs testing, but might be robust enough to do what the native
    # code does, but just skip the system() to run the executable. Although this
    # might get confused by link time optimisations that only fail at run time,
    # which the system test does detect.
    $config->{static_inline} = 'static';
}

sub specific_werror {
    my ($config) = @_;
    my $restore = _to_probe_dir();

    if ($config->{cc} ne 'gcc') {
        $config->{can_err_decl_after_stmt} = 1;
        return;
    }

    my $file = 'try.c';
    _spew($file, <<'EOT');
#include <stdlib.h>

int main(int argc, char **argv) {
     return EXIT_SUCCESS;
}
EOT

    print ::dots('    probing support of -Werror=*');

    (my $obj = $file) =~ s/\.c/$config->{obj}/;
    my $command = "gcc -Werror=declaration-after-statement $config->{ccout}$obj try.c >$devnull 2>&1";
    my $can_specific_werror = !( system $command );

    print $can_specific_werror ? "YES\n": "NO\n";
    $config->{can_specific_werror} = $can_specific_werror || 0
}


sub _gen_unaligned_access {
    my ($config, $can) = @_;
    my @align = qw(int32 int64 num64);
    my $no_msg = "your CPU can't";
    if ($config->{cflags} =~ /\B-fsanitize=undefined\b/) {
        $can = '';
        $no_msg = "with UBSAN we won't";
    }
    if ($can eq 'all') {
        ++$config->{"can_unaligned_$_"}
            foreach @align;
        print "    your CPU can read unaligned values for all of @align\n";
    } else {
        my %can;
        ++$can{$_}
            for split ' ', $can;
        $config->{"can_unaligned_$_"} = $can{$_} || 0
            foreach @align;
        if ($can) {
            print "    your CPU can read unaligned values for only $can\n";
        } else {
            print "    $no_msg read unaligned values for any of @align\n";
        }
    }
}

sub unaligned_access {
    my ($config) = @_;

    if ($^O eq 'MSWin32') {
        # Needs FIXME for Windows on ARM, but not sure how to detect that
        _gen_unaligned_access($config, 'all');
    } else {
        # AIX:
        # uname -m: 00F84C0C4C00
        # uname -p: powerpc
        # HP/UX
        # uname -m: 9000/800
        # (but will be ia64 on Itanium)
        # uname -p illegal
        # Solaris
        # uname -m: i86pc
        # uname -p: i386
        # FreeBSD
        # uname -m: amd64
        # uname -p: amd64
        # NetBSD
        # uname -m: amd64
        # uname -p: x86_64
        # OpenBSD
        # uname -m: amd64
        # uname -p: amd64
        # Assuming that the 50 other *BSD variants are forks of the 3 above
        # Linux
        # uname -p can return 'unknown'

        my $flag;
        if ($^O eq 'aix' || $^O eq 'solaris') {
            $flag = '-p';
        } else {
            $flag = '-m';
        }
        my $command = "uname $flag";
        my $arch = `$command`;
        if (defined $arch) {
            chomp $arch;
            if ($arch =~ /^(?:x86_64|amd64|i[0-9]86)$/) {
                # Don't know alignment constraints for ARMv8
                _gen_unaligned_access($config, 'all');
            } elsif ($arch =~ /armv(?:6|7)/) {
                _gen_unaligned_access($config, 'int32');
            } else {
                # ARMv5 and earlier do "interesting" things on unaligned 32 bit
                # For other architectures, play it safe by default.
                # Updates welcome.
                _gen_unaligned_access($config, '');
            }
        } else {
            print STDERR "Problem running $command, so assuming no unaligned access\n";
        }
    }
}

sub unaligned_access_cross {
    my ($config) = @_;
    _gen_unaligned_access($config, '');
}

sub ptr_size_native {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    _spew('try.c', <<'EOT');
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    printf("%u\n", (unsigned int) sizeof(void *));
    return EXIT_SUCCESS;
}
EOT

    print ::dots('    probing the size of pointers');
    compile($config, 'try')
        or die "Can't compile simple probe, so something is badly wrong";
    my $size = `./try`;
    die "Unable to run probe, so something is badly wrong"
        unless defined $size;
    chomp $size;
    die "Probe gave nonsensical answer '$size', so something it badly wrong"
        unless $size =~ /\A[0-9]+\z/;
    print "$size\n";
    $config->{ptr_size} = $size;
}

# It would be good to find a robust way to do this without needing to *run* the
# compiled code. At which point we could also use it for the native build.
sub ptr_size_cross {
    my ($config) = @_;
    warn "Guessing :-(";
    $config->{ptr_size} = 4;
}

sub computed_goto {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    _spew('try.c', <<'EOT');
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    void *cgoto_ptr;
    cgoto_ptr = &&cgoto_label;

    goto *cgoto_ptr;

    return EXIT_FAILURE;

    cgoto_label:
        return EXIT_SUCCESS;
}
EOT

    print ::dots('    probing computed goto support');
    my $can_cgoto = compile($config, 'try');
    unless ($config->{crossconf}) {
        $can_cgoto  &&= !system './try';
    }
    print $can_cgoto ? "YES\n": "NO\n";
    $config->{cancgoto} = $can_cgoto || 0
}

sub check_fn_malloc_trim {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    my $includes = '#include <malloc.h>';
    my $function = 'malloc_trim(0)';
    _spew('try.c', <<"EOT");
$includes

int main(int argc, char **argv) {
    $function;
    return 0;
}
EOT

    print ::dots('    probing existance of optional malloc_trim()');
    my $can = compile($config, 'try');
    unless ($config->{crossconf}) {
        $can  &&= !system './try';
    }
    print $can ? "YES\n": "NO\n";
    $config->{has_fn_malloc_trim} = $can || 0
}

sub C_type_bool {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    my $template = <<'EOT';
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

int main(int argc, char **argv) {
    %s foo = false;
    foo    = true;
    return foo ? EXIT_SUCCESS : EXIT_FAILURE;
}
EOT

    print ::dots('    probing C type support for: _Bool, bool');
    my %have;
    for my $type (qw(_Bool bool)) {
        _spew('try.c', sprintf $template, $type);
        $have{$type}   = compile($config, 'try');
        $have{$type} &&= !system './try' unless $config->{crossconf};
        delete $have{$type} unless $have{$type}
    }
    print %have ? "YES: " . join(',', sort keys %have) . "\n": "NO: none\n";
    $config->{havebooltype} = %have ? 1 : 0;
    $config->{booltype}     = (sort keys %have)[0] || 0;
}

sub pthread_yield {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    _spew('try.c', <<'EOT');
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

int main(int argc, char **argv) {
#ifdef _POSIX_PRIORITY_SCHEDULING
    /* hide pthread_yield so we fall back to the recommended sched_yield() */
    return EXIT_FAILURE;
#else
    pthread_yield();
    return EXIT_SUCCESS;
#endif
}
EOT

    print ::dots('    probing pthread_yield support');
    my $has_pthread_yield = compile($config, 'try') && system('./try') == 0;
    print $has_pthread_yield ? "YES\n": "NO\n";
    $config->{has_pthread_yield} = $has_pthread_yield || 0
}

sub numbits {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    _spew('numbits.c', <<'EOT');
#include <stdint.h>

int main(int argc, char **argv) {
#if UINTPTR_MAX == 0xffffffff
return 32;
/* 32-bit */
#elif UINTPTR_MAX == 0xffffffffffffffff
/* 64-bit */
return 64;
#else
/* unknown */
return -1;
#endif
}
EOT

    print ::dots('    probing number of bits');
    my $print_result;
    my $num_bits = 0;
    if(compile($config, 'numbits')) {
        $num_bits = $print_result = system('./numbits') >> 8;
    }
    if (!defined $print_result || $print_result == -1) {
        $print_result = 'UNKNOWN';
    }
    print $print_result . "\n";
    $config->{arch_bits} = $num_bits;
}

sub wchar_unsigned_native {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    _spew('try.c', <<'EOT');
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

int main(int argc, char **argv) {
    wchar_t wc = -1;
    printf("%d\n", wc > 0);
    return EXIT_SUCCESS;
}
EOT
    print ::dots("    probing the sign of wchar_t");
    compile($config, 'try')
        or die "Can't compile simple probe, so something is badly wrong";
    my $is_unsigned = `./try`;
    die "Unable to run probe, so something is badly wrong"
        unless defined $is_unsigned;
    chomp $is_unsigned;
    die "Probe gave nonsensical answer '$is_unsigned', so something is badly wrong"
        unless $is_unsigned =~ /\A[0|1]\z/;
    print "un" if $is_unsigned;
    print "signed\n";
    $config->{wchar_unsigned} = $is_unsigned;
}

sub wchar_unsigned_cross {
    my ($config) = @_;
    warn "Guessing :-(";
    # Mddore likely than not, wchar_t is signed.
    $config->{wchar_unsigned} = 0;
}

sub wchar_size_native {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    _spew('try.c', <<'EOT');
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

int main(int argc, char **argv) {
    printf("%lu\n", (unsigned long)sizeof(wchar_t));
    return EXIT_SUCCESS;
}
EOT
    print ::dots("    probing the size of wchar_t");
    compile($config, 'try')
        or die "Can't compile simple probe, so something is badly wrong";
    my $size = `./try`;
    die "Unable to run probe, so something is badly wrong"
        unless defined $size;
    chomp $size;
    die "Probe gave nonsensical answer '$size', so something is badly wrong"
        unless $size =~ /\A[0-9]+\z/;
    print "$size\n";
    $config->{wchar_size} = $size;
}

sub wchar_size_cross {
    my ($config) = @_;
    warn "Guessing :-(";
    # More likely than not, wchar_t is an int.
    $config->{wchar_size} = 4;
}

sub wint_unsigned_native {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    _spew('try.c', <<'EOT');
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

int main(int argc, char **argv) {
    wint_t wc = -1;
    printf("%d\n", wc > 0);
    return EXIT_SUCCESS;
}
EOT
    print ::dots("    probing the sign of wint_t");
    compile($config, 'try')
        or die "Can't compile simple probe, so something is badly wrong";
    my $is_unsigned = `./try`;
    die "Unable to run probe, so something is badly wrong"
        unless defined $is_unsigned;
    chomp $is_unsigned;
    die "Probe gave nonsensical answer '$is_unsigned', so something is badly wrong"
        unless $is_unsigned =~ /\A[0|1]\z/;
    print "un" if $is_unsigned;
    print "signed\n";
    $config->{wint_unsigned} = $is_unsigned;
}

sub wint_unsigned_cross {
    my ($config) = @_;
    warn "Guessing :-(";
    # Mddore likely than not, wint_t is signed.
    $config->{wint_unsigned} = 0;
}

sub wint_size_native {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    _spew('try.c', <<'EOT');
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

int main(int argc, char **argv) {
    printf("%lu\n", (unsigned long)sizeof(wint_t));
    return EXIT_SUCCESS;
}
EOT
    print ::dots("    probing the size of wint_t");
    compile($config, 'try')
        or die "Can't compile simple probe, so something is badly wrong";
    my $size = `./try`;
    die "Unable to run probe, so something is badly wrong"
        unless defined $size;
    chomp $size;
    die "Probe gave nonsensical answer '$size', so something is badly wrong"
        unless $size =~ /\A[0-9]+\z/;
    print "$size\n";
    $config->{wint_size} = $size;
}

sub wint_size_cross {
    my ($config) = @_;
    warn "Guessing :-(";
    # More likely than not, wint_t is an int.
    $config->{wint_size} = 4;
}

sub win32_compiler_toolchain {
    my ($config) = @_;
    my $has_nmake = 0 == system('nmake /? >NUL 2>&1');
    my $has_cl    = `cl 2>&1` =~ /Microsoft Corporation/;
    my $has_gmake = 0 == system('gmake --version >NUL 2>&1');
    my $has_gcc   = 0 == system('gcc --version >NUL 2>&1');
    if ($has_nmake && $has_cl) {
        $config->{win32_compiler_toolchain} = 'win32';
    }
    elsif ($has_gmake && $has_gcc) {
        $config->{win32_compiler_toolchain} = 'mingw32';
    }
    else {
        $config->{win32_compiler_toolchain} = ''
    }
    $config->{win32_compiler_toolchain}
}

sub rdtscp {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    _spew('try.c', <<'EOT');
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

int main(int argc, char **argv) {
    unsigned int _tsc_aux;
    unsigned int tscValue;
    tscValue = __rdtscp(&_tsc_aux);

    if (tscValue > 1)
        return EXIT_SUCCESS;
    return EXIT_FAILURE;
}
EOT

    print ::dots('    probing support of rdtscp intrinsic');
    my $can_rdtscp = compile($config, 'try');
    unless ($config->{crossconf}) {
        $can_rdtscp  &&= !system './try';
    }
    print $can_rdtscp ? "YES\n": "NO\n";
    $config->{canrdtscp} = $can_rdtscp || 0
}

'00';
