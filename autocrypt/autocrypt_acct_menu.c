/**
 * @file
 * XXX
 *
 * @authors
 * Copyright (C) 2019 Kevin J. McCarthy <kevin@8t8.us>
 *
 * @copyright
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include <stdbool.h>
#include <stdio.h>
#include "autocrypt_private.h"
#include "mutt/mutt.h"
#include "address/lib.h"
#include "config/lib.h"
#include "mutt.h"
#include "autocrypt.h"
#include "curs_lib.h"
#include "format_flags.h"
#include "globals.h"
#include "keymap.h"
#include "mutt_menu.h"
#include "mutt_window.h"
#include "muttlib.h"
#include "opcodes.h"

struct Entry
{
  int tagged; /* TODO */
  int num;
  struct AutocryptAccount *account;
  struct Address *addr;
};

static const struct Mapping AutocryptAcctHelp[] = {
  { N_("Exit"), OP_EXIT },
  /* L10N: Autocrypt Account Menu Help line:
     create new account
  */
  { N_("Create"), OP_AUTOCRYPT_CREATE_ACCT },
  /* L10N: Autocrypt Account Menu Help line:
     delete account
  */
  { N_("Delete"), OP_AUTOCRYPT_DELETE_ACCT },
  /* L10N: Autocrypt Account Menu Help line:
     toggle an account active/inactive
  */
  { N_("Tgl Active"), OP_AUTOCRYPT_TOGGLE_ACTIVE },
  /* L10N: Autocrypt Account Menu Help line:
     toggle "prefer-encrypt" on an account
  */
  { N_("Prf Enc"), OP_AUTOCRYPT_TOGGLE_PREFER },
  { N_("Help"), OP_HELP },
  { NULL, 0 }
};

static const char *account_format_str(char *dest, size_t destlen, size_t col, int cols,
                                      char op, const char *src, const char *fmt,
                                      const char *ifstring, const char *elsestring,
                                      unsigned long data, MuttFormatFlags flags)
{
  struct Entry *entry = (struct Entry *) data;
  char tmp[128];

  switch (op)
  {
    case 'a':
      mutt_format_s(dest, destlen, fmt, entry->addr->mailbox);
      break;
    case 'k':
      mutt_format_s(dest, destlen, fmt, entry->account->keyid);
      break;
    case 'n':
      snprintf(tmp, sizeof(tmp), "%%%sd", fmt);
      snprintf(dest, destlen, tmp, entry->num);
      break;
    case 'p':
      if (entry->account->prefer_encrypt)
        /* L10N:
           Autocrypt Account menu.
           flag that an account has prefer-encrypt set
        */
        mutt_format_s(dest, destlen, fmt, _("prefer encrypt"));
      else
        /* L10N:
           Autocrypt Account menu.
           flag that an account has prefer-encrypt unset;
           thus encryption will need to be manually enabled.
        */
        mutt_format_s(dest, destlen, fmt, _("manual encrypt"));
      break;
    case 's':
      if (entry->account->enabled)
        /* L10N:
           Autocrypt Account menu.
           flag that an account is enabled/active
        */
        mutt_format_s(dest, destlen, fmt, _("active"));
      else
        /* L10N:
           Autocrypt Account menu.
           flag that an account is disabled/inactive
        */
        mutt_format_s(dest, destlen, fmt, _("inactive"));
      break;
  }

  return (src);
}

static void account_entry(char *s, size_t slen, struct Menu *m, int num)
{
  struct Entry *entry = &((struct Entry *) m->data)[num];

  mutt_expando_format(s, slen, 0, MuttIndexWindow->cols,
                      NONULL(C_AutocryptAcctFormat), account_format_str,
                      (unsigned long) entry, MUTT_FORMAT_ARROWCURSOR);
}

static struct Menu *create_menu(void)
{
  struct Menu *menu = NULL;
  struct AutocryptAccount **accounts = NULL;
  struct Entry *entries = NULL;
  int num_accounts = 0, i;
  char *helpstr;

  if (mutt_autocrypt_db_account_get_all(&accounts, &num_accounts) < 0)
    return NULL;

