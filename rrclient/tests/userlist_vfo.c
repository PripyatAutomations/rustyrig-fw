#include <assert.h>
#include "rrclient/gtk/gtk.userlist.c"
bool dying, restarting;
time_t now;
static char selected_room[128], command[32];
void rrclient_media_room_selected(const char *room) { snprintf(selected_room, sizeof(selected_room), "%s", room); }
bool parse_chat_input_real(const char *text) { snprintf(command, sizeof(command), "%s", text); return false; }
int main(void) {
   room_userlist_entry_t entry = { .docked = false };
   room_vfo_control_t control = { .vfo = 'B', .room = "#site-rig0", .entry = &entry };
   GdkEventButton click = { .type = GDK_BUTTON_RELEASE, .button = 1 };
   assert(!room_vfo_select_clicked(NULL, &click, &control));
   assert(!command[0]);
   entry.docked = true;
   click.button = 3;
   assert(!room_vfo_select_clicked(NULL, &click, &control));
   assert(!command[0]);
   click.button = 1;
   assert(room_vfo_select_clicked(NULL, &click, &control));
   assert(!strcmp(command, "!vfo B"));
   assert(!strcmp(selected_room, "#site-rig0"));
   puts("PASS: docked VFO rows use the shared selector; detached and right-click rows do not");
}
