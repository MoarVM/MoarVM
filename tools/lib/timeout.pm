package timeout;
use strict;
use warnings;
use Exporter qw(import);

our @EXPORT_OK = qw(run_timeout);

sub run_timeout {
    my ($command, $timeout) = @_;
    my $status;
    if (my $pid = fork()) {
        local $SIG{ALRM} = sub {
            kill 'KILL', $pid;
        };
        alarm $timeout;
        waitpid $pid, 0;
        $status = $?;
        alarm 0;
    } else {
        exec @$command;
    }
    return $status;
}

1;