  menu = mutt_menu_new(MENU_AUTOCRYPT_ACCT);
  menu->menu_make_entry = account_entry;
  /* menu->tag = account_tag; */
  /* L10N:
     Autocrypt Account Management Menu title
  */
  menu->title = _("Autocrypt Accounts");
  helpstr = mutt_mem_malloc(256);
  menu->help = mutt_compile_help(helpstr, 256, MENU_AUTOCRYPT_ACCT, AutocryptAcctHelp);

  menu->data = entries = mutt_mem_calloc(num_accounts, sizeof(struct Entry));
  menu->max = num_accounts;

  for (i = 0; i < num_accounts; i++)
  {
    entries[i].num = i + 1;
    /* note: we are transfering the account pointer to the entries
     * array, and freeing the accounts array below.  the account
     * will be freed in free_menu().
     */
    entries[i].account = accounts[i];

    entries[i].addr = mutt_addr_new();
    entries[i].addr->mailbox = mutt_str_strdup(accounts[i]->email_addr);
    mutt_addr_to_local(entries[i].addr);
  }
  FREE(&accounts);

  mutt_menu_push_current(menu);

  return menu;
}

static void free_menu(struct Menu **menu)
{
  int i;
  struct Entry *entries;

  entries = (struct Entry *) (*menu)->data;
  for (i = 0; i < (*menu)->max; i++)
  {
    mutt_autocrypt_db_account_free(&entries[i].account);
    mutt_addr_free(&entries[i].addr);
  }
  FREE(&(*menu)->data);

  mutt_menu_pop_current(*menu);
  FREE(&(*menu)->help);
  mutt_menu_destroy(menu);
}

static void toggle_active(struct Entry *entry)
{
  entry->account->enabled = !entry->account->enabled;
  if (mutt_autocrypt_db_account_update(entry->account) != 0)
  {
    entry->account->enabled = !entry->account->enabled;
    /* L10N:
       This error message is displayed if a database update of an
       account record fails for some odd reason.
    */
    mutt_error(_("Error updating account record"));
  }
}

static void toggle_prefer_encrypt(struct Entry *entry)
{
  entry->account->prefer_encrypt = !entry->account->prefer_encrypt;
  if (mutt_autocrypt_db_account_update(entry->account))
  {
    entry->account->prefer_encrypt = !entry->account->prefer_encrypt;
    mutt_error(_("Error updating account record"));
  }
}

void mutt_autocrypt_account_menu(void)
{
  struct Menu *menu;
  int done = 0, op;
  struct Entry *entry;
  char msg[128];

  if (!C_Autocrypt)
    return;

  if (mutt_autocrypt_init(0))
    return;

  menu = create_menu();
  if (!menu)
    return;

  while (!done)
  {
    switch ((op = mutt_menu_loop(menu)))
    {
      case OP_EXIT:
        done = 1;
        break;

      case OP_AUTOCRYPT_CREATE_ACCT:
        if (!mutt_autocrypt_account_init(0))
        {
          free_menu(&menu);
          menu = create_menu();
        }
        break;

      case OP_AUTOCRYPT_DELETE_ACCT:
        if (menu->data)
        {
          entry = (struct Entry *) (menu->data) + menu->current;
          snprintf(msg, sizeof(msg),
                   /* L10N:
                       Confirmation message when deleting an autocrypt account
                    */
                   _("Really delete account \"%s\"?"), entry->addr->mailbox);
          if (mutt_yesorno(msg, MUTT_NO) != MUTT_YES)
            break;

          if (!mutt_autocrypt_db_account_delete(entry->account))
          {
            free_menu(&menu);
            menu = create_menu();
          }
        }
        break;

      case OP_AUTOCRYPT_TOGGLE_ACTIVE:
        if (menu->data)
        {
          entry = (struct Entry *) (menu->data) + menu->current;
          toggle_active(entry);
          menu->redraw |= REDRAW_FULL;
        }
        break;

      case OP_AUTOCRYPT_TOGGLE_PREFER:
        if (menu->data)
        {
          entry = (struct Entry *) (menu->data) + menu->current;
          toggle_prefer_encrypt(entry);
          menu->redraw |= REDRAW_FULL;
        }
        break;
    }
  }

  free_menu(&menu);
}
