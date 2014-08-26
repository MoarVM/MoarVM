#
# Regular cron jobs for the moarvm package
#
0 4	* * *	root	[ -x /usr/bin/moarvm_maintenance ] && /usr/bin/moarvm_maintenance
