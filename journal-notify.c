/*
 * (C) 2014 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include <systemd/sd-journal.h>

#include <libnotify/notify.h>

const char * program = NULL;

#define OPTSTRING	"ehi:m:nr:"
#define DEFAULTICON	"dialog-information"

int notify(const char * summary, const char * body, const char * icon) {
	NotifyNotification * notification;
	int rc = -1;

	notification =
#if NOTIFY_CHECK_VERSION(0, 7, 0)
		notify_notification_new(summary, body, icon);
#else
		notify_notification_new(summary, body, icon, NULL);
#endif

	if (notification == NULL)
		return rc;

	if (notify_notification_show(notification, NULL) == FALSE)
		goto out;

	rc = 0;

out:
	g_object_unref(G_OBJECT(notification));

	return rc;
}

int main(int argc, char **argv) {
	int i, rc = EXIT_FAILURE;

	uint8_t have_regex = 0;
	regex_t regex;
	int regex_flags = REG_NOSUB;

	sd_journal * journal;
	const void * data;
	size_t length;

	char * summary, * message;
	const char * icon = DEFAULTICON;

	program = argv[0];

	/* get command line options - part I
	 * just get -h (help), -e and -n (regex options) here */
	while ((i = getopt(argc, argv, OPTSTRING)) != -1) {
		switch (i) {
			case 'e':
				regex_flags |= REG_EXTENDED;
				break;
			case 'h':
				fprintf(stderr, "usage: %s [-e] [-h] [-m MATCH] [-n] [-r REGEX]\n", program);
				return EXIT_SUCCESS;
			case 'n':
				regex_flags |= REG_ICASE;
				break;
		}
	}

	/* reinitialize getopt() by resetting optind to 0 */
	optind = 0;

	/* open journal */
	if ((rc = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY + SD_JOURNAL_SYSTEM)) < 0) {
		fprintf(stderr, "Failed to open journal: %s\n", strerror(-rc));
		goto out10;
	}

	/* get command line options - part II*/
	while ((i = getopt(argc, argv, OPTSTRING)) != -1) {
		switch (i) {
			case 'i':
				icon = optarg;
				break;
			case 'm':
				if ((rc = sd_journal_add_match(journal, optarg, 0)) < 0) {
					fprintf(stderr, "Failed to add match '%s': %s\n", optarg, strerror(-rc));
					goto out20;
				}

				break;
			case 'r':
				if (have_regex > 0) {
					fprintf(stderr, "Only one regex allowed!\n");
					rc = EXIT_FAILURE;
					goto out20;
				}

				if ((rc = regcomp(&regex, optarg, regex_flags)) != 0) {
					fprintf(stderr, "Could not compile regex\n");
					goto out20;
				}
				have_regex++;

				break;
		}
	}

	/* seek to the end of the journal */
	if ((rc = sd_journal_seek_tail(journal)) < 0) {
		fprintf(stderr, "Failed to seek to the tail: %s\n", strerror(-rc));
		goto out30;
	}

	/* we are behind the last entry, so use previous one */
	if ((rc = sd_journal_previous(journal)) < 0) {
		fprintf(stderr, "Failed to iterate to previous entry: %s\n", strerror(-rc));
		goto out30;
	}

	if (notify_init(program) == FALSE) {
		fprintf(stderr, "Failed to initialize notify.\n");
		rc = EXIT_FAILURE;
		goto out30;
	}

	while (1) {
		if ((rc = sd_journal_next(journal)) < 0) {
			fprintf(stderr, "Failed to iterate to next entry: %s\n", strerror(-rc));
			goto out40;
		} else if (rc == 0) {
			if ((rc = sd_journal_wait(journal, (uint64_t) -1)) < 0) {
				fprintf(stderr, "Failed to wait for changes: %s\n", strerror(-rc));
				goto out40;
			}
			continue;
		}

		/* get MESSAGE field */
		if ((rc = sd_journal_get_data(journal, "MESSAGE", &data, &length)) < 0) {
			fprintf(stderr, "Failed to read message field: %s\n", strerror(-rc));
			continue;
		}
		message = g_markup_escape_text(data + 8, length - 8);

		/* get SYSLOG_IDENTIFIER field */
		if ((rc = sd_journal_get_data(journal, "SYSLOG_IDENTIFIER", &data, &length)) < 0) {
			fprintf(stderr, "Failed to read syslog identifier field: %s\n", strerror(-rc));
			continue;
		}
		summary = g_markup_escape_text(data + 18, length - 18);

		/* show notification */
		if (have_regex == 0 || regexec(&regex, message, 0, NULL, 0) == 0) {
			if ((rc = notify(summary, message, icon)) < 0) {
				fprintf(stderr, "Failed to show notification.\n");
				goto out40;
			}
		}

		free(summary);
		free(message);
	}

	rc = EXIT_SUCCESS;

out40:
	notify_uninit();

out30:
	if (have_regex > 0)
		regfree(&regex);

out20:
	sd_journal_close(journal);

out10:
	return rc;
}

// vim: set syntax=c:
