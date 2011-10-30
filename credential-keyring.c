/*
 * Copyright (C) 2011 John Szakmeister <john@szakmeister.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <gnome-keyring.h>
#include <libgnomeui/libgnomeui.h>


struct url_parts
{
    char *protocol;
    char *server;
};


static void
die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    (void) fprintf(stderr, "fatal: ");
    (void) vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}


static void
die_errno(int err)
{
    (void) fprintf(stderr, "fatal: %s\n", strerror(err));
    exit(1);
}


static void
die_result(GnomeKeyringResult result)
{
    (void) fprintf(stderr, "fatal: %s\n",
        gnome_keyring_result_to_message(result));
    exit(1);
}


static char *
xstrdup(const char *str)
{
    char *ret = strdup(str);
    if (! ret)
    {
        die_errno(errno);
    }

    return ret;
}


static void
remove_credential(const char *username, const struct url_parts *parts)
{
    GList *entries;
    GnomeKeyringNetworkPasswordData *passwordData;
    GnomeKeyringResult result;

    result = gnome_keyring_find_network_password_sync(
        username,
        NULL /* domain */,
        parts->server,
        NULL /* object */,
        parts->protocol,
        NULL /* authtype */,
        0 /* port */,
        &entries);

    if (result == GNOME_KEYRING_RESULT_NO_MATCH)
        return;

    if (result == GNOME_KEYRING_RESULT_CANCELLED)
        return;

    if (result != GNOME_KEYRING_RESULT_OK)
    {
        die_result(result);
    }

    /* Pick the first one from the list. */
    passwordData = (GnomeKeyringNetworkPasswordData *) entries->data;

    result = gnome_keyring_item_delete_sync(
        passwordData->keyring, passwordData->item_id);

    gnome_keyring_network_password_list_free(entries);

    if (result != GNOME_KEYRING_RESULT_OK)
        die_result(result);

    return;
}


static const char *
lookup_credential(
    const char **username,
    const struct url_parts *parts,
    int *cancelled)
{
    const char *password = NULL;
    GList *entries;
    GnomeKeyringNetworkPasswordData *passwordData;
    GnomeKeyringResult result;

    *cancelled = 0;

    result = gnome_keyring_find_network_password_sync(
        *username,
        NULL /* domain */,
        parts->server,
        NULL /* object */,
        parts->protocol,
        NULL /* authtype */,
        0 /* port */,
        &entries);

    if (result == GNOME_KEYRING_RESULT_NO_MATCH)
        return NULL;

    if (result == GNOME_KEYRING_RESULT_CANCELLED)
    {
        *cancelled = 1;
        return NULL;
    }

    if (result != GNOME_KEYRING_RESULT_OK)
    {
        die_result(result);
    }

    /* Pick the first one from the list. */
    passwordData = (GnomeKeyringNetworkPasswordData *) entries->data;

    password = xstrdup(passwordData->password);

    if (*username == NULL)
        *username = xstrdup(passwordData->user);

    gnome_keyring_network_password_list_free(entries);

    return password;
}


static void
store_credential(
    struct url_parts *parts,
    const char *username,
    const char *password)
{
    guint32 item_id;
    gnome_keyring_set_network_password_sync(
        GNOME_KEYRING_DEFAULT,
        username,
        NULL /* domain */,
        parts->server,
        NULL /* object */,
        parts->protocol,
        NULL /* authtype */,
        0 /* port */,
        password,
        &item_id);
    return;
}


static void
split_unique(struct url_parts *parts, const char *token)
{
    char *tmp = xstrdup(token);
    char *pch;

    /* Get the protocol */
    pch = strtok(tmp, ":");
    if (! pch)
    {
        die("invalid token passed: '%s'", token);
    }

    parts->protocol = xstrdup(pch);

    pch = strtok(NULL, ":");
    if (! pch)
    {
        die("invalid token passed: '%s'", token);
    }

    parts->server = xstrdup(pch);

    free(tmp);
}


static int
ask_credentials_gui(const char **username, const char **password)
{
    GtkWidget *dialog;
    gboolean result;

    dialog = gnome_password_dialog_new(
        "Password for XXX",
        "Please enter password for XXX",
        *username,
        NULL,
        FALSE);

    gnome_password_dialog_set_show_username(
        GNOME_PASSWORD_DIALOG(dialog), TRUE);
    if (*username)
        gnome_password_dialog_set_username(
            GNOME_PASSWORD_DIALOG(dialog), *username);

    gnome_password_dialog_set_show_password(
        GNOME_PASSWORD_DIALOG(dialog), TRUE);

    result = gnome_password_dialog_run_and_block(
        GNOME_PASSWORD_DIALOG(dialog));
    if (result == FALSE)
    {
        gtk_widget_destroy(dialog);
        return 0;
    }

    *username = xstrdup(
        gnome_password_dialog_get_username(GNOME_PASSWORD_DIALOG(dialog)));
    *password = xstrdup(
        gnome_password_dialog_get_password(GNOME_PASSWORD_DIALOG(dialog)));

    gtk_widget_destroy(dialog);
    return 1;
}


int
main(int argc, char *argv[])
{
    int reject = 0;
    const char *username = NULL;
    char *description = NULL;
    char *unique = NULL;
    int c;
    int option_index = 0;
    struct url_parts parts;
    const char *password;
    int cancelled;

    struct option long_options[] =
    {
        {"reject", no_argument, &reject, 1},
        {"username", required_argument, 0, 'u'},
        {"description", required_argument, 0, 'd'},
        {"unique", required_argument, 0, 't'},
        {0, 0, 0, 0},
    };

    gtk_init(&argc, &argv);

    for (;;)
    {
        c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c == -1)
            break;

        switch (c)
        {
        case 0:
            break;

        case 'u':
            username = optarg;
            break;
        case 'd':
            description = optarg;
            break;
        case 't':
            unique = optarg;
            break;

        case '?':
            /* getopt already printed an error message. */
            break;

        default:
            die("unrecognized option");
        }
    }

    if (optind < argc)
    {
        die("fatal: unrecognized arguments passed "
            "to git-credential-keyring.\n");
        gtk_exit(1);
    }

    if (! unique)
    {
        /* Not sure what to do here... so pass on it for now. */
        gtk_exit(0);
    }

    split_unique(&parts, unique);

    if (reject)
    {
        remove_credential(username, &parts);
        gtk_exit(0);
    }

    password = lookup_credential(&username, &parts, &cancelled);
    if (! password)
    {
        if (cancelled)
            gtk_exit(0);

        if (! ask_credentials_gui(&username, &password))
        {
            gtk_exit(0);
        }

        store_credential(&parts, username, password);
    }

    printf("username=%s\npassword=%s\n", username, password);

    gtk_exit(0);
    return 0;
}
