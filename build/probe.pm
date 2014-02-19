package build::probe;
use strict;
use warnings;

use File::Path qw(make_path remove_tree);
use File::Spec::Functions qw(curdir catdir rel2abs devnull);

my $devnull = devnull();

my $probe_dir;

END {
    remove_tree($probe_dir)
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
        make_path($probe_dir);
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

    my $command = "$config->{ld} $config->{ldout}$leaf @objs  >$devnull 2>&1";
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

'00';
