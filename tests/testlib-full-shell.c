/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "testlib.h"
#include "testlib-full-shell.h"

#include "log.h"
#include "shell.h"

#include <handy.h>
#include <call-ui.h>

GPid comp_pid;


static gboolean
stop_shell (gpointer unused)
{
  g_debug ("Stopping shell");
  gtk_main_quit ();

  return G_SOURCE_REMOVE;
}


static void kill_compositor (int signum)
{
  kill(comp_pid, SIGTERM);
}


static gpointer
phosh_test_full_shell_thread (gpointer data)
{
  PhoshShell *shell;
  GLogLevelFlags flags;
  PhoshTestFullShellFixture *fixture = (PhoshTestFullShellFixture *)data;

  /* compositor setup in thread since this invokes gdk already */
  fixture->state = phosh_test_compositor_new ();
  /* We assume only one compositor running at a given time */
  comp_pid = fixture->state->pid;

  signal(SIGTRAP, kill_compositor);

  gtk_init (NULL, NULL);
  hdy_init ();
  cui_init (TRUE);

  phosh_log_set_log_domains (fixture->log_domains);

  /* Drop warnings from the fatal log mask since there's plenty
   * when running without recommended DBus services */
  flags = g_log_set_always_fatal (0);
  g_log_set_always_fatal (flags & ~G_LOG_LEVEL_WARNING);

  shell = phosh_shell_get_default ();
  g_assert_true (PHOSH_IS_SHELL (shell));

  g_assert_false (phosh_shell_is_startup_finished (shell));

  /* Process events to startup shell */
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  g_assert_true (phosh_shell_is_startup_finished (shell));

  g_async_queue_push (fixture->queue, (gpointer)TRUE);

  gtk_main ();

  g_assert_finalize_object (shell);
  cui_uninit ();
  phosh_test_compositor_free (fixture->state);

  /* Process events to tear down compositor */
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  return NULL;
}

PhoshTestFullShellFixtureCfg *
phosh_test_full_shell_fixture_cfg_new (const char *display, const char *log_domains)
{
  PhoshTestFullShellFixtureCfg *self = g_new0 (PhoshTestFullShellFixtureCfg, 1);

  if (display)
    self->display = g_strdup (display);

  if (log_domains)
    self->log_domains = g_strdup (log_domains);

  return self;
}

void
phosh_test_full_shell_fixture_cfg_dispose (PhoshTestFullShellFixtureCfg *self)
{
  g_clear_pointer (&self->display, g_free);
  g_clear_pointer (&self->log_domains, g_free);

  g_free (self);
}

static void
phosh_test_remove_tree (GFile *file)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;

  enumerator = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          NULL, NULL);

  while (enumerator != NULL) {
    GFile *child;
    gboolean ret;

    ret = g_file_enumerator_iterate (enumerator, NULL, &child, NULL, &err);
    g_assert_no_error (err);
    g_assert_true (ret);

    if (child == NULL)
      break;

    phosh_test_remove_tree (child);
  }

  g_assert_true (g_file_delete (file, NULL, &err));
  g_assert_no_error (err);
}

/**
 * phosh_test_full_shell_setup:
 * @fixture: Test fixture
 * @data: Data for test setup
 *
 * Sets up a test environment with compositor, own DBus session bus and running shell object. This
 * function is meant to be used with g_test_add().
 */
void
phosh_test_full_shell_setup (PhoshTestFullShellFixture *fixture, gconstpointer data)
{
  const PhoshTestFullShellFixtureCfg *cfg = data;
  g_autoptr (GError) err = NULL;

  fixture->bus = g_test_dbus_new (G_TEST_DBUS_NONE);

  g_test_dbus_up (fixture->bus);

  g_setenv ("NO_AT_BRIDGE", "1", TRUE);

  fixture->tmpdir = g_dir_make_tmp ("phosh-test-comp.XXXXXX", &err);
  g_assert_no_error (err);

  g_setenv ("XDG_RUNTIME_DIR", fixture->tmpdir, TRUE);
  /* Display for wlroots X11 backend */
  if (cfg->display)
    g_setenv ("DISPLAY", cfg->display, TRUE);

  if (cfg->log_domains)
    fixture->log_domains = g_strdup (cfg->log_domains);

  /* Run shell in a thread so we can sync call to the DBus interfaces */
  fixture->queue = g_async_queue_new ();
  fixture->comp_and_shell = g_thread_new ("comp-and-shell-thread", phosh_test_full_shell_thread, fixture);
}

/**
 * phosh_test_full_shell_teardown:
 * @fixture: Test fixture
 * @data: Data for test setup
 *
 * Tears down the test environment that was setup with
 * phosh_test_full_shell_setup(). This function is meant to be used
 * with g_test_add().
 */
void
phosh_test_full_shell_teardown (PhoshTestFullShellFixture *fixture, gconstpointer unused)
{
  g_autoptr (GFile) file = g_file_new_for_path (fixture->tmpdir);

  gdk_threads_add_idle (stop_shell, NULL);
  g_thread_join (fixture->comp_and_shell);
  g_async_queue_unref (fixture->queue);

  g_test_dbus_down (fixture->bus);

  signal (SIGTRAP, SIG_DFL);

  phosh_test_remove_tree (file);
  g_free (fixture->tmpdir);

  g_free (fixture->log_domains);
}
