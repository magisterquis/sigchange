sigchange
=========

Small program to execute an arbitrary command when a file changes.
Useful for notifications when log entries are added or (it's original purpose)
when /etc/hosts is updated.

Usage
-----
    sigchange file_to_monitor [command to run]

Whenever the file is changed (or renamed or moved or deleted), the given
command is run.

Examples
--------
Send a message to all users when someone tries to authenticate.

    sigchange /var/log/authlog sh -c 'tail -n 1 /var/log/authlog | wall'

Keep track of when someone changes /etc/hosts

    sigchange /etc/hosts date

Restart dnsmasq when its config file changes

    sigchange /etc/dnsmasq.conf sudo /etc/rc.d/dnsmasq restart

Gotchas
-------
This program uses kqueues, so it probably won't work on Linux.
