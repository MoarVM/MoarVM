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
    my ($config, $leaf, $defines, $files, $extra_libs) = @_;
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

    my $libs = join ' ', $config->{ldlibs}, @{$extra_libs // []};

    my $command = "$config->{ld} $ENV{LDFLAGS} $config->{ldout}$leaf @objs $libs >$devnull 2>&1";
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

sub simple_compile_probe {
    my %args = @_;
    my ($config, $probing, $code, $key, $invert)
        = @args{qw(config probing code key invert)};

    my $restore = _to_probe_dir();
    _spew('try.c', $code);

    print ::dots('    probing ' . $probing);
    my $good = compile($config, 'try');
    unless ($config->{crossconf}) {
        $good &&= !system './try';
    }
    print $good ? "YES\n": "NO\n";
    $good = !$good
        if $invert;
    $config->{$key} = $good ? 1 : 0;
    return;
}

sub compile_probe_first_of {
    my %args = @_;
    my ($config, $probing, $code, $key, $fallback) = @args{qw(config probing code key fallback)};
    my @options = @{$args{options}};

    my $restore = _to_probe_dir();
    _spew('try.c', $code);

    print ::dots('    probing ' . $probing);

    my $candidate;
    while ($candidate = shift @options) {
        next unless compile($config, 'try', ["PROBE_MACRO=$candidate"]);
        # If cross compiling we have to assume that the first thing that
        # compiles is workable :-(
        last if $config->{crossconf};
        last if !system "./try";
    }

    if (defined $candidate) {
        print "$candidate\n";
        $config->{"has_$key"} = 1;
        $config->{$key} = $candidate;
    } else {
        print "$fallback\n";
        $config->{"has_$key"} = 0;
        $config->{$key} = "";
    }
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

sub static_inline {
    my ($config) = @_;
    if ($config->{crossconf}) {
        # FIXME. Needs testing, but might be robust enough to do what the native
        # code does, but just skip the system() to run the executable. Although
        # this might get confused by link time optimisations that only fail at
        # run time, which the system test does detect.
        $config->{static_inline} = 'static';
        return;
    }

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

sub thread_local {
    my ($config) = @_;

    if ($config->{crossconf}) {
        $config->{has_thread_local} = 0;
        $config->{thread_local} = "";
        return;
    }

    # We don't need to probe for this on Win32, as UV sets it for us
    if ($^O eq 'MSWin32') {
        $config->{has_thread_local} = 0;
        $config->{thread_local} = "";
        return;
    }

    return compile_probe_first_of(config => $config,
                                  probing => 'if your compiler offers thread local storage',
                                  fallback => "it doesn't, so falling back to UV's API",
                                  options => [qw(_Thread_local __thread)],
                                  key => 'thread_local',
                                  code => <<'EOT');
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

static int plus_one = 1;
static int minus_one = -1;

PROBE_MACRO int *minion;

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
}

my @floating_point_vals = qw(zero one nan inf neg_inf neg_zero
                             int_less_than_minus_one int_more_than_one
                             roughly_ten roughly_minus_ten);

my $floating_point_main = <<'EOT';
#include <math.h>
#include <stdio.h>

int main(int argc, char **argv) {
    /* Hopefully these games defeat the optimiser, such that we call the runtime
     * library pow, instead of having the C compiler optimiser constant fold it.
     * Without this (for me on Solaris with gcc)
     * pow(1.0, NaN) will be constant folded always
     * pow(1.0, Inf) will be constant folded if optimisation is enabled
     */
    double zero = argv[0][0] == '\0';
    double one = zero + 1.0;
    double int_less_than_minus_one = 34 - argv[0][0];
    double int_more_than_one = argv[0][0] - 30;
    double inf = int_more_than_one * pow(10.0, pow(10.0, 100));
    double neg_inf = int_less_than_minus_one * pow(10.0, pow(10.0, 100));
    double neg_zero = one / neg_inf;
    double roughly_ten = atan2(zero, -one) * atan2(zero, -one);
    double roughly_minus_ten = -roughly_ten;
    double nan = sqrt(roughly_minus_ten);

    if (zero) {
#ifdef CHATTY
        fprintf(stderr, "Can't generate zero - get %g\n", zero);
#endif
        return 1;
    }

    if (nan == nan) {
#ifdef CHATTY
        fprintf(stderr, "Can't generate NaN - get %g\n", nan);
#endif
        return 2;
    }

    if (! (inf > 0.0)) {
#ifdef CHATTY
        fprintf(stderr, "Can't generate Inf - get %g\n", inf);
#endif
        return 3;
    }
    if (inf != 2.0 * inf) {
#ifdef CHATTY
        fprintf(stderr, "Can't generate Inf - get %g\n", inf);
#endif
        return 4;
    }

    if (! (neg_inf < 0.0)) {
#ifdef CHATTY
        fprintf(stderr, "Can't generate -Inf - get %g\n", neg_inf);
#endif
        return 5;
    }
    if (neg_inf != 2.0 * neg_inf) {
#ifdef CHATTY
        fprintf(stderr, "Can't generate -Inf - get %g\n", neg_inf);
#endif
        return 6;
    }

    if (neg_zero != 0.0) {
        fprintf(stderr, "Can't generate -0.0 - get %g\n", neg_zero);
        return 7;
    }
    if (1.0 / neg_zero != neg_inf) {
        fprintf(stderr, "Can't generate -0.0 - get %g\n", neg_zero);
        return 8;
    }
EOT

sub substandard_pow {
    my ($config) = @_;

    if ($config->{crossconf}) {
        # A guess, but so far only pre-C99 Solaris has failed this:
        $config->{has_substandard_pow} = 0;
        return;
    }

    return simple_compile_probe(config => $config,
                                probing => 'if your pow() handles NaN and Inf corner cases',
                                invert => 1,
                                key => 'has_substandard_pow',
                                code => $floating_point_main . <<'EOT');
    /* Solaris is documented not to be conformant with SUSv3 (which I think
     * is POSIX 2001) unless
     * "the application was compiled with the c99 compiler driver"
     * which as best I can tell gcc doesn't, even with -std=c99 */
    double got = pow(one, nan);
    if (got != one) {
#ifdef CHATTY
        fprintf(stderr, "1.0, NaN: pow(%g, %g) is %g, not 1.0\n", one, nan, got);
#endif
        return 11;
    }
    got = pow(one, inf);
    if (got != one) {
#ifdef CHATTY
        fprintf(stderr, "1.0, Inf: pow(%g, %g) is %g, not 1.0\n", one, inf, got);
#endif
        return 12;
    }
    got = pow(one, neg_inf);
    if (got != one) {
#ifdef CHATTY
        fprintf(stderr, "1.0, -Inf: pow(%g, %g) is %g, not 1.0\n", one, neg_inf, got);
#endif
        return 13;
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
        return 14;
    }
    /* Not seen either of these fail anywhere, but let's test anyway: */
    got = pow(inf, zero);
    if (got != one) {
#ifdef CHATTY
        fprintf(stderr, "Inf, 0.0: pow(%g, %g) is %g, not 1.0\n", inf, zero, got);
#endif
        return 15;
    }
    got = pow(neg_inf, zero);
    if (got != one) {
#ifdef CHATTY
        fprintf(stderr, "-Inf, 0.0: pow(%g, %g) is %g, not 1.0\n", neg_inf, zero, got);
#endif
        return 16;
    }

    /* Negative odd integers preserve sign */
    got = pow(zero, -3.0);
    if (!(got > 0)) {
#ifdef CHATTY
        fprintf(stderr, "0.0, -3.0: pow(%g, -3.0) is %g, not +Inf\n", zero, got);
#else
        return 17;
#endif
    }
    if (got != 2 * got) {
#ifdef CHATTY
        fprintf(stderr, "0.0, -3.0: pow(%g, -3.0) is %g, not +Inf\n", zero, got);
#else
        return 18;
#endif
    }

    got = pow(neg_zero, -3.0);
    if (!(got < 0)) {
#ifdef CHATTY
        fprintf(stderr, "-0.0, -3.0: pow(%g, -3.0) is %g, not -Inf\n", neg_zero, got);
#else
        return 19;
#endif
    }
    if (got != 2 * got) {
#ifdef CHATTY
        fprintf(stderr, "-0.0, -3.0: pow(%g, -3.0) is %g, not -Inf\n", neg_zero, got);
#else
        return 20;
#endif
    }

    /* Everything else is positive infinity. */
    got = pow(zero, -2.0);
    if (!(got > 0)) {
#ifdef CHATTY
        fprintf(stderr, "0.0, -2.0: pow(%g, -2.0) is %g, not +Inf\n", zero, got);
#else
        return 21;
#endif
    }
    if (got != 2 * got) {
#ifdef CHATTY
        fprintf(stderr, "0.0, -2.0: pow(%g, -2.0) is %g, not +Inf\n", zero, got);
#else
        return 21;
#endif
    }

    got = pow(neg_zero, -2.0);
    if (!(got > 0)) {
#ifdef CHATTY
        fprintf(stderr, "-0.0, -2.0: pow(%g, -2.0) is %g, not +Inf\n", neg_zero, got);
#else
        return 22;
#endif
    }
    if (got != 2 * got) {
#ifdef CHATTY
        fprintf(stderr, "-0.0, -2.0: pow(%g, -2.0) is %g, not +Inf\n", neg_zero, got);
#else
        return 23;
#endif
    }

    got = pow(zero, -2.78);
    if (!(got > 0)) {
#ifdef CHATTY
        fprintf(stderr, "0.0, -2.78: pow(%g, -2.78) is %g, not +Inf\n", zero, got);
#else
        return 24;
#endif
    }
    if (got != 2 * got) {
#ifdef CHATTY
        fprintf(stderr, "0.0, -2.78: pow(%g, -2.78) is %g, not +Inf\n", zero, got);
#else
        return 25;
#endif
    }

    got = pow(neg_zero, -2.78);
    if (!(got > 0)) {
#ifdef CHATTY
        fprintf(stderr, "-0.0, -2.78: pow(%g, -2.78) is %g, not +Inf\n", neg_zero, got);
#else
        return 26;
#endif
    }
    if (got != 2 * got) {
#ifdef CHATTY
        fprintf(stderr, "-0.0, -2.78: pow(%g, -2.78) is %g, not +Inf\n", neg_zero, got);
#else
        return 27;
#endif
    }

    return 0;
}
EOT
}

sub substandard_log {
    my ($config) = @_;

    if ($config->{crossconf}) {
        # A guess, but so far only pre-C99 Solaris has failed this:
        $config->{has_substandard_log} = 0;
        $config->{has_substandard_log10} = 0;
        return;
    }

    for my $fn (qw(log log10)) {
        simple_compile_probe(config => $config,
                             probing => "if your $fn() returns NaN for negative values",
                             invert => 1,
                             key => "has_substandard_$fn",
                             code => $floating_point_main . <<"EOT");
    double got = $fn(neg_inf);
    if (got == got) {
#ifdef CHATTY
        fprintf(stderr, "-Inf: $fn(%g) is %g, not NaN\\n", neg_inf, got);
#else
        return 11;
#endif
    }

    got = $fn(int_less_than_minus_one);
    if (got == got) {
#ifdef CHATTY
        fprintf(stderr, "minus something: $fn(%g) is %g, not NaN\\n", int_less_than_minus_one, got);
#else
        return 12;
#endif
    }

    return 0;
}
EOT
    }
}

sub substandard_trig {
    my ($config) = @_;

    if ($config->{crossconf}) {
        # A guess, but so far only pre-C99 Solaris has failed this:
        $config->{has_substandard_asin} = 0;
        $config->{has_substandard_acos} = 0;
        return;
    }

    for my $fn (qw(asin acos)) {
        simple_compile_probe(config => $config,
                             probing => "if your $fn() returns NaN for negative values",
                             invert => 1,
                             key => "has_substandard_$fn",
                             code => $floating_point_main . <<"EOT");

    double got = $fn(int_more_than_one);
    if (got == got) {
#ifdef CHATTY
        fprintf(stderr, "int_more_than_one: $fn(%g) is %g, not NaN\\n", int_more_than_one, got);
#else
        return 11;
#endif
    }
    got = $fn(inf);
    if (got == got) {
#ifdef CHATTY
        fprintf(stderr, "Inf: $fn(%g) is %g, not NaN\\n", inf, got);
#else
        return 12;
#endif
    }

    got = $fn(int_less_than_minus_one);
    if (got == got) {
#ifdef CHATTY
        fprintf(stderr, "int_less_than_minus_one: $fn(%g) is %g, not NaN\\n", int_less_than_minus_one, got);
#else
        return 13;
#endif
    }
    got = $fn(neg_inf);
    if (got == got) {
#ifdef CHATTY
        fprintf(stderr, "-Inf: $fn(%g) is %g, not NaN\\n", neg_inf, got);
#else
        return 14;
#endif
    }

    return 0;
}
EOT
    }
}

sub has_isinf_and_isnan {
    my ($config) = @_;

    my @probes = (
        [ isnan => 'nan' ],
        [ isinf => qw(inf neg_inf) ],
        [ signbit => qw(neg_inf neg_zero int_less_than_minus_one roughly_minus_ten)],
    );

    if ($config->{crossconf}) {
        # A guess
        for (@probes) {
            my $func = $_->[0];
            $config->{"has_$func"} = 0;
        }
        return;
    }

    for (@probes) {
        my ($func, @vals) = @$_;
        my %true;
        @true{@vals} = (1) x @vals;

        my $code = $floating_point_main;
        my $exit = 11;

        for my $val (@floating_point_vals) {
            # NaNs can actually have sign bits, so the sign bit might/might not
            # be set, and we can't know (and shouldn't care):
            next
                if $func eq 'signbit' && $val eq 'nan';

            my $want = $true{$val} ? 'T' : 'f';

            $code .= <<"EOT";
    {
        int want = '$want';
        int have = $func($val) ? 'T' : 'f';

        if (want != have) {
#ifdef CHATTY
            fprintf(stderr, "$func($val): Have %c, Want $want\\n", have);
#else
            return $exit;
#endif
        }
    }
EOT
            ++$exit;
        }
        $code .= <<"EOT";
    return 0;
}
EOT

        simple_compile_probe(config => $config,
                             probing => "if you have $func",
                             key => "has_$func",
                             code => $code);
    }
    return;
}

