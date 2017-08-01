sub MAIN($spesh-log) {
    with slurp($spesh-log) {
        my $stats = [+] .match(/:r 'statistics updated in ' <( \d+ )> 'us'/, :g);
        my $plan = [+] .match(/:r 'planned in ' <( \d+ )> 'us'/, :g);
        my $spesh = [+] .match(/:r 'Specialization took ' <( \d+ )> 'us'/, :g);
        say qq:to/REPORT/
            Total statistics time:      {$stats / 1000}ms
            Total planning time:        {$plan / 1000}ms
            Total specialization time:  {$spesh / 1000}ms
            REPORT
    }
}
