//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Native local shell commands; stdout forwarding is explicit and asynchronous.
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librustyaxe/subproc.h>
#include <rrclient/cmd.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
#define EXEC_LINE_MAX 16384
#define EXEC_JOBS_MAX 4

typedef struct {
   GString *pending;
   bool eof, dropping;
} exec_pipe_t;
typedef struct {
   subproc_t process;
   exec_pipe_t out, err;
   char *room, *profile, *window;
   rrconn_t *connection;
   unsigned id, mode;
   bool cancelled, kill_sent;
   uint64_t next_line;
} exec_job_t;
static GList *exec_jobs;
static guint exec_source;
static rr_event_token_t exec_closed_token;
static unsigned exec_serial;
static bool exec_stopping;

typedef struct {
   exec_job_t *job;
   const char *line;
   bool local;
} exec_delivery_t;
static void exec_deliver(void *data) {
   exec_delivery_t *delivery = data;
   exec_job_t *job = delivery->job;

   if (delivery->local) {
      ui_print(job->room, "%s", delivery->line);
   } else if (job->mode == 1) {
      parse_chat_input_text(delivery->line);
   } else {
      parse_chat_input_real(delivery->line);
   }
}
static void exec_line(exec_job_t *job, const char *line, bool local) {
   rrconn_t *connection = job->connection && job->profile && rrclient_connection_find(job->profile) == job->connection ? job->connection : NULL;

   if (!local && job->connection && !connection) {
      // A reconnect is a new session. Never forward queued output into it.
      rrclient_input_dispatch(NULL, NULL, exec_deliver, &(exec_delivery_t) {
         .job = job, .line = "Exec output forwarding stopped: originating session disconnected.", .local = true
      });
      job->cancelled = true;

      return;
   }

   if (local && !connection && job->window) {
      ui_print(job->window, "%s", line);

      return;
   }
   exec_delivery_t delivery = {
      .job = job, .line = line, .local = local
   };
   rrclient_input_dispatch(connection, job->room, exec_deliver, &delivery);
}
static void exec_output(exec_job_t *job, exec_pipe_t *pipe, size_t length, bool error) {
   if (length && pipe->pending->str[length - 1] == '\r') {
      length--;
   }

   if (memchr(pipe->pending->str, 0, length)) {
      exec_line(job, "Exec skipped a binary output line.", true);

      return;
   }
   char *text = g_utf8_make_valid(pipe->pending->str, length);

   if (error) {
      char *message = g_strdup_printf("[exec %u stderr] %s", job->id, text);
      exec_line(job, message, true);
      g_free(message);
   } else if (job->mode && *text) {
      exec_line(job, text, false);
   } else if (!job->mode) {
      exec_line(job, text, true);
   }
   g_free(text);
}
static void exec_drain(exec_job_t *job, exec_pipe_t *pipe, int fd, bool error) {
   unsigned budget = job->mode && !error ? 1 : 16;
   unsigned bytes = 0;
   while (budget && !job->cancelled) {
      char *newline = memchr(pipe->pending->str, '\n', pipe->pending->len);

      if (newline) {
         size_t length = newline - pipe->pending->str;

         if (!pipe->dropping && length <= EXEC_LINE_MAX) {
            exec_output(job, pipe, length, error);
         } else if (!pipe->dropping) {
            exec_line(job, "Exec skipped an output line exceeding 16 KiB.", true);
         }
         g_string_erase(pipe->pending, 0, length + 1);
         pipe->dropping = false;
         budget--;
         continue;
      }

      if (pipe->pending->len > EXEC_LINE_MAX) {
         if (!pipe->dropping) {
            exec_line(job, "Exec skipped an output line exceeding 16 KiB.", true);
         }
         pipe->dropping = true;
         g_string_truncate(pipe->pending, 0);
      }

      if (pipe->eof) {
         if (pipe->pending->len && !pipe->dropping) {
            exec_output(job, pipe, pipe->pending->len, error);
         }
         g_string_truncate(pipe->pending, 0);
         break;
      }

      if (bytes >= 4096) {
         break;
      }
      char buffer[1024];
      ssize_t got = read(fd, buffer, sizeof(buffer));

      if (got > 0) {
         g_string_append_len(pipe->pending, buffer, got);
         bytes += got;
      } else if (!got) {
         pipe->eof = true;
      } else if (errno == EINTR) {
         continue;
      } else if (errno == EAGAIN) {
         break;
      }
#if EWOULDBLOCK != EAGAIN
      else if (errno == EWOULDBLOCK) {
         break;
      }
#endif
      else {
         pipe->eof = true;
         exec_line(job, "Exec output pipe failed.", true);
      }
   }
}
static void exec_free(exec_job_t *job) {
   if (job->process.pid > 0) {
      // Retain the leader PID until pipes close, so this group cannot be reused.
      kill(-job->process.pid, SIGKILL);
      subproc_stop(&job->process, SIGKILL);
   } else {
      subproc_close(&job->process);
   }
   g_string_free(job->out.pending, TRUE);
   g_string_free(job->err.pending, TRUE);
   g_free(job->room);
   g_free(job->profile);
   g_free(job->window);
   g_free(job);
}
static gboolean exec_poll(gpointer unused) {
   (void)unused;
   uint64_t current = mono_us();

   for (GList *p = exec_jobs, *next ; p ; p = next) {
      next = p->next;
      exec_job_t *job = p->data;

      if (!job->cancelled) {
         if (!job->mode || current >= job->next_line) {
            exec_drain(job, &job->out, fileno(job->process.output), false);
            job->next_line = current + 250000;  // Bound chat/parser output to four lines/second.
         }
         exec_drain(job, &job->err, job->process.error_fd, true);
      }
      int status = 0;
      bool done = false;

      if (job->cancelled) {
         if (!job->kill_sent && job->process.pid > 0) {
            kill(-job->process.pid, SIGKILL);
            kill(job->process.pid, SIGKILL);
            job->kill_sent = true;
         }
         done = job->process.pid <= 0 || subproc_reap(&job->process, &status);
      } else if (job->out.eof && !job->out.pending->len && job->err.eof && !job->err.pending->len) {
         done = subproc_reap(&job->process, &status);

         if (!done && job->process.pid <= 0) {
            job->cancelled = done = true;
         }
      }

      if (done) {
         char *message = job->cancelled ? g_strdup_printf("Exec %u stopped.", job->id) :
            g_strdup_printf("Exec %u exited (%s %d).", job->id, WIFEXITED(status) ? "status" : "signal", WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(
            status));
         exec_line(job, message, true);
         g_free(message);
         exec_jobs = g_list_delete_link(exec_jobs, p);
         exec_free(job);
      }
   }

   if (!exec_jobs) {
      exec_source = 0;

      return G_SOURCE_REMOVE;
   }

   return G_SOURCE_CONTINUE;
}
static void exec_closed(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)data;
   (void)unused;

   for (GList *p = exec_jobs ; p ; p = p->next) {
      exec_job_t *job = p->data;

      if (job->mode && job->connection && job->connection == (connection ? connection : ws_conn)) {
         job->cancelled = true;
      }
   }
}
/* Display a local text file in the originating client window. Never invoke a
 * shell or route these lines through the chat command parser. */
