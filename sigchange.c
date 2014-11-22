/*
 * sigchange.c
 * Program to send a signal to a process when a file changes
 * by J. Stuart McMurray
 * Created 20141120
 * Last modified 20141120
 *
 *Copyright (c) 2014 J. Stuart McMurray
 *
 *Permission is hereby granted, free of charge, to any person obtaining a copy
 *of this software and associated documentation files (the "Software"), to deal
 *in the Software without restriction, including without limitation the rights
 *to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *copies of the Software, and to permit persons to whom the Software is
 *furnished to do so, subject to the following conditions:
 *
 *The above copyright notice and this permission notice shall be included in all
 *copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *SOFTWARE.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <syslog.h>
#include <unistd.h>

#define SLEEPTIME 10 /* Number of seconds to sleep on errors */
#define KQTIME 21600 /* Number of seconds to wait for kevent */
#define KQTIME_ 60 /* Number of seconds to wait for kevent */
/* check_condition shortcut */
#define CC(c, m) check_condition(ev, c, argv[1], m)

/* Print the usage */
void usage(const char* argv0);
/* Open the file or sleep */
int open_or_sleep(const char *path);
/* Send the message to syslog if the condition was set in ev.  fname will be
 * prepended to message, separated by a space */
void check_condition(struct kevent ev, u_int condition, char *fname,
                char *message);
/* Close a file descriptor and set it to -1 */
void closefd(int *fd);

