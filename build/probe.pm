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
        my $command = "$config->{cc} $cl_define $config->{ccout}$obj $config->{ccswitch} $file >$devnull 2>&1";
        system $command
            and return;
        push @objs, $obj;
    }

    my $command = "$config->{ld} $config->{ldout}$leaf @objs $config->{ldlibs} >$devnull 2>&1";
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


sub _gen_unaligned_access {
    my ($config, $can) = @_;
    my @align = qw(int32 int64 num64);
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
            print "    your CPU can't read unaligned values for any of @align\n";
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

sub pthread_yield {
    my ($config) = @_;
    my $restore = _to_probe_dir();
    _spew('try.c', <<'EOT');
#include <stdlib.h>
#include <pthread.h>

int main(int argc, char **argv) {
    pthread_yield();
    return EXIT_SUCCESS;
}
EOT

    print ::dots('    probing pthread_yield support');
    my $has_pthread_yield = compile($config, 'try');
    print $has_pthread_yield ? "YES\n": "NO\n";
    $config->{has_pthread_yield} = $has_pthread_yield || 0
}

'00';
