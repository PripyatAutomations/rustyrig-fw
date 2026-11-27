/* SPDX-License-Identifier: GPL-2.0-only */
#include <reset.files.h>
#include <assert.h>
#include <glib/gstdio.h>

int main(void) {
   GError *error=NULL;
   char *root=g_dir_make_tmp("rr-reset-test-XXXXXX",&error);
   assert(root && !error);
   char *tree=g_build_filename(root,"rustyrig",NULL);
   char *outside=g_build_filename(root,"outside",NULL);
   assert(g_mkdir(tree,0700)==0 && g_mkdir(outside,0700)==0);
   char *generated=g_build_filename(tree,"client.db",NULL);
   char *packaged=g_build_filename(tree,"schema.sql",NULL);
   char *lock=g_build_filename(tree,"master.db.rrlock",NULL);
   char *target=g_build_filename(outside,"untouched",NULL);
   char *link=g_build_filename(tree,"external",NULL);
   assert(g_file_set_contents(generated,"db",-1,NULL));
   assert(g_file_set_contents(packaged,"schema",-1,NULL));
   assert(g_file_set_contents(lock,"lock",-1,NULL));
   assert(g_file_set_contents(target,"outside",-1,NULL));
   assert(symlink(outside,link)==0);
   rr_reset_t reset={.preview=true,.packaged=g_hash_table_new(g_str_hash,g_str_equal)};
   g_hash_table_add(reset.packaged,packaged);
   assert(rr_reset_target(&reset,tree,true));
   assert(g_file_test(generated,G_FILE_TEST_EXISTS));
   assert(g_file_test(link,G_FILE_TEST_IS_SYMLINK));
   assert(!rr_reset_target(&reset,link,true));
   reset.preview=false;
   assert(rr_reset_target(&reset,tree,true));
   assert(reset.removed==2);
   assert(!g_file_test(generated,G_FILE_TEST_EXISTS));
   assert(!g_file_test(link,G_FILE_TEST_IS_SYMLINK));
   assert(g_file_test(packaged,G_FILE_TEST_EXISTS));
   assert(g_file_test(lock,G_FILE_TEST_EXISTS));
   assert(g_file_test(target,G_FILE_TEST_EXISTS));
   assert(rr_reset_target(&reset,tree,true));
   assert(reset.removed==2);
   assert(g_unlink(packaged)==0 && g_unlink(lock)==0 && g_unlink(target)==0);
   assert(g_rmdir(tree)==0 && g_rmdir(outside)==0 && g_rmdir(root)==0);
   g_hash_table_unref(reset.packaged);
   g_free(generated);g_free(packaged);g_free(lock);g_free(target);g_free(link);
   g_free(tree);g_free(outside);g_free(root);
   puts("PASS: reset preview, package preservation, lock preservation, symlink isolation, idempotence");
   return 0;
}
