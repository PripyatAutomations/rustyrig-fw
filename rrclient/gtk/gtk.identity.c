// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// Client certificate management and first-run local Ed25519 identity setup.
// Part of rustyrig-fw; GPL-2.0-only.
#include "gtk.identity.h"
#include "gtk.core.h"
#include <rrclient/identity.store.h>
#include <rrclient/database.h>
#include <librrprotocol/crypto.h>
#include <librustyaxe/core.h>
#include <glib/gstdio.h>
#include <glib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void gtk_manage_certificates(GtkWidget *unused, gpointer unused_data);
GtkWidget *gtk_certificates_button_new(void) {
   GtkWidget *button = gtk_button_new_with_label("Certificates");
   GtkWidget *icon = gtk_image_new_from_icon_name("dialog-password-symbolic", GTK_ICON_SIZE_BUTTON);
   gtk_button_set_image(GTK_BUTTON(button), icon);
   gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
   gtk_widget_set_tooltip_text(button, "Manage local Ed25519 client keys");
   g_signal_connect(button, "clicked", G_CALLBACK(gtk_manage_certificates), NULL);

   return button;
}

/* First-run client identity. Never replace a key silently: doing so would orphan all server-side public-key registrations for this device. */
static char *gtk_identity_path(void) {
   return g_build_filename(g_get_user_config_dir(), "rustyrig", "identity", "client-ed25519.pem", NULL);
}
static bool gtk_identity_save_new(rr_crypto_key_t *key, const char *path) {
   char *dir = g_path_get_dirname(path);
   bool ok = g_mkdir_with_parents(dir, 0700) == 0;
   struct stat st;

   if (ok) {
      ok = lstat(dir, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0077);
   }

   if (ok) {
      int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);

      if (fd >= 0) {
         FILE *f = fdopen(fd, "wb");

         if (f) {
            ok = rr_crypto_key_write_pem_stream(key, f) && fflush(f) == 0 && fsync(fileno(f)) == 0;

            if (fclose(f)) {
               ok = false;
            }
         } else {
            close(fd);
            ok = false;
         }

         if (!ok) {
            unlink(path);
         }
      } else {
         ok = false;
      }
   }
   g_free(dir);

   return ok;
}