sub specific_werror {
    my ($config) = @_;
    my $restore = _to_probe_dir();

    if ($config->{cc} ne 'gcc') {
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

    if ($config->{crossconf}) {
        _gen_unaligned_access($config, '');
    } elsif ($^O eq 'MSWin32') {
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

# It would be good to find a robust way to do this without needing to *run* the
# compiled code. At which point we could also use it for the native build.
sub ptr_size {
    my ($config) = @_;

    if ($config->{crossconf}) {
        warn "Guessing :-(";
        $config->{ptr_size} = 4;
        return;
    }

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

sub computed_goto {
    my ($config) = @_;
    return simple_compile_probe(config => $config,
                                probing => 'computed goto support',
                                key => 'cancgoto',
                                code => <<'EOT');
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
}

sub check_fn_malloc_trim {
    my ($config) = @_;
    # Seems that this was written with a plan to be generic, but so far nothing
    # else has wanted it.
    my $includes = '#include <malloc.h>';
    my $function = 'malloc_trim(0)';
    return simple_compile_probe(config => $config,
                                probing => 'existance of optional malloc_trim()',
                                key => 'has_fn_malloc_trim',
                                code => <<"EOT");
$includes

int main(int argc, char **argv) {
    $function;
    return 0;
}
EOT
}

sub C_type_bool {
    my ($config) = @_;
    return compile_probe_first_of(config => $config,
                                  probing => 'C type support for booleans',
                                  fallback => '(none found)',
                                  options => [qw(_Bool bool)],
                                  key => 'booltype',
                                  code => <<'EOT');
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

int main(int argc, char **argv) {
    PROBE_MACRO foo = false;
    foo    = true;
    return foo ? EXIT_SUCCESS : EXIT_FAILURE;
}
EOT
}

sub pthread_yield {
    my ($config) = @_;
    return simple_compile_probe(config => $config,
                                probing => 'pthread_yield support',
                                key => 'has_pthread_yield',
                                code => <<'EOT');
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
}

sub pthread_setname_np {
    my ($config) = @_;
    return simple_compile_probe(config => $config,
                                probing => 'pthread_setname_np support (optional)',
                                key => 'has_pthread_setname_np',
                                code => <<'EOT');
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
}

sub rdtscp {
    my ($config) = @_;
    return simple_compile_probe(config => $config,
                                probing => 'support of rdtscp intrinsic',
                                key => 'has_rdtscp',
                                code => <<'EOT');
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
}

sub stdatomic {
    my ($config) = @_;

    if ($config->{crossconf}) {
        warn "Guessing :-(";
        $config->{has_stdatomic} = 0;
        return;
    }

    my $restore = _to_probe_dir();
    _spew('try.c', <<'EOT');
#include <stdlib.h>
#include <stdatomic.h>

/* mimalloc relies on behaviour that was buggy in the original C11 *spec*. See
 * http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1807.htm
 * At least one compiler we've tried (clang version 7.0.1-8+rpi3+deb10u2) will
 * fail to compile this probe because it implements C11 correctly. C11-as-was.
 * It is unable to build mimalloc */

int value (const _Atomic long long *ptr) {
    return atomic_load(ptr);
}

/* on some 32 bit systems atomic operations on 64 bit values rely on a support
 * library. */

int main(int argc, char **argv) {
    _Atomic long long probe = 42;
    probe -= 6 * 7;
    return value(&probe);
}
EOT

    print ::dots('    probing stdatomic');

    # gcc might need -latomic.
    # (looks like Solaris Studio 12.5 and later needs libatomic.so)
    for my $lib (undef, '-latomic') {
        if (compile($config, 'try', undef, undef, $lib && [$lib])) {
            if (!system './try') {
                if ($lib) {
                    print "YES, with $lib\n";
                    $config->{ldlibs} .= " $lib";
                }
                else {
                    print "YES\n";
                }
                $config->{has_stdatomic} = 1;
                return;
            }
        }
    }
    print "NO\n";
    return;
}

'00';
