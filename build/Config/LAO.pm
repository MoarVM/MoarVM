package Config::LAO;
use strict;
use warnings;

sub configure {
    my %config = @_;

    if ($^O =~ /MSWin32/) {
        return (%config,
            # libatomic_ops is headers-only on Windows \q/
            lao_build_line => 'rem nop',
            lao_lib => 'nonexistent_libatomic_ops.lib',
            lao_whether_build => ''
        );
    }
    else {
        return (%config,
            # XXX add compiler optimizations here?
            lao_build_line => 'cd 3rdparty/libatomic_ops && ./configure && make',
            lao_lib => '3rdparty/libatomic_ops/src/libatomic_ops.a',
            lao_whether_build => '3rdparty/libatomic_ops/src/libatomic_ops.a'
        );
    }
}

'Yeti';