/* The GTK key manager deliberately operates only on the local identity. A future network profile can select an identity by stable fingerprint. */
static void identity_message(GtkWindow *parent, GtkMessageType type, const char *message) {
   GtkWidget *dlg = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, type, GTK_BUTTONS_CLOSE, "%s", message);
   gtk_dialog_run(GTK_DIALOG(dlg));
   gtk_widget_destroy(dlg);
}
static char *identity_ask_password(GtkWindow *parent, const char *title, bool confirm) {
   GtkWidget *dlg = gtk_dialog_new_with_buttons(title, parent, GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, NULL);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
   GtkWidget *one = gtk_entry_new(), *two = confirm ? gtk_entry_new() : NULL;
   gtk_entry_set_visibility(GTK_ENTRY(one), FALSE);
   gtk_entry_set_placeholder_text(GTK_ENTRY(one), "Password");
   gtk_box_pack_start(GTK_BOX(box), one, FALSE, FALSE, 5);

   if (two) {
      gtk_entry_set_visibility(GTK_ENTRY(two), FALSE);
      gtk_entry_set_placeholder_text(GTK_ENTRY(two), "Confirm password");
      gtk_box_pack_start(GTK_BOX(box), two, FALSE, FALSE, 5);
   }
   gtk_widget_show_all(dlg);
   char *value = NULL;

   if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
      const char *first = gtk_entry_get_text(GTK_ENTRY(one));

      if (*first && (!two || !strcmp(first, gtk_entry_get_text(GTK_ENTRY(two))))) {
         value = g_strdup(first);
      }
   }
   gtk_widget_destroy(dlg);

   return value;
}
static rr_crypto_key_t *identity_read_key(const char *path, GtkWindow *parent) {
   FILE *f = fopen(path, "rb");

   if (!f) {
      return NULL;
   }
   /* Import accepts both protected and unprotected PEM; never allow OpenSSL to prompt from the terminal. */
   rr_crypto_key_t *key = rr_crypto_key_read_encrypted_pem(f, "");

   if (!key) {
      rewind(f);
      char *pass = identity_ask_password(parent, "Unlock Private Key", false);

      if (pass) {
         key = rr_crypto_key_read_encrypted_pem(f, pass);
         g_free(pass);
      }
   }
   fclose(f);

   if (key) {
      unsigned char proof[64];
      static const char check[] = "rustyrig.client.identity.private-key-check.v1";

      if (!rr_crypto_ed25519_sign(key, check, sizeof(check) - 1, proof)) {
         rr_crypto_key_free(key);
         key = NULL;
      }
   }

   return key;
}
static void identity_menu_action(GtkMenuItem *item, gpointer dialog) {
   gtk_dialog_response(GTK_DIALOG(dialog), GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "response")));
}
static gboolean identity_list_menu(GtkWidget *list, GdkEventButton *event, gpointer dialog) {
   if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
      return FALSE;
   }
   GtkListBoxRow *row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(list), (gint) event->y);

   if (!row) {
      return FALSE;
   }
   gtk_list_box_select_row(GTK_LIST_BOX(list), row);
   const char *path = g_object_get_data(G_OBJECT(row), "identity-path");
   GtkWidget *menu = gtk_menu_new();
   const char *labels[] = {
      "Info", NULL, "Create New", "Import Private Key",
      "Export Encrypted Key", NULL,
      rrclient_identity_get(path) ? "Lock" : "Unlock",
      "Rename", "Lock All", NULL, "Delete"
   };
   const int responses[] = {
      8, 0, 1, 2, 3, 0, 4, 7, 6, 0, 5
   };

   for (size_t i = 0 ; i < G_N_ELEMENTS(labels) ; i++) {
      if (!labels[i]) {
         gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
         continue;
      }
      GtkWidget *item = gtk_menu_item_new_with_label(labels[i]);
      g_object_set_data(G_OBJECT(item), "response", GINT_TO_POINTER(responses[i]));
      g_signal_connect(item, "activate", G_CALLBACK(identity_menu_action), dialog);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   }

   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
}
static char *identity_prompt_name(GtkWindow *parent, const char *title, const char *existing) {
   GtkWidget *dialog = gtk_dialog_new_with_buttons(title, parent, GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Letters, digits, _ or -");

   if (existing) {
      gtk_entry_set_text(GTK_ENTRY(entry), existing);
   }
   gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry);
   gtk_widget_show_all(dialog);
   char *name = NULL;
   while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      const char *candidate = gtk_entry_get_text(GTK_ENTRY(entry));

      if (*candidate && strlen(candidate) <= 64 && strspn(candidate, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == strlen(candidate)) {
         name = g_strdup(candidate);
         break;
      }
      identity_message(parent, GTK_MESSAGE_WARNING, "Name must contain 1–64 letters, digits, _ or -.");
   }
   gtk_widget_destroy(dialog);

   return name;
}
static void gtk_manage_certificates(GtkWidget *unused, gpointer unused_data) {
   (void)unused;
   (void)unused_data;
   GtkWindow *parent = main_window ? GTK_WINDOW(main_window) : NULL;
   GtkWidget *dlg = gtk_dialog_new_with_buttons("Manage Certificates", parent, GTK_DIALOG_MODAL, "_Close", GTK_RESPONSE_CLOSE, NULL);
   gtk_window_set_default_size(GTK_WINDOW(dlg), 510, -1);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
   gtk_container_set_border_width(GTK_CONTAINER(area), 14);
   GtkWidget *description = gtk_label_new("Local Ed25519 identities");
   gtk_label_set_xalign(GTK_LABEL(description), 0);
   gtk_box_pack_start(GTK_BOX(area), description, FALSE, FALSE, 4);
   GtkWidget *list = gtk_list_box_new();
   gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
   gtk_widget_set_size_request(list, 380, 90);
   gtk_box_pack_start(GTK_BOX(area), list, TRUE, TRUE, 6);
   g_signal_connect(list, "button-press-event", G_CALLBACK(identity_list_menu), dlg);
   GtkWidget *status = gtk_label_new("");
   gtk_label_set_xalign(GTK_LABEL(status), 0);
   gtk_box_pack_start(GTK_BOX(area), status, FALSE, FALSE, 8);
   /* GTK action-area order follows creation order, left to right. */
   gtk_dialog_add_button(GTK_DIALOG(dlg), "Info", 8);
   gtk_dialog_add_button(GTK_DIALOG(dlg), "Create New", 1);
   gtk_dialog_add_button(GTK_DIALOG(dlg), "Import / Export", 9);
   gtk_dialog_add_button(GTK_DIALOG(dlg), "Lock / Unlock", 4);
   gtk_dialog_add_button(GTK_DIALOG(dlg), "Rename", 7);
   gtk_dialog_add_button(GTK_DIALOG(dlg), "Lock All", 6);
   gtk_dialog_add_button(GTK_DIALOG(dlg), "Delete", 5);
   char *path = NULL;
   char *selected_path = NULL;
   char *dir = g_build_filename(g_get_user_config_dir(), "rustyrig", "identity", NULL);

   for ( ;; ) {
      GList *rows = gtk_container_get_children(GTK_CONTAINER(list));

      for (GList *item = rows ; item ; item = item->next) {
         gtk_widget_destroy(GTK_WIDGET(item->data));
      }

      g_list_free(rows);
      GDir *directory = g_dir_open(dir, 0, NULL);
      const char *filename;
      GtkListBoxRow *preferred = NULL;
      unsigned count = 0;

      if (directory) {
         while ((filename = g_dir_read_name(directory))) {
            if (!g_str_has_suffix(filename, ".pem")) {
               continue;
            }
            char *candidate = g_build_filename(dir, filename, NULL);
            struct stat st;

            if (lstat(candidate, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077)) {
               g_free(candidate);
               continue;
            }
            GtkWidget *row = gtk_list_box_row_new();
            char *display = !strcmp(filename, "client-ed25519.pem") ? g_strdup("Default Client Identity") : g_strndup(filename, strlen(filename) - 4);
            GtkWidget *label = gtk_label_new(display);
            gtk_label_set_xalign(GTK_LABEL(label), 0);
            gtk_widget_set_margin_start(label, 12);
            gtk_widget_set_margin_top(label, 8);
            gtk_widget_set_margin_bottom(label, 8);
            gtk_container_add(GTK_CONTAINER(row), label);
            g_object_set_data_full(G_OBJECT(row), "identity-path", candidate, g_free);
            gtk_container_add(GTK_CONTAINER(list), row);

            if (selected_path && !strcmp(selected_path, candidate)) {
               preferred = GTK_LIST_BOX_ROW(row);
            }

            if (!selected_path && count == 0) {
               preferred = GTK_LIST_BOX_ROW(row);
            }
            count++;
            g_free(display);
         }
         g_dir_close(directory);
      }

      if (preferred) {
         gtk_list_box_select_row(GTK_LIST_BOX(list), preferred);
      }
      GtkListBoxRow *highlight = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
      const char *highlight_path = highlight ? g_object_get_data(G_OBJECT(highlight), "identity-path") : NULL;
      gtk_label_set_text(GTK_LABEL(status), !count ?
         "No identities yet. Create or import an Ed25519 key." : rrclient_identity_get(highlight_path) ? "Selected key: Unlocked" : "Selected key: Locked");

      gtk_widget_show_all(dlg);
      int response = gtk_dialog_run(GTK_DIALOG(dlg));
      GtkListBoxRow *selected = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
      g_free(selected_path);
      selected_path = selected ? g_strdup(g_object_get_data(G_OBJECT(selected), "identity-path")) : NULL;
      g_free(path);
      path = selected_path ? g_strdup(selected_path) : NULL;
      bool safe = path != NULL;

      if (response == GTK_RESPONSE_CLOSE || response == GTK_RESPONSE_DELETE_EVENT) {
         break;
      }

      /* Only the selected row is a target for key operations. */
      if ((response == 3 || response == 4 || response == 5 || response == 7 || response == 8) && !gtk_list_box_get_selected_row(GTK_LIST_BOX(list))) {
         continue;
      }

      if (response == 9) {
         GtkWidget *choose = gtk_message_dialog_new(GTK_WINDOW(dlg), GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
            "Import a private key or export the selected identity?");
         gtk_dialog_add_button(GTK_DIALOG(choose), "_Cancel", GTK_RESPONSE_CANCEL);
         gtk_dialog_add_button(GTK_DIALOG(choose), "_Import", 2);
         GtkWidget *export_button = gtk_dialog_add_button(GTK_DIALOG(choose), "_Export", 3);
         gtk_widget_set_sensitive(export_button, safe);
         gint choice = gtk_dialog_run(GTK_DIALOG(choose));
         gtk_widget_destroy(choose);
         if (choice != 2 && choice != 3) continue;
         response = choice;
      }
      if (response == 8 && safe) {
         rr_crypto_key_t *key = rrclient_identity_get(path);
         bool owned = false;
         if (!key) {
            key = identity_read_key(path, GTK_WINDOW(dlg));
            owned = true;
         }
         if (!key) {
            identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_ERROR,
                             "Unable to read the selected identity.");
            continue;
         }
         uint8_t raw[32], hash[32];
         bool ok = rr_crypto_ed25519_public_bytes(key, raw) &&
                   rr_crypto_sha256(raw, sizeof(raw), hash);
         if (ok) {
            char fingerprint[32 * 3];
            for (size_t i = 0; i < sizeof(hash); i++)
               g_snprintf(fingerprint + i * 3,
                          sizeof(fingerprint) - i * 3,
                          i == sizeof(hash)-1 ? "%02X" : "%02X:", hash[i]);
            char *name = g_path_get_basename(path);
            char *details = g_strdup_printf(
               "Identity: %s\nAlgorithm: Ed25519\nStatus: %s\n"
               "Public key SHA-256: %s\nFile: %s\n"
               "This fingerprint identifies the raw Ed25519 public key, "
               "not a TLS certificate or TLS SPKI fingerprint.",
               name, rrclient_identity_get(path) ? "Unlocked" : "Locked",
               fingerprint, path);
            identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_INFO, details);
            g_free(details);
            g_free(name);
         } else {
            identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_ERROR,
                             "Unable to compute public-key fingerprint.");
         }
         if (owned) rr_crypto_key_free(key);
         continue;
      }
      if (response == 6) {
         rrclient_identity_lock_all();
         continue;
      }

      if (response == 4) {
         if (rrclient_identity_get(path)) {
            rrclient_identity_lock(path);
         } else if (safe) {
            /* Try an unencrypted key first. An encrypted key prompts once. */
            if (!rrclient_identity_unlock(path, "")) {
               char *pass = identity_ask_password(GTK_WINDOW(dlg), "Unlock Private Key", false);

               if (pass) {
                  if (!rrclient_identity_unlock(path, pass)) {
                     identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_ERROR, "Unable to unlock this private key.");
                  }
                  g_free(pass);
               }
            }
         }
         continue;
      }

      if (response == 7 && safe) {
         char *old = g_path_get_basename(path);

         if (g_str_has_suffix(old, ".pem")) {
            old[strlen(old) - 4] = 0;
         }
         char *name = identity_prompt_name(GTK_WINDOW(dlg), "Rename Certificate", old);
         g_free(old);

         if (name) {
            char *base = g_strconcat(name, ".pem", NULL);
            char *target = g_build_filename(dir, base, NULL);

            if (strcmp(path, target) && g_file_test(target, G_FILE_TEST_EXISTS)) {
               identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_WARNING, "That key name already exists.");
            } else if (strcmp(path, target) && rename(path, target)) {
               identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_ERROR, "Unable to rename the key file.");
            } else {
               rrclient_identity_lock(path);
               g_free(selected_path);
               selected_path = g_strdup(target);
            }
            g_free(target);
            g_free(base);
            g_free(name);
         }
         continue;
      }

      if (response == 5 && safe) {
         GtkWidget *confirm = gtk_message_dialog_new(GTK_WINDOW(dlg), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
            "Delete this private key? Servers registered to it will no longer recognize this device. Export a backup first.");
         bool yes = gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_YES;
         gtk_widget_destroy(confirm);

         if (yes && unlink(path) == 0) {
            rrclient_identity_lock(path);
            g_clear_pointer(&selected_path, g_free);
         }
         continue;
      }

      if (response == 1 || response == 2) {
         rr_crypto_key_t *key = NULL;

         if (response == 1) {
            key = rr_crypto_ed25519_generate();
         } else {
            GtkWidget *chooser =
               gtk_file_chooser_dialog_new("Import Private Key", GTK_WINDOW(dlg), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Import",
               GTK_RESPONSE_ACCEPT, NULL);

            if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
               char *source = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
               key = identity_read_key(source, GTK_WINDOW(dlg));
               g_free(source);
            }
            gtk_widget_destroy(chooser);
         }
         char *uuid = response == 1 ? identity_prompt_name(GTK_WINDOW(dlg), "Name New Certificate", NULL) : g_uuid_string_random();

         if (!uuid) {
            rr_crypto_key_free(key);
            continue;
         }
         char *basename = g_strdup_printf("%s.pem", uuid);
         char *new_path = g_build_filename(dir, basename, NULL);
         bool stored = key && !g_file_test(new_path, G_FILE_TEST_EXISTS) && gtk_identity_save_new(key, new_path);

         if (stored) {
            g_free(selected_path);
            selected_path = g_strdup(new_path);
         }
         g_free(new_path);
         g_free(basename);
         g_free(uuid);

         if (!stored) {
            identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_ERROR, "Unable to create or import the private key.");
         }
         rr_crypto_key_free(key);
         continue;
      }

      if (response == 3 && safe) {
         rr_crypto_key_t *key = rrclient_identity_get(path);
         bool owned = false;

         if (!key) {
            key = identity_read_key(path, GTK_WINDOW(dlg));
            owned = true;
         }

         if (!key) {
            continue;
         }
         char *password = identity_ask_password(GTK_WINDOW(dlg), "Export Password (required)", true);

         if (password) {
            GtkWidget *chooser = gtk_file_chooser_dialog_new("Export Encrypted Private Key", GTK_WINDOW(dlg), GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel",
               GTK_RESPONSE_CANCEL, "_Export", GTK_RESPONSE_ACCEPT, NULL);
            gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);

            if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
               char *destination = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
               /* Create with 0600, not fopen's process-umask-dependent mode. */
               int fd = open(destination, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
               bool ok = false;

               if (fd >= 0) {
                  FILE *f = fdopen(fd, "wb");

                  if (f) {
                     ok = rr_crypto_key_write_encrypted_pem_stream(key, f, password) && fflush(f) == 0 && fsync(fileno(f)) == 0;

                     if (fclose(f)) {
                        ok = false;
                     }
                  } else {
                     close(fd);
                  }

                  if (!ok) {
                     unlink(destination);
                  }
               }
               identity_message(GTK_WINDOW(dlg), ok ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR, ok ? "Encrypted private key exported." :
                  "Unable to export private key.");
               g_free(destination);
            }
            gtk_widget_destroy(chooser);
            g_free(password);
         } else {
            identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_WARNING, "Export requires a nonempty matching password.");
         }

         if (owned) {
            rr_crypto_key_free(key);
         }
      }
   }

   gtk_widget_destroy(dlg);
   g_free(path);
   g_free(selected_path);
   g_free(dir);
}
void gtk_identity_first_run(void) {
   /* Additional identities are first-class; do not show first-run setup solely because the legacy default key is absent. */
   char *keydir = g_build_filename(g_get_user_config_dir(), "rustyrig", "identity", NULL);
   GDir *keys = g_dir_open(keydir, 0, NULL);
   const char *entry;
   bool any = false;

   if (keys) {
      while ((entry = g_dir_read_name(keys))) {
         if (!g_str_has_suffix(entry, ".pem")) {
            continue;
         }
         char *candidate = g_build_filename(keydir, entry, NULL);
         struct stat st;
         any = lstat(candidate, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0077);
         g_free(candidate);

         if (any) {
            break;
         }
      }
      g_dir_close(keys);
   }
   g_free(keydir);

   if (any) {
      return;
   }
   char *path = gtk_identity_path();

   if (g_file_test(path, G_FILE_TEST_EXISTS)) {
      struct stat st;

      if (lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077)) {
         Log(LOG_WARN, "client.identity", "Client key has unsafe ownership or permissions: %s", path);
      }
      g_free(path);

      return;
   }
   GtkWidget *dialog = gtk_dialog_new_with_buttons("RustyRig Client Identity", NULL, GTK_DIALOG_MODAL, "Create New Key", 2, "Import Existing Key", 1,
      "Later (legacy login)", GTK_RESPONSE_CANCEL, NULL);
   GtkWidget *label = gtk_label_new("RustyRig uses a private key to identify this computer.\n\n"
      "Create a new identity or import an existing key.\n" "Your private key stays on this computer; only the public key is shared with servers.");
   gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
   gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
   gtk_label_set_max_width_chars(GTK_LABEL(label), 58);
   gtk_widget_set_size_request(label, 360, -1);
   gtk_window_set_default_size(GTK_WINDOW(dialog), 460, -1);
   gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), 14);
   gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), label);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), 2);
   gtk_widget_show_all(dialog);

   for ( ;; ) {
      gint choice = gtk_dialog_run(GTK_DIALOG(dialog));

      if (choice == GTK_RESPONSE_CANCEL || choice == GTK_RESPONSE_DELETE_EVENT) {
         break;
      }
      rr_crypto_key_t *key = NULL;

      if (choice == 2) {
         key = rr_crypto_ed25519_generate();
      } else if (choice == 1) {
         GtkWidget *chooser = gtk_file_chooser_dialog_new("Import Ed25519 Private Key", GTK_WINDOW(dialog), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel",
            GTK_RESPONSE_CANCEL, "_Import", GTK_RESPONSE_ACCEPT, NULL);

         if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
            char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
            FILE *f = fopen(filename, "rb");

            if (f) {
               GtkWidget *passdialog = gtk_dialog_new_with_buttons("Import Key Password", GTK_WINDOW(dialog), GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL,
                  "_Unlock", GTK_RESPONSE_ACCEPT, NULL);
               GtkWidget *password = gtk_entry_new();
               gtk_entry_set_visibility(GTK_ENTRY(password), FALSE);
               gtk_entry_set_placeholder_text(GTK_ENTRY(password), "Password (leave empty for unencrypted key)");
               gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(passdialog))), password);
               gtk_widget_show_all(passdialog);

               if (gtk_dialog_run(GTK_DIALOG(passdialog)) == GTK_RESPONSE_ACCEPT) {
                  const char *pass = gtk_entry_get_text(GTK_ENTRY(password));
                  key = *pass ? rr_crypto_key_read_encrypted_pem(f, pass) : rr_crypto_key_read_encrypted_pem(f, "");
               }
               gtk_widget_destroy(passdialog);
               fclose(f);
            }
            g_free(filename);
         }
         gtk_widget_destroy(chooser);
      }
      unsigned char proof[64];
      const char test_message[] = "rustyrig.client.identity.private-key-check.v1";

      if (key && !rr_crypto_ed25519_sign(key, test_message, sizeof(test_message) - 1, proof)) {
         rr_crypto_key_free(key);
         key = NULL;
      }

      if (key && gtk_identity_save_new(key, path)) {
         Log(LOG_INFO, "client.identity", "Client Ed25519 identity created at %s", path);
         rr_crypto_key_free(key);
         break;
      }
      rr_crypto_key_free(key);
      GtkWidget *error = gtk_message_dialog_new(GTK_WINDOW(dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
         "Unable to save a new Ed25519 identity. Check key format and local directory permissions.");
      gtk_dialog_run(GTK_DIALOG(error));
      gtk_widget_destroy(error);
   }

   gtk_widget_destroy(dialog);
   g_free(path);
}

void rrclient_gtk_open_key_manager(void) {
   gtk_manage_certificates(NULL, NULL);
}
