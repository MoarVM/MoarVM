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

sub thread_local_cross {
    my ($config) = @_;
    $config->{has_thread_local} = 0;
    $config->{thread_local} = "";
}

sub thread_local_native {
    my ($config) = @_;

    # We don't need to probe for this on Win32, as UV sets it for us
    if ($^O eq 'MSWin32') {
        $config->{has_thread_local} = 0;
        $config->{thread_local} = "";
        return;
    }

    my $restore = _to_probe_dir();

    print ::dots('    probing if your compiler offers thread local storage');
    my $file = 'thread_local.c';
    _spew($file, <<'EOT');
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

static int plus_one = 1;
static int minus_one = -1;

THREAD_LOCAL int *minion;

int callback (const void *a, const void *b) {
    int val_a = *minion * *(const int *)a;
    int val_b = *minion * *(const int *)b;
    return val_a < val_b ? -1 : val_a > val_b;
}

#define SIZE 8

void *thread_function(void *arg) {
    /* thread local variables should start zeroed in each thread. */
    if (minion != NULL) {
        fprintf(stderr, "__thread variable started with %p, should be NULL\n",
                minion);
        exit(2);
    }
    minion = &minus_one;

    int array[SIZE];
    unsigned int i;
    for (i = 0; i < SIZE; ++i) {
        /* "Hash randomisation" - this array isn't in sorted order: */
        array[i ^ 5] = i * i;
    }

    qsort(array, SIZE, sizeof(int), callback);

    int bad = 0;
    for (i = 0; i < SIZE; ++i) {
        int want = (SIZE - 1 - i) * (SIZE - 1 - i);
        int have = array[i];
        if (want != have) {
            ++bad;
            fprintf(stderr, "array[%u] - want %i, have %i\n", i, want, have);
        }
    }
    if (bad)
        exit(3);

    return NULL;
}

int main(int argc, char **argv) {
    if (minion != NULL) {
        fprintf(stderr, "__thread variable started with %p, should be NULL\n",
                minion);
        exit(4);
    }

    minion = &plus_one;

    pthread_t tid;
    int result = pthread_create(&tid, NULL, thread_function, NULL);
    if (result) {
        fprintf(stderr, "pthread_create failed (%d)\n", result);
        exit(5);
    }

    result = pthread_join(tid, NULL);
    if (result) {
        fprintf(stderr, "pthread_join failed (%d)\n", result);
        exit(6);
    }

    if (minion == NULL) {
        fprintf(stderr, "__thread variable should not be NULL\n");
        exit(7);
    }
    if (!(minion == &plus_one && *minion == 1)) {
        fprintf(stderr, "__thread variable should be %d @ %p, not %d @ %p\n",
                1, &plus_one, *minion, minion);
        exit(8);
    }

    return 0;
}
EOT

    my @try = qw(_Thread_local __thread);

    my $t_l;
    while ($t_l = shift @try) {
        next unless compile($config, 'thread_local', ["THREAD_LOCAL=$t_l"]);
        last if !system "./thread_local";
    }

    if ($t_l) {
        print "$t_l\n";
        $config->{has_thread_local} = 1;
        $config->{thread_local} = $t_l;
    } else {
        print "it doesn't, so falling back to UV's API\n";
        $config->{has_thread_local} = 0;
        $config->{thread_local} = "";
    }
}

sub substandard_pow_cross {
    my ($config) = @_;
    $config->{has_substandard_pow} = 0;
}

