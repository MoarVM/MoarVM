#!/usr/bin/env perl
use strict;
use warnings;
use Getopt::Long;
use File::Spec;
use FindBin;


sub run_with {
    my ($command, $env) = @_;
    my $status;
    {
        # simulate 'local' env vars, which doesn't really work with
        # child processes
        my %copy;
        while (my ($k,$v) = each %$env) {
            $copy{$k} = $ENV{$v};
            $ENV{$k} = $v;
        }
        $status = system @$command;
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
    my ($varname, $program, $env) = @_;

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
            run_with($program, { %$env, $varname => $high });
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
            run_with($program, { %$env, $varname => $mid });
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
    dump => 1
);
GetOptions(\%OPTS, qw(verbose dump!)) or die "Could not get options";

my @command = @ARGV;
die 'Command is required' unless @command;

if ($OPTS{verbose}) {
    no warnings 'redefine';
    *quietly = \&noisily;
}

# start with a clean slate
delete @ENV{qw(
    MVM_JIT_EXPR_DISABLE
    MVM_JIT_EXPR_LAST_FRAME
    MVM_JTI_EXPR_LAST_BB
    MVM_JIT_DISABLE
    MVM_SPESH_DISABLE
)};


quietly { run_with(\@command, {}) } or do {
    die "This program is quite alright";
};
quietly { run_with(\@command, { MVM_JIT_EXPR_DISABLE => 1 }) } and do {
    die "This program cannot be bisected: $?";
};
printf STDERR "Checks OK, this program can be bisected\n";



my $last_good_frame = bisect('MVM_JIT_EXPR_LAST_FRAME', \@command);
my $last_good_block = bisect('MVM_JIT_EXPR_LAST_BB', \@command, {
    MVM_JIT_EXPR_LAST_FRAME => $last_good_frame + 1
});
printf STDERR ('Broken Frame/BB: %d / %d'."\n", $last_good_frame + 1, $last_good_block + 1);

my $dump_script = File::Spec->catfile($FindBin::Bin, 'jit-dump.pl');
my @dump_command = (
    $^X, $dump_script,
    '--frame' => $last_good_frame + 1, '--block' => $last_good_block + 1,
    '--', @command
);
run_with(\@dump_command, {}) if $OPTS{dump};

__END__

