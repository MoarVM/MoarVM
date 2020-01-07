package timeout;
use strict;
use warnings;
use Exporter qw(import);

our @EXPORT_OK = qw(run_timeout);

sub run_timeout {
    my ($command, $timeout) = @_;
    my $status;
    my $pid = fork();
    local $SIG{CHLD} = sub {};
    if ($pid) {
        local $SIG{ALRM} = sub {
            kill 'KILL', $pid;
        };
        alarm $timeout;
        waitpid $pid, 0;
        $status = $?;
        alarm 0 if $timeout;
    } elsif (defined $pid) {
        # child, does not return
        exec @$command;
    } else {
        die $!;
    }
    return wantarray ? ($status, $pid) : $status;

}

1;
