#!/usr/bin/env perl
use strict;
use warnings;
use Getopt::Long;
use File::Spec;
use FindBin;
use lib File::Spec->catdir($FindBin::Bin, 'lib');
use timeout qw(run_timeout);

sub run_with {
    my ($command, $env, $timeout) = @_;
    my $status;
    {
        # simulate 'local' env vars, which doesn't really work with
        # child processes
        my %copy;
        while (my ($k,$v) = each %$env) {
            $copy{$k} = $ENV{$v};
            $ENV{$k} = $v;
        }
        if (defined $timeout) {
            $status = run_timeout $command, $timeout;
        } else {
            $status = system @$command;
        }
        while (my ($k,$v) = each %copy) {
            if (defined $v) {
                $ENV{$k} = $v;
            } else {
                delete $ENV{$k};
            }
        }
    }

    if ($status == -1) {
        local $" = ' ';
        die "Failed to start: `@$command`: $!";
    }

    return $status;
}

sub quietly(&) {
    my ($code) = @_;
    my ($error, @result);
    my ($dupout, $duperr);


    open $dupout, '>&', \*STDOUT;
    open $duperr, '>&', \*STDERR;
    close STDOUT;
    close STDERR;
    open STDOUT, '>', File::Spec->devnull;
    open STDERR, '>', File::Spec->devnull;

    eval {
        if (!defined wantarray) {
            $code->();
        } elsif (wantarray) {
            @result = $code->();
        } else {
            $result[0] = scalar $code->();
        }
        1;
    } or do {
        $error = $@ || $!;
    };

    close STDOUT;
    close STDERR;
    open STDOUT, '>&', $dupout;
    open STDERR, '>&', $duperr;
    close $dupout;
    close $duperr;

    die $error if $error;

    return wantarray ? @result : $result[0];
}

sub noisily(&) {
    my ($code) = @_;
    $code->();
}

sub bisect {
    my ($varname, $program, $env, $timeout) = @_;

    $env ||= {};
    printf STDERR ("Bisecting %s\n", $varname);
    if (%$env) {
        printf STDERR "Given:\n";
        printf STDERR "  %s=%s\n", $_, $env->{$_} for keys %$env;
    }

    my ($low, $high, $mid) = (0,1,0);
    my $status;

    do {
        printf STDERR "%s=%d", $varname, $high;
        $status = quietly {
            run_with($program, { %$env, $varname => $high }, $timeout);
        };
        if ($status == 0) {
            print STDERR "\tOK\n";
            ($low, $high) = ($high, $high * 2);
        } else {
            print STDERR "\tNOT OK\n";
        }
    } while ($status == 0);

     while (($high - $low) > 1) {
        $mid = int(($high + $low) / 2);
        printf STDERR "%s=%d", $varname, $mid;
        $status = quietly {
            run_with($program, { %$env, $varname => $mid }, $timeout);
        };
        if ($status == 0) {
            $low = $mid;
            print STDERR "\tOK\n";
        } else {
            $high = $mid;
            print STDERR "\tNOT OK\n";
        }
    }
    return $status ? $low : $mid;
}


my %OPTS = (
    verbose => 0,
    dump => 1,
    timeout => undef,
    spesh => 0,
    nodelay => 0,
);
GetOptions(\%OPTS, qw(verbose dump! timeout=i spesh nodelay)) or die "Could not get options";

my @command = @ARGV;
die 'Command is required' unless @command;

if ($OPTS{verbose}) {
    no warnings 'redefine';
    *quietly = \&noisily;
}
my $timeout = delete $OPTS{timeout};

# start with a clean slate
delete @ENV{qw(
    MVM_JIT_EXPR_DISABLE
    MVM_JIT_EXPR_LAST_FRAME
    MVM_JTI_EXPR_LAST_BB
    MVM_JIT_DISABLE
    MVM_SPESH_LIMIT
    MVM_SPESH_DISABLE
)};

# if we want to 'bisect' a spesh problem, also separate out the
# inline/osr flags
delete @ENV{qw(
    MVM_SPESH_INLINE_DISABLE
    MVM_SPESH_OSR_DISABLE
)} if $OPTS{spesh};
$ENV{MVM_SPESH_BLOCKING} = 1;
$ENV{MVM_SPESH_NODELAY} = 1 if exists $OPTS{nodelay};

# I find that the addition of the MVM_SPESH_LOG / MVM_JIT_LOG
# environment variable can sometimes change the spesh order of
# frames. So let's add it always so that when we run it for logging
# output, we don't accidentally log the wrong frame.
$ENV{$_} = File::Spec->devnull for qw(MVM_SPESH_LOG MVM_JIT_LOG);

quietly { run_with(\@command, {}, $timeout) } or do {
    die "This program is quite alright";
};
quietly {
    run_with(\@command, {
        ($OPTS{spesh} ? (MVM_SPESH_DISABLE => 1) : (MVM_JIT_EXPR_DISABLE => 1))
    }, $timeout)
} and do {
    die "This program cannot be bisected: $?";
};
printf STDERR "Checks OK, this program can be bisected\n";

if ($OPTS{spesh}) {
    # on the hypothesis that it is simpler to debug a spesh log
    # /without/ inlining or OSR, than with it, let's first try to
    # switch flags until we find a breaking combination
    my @flags = ({});
    for my $flag (qw(MVM_SPESH_OSR_DISABLE MVM_SPESH_INLINE_DISABLE MVM_JIT_DISABLE)) {
        @flags = map { $_, { %$_, $flag => 1 } } @flags;
    }

    my $spesh_flags;
    for my $try_flags (reverse @flags) {
        quietly {
            run_with(\@command, $try_flags, $timeout);
        } and do {
            $spesh_flags = $try_flags;
            last;
        }
    }

    my $last_good_frame = bisect('MVM_SPESH_LIMIT', \@command, $spesh_flags, $timeout);
    printf STDERR ("SPESH Broken frame: %d.\n", $last_good_frame + 1);

    # alright, get a spesh diff
    my $log_file = sprintf("spesh-%04d.txt", $last_good_frame + 1);
    printf STDERR ("SPESH Acquiring log: %s\n", $log_file);
    run_with(\@command, {
        %$spesh_flags,
        MVM_SPESH_LOG => $log_file,
        MVM_SPESH_LIMIT => $last_good_frame + 1,
        MVM_JIT_DEBUG => 1,
    }, $timeout);
    print STDERR "Done\n";
} else {
    my $last_good_frame = bisect('MVM_JIT_EXPR_LAST_FRAME', \@command, {}, $timeout);
    my $last_good_block = bisect('MVM_JIT_EXPR_LAST_BB', \@command, {
        MVM_JIT_EXPR_LAST_FRAME => $last_good_frame + 1
    }, $timeout);
    printf STDERR ('JIT Broken Frame/BB: %d / %d'."\n", $last_good_frame + 1, $last_good_block + 1);

    run_with(\@command, {
        MVM_SPESH_LOG => sprintf('spesh-%04d-%04d.txt',
                                 $last_good_frame + 1, $last_good_block + 1),
        MVM_JIT_DEBUG => 1,
        MVM_SPESH_LIMIT => $last_good_frame + 1,
        MVM_JIT_EXPR_LAST_FRAME => $last_good_frame + 1,
        MVM_JIT_EXPR_LAST_BB => $last_good_block + 1,
    }) if $OPTS{dump};
}

__END__