static bool exec_cat_file(const char *path) {
   const char *window = ui_active_window_name();
   FILE *file = fopen(path, "rb");

   if (!file) {
      ui_print(window, "Exec -c: cannot open %s: %s", path, g_strerror(errno));
      return true;
   }
   char *line = NULL;
   size_t capacity = 0;
   ssize_t length;
   unsigned long lines = 0;

   while ((length = getline(&line, &capacity, file)) >= 0) {
      if (length && line[length - 1] == '\n') {
         length--;
      }
      if (length && line[length - 1] == '\r') {
         length--;
      }
      if ((size_t)length > EXEC_LINE_MAX) {
         ui_print(window, "Exec -c: skipped a line exceeding 16 KiB.");
         continue;
      }
      if (memchr(line, 0, (size_t)length)) {
         ui_print(window, "Exec -c: skipped a binary line.");
         continue;
      }
      char *text = g_utf8_make_valid(line, length);
      ui_print(window, "%s", text);
      g_free(text);
      lines++;
   }
   bool failed = ferror(file) != 0;

   if (failed) {
      ui_print(window, "Exec -c: error reading %s: %s", path, g_strerror(errno));
   }
   free(line);
   fclose(file);
   return failed;
}
bool cmd_exec(int argc, char **args) {
   if (argc < 2 || !*args[1]) {
      ui_print(ui_active_window_name(), "Usage: /exec [-o|-e] <shell command>; /exec -c <local file>; /exec -stop [id|all]");

      for (GList *p = exec_jobs ; p ; p = p->next) {
         ui_print(ui_active_window_name(), "Exec %u running", ((exec_job_t *) p->data)->id);
      }

      return false;
   }
   const char *command = args[1];
   unsigned mode = 0;

   if (!strncmp(command, "-c", 2) && (!command[2] || isspace((unsigned char)command[2]))) {
      const char *path = command + 2;
      while (isspace((unsigned char)*path)) {
         path++;
      }
      if (!*path) {
         ui_print(ui_active_window_name(), "Usage: /exec -c <local file>");
         return true;
      }
      return exec_cat_file(path);
   }

   if (!strncmp(command, "-stop", 5) && (!command[5] || isspace((unsigned char)command[5]))) {
      const char *target = command + 5;
      while (isspace((unsigned char)*target)) {
         target++;
      }
      char *end;
      unsigned long id = strtoul(target, &end, 10);
      bool all = !*target || !strcmp(target, "all");

      if (!all && (!*target || *end)) {
         return true;
      }

      for (GList *p = exec_jobs ; p ; p = p->next) {
         exec_job_t *job = p->data;

         if (all || job->id == id) {
            job->cancelled = true;
         }
      }

      return false;
   }

   if (command[0] == '-' && (command[1] == 'o' || command[1] == 'e') && (!command[2] || isspace((unsigned char)command[2]))) {
      mode = command[1] == 'o' ? 1 : 2;
      command += 2;
      while (isspace((unsigned char)*command)) {
         command++;
      }
   } else if (!strncmp(command, "--", 2) && isspace((unsigned char)command[2])) {
      command += 3;
   }

   if (!*command || (mode && command[0] == '-' && (command[1] == 'e' || command[1] == 'o') && (!command[2] || isspace((unsigned char)command[2])))) {
      ui_print(ui_active_window_name(), "Use /exec [-o|-e] <command>; output modes are mutually exclusive.");

      return true;
   }

   if (exec_stopping || g_list_length(exec_jobs) >= EXEC_JOBS_MAX) {
      ui_print(ui_active_window_name(), "At most four exec jobs may run; use /exec -stop [id|all].");

      return true;
   }
   const char *room = rrclient_active_room();

   if (mode == 1 && (!room || !*room || !strcasecmp(room, "status"))) {
      ui_print(ui_active_window_name(), "Select a conversation tab for /exec -o.");

      return true;
   }
   rrconn_t *connection = rrclient_active_connection();

   if (mode == 1 && (!connection || !connection->authenticated)) {
      ui_print(ui_active_window_name(), "Connect before using /exec -o.");

      return true;
   }
   exec_job_t *job = g_new0(exec_job_t, 1);
   job->process = (subproc_t) SUBPROC_INIT;
   job->connection = connection;
   job->room = g_strdup(room);
   job->window = g_strdup(rrclient_window_name(ui_active_window_name()));
   job->profile = g_strdup(job->connection ? rrclient_connection_name(job->connection) : NULL);
   job->mode = mode;
   job->id = ++exec_serial;
   job->out.pending = g_string_new("");
   job->err.pending = g_string_new("");
   const char *argv[] = {
      "sh", "-c", command, NULL
   };

   if (!subproc_spawn_group(&job->process, "/bin/sh", argv, false)) {
      exec_line(job, "Unable to start local command.", true);
      exec_free(job);

      return true;
   }
   fclose(job->process.input);
   job->process.input = NULL;
   int output = fileno(job->process.output);

   if (fcntl(output, F_SETFL, fcntl(output, F_GETFL) | O_NONBLOCK) < 0 ||
      fcntl(job->process.error_fd, F_SETFL, fcntl(job->process.error_fd, F_GETFL) | O_NONBLOCK) < 0) {
      exec_free(job);
      ui_print(ui_active_window_name(), "Unable to monitor local command output.");

      return true;
   }

   if (!exec_closed_token) {
      exec_closed_token = event_on_token("disconnected", exec_closed, NULL);
   }
   exec_jobs = g_list_prepend(exec_jobs, job);

   if (!exec_source) {
      exec_source = g_timeout_add(50, exec_poll, NULL);
   }
   ui_print(ui_active_window_name(), "Exec %u started.", job->id);

   return false;
}
void rrclient_exec_shutdown(void) {
   exec_stopping = true;

   if (exec_closed_token) {
      event_off_token(exec_closed_token);
   }
   exec_closed_token = NULL;

   if (exec_source) {
      g_source_remove(exec_source);
   }
   exec_source = 0;
   while (exec_jobs) {
      exec_job_t *job = exec_jobs->data;
      exec_jobs = g_list_delete_link(exec_jobs, exec_jobs);
      exec_free(job);
   }
}