sub substandard_pow {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    print ::dots('    probing if your pow() handles NaN and Inf corner cases');
    my $file = 'try.c';
    _spew($file, <<'EOT');
#include <math.h>
#include <stdio.h>

int main(int argc, char **argv) {
    /* Hopefully these games defeat the optimiser, such that we call the runtime
     * library pow, instead of having the C compiler optimiser constant fold it.
     * Without this (for me on Solaris with gcc)
     * pow(1.0, NaN) will be constant folded always
     * pow(1.0, Inf) will be constant folded if optimisation is enabled
     */
    double one = argv[0][0] != '\0';
    double zero = one - 1.0;
    double nan = sqrt(-1);
    if (nan == nan) {
#ifdef CHATTY
        fprintf(stderr, "Can't generate NaN - get %g\n", nan);
#endif
        return 1;
    }
    double inf = pow(10.0, pow(10.0, 100));
    if (inf != 2.0 * inf) {
#ifdef CHATTY
        fprintf(stderr, "Can't generate Inf - get %g\n", inf);
#endif
        return 2;
    }
    double neg_inf = -inf;
    if (! (neg_inf < 0.0)) {
#ifdef CHATTY
        fprintf(stderr, "Can't generate -Inf - get %g\n", inf);
#endif
        return 3;
    }
    if (neg_inf != 2.0 * neg_inf) {
#ifdef CHATTY
        fprintf(stderr, "Can't generate -Inf - get %g\n", inf);
#endif
        return 4;
    }

    /* Solaris is documented not to be conformant with SUSv3 (which I think
     * is POSIX 2001) unless
     * "the application was compiled with the c99 compiler driver"
     * which as best I can tell gcc doesn't, even with -std=c99 */
    double got = pow(one, nan);
    if (got != one) {
#ifdef CHATTY
        fprintf(stderr, "1.0, NaN: pow(%g, %g) is %g, not 1.0\n", one, nan, got);
#endif
        return 5;
    }
    got = pow(one, inf);
    if (got != one) {
#ifdef CHATTY
        fprintf(stderr, "1.0, Inf: pow(%g, %g) is %g, not 1.0\n", one, inf, got);
#endif
        return 6;
    }
    got = pow(one, neg_inf);
    if (got != one) {
#ifdef CHATTY
        fprintf(stderr, "1.0, -Inf: pow(%g, %g) is %g, not 1.0\n", one, neg_inf, got);
#endif
        return 7;
    }

    /* However, Solaris does state that unconditionally:
     *     For any value of x (including NaN), if y is +0, 1.0 is returned.
     * (without repeating that caveat about the c99 compiler driver)
     * and yet behaviour of gcc and *Solaris* studio is consistent in returning
     * NaN for pow(NaN, 0.0). Oracle studio seems to default to c99.
     */
    got = pow(nan, zero);
    if (got != one) {
#ifdef CHATTY
        fprintf(stderr, "NaN, 0.0: pow(%g, %g) is %g, not 1.0\n", nan, zero, got);
#endif
        return 8;
    }
    /* Not seen either of these fail anywhere, but let's test anyway: */
    got = pow(inf, zero);
    if (got != one) {
#ifdef CHATTY
        fprintf(stderr, "Inf, 0.0: pow(%g, %g) is %g, not 1.0\n", inf, zero, got);
#endif
        return 9;
    }
    got = pow(neg_inf, zero);
    if (got != one) {
#ifdef CHATTY
        fprintf(stderr, "-Inf, 0.0: pow(%g, %g) is %g, not 1.0\n", neg_inf, zero, got);
#endif
        return 10;
    }

    return 0;
}
EOT

    my $pow_good = compile($config, 'try') && system('./try') == 0;
    print $pow_good ? "YES\n": "NO\n";
    $config->{has_substandard_pow} = $pow_good ? 0 : 1;
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

sub pthread_setname_np {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    _spew('try.c', <<'EOT');
#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv) {
    char name_target[20];
    pthread_setname_np(pthread_self(), "testthread");
    if (pthread_getname_np(pthread_self(), name_target, 20) == 0) {
        if (strncmp(name_target, "testthread", strlen("testthread")) == 0) {
            return EXIT_SUCCESS;
        }
        else {
            return EXIT_FAILURE;
        }
    }
    return EXIT_FAILURE;
}
EOT

    print ::dots('    probing pthread_setname_np support (optional)');
    my $has_pthread_setname_np = compile($config, 'try') && system('./try') == 0;
    print $has_pthread_setname_np ? "YES\n": "NO\n";
    $config->{has_pthread_setname_np} = $has_pthread_setname_np || 0
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