int main(int argc, char **argv) {
        int fd; /* File descriptor to monitor */
        int kq; /* KQueue */
        int ret; /* Return value */
        struct kevent ev;
        struct timespec ts;

        /* Don't care about our children */
        signal(SIGCHLD, SIG_IGN);

        /* Open syslog */
#ifdef DEBUG
        openlog(argv[0], LOG_CONS|LOG_PID|LOG_PERROR, LOG_DAEMON);
#else
        openlog(argv[0], LOG_CONS|LOG_PID, LOG_DAEMON);
#endif /* #ifdef DEBUG */

        /* Make sure we have at least a file and one parameter */
        if (argc < 2) {
                usage(argv[0]);
                exit(EX_USAGE);
        }

        /* Make a kqueue */
        if (-1 == (kq = kqueue())) {
                syslog(LOG_ERR, "Unable to create kqueue: %s",
                                strerror(errno));
                exit(EX_OSERR);
        }

        fd = -1;

        /* Main loop */
        for (;;) {
                /* Close the open file if we have one */
                if (-1 != fd) {
                        closefd(&fd);
                }
                /* Get a descriptor for the file to monitor */
#ifdef DEBUG
                printf("Opening %s\n", argv[1]);
#endif /* #ifdef DEBUG */
                fd = open_or_sleep(argv[1]);
#ifdef DEBUG
                printf("Opened %s: %i\n", argv[1], fd);
#endif /* #ifdef DEBUG */
                /* Set up the event */
                EV_SET(&ev, fd, EVFILT_VNODE, EV_ADD, NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND|NOTE_TRUNCATE|NOTE_RENAME|NOTE_REVOKE, 0, NULL);

                /* Add the event to the kqueue */
#ifdef DEBUG
                printf("Adding event to kqueue\n");
#endif /* #ifdef DEBUG */
                if (-1 == (ret = kevent(kq, &ev, 1, NULL, 0, NULL))) {
                        /* If it fails, log it and try again */
                        syslog(LOG_WARNING, "Sleeping %is because "
                                        "setting watch on %s failed: "
                                        "%s", SLEEPTIME, argv[1],
                                        strerror(errno));

                        closefd(&fd);
                        sleep(SLEEPTIME);
                }
#ifdef DEBUG
                printf("Event added\n");
#endif /* #ifdef DEBUG */

                /* Time to sleep between logging that nothing happened */
                ts.tv_sec = KQTIME;
                ts.tv_nsec = 0;
                /* Clear kevent */
                memset((void*)&ev, 0, sizeof(ev));

#ifdef DEBUG
                printf("Waiting for a kevent\n");
#endif /* #ifdef DEBUG */
                /* Get an event from the kqueue (or timeout) */
                ret = kevent(kq, NULL, 0, &ev, 1, &ts);
#ifdef DEBUG
                printf("Got a kevent.  Ret: %i\n", ret);
#endif /* #ifdef DEBUG */

                /* Nothing has happened */
                if (0 == ret) {
                        syslog(LOG_INFO, "Still waiting for changes to %s",
                                        argv[1]);
                        continue;
                }

                /* An error occurred */
                if ((EV_ERROR & ev.flags) || ret == -1) {
                        syslog(LOG_WARNING, "Sleeping %is due to an error "
                                        "waiting for a change in %s: %s",
                                        SLEEPTIME, argv[1], strerror(errno));
                        sleep(SLEEPTIME);
                        closefd(&fd);
                        continue;
                }
                /* Log what happened */
                if (ev.fflags & NOTE_DELETE) {
                        syslog(LOG_INFO, "%s was deleted", argv[1]);
                }
                CC(NOTE_DELETE, "was deleted");
                CC(NOTE_WRITE, "was written");
                CC(NOTE_EXTEND, "was extended");
                CC(NOTE_TRUNCATE, "was truncated");
                CC(NOTE_RENAME, "was renamed");
                CC(NOTE_REVOKE, "was disappeared");

                /* Execute command if something happened and we have a
                 * command to execute */
                if (((NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND|NOTE_TRUNCATE|NOTE_RENAME|NOTE_REVOKE) &
                                ev.fflags) && argc >= 3) {
#ifdef DEBUG
                        int i;
                        printf("Executing commands:\n");
                        for (i = 2; i < argc; i++)
                                printf("\targv[%i]: %s\n", i-2, argv[i]);
#endif /* #ifdef DEBUG */
                        ret = fork();
                        /* If we're the child code, execute argv[3]... */
                        if (0 == ret) {
                                execvp(argv[2], argv+2);
                                /* If execlp returned, an error occurred */
                                syslog(LOG_ERR, "Unable to execute command "
                                                "(arguments to) follow: %s",
                                                strerror(errno));
                                /* Send the arguments to syslog */
                                for (fd = 2; fd < argc; fd++) {
                                        syslog(LOG_WARNING, "%i: %s", fd,
                                                        argv[fd]);
                                }
                                return -2;
                        } else if (-1 == ret) {
                                syslog(LOG_ERR, "Command not executed "
                                                "because forking failed: %s",
                                                strerror(errno));
                        }
                }
        }
        return 0;
}

/* Open the file or sleep */
int open_or_sleep(const char *path) {
        int fd;
        /* Open the file to monitor */
        fd = -1;
        while (fd < 0) {
                if (-1 == (fd = open(path, O_RDONLY))) {
                        syslog(LOG_WARNING, "Sleeping %is because open of %s "
                                        "failed: %s", SLEEPTIME, path,
                                        strerror(errno));
                        sleep(SLEEPTIME);
                }
        }
        return fd;
}

/* Close a file descriptor and set it to -1 */
void closefd(int *fd) {
        /* Close it */
        if (-1 == close(*fd)) {
                syslog(LOG_WARNING, "Unable to close fd %i: %s", *fd,
                                strerror(errno));
        }
        /* Set it to -1 */
        *fd = -1;
        return;
}

/* Send the message to syslog if the condition was set in ev.  fname will be
 * prepended to message, separated by a space */
void check_condition(struct kevent ev, u_int condition, char *fname,
                char *message) {
        if (ev.fflags & condition) {
                syslog(LOG_INFO, "%s %s", fname, message);
        }
}

/* Print the usage */
void usage(const char* argv0) {
        printf("Usage: %s file [command]\n", argv0);
        return;
}
