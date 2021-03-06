/* Copyright (C) 2004-2011 Daniel Verite

   This file is part of Manitou-Mail (see http://www.manitou-mail.org)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#include "main.h"
#include "app_config.h"
#include "errors.h"

#include <fstream>
#include <iostream>
#include <list>

#include "msg_list_window.h"
#include "message_port.h"

#include <time.h>

#include "newmailwidget.h"
#include "body_edit.h"
#include "searchbox.h"
#include "selectmail.h"
#include "notewidget.h"
#include "tagsdialog.h"
#include "addressbook.h"
#include "mail_displayer.h"
#include "tags.h"
#include "filter_log.h"
#include "notepad.h"

#ifdef HAVE_TRAYICON
#include "trayicon.h"
#endif

#include <QApplication>
#include <QStatusBar>
#include <QProgressBar>

#include <QPushButton>
#include <QToolBar>

#include <QFileDialog>
#include <QFontDialog>
#include <QComboBox>
#include <QMessageBox>
#include <QLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QCursor>
#include <QMenuBar>
#include <QAction>
#include <QTimer>
#include <QCloseEvent>
#include <QShortcut>

#include "msg_properties.h"
#include "users.h"
#include "mail_listview.h"
#include "attachment_listview.h"
#include "message_view.h"
#include "preferences.h"
#include "about.h"
#include "helper.h"
#include "edit_rules.h"
#include "mime_msg_viewer.h"

#include "icons.h"
#include "words.h"

#define STACKED_LV

void
msg_list_window::search_db()
{
  QString txt = m_ql_search->text().toLower();
  if (txt.length()<3) {
    QMessageBox::information(NULL, tr("Search"), tr("Please enter a string containing at least 3 characters"));
    return;
  }
  msgs_filter f;
  //  f.m_max_results=200;
  f.set_date_order(-1);	// latest results first
  f.parse_search_string(txt, f.m_words, f.m_substrs);
  //  DBG_PRINTF(3, "words=(%s)\n", f.m_words.join("/").latin1());
  //  DBG_PRINTF(3, "substrs=(%s)\n", f.m_substrs.join("/").latin1());
  //  f.m_words = QStringList::split(" ", txt);
  sel_filter(f);
  m_query_lv->clear_selection();
  QStringList::Iterator it = f.m_words.begin();
  m_highlighted_text.clear();
  for (; it!=f.m_words.end(); ++it) {
    searched_text s;
    s.m_text=*it;
    s.m_is_cs=false;		// word search is case insensitive
    s.m_is_word=true;
    m_highlighted_text.push_back(s);
  }
}

void
msg_list_window::search_text_changed(const QString& newtext)
{
  // stop highlighting text when the input widget gets cleared
  if (newtext.isEmpty() && !m_highlighted_text.empty()) {
    m_highlighted_text.clear();
    if (!m_fetch_on_demand) {
      m_msgview->clear();
      display_body();
    }
  }
}

void
msg_list_window::make_search_toolbar()
{
  QToolBar* toolbar=addToolBar(tr("Search"));
  toolbar->addWidget(new QLabel(tr("Search:")));
  m_ql_search = new search_edit(this);
  toolbar->addWidget(m_ql_search);
  m_ql_search->setMinimumWidth(200);
  connect(m_ql_search, SIGNAL(returnPressed()), this, SLOT(search_db()));
  connect(m_ql_search, SIGNAL(textChanged(const QString&)), this, SLOT(search_text_changed(const QString&)));
  toolbar->addAction(m_action_search);
}

void
msg_list_window::create_actions()
{
  m_action_move_backward = new QAction(FT_MAKE_ICON(FT_ICON16_ARROW_LEFT),
				       tr("Go back one page"), this);
  m_action_move_backward->setShortcut(Qt::ALT+Qt::Key_Left);
  connect(m_action_move_backward, SIGNAL(triggered()),
	  this, SLOT(move_backward()));

  m_action_move_forward = new QAction(FT_MAKE_ICON(FT_ICON16_ARROW_RIGHT),
				      tr("Go forward one page"), this);
  m_action_move_forward->setShortcut(Qt::ALT+Qt::Key_Right);
  connect(m_action_move_forward, SIGNAL(triggered()), this, SLOT(move_forward()));

  m_action_reply_sender = new QAction(FT_MAKE_ICON(FT_ICON16_REPLY),
				      tr("Reply to sender"), this);
  m_action_reply_sender->setShortcut(Qt::Key_R);
  connect(m_action_reply_sender, SIGNAL(triggered()),
	  this, SLOT(mail_reply_sender()));

  m_action_reply_all = new QAction(FT_MAKE_ICON(FT_ICON16_REPLY_ALL),
				   tr("Reply all"), this);
  m_action_reply_all->setShortcut(Qt::CTRL+Qt::Key_R);
  connect(m_action_reply_all, SIGNAL(triggered()),
	  this, SLOT(mail_reply_all()));

  m_action_reply_list = new QAction(FT_MAKE_ICON(FT_ICON16_REPLY),
				    tr("Reply to list"), this);
  m_action_reply_list->setShortcut(Qt::CTRL+Qt::Key_L);
  addAction(m_action_reply_list);
  connect(m_action_reply_list, SIGNAL(triggered()),
	  this, SLOT(mail_reply_list()));

  m_action_msg_archive = new QAction(FT_MAKE_ICON(FT_ICON16_STATUS_PROCESSED),
				     tr("Archive"), this);
  m_action_msg_archive->setShortcut(Qt::Key_A);
  connect(m_action_msg_archive, SIGNAL(triggered()),
	  this, SLOT(msg_archive()));

  m_action_msg_trash = new QAction(FT_MAKE_ICON(FT_ICON16_ACTION_TRASH),
				   tr("Trash"), this);
  m_action_msg_trash->setShortcut(Qt::Key_Delete);
  connect(m_action_msg_trash, SIGNAL(triggered()),
	  this, SLOT(msg_trash()));

  QAction* action_goto_next = new QAction(tr("Move to next message"), this);
  action_goto_next->setShortcut(Qt::Key_N);
  addAction(action_goto_next);
  connect(action_goto_next, SIGNAL(triggered()), this, SLOT(goto_next_message()));

  QAction* action_goto_prev = new QAction(tr("Move to previous message"), this);
  action_goto_prev->setShortcut(Qt::Key_P);
  addAction(action_goto_prev);
  connect(action_goto_prev, SIGNAL(triggered()), this, SLOT(goto_previous_message()));
  
  m_action_msg_untrash = new QAction(FT_MAKE_ICON(FT_ICON16_UNTRASH),
				     tr("UnTrash"), this);
  connect(m_action_msg_untrash, SIGNAL(triggered()),
	  this, SLOT(msg_untrash()));

  m_action_msg_delete = new QAction(FT_MAKE_ICON(FT_ICON16_DELETE_MSG),
				    tr("Delete"), this);
  m_action_msg_delete->setShortcut(Qt::SHIFT+Qt::Key_Delete);
  connect(m_action_msg_delete, SIGNAL(triggered()),
	  this, SLOT(msg_delete()));

  m_action_cycle_headers = new QAction(tr("Cycle headers"), this);
  m_action_cycle_headers->setShortcut(Qt::Key_H);
  connect(m_action_cycle_headers, SIGNAL(triggered()),
	  this, SLOT(cycle_headers()));
  // this one needs to be added explicitly because it's not part of a menubar
  this->addAction(m_action_cycle_headers);

  m_action_msg_forward = new QAction(FT_MAKE_ICON(FT_ICON16_FORWARD),
				     tr("Forward"), this);
  m_action_msg_forward->setShortcut(Qt::Key_F);
  connect(m_action_msg_forward, SIGNAL(triggered()),
	  this, SLOT(forward()));

  m_action_find_text = new QAction(FT_MAKE_ICON(FT_ICON16_FIND),
				   tr("Find text"), this);
  m_action_find_text->setShortcut(Qt::CTRL+Qt::Key_F);
  connect(m_action_find_text, SIGNAL(triggered()),
	  this, SLOT(find_text()));

  m_action_msg_print = new QAction(FT_MAKE_ICON(FT_ICON16_PRINT),
				   tr("Print"), this);
  connect(m_action_msg_print, SIGNAL(triggered()),
	  this, SLOT(msg_print()));

  m_action_msg_sender_details = new QAction(FT_MAKE_ICON(FT_ICON16_PEOPLE),
					    tr("Sender details"), this);
  connect(m_action_msg_sender_details, SIGNAL(triggered()),
	  this, SLOT(sender_properties()));

  m_action_search = new QAction(FT_MAKE_ICON(FT_ICON16_FIND), tr("Search"), this);
  connect(m_action_search, SIGNAL(triggered()), this, SLOT(search_db()));

  m_action_new_mail = new QAction(FT_MAKE_ICON(FT_ICON16_EDIT), tr("New mail"), this);
  connect(m_action_new_mail, SIGNAL(triggered()), this, SLOT(new_mail()));

  m_action_new_selection = new QAction(FT_MAKE_ICON(FT_ICON16_NEW_QUERY),
				       tr("Query new selection"), this);
  connect(m_action_new_selection, SIGNAL(triggered()), this, SLOT(new_list()));


  m_action_refresh_results = new QAction(FT_MAKE_ICON(FT_ICON16_REFRESH),
					 tr("Refresh query"), this);
  connect(m_action_refresh_results, SIGNAL(triggered()), this, SLOT(sel_refresh()));

  m_action_goto_last_msg = new QAction(FT_MAKE_ICON(FT_ICON16_GOTO_BOTTOM),
				       tr("Select bottom message"), this);
  connect(m_action_goto_last_msg, SIGNAL(triggered()), this, SLOT(sel_bottom()));
}

void
msg_list_window::make_toolbar()
{
  QToolBar* toolbar=addToolBar(tr("Message Operations"));
  toolbar->setIconSize(QSize(16,16));

  toolbar->addAction(m_action_new_mail);
  toolbar->addSeparator();
  toolbar->addAction(m_action_move_backward);
  toolbar->addAction(m_action_move_forward);
  toolbar->addAction(m_action_new_selection);
  toolbar->addAction(m_action_refresh_results);
  toolbar->addAction(m_action_goto_last_msg);

  toolbar->addAction(m_action_reply_sender);
  toolbar->addAction(m_action_reply_all);

  toolbar->addAction(m_action_msg_forward);
  toolbar->addAction(m_action_msg_archive);
  toolbar->addAction(m_action_find_text);
  toolbar->addAction(m_action_msg_trash);
  toolbar->addAction(m_action_msg_print);

  toolbar->addAction(m_action_msg_print);

  toolbar->addAction(m_action_msg_sender_details);

  QAction* action_notepad = new QAction(UI_ICON(ICON16_NOTEPAD),
					tr("Global Notepad"), this);
  connect(action_notepad, SIGNAL(triggered()),
	  this, SLOT(open_global_notepad()));
  toolbar->addAction(action_notepad);

  m_toolbar = toolbar;
}


/*
  Called when a tag is switched on or off: applies the change
  to the selected mail(s)
*/
void
msg_list_window::tag_toggled(int tag_id, bool checked)
{
  DBG_PRINTF(5, "tag_toggled(%d)", tag_id);
  std::vector<mail_msg*> v;
  m_qlist->get_selected(v);

  const uint batch_size=20;
  uint idx=0;
  uint nb_msg = v.size();
  uint size = nb_msg;
  uint nsteps = (size+batch_size-1)/batch_size;
  if (nsteps>1) {
    uint processed=0;
    install_progressbar(this);
    emit progress(-nsteps);

    std::set<mail_msg*> mset;
    for (uint step=0; step < nsteps && !progress_aborted(); step++) {
      mset.clear();
      for (uint ivec=0; idx<size && ivec<batch_size; ivec++) {
	if (m_query_lv && v[idx]->is_current())
	  m_query_lv->mail_tag_changed(*(v[idx]), (uint)tag_id, checked);
	mset.insert(v[idx++]);
      }
      // process in database
      if (!mset.empty()) {
	processed += mail_msg::toggle_tags_set(mset, tag_id, checked);
      }
      emit progress(1+step);
    }
    uninstall_progressbar(this);
    statusBar()->showMessage(tr("%1 message(s) processed.").arg(processed), 3000);
  }
  else for (idx=0; idx < v.size(); idx++) {
    mail_msg* p = v[idx];
    if (checked) {
      // put the tag if it's not already on
      if (!p->hasTag(tag_id)) {
	p->set_tag(tag_id, true);
	m_tags_box->set_tag(tag_id);
      }
    }
    else {
      // take the tag off
      p->set_tag(tag_id, false);
      m_tags_box->unset_tag(tag_id);
    }
    // update the tags counters in the quick selection view
    DBG_PRINTF(5, "mail_id=%d current=%d, status=%d", p->get_id(), p->is_current(), p->status());
    if (m_query_lv && p->is_current()) {
      m_query_lv->mail_tag_changed(*p, (uint)tag_id, checked);
    }
  }
  if (m_pCurrentItem) {
    // redisplay the body
    display_body();
  }
}

/*
  menuBar()->setFont() doesn't propagate to the child menus, we need
  to do it explicitly
*/
void
msg_list_window::set_menu_font(const QFont& f)
{
  QMenu* tab[] = {
    // top level
    m_pMenuFile, m_pMenuEdit, m_pMenuSelection, m_pMenuMessage, m_pMenuDisplay,
    m_pMenuHelp,
    // sub menus
    m_pPopupFonts, m_pPopupHeaders, m_popup_body, m_pPopupAttach
  };
  for (uint i=0; i<sizeof(tab)/sizeof(tab[0]); i++) {
    tab[i]->setFont(f);
  }
  menuBar()->setFont(f);
}


QMenuBar*
msg_list_window::init_menu()
{
  QIcon ico_cut(FT_MAKE_ICON(FT_ICON16_EDIT_CUT));
  QIcon ico_copy(FT_MAKE_ICON(FT_ICON16_EDIT_COPY));
  QIcon ico_paste(FT_MAKE_ICON(FT_ICON16_EDIT_PASTE));
  QIcon ico_prefs(FT_MAKE_ICON(FT_ICON16_PREFS));
  QIcon ico_filters(FT_MAKE_ICON(FT_ICON16_FILTERS));
  QIcon ico_mail_merge(FT_MAKE_ICON(ICON16_MAIL_MERGE));
  QIcon ico_tags(FT_MAKE_ICON(FT_ICON16_TAGS));
  QIcon ico_properties(FT_MAKE_ICON(FT_ICON16_PROPERTIES));
  QIcon ico_new_window(FT_MAKE_ICON(FT_ICON16_NEW_WINDOW));
  QIcon ico_close_window(FT_MAKE_ICON(FT_ICON16_CLOSE_WINDOW));
  QIcon ico_forward(FT_MAKE_ICON(FT_ICON16_FORWARD));
  QIcon ico_find(FT_MAKE_ICON(FT_ICON16_FIND));
  QIcon ico_print(FT_MAKE_ICON(FT_ICON16_PRINT));
  QIcon ico_new_msg(FT_MAKE_ICON(FT_ICON16_EDIT));
  QIcon ico_trash(FT_MAKE_ICON(FT_ICON16_ACTION_TRASH));
  QIcon ico_new_query(FT_MAKE_ICON(FT_ICON16_NEW_QUERY));
  QIcon ico_replace_query(FT_MAKE_ICON(FT_ICON16_REPLACE_QUERY));
  QIcon ico_refresh(FT_MAKE_ICON(FT_ICON16_REFRESH));
  QIcon ico_save_query(FT_MAKE_ICON(FT_ICON16_SAVE_SQL));
  QIcon ico_edit_note(FT_MAKE_ICON(FT_ICON16_EDIT_NOTE));
  QIcon ico_get_new_mail(FT_MAKE_ICON(FT_ICON16_GET_NEW_MAIL));
  QIcon ico_get_unproc_mail(FT_MAKE_ICON(FT_ICON16_GET_UNPROC_MAIL));
  QIcon ico_help(FT_MAKE_ICON(FT_ICON16_HELP));
  QIcon ico_about(FT_MAKE_ICON(FT_ICON16_ABOUT));
  QIcon ico_outbox(FT_MAKE_ICON(FT_ICON16_OUTBOX));
  QIcon ico_quit(FT_MAKE_ICON(FT_ICON16_QUIT));

  // Application
  m_pMenuFile=new QMenu(tr("&File"), this);
  CHECK_PTR(m_pMenuFile);

  m_menu_actions[me_File_New_Window] =
    m_pMenuFile->addAction(ico_new_window, tr("&New Window"), this,
			    SLOT(new_window()), Qt::CTRL+Qt::Key_N);

  m_menu_actions[me_File_Close_Window] =
    m_pMenuFile->addAction(ico_close_window, tr("&Close Window"), this,
			    SLOT(close_window()), Qt::CTRL+Qt::Key_W);
  m_pMenuFile->addSeparator();

  m_menu_actions[me_File_Preferences] =
    m_pMenuFile->addAction(ico_prefs, tr("&Preferences"), this, SLOT(preferences()));

  m_menu_actions[me_File_Global_Notepad] =
    m_pMenuFile->addAction(FT_MAKE_ICON(ICON16_NOTEPAD), tr("Global Notepad"), this, SLOT(open_global_notepad()), Qt::CTRL+Qt::Key_G);

  m_menu_actions[me_Configuration_EditTags] =
    m_pMenuFile->addAction(ico_tags, tr("&Tags"), this, SLOT(edit_tags()));

  m_menu_actions[me_Configuration_EditFilters] =
    m_pMenuFile->addAction(ico_filters, tr("Filters"), this, SLOT(edit_filters()));

  m_menu_actions[me_File_Mailing] =
    m_pMenuFile->addAction(ico_mail_merge, tr("Mailings"), this, SLOT(start_mailing()));

  m_pMenuFile->addSeparator();

  m_menu_actions[me_File_Quit] =
    m_pMenuFile->addAction(ico_quit, tr("&Quit"), gl_pApplication, SLOT(quit()));
/*
  m_menu_actions[me_Application_Import] =
    m_pMenuFile->addAction(tr("&Import mailbox"), this, SLOT(sel_import()));
*/

  // Edit
  m_pMenuEdit=new QMenu(tr("Edit"), this);
  CHECK_PTR(m_pMenuEdit);

  // Message
  m_pMenuMessage=new QMenu(tr("&Message"), this);
  CHECK_PTR(m_pMenuMessage);

  // Display
  m_pMenuDisplay=new QMenu(tr("&Display"), this);
  CHECK_PTR (m_pMenuDisplay);
  m_menu_actions[me_Display_Tags] = m_pMenuDisplay->addAction(tr("&Tags panel"), this, SLOT(toggle_show_tags(bool)));
  m_menu_actions[me_Display_Tags]->setCheckable(true);
  m_menu_actions[me_Display_Tags]->setChecked(display_vars.m_show_tags);

  m_menu_actions[me_Display_Threaded]=m_pMenuDisplay->addAction(tr("&Threaded"), this, SLOT(toggle_threaded(bool)));
  m_menu_actions[me_Display_Threaded]->setCheckable(true);
  m_menu_actions[me_Display_Threaded]->setChecked(display_vars.m_threaded);

  m_menu_actions[me_Display_WrapLines] = m_pMenuDisplay->addAction(tr("Wrap lines"), this, SLOT(toggle_wrap_lines(bool)));
  m_menu_actions[me_Display_WrapLines]->setCheckable(true);
  m_menu_actions[me_Display_WrapLines]->setChecked(display_vars.m_wrap_lines);

  m_menu_actions[me_Display_Hide_Quoted] = m_pMenuDisplay->addAction(tr("Hide quoted text"), this, SLOT(toggle_hide_quoted(bool)), Qt::CTRL+Qt::Key_H);
  m_menu_actions[me_Display_Hide_Quoted]->setCheckable(true);
  m_menu_actions[me_Display_Hide_Quoted]->setChecked(display_vars.m_hide_quoted);

  m_menu_actions[me_Display_FastBrowse] = m_pMenuDisplay->addAction(tr("Fetch on demand"), this, SLOT(toggle_fetch_on_demand(bool)));
  m_menu_actions[me_Display_FastBrowse]->setCheckable(true);
  m_menu_actions[me_Display_FastBrowse]->setChecked(m_fetch_on_demand);


  // Display->Fonts
  m_pPopupFonts = new QMenu(tr("&Fonts"), this);

  m_menu_actions[me_Display_Font_All] =
    m_pPopupFonts->addAction(tr("All"));
  m_menu_actions[me_Display_Font_Menus] =
    m_pPopupFonts->addAction(tr("Menus"));
  m_menu_actions[me_Display_Font_QuickSel] =
    m_pPopupFonts->addAction(tr("Quick selection"));
  m_menu_actions[me_Display_Font_Tags] =
    m_pPopupFonts->addAction(tr("Tags"));
  m_menu_actions[me_Display_Font_Msglist] =
    m_pPopupFonts->addAction(tr("Messages list"));
  m_menu_actions[me_Display_Font_Msgbody] =
    m_pPopupFonts->addAction(tr("Message body"));

  m_pMenuDisplay->addMenu(m_pPopupFonts);
  connect(m_pPopupFonts, SIGNAL(triggered(QAction*)), SLOT(change_font(QAction*)));

  // Display->Message
  m_popup_display_body = new QMenu(tr("Body"), this);
  m_pMenuDisplay->addMenu(m_popup_display_body);
  QAction* action;
  
  // Display->Message->Zoom In
  action = new QAction(tr("Zoom In"), this);
  action->setShortcut(tr("Ctrl++"));
  action->setStatusTip(tr("Zoom in on the message contents"));
  connect(action, SIGNAL(triggered()), this, SLOT(msg_zoom_in()));
  m_menu_actions[me_Display_Body_ZoomIn] = action;
  m_popup_display_body->addAction(action);

  // Display->Message->Zoom Out
  action = new QAction(tr("Zoom Out"), this);
  action->setShortcut(tr("Ctrl+-"));
  action->setStatusTip(tr("Zoom out on the message contents"));
  connect(action, SIGNAL(triggered()), this, SLOT(msg_zoom_out()));
  m_menu_actions[me_Display_Body_ZoomOut] = action;
  m_popup_display_body->addAction(action);

  // Display->Message->Reset Zoom
  action = new QAction(tr("Reset zoom"), this);
  action->setShortcut(tr("Ctrl+0"));
  action->setStatusTip(tr("Reset the zoom on the message contents"));
  connect(action, SIGNAL(triggered()), this, SLOT(msg_zoom_zero()));
  m_menu_actions[me_Display_Body_ZoomZero] = action;
  m_popup_display_body->addAction(action);

  // Display->Headers
  m_pPopupHeaders=new QMenu(tr("Headers"), this);
  m_menu_actions[me_Display_Headers_None] = m_pPopupHeaders->addAction(tr("None"));
  m_menu_actions[me_Display_Headers_Most] = m_pPopupHeaders->addAction(tr("Most"));
  m_menu_actions[me_Display_Headers_All] =  m_pPopupHeaders->addAction(tr("All"));
  m_menu_actions[me_Display_Headers_Raw] =  m_pPopupHeaders->addAction(tr("Raw"));
  m_menu_actions[me_Display_Headers_RawDec] = m_pPopupHeaders->addAction(tr("Decoded"));

  QActionGroup* qag_show_headers = new QActionGroup(this);
  qag_show_headers->setExclusive(true);
  int header_show_options[] = {
    me_Display_Headers_None, me_Display_Headers_Most, me_Display_Headers_All,
    me_Display_Headers_Raw, me_Display_Headers_RawDec
  };

  for (uint i=0; i<sizeof(header_show_options)/sizeof(int); i++) {
    qag_show_headers->addAction(m_menu_actions[header_show_options[i]]);
    m_menu_actions[header_show_options[i]]->setCheckable(true);
  }
  m_pPopupHeaders->addSeparator();
  // show tags in headers
  m_menu_actions[me_Display_Headers_Tags] = m_pPopupHeaders->addAction(tr("Include tags"), this, SLOT(toggle_include_tags_in_headers(bool)));
  m_menu_actions[me_Display_Headers_Tags]->setCheckable(true);
  m_menu_actions[me_Display_Headers_Tags]->setChecked(display_vars.m_show_tags_in_headers);

  // show filter logs in headers
  m_menu_actions[me_Display_Show_FiltersTrace] = m_pPopupHeaders->addAction(tr("Show Filters Log"), this, SLOT(toggle_show_filters_log(bool)));
  m_menu_actions[me_Display_Show_FiltersTrace]->setCheckable(true);
  m_menu_actions[me_Display_Show_FiltersTrace]->setChecked(display_vars.m_show_filters_trace);

  connect(m_pPopupHeaders, SIGNAL(triggered(QAction*)), SLOT(show_headers(QAction*)));
  m_pMenuDisplay->addMenu(m_pPopupHeaders);

  m_pMenuDisplay->addAction(tr("Store settings"), this, SLOT(save_display_settings()));


  
  QAction* header_checked=NULL;
  if (get_config().exists("show_headers_level")) {
    display_vars.m_show_headers_level=get_config().get_number("show_headers_level");
    if (display_vars.m_show_headers_level<0 ||
	display_vars.m_show_headers_level>4)
      {
	// incorrect value => use the default instead
	DBG_PRINTF(5,"warning: incorrect value '%d' for show_headers_level in configuration", display_vars.m_show_headers_level);
	display_vars.m_show_headers_level=1; // default=Most headers
      }
  }
  else
    display_vars.m_show_headers_level=1; // default=Most headers

  switch(display_vars.m_show_headers_level) {
  case 0:
    header_checked=m_menu_actions[me_Display_Headers_None];
    break;
  default:			// default shouldn't happen
  case 1:
    header_checked=m_menu_actions[me_Display_Headers_Most];
    break;
  case 2:
    header_checked=m_menu_actions[me_Display_Headers_All];
    break;
  case 3:
    header_checked=m_menu_actions[me_Display_Headers_Raw];
    break;
  case 4:
    header_checked=m_menu_actions[me_Display_Headers_RawDec];
    break;
  }
  if (header_checked!=NULL)
    header_checked->setChecked(true);

  // focus on F10 key
  (void)new QShortcut(Qt::Key_F10, this, SLOT(focus_on_msglist()));

  // Help menu
  m_pMenuHelp=new QMenu(tr("&Help"), this);
  CHECK_PTR(m_pMenuHelp);
  m_pMenuHelp->addAction(ico_help, tr("Help window"), this, SLOT(open_help()), Qt::Key_F1);
  m_menu_actions[me_Help_Dynamic] = m_pMenuHelp->addAction(tr("Track context"),
							  this, SLOT(dynamic_help()));
  m_menu_actions[me_Help_Dynamic]->setCheckable(true);
  m_menu_actions[me_Help_Dynamic]->setChecked(false);

  m_pMenuHelp->addAction(ico_about, tr("&About"), this, SLOT(about()));

  m_pMenuSelection=new QMenu(tr("&Selection"), this);
  CHECK_PTR(m_pMenuSelection);

  QMenuBar* menu=menuBar(); //new QMenuBar(this);
  menu->addMenu(m_pMenuFile);
  menu->addMenu(m_pMenuEdit);
  menu->addMenu(m_pMenuSelection);
  menu->addMenu(m_pMenuMessage);
  menu->addMenu(m_pMenuDisplay);
//  menu->addMenu(tr("&Configuration"), m_pMenuConfig);
  menu->addSeparator();
  menu->addMenu(m_pMenuHelp);

  connect(m_pMenuFile, SIGNAL(aboutToShow()), this, SLOT(enable_commands()));
  connect(m_pMenuSelection, SIGNAL(aboutToShow()), this, SLOT(enable_commands()));
  connect(m_pMenuMessage, SIGNAL(aboutToShow()), this, SLOT(enable_commands()));
  connect(m_pMenuDisplay, SIGNAL(aboutToShow()), this, SLOT(enable_commands()));
  connect(m_pPopupHeaders, SIGNAL(aboutToShow()), this, SLOT(enable_commands()));

  m_menu_actions[me_Edit_Cut] = m_pMenuEdit->addAction(ico_cut, tr("Cut"));
  m_menu_actions[me_Edit_Cut]->setEnabled(false);

  m_menu_actions[me_Edit_Copy] = m_pMenuEdit->addAction(ico_copy, tr("Copy"), this, SLOT(edit_copy()), Qt::CTRL+Qt::Key_C);
  m_menu_actions[me_Edit_Copy]->setEnabled(true);

  m_menu_actions[me_Edit_Paste] = m_pMenuEdit->addAction(ico_paste, tr("Paste"));
  m_menu_actions[me_Edit_Paste]->setEnabled(false);

  m_menu_actions[me_Message_New] =
    m_pMenuMessage->addAction(ico_new_msg, tr("&New message"),
			       this, SLOT(new_mail()));


  m_pMenuMessage->addAction(m_action_reply_sender);
  m_pMenuMessage->addAction(m_action_reply_all);
  m_pMenuMessage->addAction(m_action_msg_forward);
  m_pMenuMessage->addAction(m_action_find_text);

  m_menu_actions[me_Message_EditNote] =
    m_pMenuMessage->addAction(ico_edit_note, tr("&Edit note"),
			       this, SLOT(edit_note()));

  m_pMenuMessage->addAction(m_action_msg_archive);

  m_menu_actions[me_Message_Properties] =
    m_pMenuMessage->addAction(ico_properties, tr("Pr&operties"), this, SLOT(msg_properties()));

  m_pMenuMessage->addAction(m_action_msg_trash);
  m_pMenuMessage->addAction(m_action_msg_untrash);
  m_pMenuMessage->addAction(m_action_msg_delete);
  m_pMenuMessage->addAction(m_action_msg_print);

  m_popup_body=new QMenu(tr("Body"), this);
  m_menu_actions[me_Message_Body_Save] =
    m_popup_body->addAction(tr("Save"), this, SLOT(save_body()));
  m_menu_actions[me_Message_Body_Edit] =
    m_popup_body->addAction(tr("Edit"), this, SLOT(edit_body()));
  /*
  m_menu_actions[me_Message_Save_ToMbox] =
    m_pPopupSaveMsg->addAction(tr("To mailbox"), this, SLOT(save_to_mbox()));
  */

  m_pMenuMessage->addMenu(m_popup_body);

  m_pPopupAttach = new QMenu(tr("Attachment"), this);
  m_menu_actions[me_Message_Attch_View] =
    m_pPopupAttach->addAction(tr("View as text"), this, SLOT(view_attachment()));

  m_menu_actions[me_Message_Attch_Save] =
    m_pPopupAttach->addAction(tr("Save"), this, SLOT(save_attachment()));

  m_pMenuMessage->addMenu(m_pPopupAttach);

  m_menu_actions[me_Selection_NewMessages] =
    m_pMenuSelection->addAction(ico_get_new_mail, tr("New mail"),
				 this, SLOT(new_messages()));

  m_menu_actions[me_Selection_CurrentMessages] =
    m_pMenuSelection->addAction(ico_get_unproc_mail,
				 tr("Current mail"), this,
				 SLOT(non_processed_messages()));

  m_menu_actions[me_Selection_Trashcan] =
    m_pMenuSelection->addAction(ico_trash, tr("Trashcan"), this, SLOT(sel_trashcan()));

  m_menu_actions[me_Selection_Sent] =
    m_pMenuSelection->addAction(ico_outbox, tr("Sent"), this, SLOT(sel_sent()));

  m_pMenuSelection->addSeparator();

  m_menu_actions[me_Selection_New] =
    m_pMenuSelection->addAction(ico_new_query, tr("&New query"), this, SLOT(new_list()), Qt::Key_F2);
  m_menu_actions[me_Selection_Refine] =
    m_pMenuSelection->addAction(ico_replace_query, tr("&Modify query"), this, SLOT(sel_refine()));
  m_menu_actions[me_Selection_Refresh] =
    m_pMenuSelection->addAction(ico_refresh, tr("&Refresh"), this, SLOT(sel_refresh()), Qt::Key_F5);
  m_menu_actions[me_Selection_Save_Query] =
    m_pMenuSelection->addAction(ico_save_query, tr("&Save query"), this, SLOT(sel_save_query()));

#if 0
  m_pMenuSelection->addSeparator();
  m_menu_actions[me_Selection_Header_Analysis] =
    m_pMenuSelection->addAction(tr("Headers Analysis"), this, SLOT(sel_header_analysis()));
#endif

  return menu;
}

msg_list_window::msg_list_window (const msgs_filter* filter, display_prefs* dprefs, QWidget *parent)  : QMainWindow(parent)
{
  m_abort = false;
  m_ignore_selection_change = false;
  m_waiting_for_results = true;
  m_qlist = NULL;
  m_filter = new msgs_filter(*filter);
  m_pCurrentItem = NULL;

  // application icon
  setWindowIcon(FT_MAKE_ICON(FT_ICON16_EDIT));

  QWidget* main_hb=new QWidget(this); // container widget
  setCentralWidget(main_hb);

  QHBoxLayout* topLayout = new QHBoxLayout(main_hb);
  topLayout->setSpacing(5);
  topLayout->setMargin(5);

  QSplitter* lvert = new QSplitter(main_hb);
  topLayout->addWidget(lvert);

  QSplitter* l2 = new QSplitter(Qt::Vertical, lvert);

  m_query_lv = new query_listview(l2);
  m_query_lv->init();

  connect(m_query_lv, SIGNAL(itemActivated(QTreeWidgetItem *,int)),
	  this, SLOT(quick_query_selection(QTreeWidgetItem *,int)));

  connect(m_query_lv, SIGNAL(itemPressed(QTreeWidgetItem *,int)),
	  this, SLOT(quick_query_selection(QTreeWidgetItem *,int)));

  connect(m_query_lv, SIGNAL(run_selection_filter(const msgs_filter&)),
	  this, SLOT(sel_filter(const msgs_filter&)));

  m_pages = new msgs_page_list(lvert);
  // the query listview should not be stretched horizontally
  lvert->setStretchFactor(lvert->indexOf(l2), 0);
  lvert->setStretchFactor(lvert->indexOf((QWidget*)m_pages->stacked_widget()), 1);

  m_tags_box=new tags_box_widget(l2/*main_hb*/);

  connect(m_tags_box, SIGNAL(state_changed(int,bool)), this, SLOT(tag_toggled(int,bool)));

//  (void)new QVBox(m_tags_box); // takes up the remaining space
//  topLayout->addWidget(m_tags_box);
  if (dprefs) {
    display_vars = *dprefs;
/*
    display_vars.m_threaded = dprefs->m_threaded;
    display_vars.m_wrap_lines = dprefs->m_wrap_lines;
    display_vars.m_show_tags = dprefs->m_show_tags;
    display_vars.m_hide_quoted = dprefs->m_hide_quoted;
    display_vars.m_clickable_urls = dprefs->m_clickable_urls;
    display_vars.m_show_filters_trace = dprefs->m_show_filters_trace;
    display_vars.m_show_tags_in_headers = dprefs->m_show_tags_in_headers;
*/
  }
  else {
    display_vars.init();
  }
  m_fetch_on_demand = false;

  if (!display_vars.m_show_tags) {
    m_tags_box->hide();
  }

  int height=get_config().get_number("display/msglist/height");
  if (height==0) height=600;
  int width=get_config().get_number("display/msglist/width");
  if (width==0) width=800;

  resize(width,height);

//  topLayout->setMenuBar(menu);

  create_actions();
  add_msgs_page(m_filter,false);

  make_toolbar();
  make_search_toolbar();

  init_menu();
  enable_forward_backward();
  init_fonts();

  m_new_mail_btn = new newmail_button(tr("New mail !"), statusBar());
  statusBar()->addPermanentWidget(m_new_mail_btn);
  m_new_mail_btn->enable(false);
  /* Hide the new mail button. This button has somehow become obsolete
     since new messages can be incorporated automatically into the
     list */
  m_new_mail_btn->hide();
  connect(m_new_mail_btn, SIGNAL(clicked()), this, SLOT(sel_refresh()));
  connect(m_new_mail_btn, SIGNAL(show_new_mail()), this, SLOT(raise_refresh()));
  m_timer_idle = new QTimer(this);
  m_timer_idle->start(3000);
  connect(m_timer_idle, SIGNAL(timeout()), this, SLOT(timer_idle()));

  m_timer = new QTimer(this);
  m_timer_ticks=0;
  m_timer->start(200);
  connect(m_timer, SIGNAL(timeout()), this, SLOT(timer_func()));

  connect(this, SIGNAL(mail_chg_status(int,mail_msg*)), SLOT(change_mail_status(int,mail_msg*)));

  connect(this, SIGNAL(mail_multi_chg_status(int,std::vector<mail_msg*>*)), SLOT(change_multi_mail_status(int,std::vector<mail_msg*>*)));

  // subscribe to refresh requests
  message_port::connect_receiver(SIGNAL(list_refresh_request()),
				 this, SLOT(sel_auto_refresh_list()));

}

void
msg_list_window::raise_refresh()
{
  sel_refresh();
  raise();
  activateWindow();
}

/*
  Set various display settings for m_qlist, once its mail_msg's
  are loaded
*/
void
msg_list_window::msg_list_postprocess()
{
  // sort by message date
  DBG_PRINTF(8, "start sort");
  m_qlist->setSortingEnabled(true);
  m_qlist->sortByColumn(mail_item_model::column_date, Qt::AscendingOrder);
  DBG_PRINTF(8, "end sort");
  m_qlist->header()->setSortIndicatorShown(true);
  m_qlist->setRootIsDecorated(display_vars.m_threaded);
  // m_qlist->scroll_to_bottom(); // too slow
  m_qlist->expandAll();
  set_title();
}

void
msg_list_window::edit_copy()
{
  m_msgview->copy();
}

void
msg_list_window::toggle_wrap_lines(bool wrap)
{
  display_vars.m_wrap_lines = wrap; // !display_vars.m_wrap_lines;
//  m_pMenuDisplay->setItemChecked(id, display_vars.m_wrap_lines);
  if (!m_fetch_on_demand)
    display_body();
}

void
msg_list_window::toggle_include_tags_in_headers(bool include)
{
  display_vars.m_show_tags_in_headers = include;
  if (!m_fetch_on_demand)
    display_body();  
}

void
msg_list_window::toggle_hide_quoted(bool hide)
{
  display_vars.m_hide_quoted = hide;
  if (!m_fetch_on_demand)
    display_body();
}

void
msg_list_window::toggle_show_filters_log(bool show)
{
  display_vars.m_show_filters_trace = show;
  if (!m_fetch_on_demand)
    display_body();
}

void
msg_list_window::toggle_fetch_on_demand(bool on_demand)
{
  m_fetch_on_demand = on_demand;
//  m_pMenuDisplay->setItemChecked(id, m_fetch_on_demand);
  m_msgview->set_show_on_demand(m_fetch_on_demand);

  if (!m_fetch_on_demand) {
    display_msg_contents();
    display_selection_tags();
  }
  else
    m_tags_box->reset_tags();
}

msg_list_window::~msg_list_window()
{
  if (m_wSearch) {
    delete m_wSearch;
  }
  // delete all pages
  m_pages->cut_pages(m_pages->first_page());
  delete m_pages;
}


// slot
void
msg_list_window::global_refresh_status(mail_id_t mail_id)
{
  mail_msg* first_msg=NULL;
  uint status=0;
  std::list<msgs_page*>::iterator page_it;
  for (page_it = msgs_page_list::m_all_pages_list.begin();
       page_it != msgs_page_list::m_all_pages_list.end();
       ++page_it)
  {
    mail_listview* l = (*page_it)->m_page_qlist;
    mail_msg* msg = l->find(mail_id);
    if (msg!=NULL) {
      if (!first_msg) {
	// re-read from database to get the latest status
	// as an optimisation, we read it only once and cache the value
	// for other occurrences in different views
	first_msg=msg;
	msg->refresh(); // db read
	status=msg->status();
      }
      else {
	msg->set_status(status); // status has been set previously in the loop
      }
      l->update_msg(msg);
    }
  }
  if (m_query_lv && first_msg) {
    m_query_lv->mail_status_changed(first_msg, status);
  }
}

// code=-1 if the message has been deleted
void
msg_list_window::propagate_status(mail_msg* item, int code/*=0*/)
{
  DBG_PRINTF(8, "propagate_status(id=%d, code=%d)", item->get_id(), code);
  // tell all pages (except the current page that should be the
  // caller) about the change
  std::list<msgs_page*>::iterator page_it;
  msgs_page* our_current_page = m_pages->current_page();
  for (page_it = msgs_page_list::m_all_pages_list.begin();
       page_it != msgs_page_list::m_all_pages_list.end();
       ++page_it)
  {
    if (*page_it != our_current_page)
      (*page_it)->refresh_status(item, code);
  }
  if (m_query_lv) {
    m_query_lv->mail_status_changed(item, (code==-1)?-1:(int)item->status());
  }
}

/*
  (slot) Called to change the status of a message
  Reflects the change in the listview.
  Can also remove the message(s) from the listview and make it non longer
  current.
*/
void
msg_list_window::change_mail_status (int status_mask, mail_msg* msg)
{
  DBG_PRINTF(8, "change_mail_status(mask=%d, mail_id=%d)", status_mask, msg->get_id());
  msg->set_status(msg->status()|status_mask);
  if (msg->update_status()) {
    DBG_PRINTF(5, "status of mail %d updated", msg->get_id());
    if (status_mask & (int)mail_msg::statusTrashed) {
      propagate_status(msg, 0);
      remove_msg(msg);
      set_title();
    }
    else if (status_mask & (int)mail_msg::statusArchived) {
      m_qlist->update_msg(msg);
      m_qlist->select_below(msg); // FIXME: should move this call somewhere else
      propagate_status(msg);
    }
    else {
      m_qlist->update_msg(msg);
      propagate_status(msg);
    }
  }
  // else database error in status update. TODO
}

void
msg_list_window::remove_msg(mail_msg* msg, bool auto_select_next)
{
  DBG_PRINTF(8, "remove_msg(id=%d, auto_select_next=%d)", msg->get_id(), auto_select_next);
  // remove from the treeview
  m_qlist->remove_msg(msg, auto_select_next);
  // remove from our list
  m_filter->m_list_msgs.remove(msg);
}

void
msg_list_window::msg_properties()
{
  if (m_qlist->selection_size()==1) {
    std::vector<mail_msg*> v;
    m_qlist->get_selected(v);
    properties_dialog* w = new properties_dialog (v[0], NULL);
    m_qlist->connect(w, SIGNAL(change_status_request(mail_id_t,uint,int)),
		     m_qlist, SLOT(force_msg_status(mail_id_t,uint,int)));
    w->show();
  }
}


void
msg_list_window::msg_delete()
{
  DBG_PRINTF(8, "msg_delete()");
  remove_selected_msgs(1);
}

void
msg_list_window::msg_trash()
{
  DBG_PRINTF(8, "msg_trash()");
  remove_selected_msgs(0);
}

/*
  action=0 => trash
  action=1 => delete
*/
void
msg_list_window::remove_selected_msgs(int action)
{
  DBG_PRINTF(8, "remove_selected_msgs(%d)", action);
  const QCursor cursor(Qt::WaitCursor);
  QApplication::setOverrideCursor(cursor);

  uint removed=0;
  std::vector<mail_msg*> v;
  m_qlist->get_selected (v);

  const uint batch_size=20;
  uint idx=0;
  uint nb_msg = v.size();
  DBG_PRINTF(6, "# of msgs to trash/delete = %d", nb_msg);
  uint size = nb_msg;
  uint nsteps = (size+batch_size-1)/batch_size;
  m_ignore_selection_change = true;
  if (nsteps > 1 && action==0) {
    // massive trash, do it in batches
    install_progressbar(this);
    emit progress(-nsteps);

    std::set<mail_msg*> mset;
    for (uint step=0; step < nsteps && !progress_aborted(); step++) {
      mset.clear();
      std::set<mail_msg*> imset;
      for (uint ivec=0; idx<size && ivec<batch_size; ivec++) {
	imset.insert(v[idx]);
	mset.insert(v[idx++]);
      }
      // process in database
      if (!mset.empty()) {
	mail_msg::trash_set(mset);
	removed += mset.size();
      }
      // propagate
      std::set<mail_msg*>::iterator it = imset.begin();
      for (unsigned int cnt=1; it!=imset.end(); ++it,++cnt) {
	propagate_status(*it);
	// auto-select next one only for the last message
	remove_msg(*it, (cnt==imset.size()));
      }
      // report progress
      emit progress(1+step);
    }
    if (nsteps > 1)
      uninstall_progressbar(this);
  }
  else for (uint i=0; i<nb_msg; i++) {
    // small update
    if (action==0) {
      statusBar()->showMessage(QString(tr("Trashing messages: %1 of %2")).arg(i+1).arg(nb_msg));
      statusBar()->repaint();
      QApplication::flush();
      if (v[i]->trash()) {
	propagate_status(v[i]);
	remove_msg(v[i], (i==nb_msg-1));
	removed++;
      }
    }
    else if (action==1) {
      statusBar()->showMessage(QString(tr("Deleting messages: %1 of %2")).arg(i+1).arg(nb_msg));
      statusBar()->repaint();
      QApplication::flush();
      if (v[i]->mdelete()) {
	propagate_status(v[i], -1);
	remove_msg(v[i], (i==nb_msg-1));
	removed++;
      }
    }
  }
  m_ignore_selection_change = false;
  mails_selected();
  set_title();
  QApplication::restoreOverrideCursor();
  if (action==0)
    statusBar()->showMessage(tr("%1 message(s) trashed.").arg(removed), 3000);
  else
    statusBar()->showMessage(tr("%1 message(s) deleted.").arg(removed), 3000);
}

void
msg_list_window::msg_untrash()
{
  std::vector<mail_msg*> v;
  m_qlist->get_selected (v);
  for (uint j=0; j < v.size(); j++) {
    mail_msg* msg = v[j];
    if (msg->status() & mail_msg::statusTrashed) {
      if (msg->untrash()) {
	m_qlist->update_msg(msg);
	propagate_status(msg);
      }
      // else database error in status update.
    }
  }
}

/*
  (slot) Called to change the status of a message
  Reflects the change in the listview.
  Can also remove the message(s) from the listview and make it non longer
  current.
*/
void
msg_list_window::change_multi_mail_status(int statusMask,
					  std::vector<mail_msg*>* v)
{
  const QCursor cursor(Qt::WaitCursor);
  QApplication::setOverrideCursor(cursor);

  const uint batch_size=20;
  uint idx=0;
  uint size=v->size();

  statusBar()->showMessage(tr("Updating messages..."));
  uint nsteps = (size+batch_size-1)/batch_size;
  if (nsteps > 1) {
    install_progressbar(this);
    emit progress(-nsteps);
  }

  DBG_PRINTF(5, "nsteps=%d, size=%d", nsteps, size);
  std::set<mail_msg*> mset;
  for (uint step=0; step < nsteps && !progress_aborted(); step++) {
    mset.clear();
    std::set<mail_msg*> imset;
    for (uint ivec=0; idx<size && ivec<batch_size; ivec++) {
      imset.insert((*v)[idx]);
      mset.insert((*v)[idx++]);
    }
    // set in database
    if (!mset.empty()) {
      mail_msg::set_or_with_status(mset, statusMask);
    }
    // propagate
    std::set<mail_msg*>::iterator it = imset.begin();
    for (; it!=imset.end(); ++it) {
      if (statusMask & (int)mail_msg::statusArchived) {
	m_qlist->update_msg(*it);
	propagate_status(*it);
      }
    }
    // report progress
    emit progress(1+step);
  }

  if (nsteps > 1)
    uninstall_progressbar(this);
  m_qlist->update();
  QApplication::restoreOverrideCursor();
  statusBar()->showMessage(tr("Done."), 3000);
}

// set the window title. If no argument is given, display the
// number of messages in the list
void
msg_list_window::set_title(const QString title/*=QString::null*/)
{
  if (title.isEmpty()) {
    QString sTitle=QString("%3: %1%4 %2").arg(m_filter->m_list_msgs.size()).arg(tr("message(s)")).arg(db_cnx::dbname(), m_filter->has_more_results()?"+":"");
    setWindowTitle(sTitle);
  }
  else
    setWindowTitle(title);
}

/*
  Menu Handlers
*/

/* Enable or disable the menu entries according to what messages are
   currently selected (the current selection is in the vector 'v') */
void
msg_list_window::enable_commands()
{
  int size=m_qlist->selection_size();
  if (size==1) {
    // one mail selected
    std::vector<mail_msg*> v;
    m_qlist->get_selected(v);

    m_action_reply_all->setEnabled(true);
    m_action_reply_sender->setEnabled(true);
    m_action_reply_list->setEnabled(true);

    m_action_msg_forward->setEnabled(true);
    bool can_trash = ((v[0]->status() & mail_msg::statusTrashed)==0);
    m_action_msg_trash->setEnabled(can_trash);
    m_action_msg_untrash->setEnabled(!can_trash);
    m_action_msg_delete->setEnabled(true);
    m_action_msg_archive->setEnabled(true);

    m_action_msg_print->setEnabled(true);
    m_action_msg_sender_details->setEnabled(true);
    m_menu_actions[me_Message_Properties]->setEnabled(true);
  }
  else if (size==0) {
    // none selected
    m_action_reply_all->setEnabled(false);
    m_action_reply_sender->setEnabled(false);
    m_action_reply_list->setEnabled(false);
    m_action_msg_forward->setEnabled(false);
    m_action_msg_archive->setEnabled(false);

    m_action_msg_trash->setEnabled(false);
    m_action_msg_untrash->setEnabled(false);
    m_action_msg_delete->setEnabled(false);
    m_menu_actions[me_Message_Properties]->setEnabled(false);

    m_action_msg_print->setEnabled(false);
    m_action_msg_sender_details->setEnabled(false);
  }
  else {
    // several messages selected
    m_action_reply_all->setEnabled(false);
    m_action_reply_sender->setEnabled(false);
    m_action_reply_list->setEnabled(false);
    m_action_msg_forward->setEnabled(false);
    m_action_msg_archive->setEnabled(true);
    m_action_msg_trash->setEnabled(true);
    m_action_msg_untrash->setEnabled(true);
    m_action_msg_delete->setEnabled(true);
    m_action_msg_print->setEnabled(false);
    m_action_msg_sender_details->setEnabled(false);
    m_menu_actions[me_Message_Properties]->setEnabled(false);
  }
}

// Hide/Show the tags panel
void
msg_list_window::toggle_show_tags(bool on)
{
  display_vars.m_show_tags=on; // !display_vars.m_show_tags;

  //  m_pMenuDisplay->setItemChecked(id, display_vars.m_show_tags);
  if (display_vars.m_show_tags)
    m_tags_box->show();
  else
    m_tags_box->hide();
}

// Show/don't show mail threads as trees
void
msg_list_window::toggle_threaded(bool on)
{
  display_vars.m_threaded=on; // !display_vars.m_threaded;

//  m_pMenuDisplay->setItemChecked(id, display_vars.m_threaded);
  m_qlist->set_threaded(display_vars.m_threaded);
  m_qlist->clear();
  /* Re-insert the whole list because in-place reparenting is way slower
     with current versions of QTreeView (as of Qt-4.3.3) */
  m_qlist->insert_list(m_filter->m_list_msgs);
  m_qlist->setRootIsDecorated(display_vars.m_threaded);
}

void
msg_list_window::change_font(QAction* which)
{
  bool ok;
  QFont current_font=m_msgview->font(); // sane default

  if (which==m_menu_actions[me_Display_Font_Msglist]) {
    current_font=m_qlist->font();
  }
  else if (which==m_menu_actions[me_Display_Font_Msgbody]) {
    current_font=m_msgview->font();
  }
  else if (which==m_menu_actions[me_Display_Font_Tags]) {
    current_font=m_tags_box->font();
  }
  else if (which==m_menu_actions[me_Display_Font_QuickSel]) {
    current_font=m_query_lv->font();
  }
  else if (which==m_menu_actions[me_Display_Font_Menus]) {
    current_font=menuBar()->font();
  }

  QFont f=QFontDialog::getFont(&ok, current_font);
  if (ok) {
    app_config& conf=get_config();
    QAction* all=m_menu_actions[me_Display_Font_All];
    if (which==all || which==m_menu_actions[me_Display_Font_Msglist]) {
      if (!m_pages->empty())
	set_pages_font(1,f);
      else {
	m_qlist->setFont(f);
	m_qAttch->setFont(f);
      }
      conf.set_string("display/font/msglist", f.toString());
    }
    if (which==all || which==m_menu_actions[me_Display_Font_Msgbody]) {
      if (!m_pages->empty()) {
	set_pages_font(2,f);
      }
      else {
	m_msgview->setFont(f);
      }
      if (!m_fetch_on_demand)
	display_msg_contents();	// redisplaying is necessary with Qt2
      conf.set_string("display/font/msgbody", f.toString());
    }
    if (which==all || which==m_menu_actions[me_Display_Font_Tags]) {
      m_tags_box->setFont(f);
      conf.set_string("display/font/tags", f.toString());
    }
    if (which==all || which==m_menu_actions[me_Display_Font_QuickSel]) {
      m_query_lv->setFont(f);
      conf.set_string("display/font/quicksel", f.toString());
    }
    if (which==all || which==m_menu_actions[me_Display_Font_Menus]) {
      menuBar()->setFont(f);
      set_menu_font(f);
      // by extension, the statusbar and toolbar are made to use
      // the same font than the menus
      if (m_ql_search) m_ql_search->setFont(f);
      if (m_toolbar) m_toolbar->setFont(f);
      statusBar()->setFont(f);
      if (m_new_mail_btn) m_new_mail_btn->update_font(f);
      conf.set_string("display/font/menus", f.toString());
    }
    if (which==all) {
      QApplication::setFont(f);
      conf.set_string("display/font/all", f.toString());
    }
  }
}

static void
initf(QFont& f, const QString v)
{
  if (v=="xft") return;
  if (v.at(0)=='-')
    f.setRawName(v);		// for pre-0.9.5 entries
  else
    f.fromString(v);
}

void
msg_list_window::init_fonts()
{
  QFont f;
  app_config& conf=get_config();
  QString fontname;

  fontname=conf.get_string("display/font/all");
  if (!fontname.isEmpty()) {
    initf(f, fontname);
    QApplication::setFont(f);
  }

  fontname=conf.get_string("display/font/msglist");
  if (!fontname.isEmpty()) {
    initf(f, fontname);
    if (!m_pages->empty())
      set_pages_font(1,f);
    else {
      m_qlist->setFont(f);
      m_qAttch->setFont(f);
    }
  }

  fontname=conf.get_string("display/font/msgbody");
  if (!fontname.isEmpty()) {
    initf(f, fontname);
    if (!m_pages->empty()) {
      set_pages_font(2,f);
    }
    else {
      m_msgview->setFont(f);
    }
  }

  fontname=conf.get_string("display/font/tags");
  if (!fontname.isEmpty()) {
    initf(f, fontname);
    m_tags_box->setFont(f);
  }

  fontname=conf.get_string("display/font/quicksel");
  if (!fontname.isEmpty()) {
    initf(f, fontname);
    m_query_lv->setFont(f);
  }

  fontname=conf.get_string("display/font/menus");
  if (!fontname.isEmpty()) {
    initf(f, fontname);
    set_menu_font(f);
  }
}

void
msg_list_window::save_display_settings()
{
  app_config& conf=get_config();

  msgs_page* p = m_pages->current_page();
  if (p) {
    QSplitter* l = p->m_page_splitter;
    QList<int> sizes = l->sizes();
    QList<int>::Iterator it;
    int i=0;
    QString s;
    for (it=sizes.begin(); it != sizes.end(); ++it) {
      s.sprintf("display/msglist/panel%d_size", ++i);
      conf.set_number(s, *it);
    }
  }
  else {
    ERR_PRINTF("no current page");
  }
  conf.set_number("display/msglist/height", height());
  conf.set_number("display/msglist/width", width());

  // if sender and recipient are swapped, we want to save the non-swapped order
  bool col_swap=false;
  if (m_qlist->sender_recipient_swapped()) {
    col_swap=true;
    m_qlist->swap_sender_recipient(false);
  }
  QString colpos;
  for (int visual_idx=0; visual_idx<m_qlist->header()->count(); ++visual_idx) {
    int logical_idx = m_qlist->header()->logicalIndex(visual_idx);
    if (logical_idx==-1) // shouldn't happen
      continue;
    conf.set_number(QString("display/msglist/column%1_size").arg(logical_idx),
		    m_qlist->header()->sectionSize(logical_idx));
    if (!m_qlist->header()->isSectionHidden(logical_idx)) {
      colpos.append('0'+logical_idx);
    }
  }
  conf.set_string("display/msglist/columns_ordering", colpos);
  if (col_swap)
    m_qlist->swap_sender_recipient(true);   // back to the initial state

  conf.set_number("show_tags", display_vars.m_show_tags);
  conf.set_number("show_tags", display_vars.m_show_tags);
  conf.set_number("display_threads", display_vars.m_threaded);
  conf.set_number("show_headers_level", display_vars.m_show_headers_level);
  conf.set_number("display/filters_trace", display_vars.m_show_filters_trace);
  conf.set_number("display/tags_in_headers", display_vars.m_show_tags_in_headers);

  db_cnx db;
  db.begin_transaction();
  if (conf.store("display/")
      && conf.store("show_tags")
      && conf.store("display_threads")
      && conf.store("show_headers_level"))
    {
      db.commit_transaction();
    }
  else {
    // FIXME: the transaction is already aborted with pgsql if a database
    // error has occurred
    db.rollback_transaction();
  }
  QString user_msg;
  if (conf.name().isEmpty())
    user_msg = QString(tr("The display settings have been saved in the default configuration."));
  else
    user_msg = QString(tr("The display settings have been saved in the '%1' configuration.")).arg(conf.name());
  QMessageBox::information(NULL, tr("Confirmation"), user_msg);
}

void
msg_list_window::cycle_headers()
{
  DBG_PRINTF(3, "cycle_headers()");
  // if not "most", then display "most" headers, else display "all" headers
  if (display_vars.m_show_headers_level!=1) {
    show_headers(m_menu_actions[me_Display_Headers_Most]);
    m_menu_actions[me_Display_Headers_Most]->setChecked(true);
  }
  else {
    show_headers(m_menu_actions[me_Display_Headers_All]);
    m_menu_actions[me_Display_Headers_All]->setChecked(true);
  }
}

// Select how much of the mail headers should be displayed
void
msg_list_window::show_headers(QAction* which)
{
  if (which==m_menu_actions[me_Display_Headers_None])
    display_vars.m_show_headers_level=0;
  else if (which==m_menu_actions[me_Display_Headers_Most])
    display_vars.m_show_headers_level=1;
  else if (which==m_menu_actions[me_Display_Headers_All])
    display_vars.m_show_headers_level=2;
  else if (which==m_menu_actions[me_Display_Headers_Raw])
    display_vars.m_show_headers_level=3;
  else if (which==m_menu_actions[me_Display_Headers_RawDec])
    display_vars.m_show_headers_level=4;
  // redisplay the current message
  mails_selected();
}

void
msg_list_window::open_global_notepad()
{
  notepad* n = notepad::open_unique();
  if (n) {
    n->show();
    n->activateWindow();
    n->raise();
  }
}

void
msg_list_window::edit_tags()
{
  tags_dialog* w=NULL;
  try {
    w=new tags_dialog(this);
    if (!w) return;
    connect(w, SIGNAL(tags_restructured()), m_tags_box, SLOT(tags_restructured()));
    w->setWindowModality(Qt::WindowModal);
    w->show();
  }
  catch (ui_error& e) {
    e.display();
    if (w) delete w;
  }
}

void
msg_list_window::start_mailing()
{
  extern void open_mailing_window();
  open_mailing_window();
}

void
msg_list_window::edit_filters()
{
  filter_edit* w = new filter_edit(0);
  CHECK_PTR(w);
#if 0
  std::list<unsigned int> l;
  std::vector<mail_msg*> v_sel;
  m_qlist->get_selected(v_sel);

  if (v_sel.size() > 1) {
    std::vector<mail_msg*>::iterator it;
    for (it=v_sel.begin(); it!=v_sel.end(); ++it) {
      l.push_back((*it)->get_id());
    }
    w->set_sel_list(l);
  }
#endif
  w->show();
}

#if 0				// later
void
msg_list_window::open_address_book()
{
  address_book* w = new address_book;
  CHECK_PTR (w);
  w->show();
}
#endif

void
msg_list_window::action_click_msg_list(const QModelIndex& index)
{
  if (index.column() == mail_item_model::column_note) {
    if (m_pCurrentItem) {
      edit_note();
    }
  }
}

void
msg_list_window::mail_reply_all()
{
  mail_reply(2);
}

void
msg_list_window::mail_reply_list()
{
  mail_reply(3);
}

void
msg_list_window::mail_reply_sender()
{
  mail_reply(1);
}

void
msg_list_window::mail_reply(int whom_to)
{
  if (!m_pCurrentItem)
    return;
  QString text_to_quote = m_msgview->selected_text();

  mail_msg::body_format format = mail_msg::format_plain_text;;
  QString html_to_quote;
  QString fm_rep = get_config().get_string("composer/format_for_replies");

  if (m_msgview->content_type_shown() <= 1) {
    // plain text
    if (text_to_quote.isEmpty()) {
      m_pCurrentItem->find_text_to_quote(text_to_quote);
    }
    if (fm_rep == "text/html") {
      format = mail_msg::format_html;
      if (text_to_quote.isEmpty()) {
	// Extract an HTML fragment containing the selection if
	// there is one, otherwise get the whole body
	text_to_quote = m_msgview->selected_html_fragment();
      }
      else {
	text_to_quote = mail_displayer::htmlize(text_to_quote);
	text_to_quote.replace("\n", "<br>\n");
      }
    }
  }
  else {
    // the current mail is HTML and displayed as such
    if (fm_rep == "text/html" || fm_rep == "same_as_sender") {
      format = mail_msg::format_html;
      if (text_to_quote.isEmpty()) {
	// Extract an HTML fragment containing the selection if
	// there is one, otherwise get the whole body
	text_to_quote = m_msgview->selected_html_fragment();
      }
      else
	text_to_quote = mail_displayer::htmlize(text_to_quote);
    }
    else { /* should be limited to (fm_rep == "text/plain"), but we let's have
	      other cases be treated as text/plain as well */
      // Convert HTML into quoted plain text
      format = mail_msg::format_plain_text;
      if (text_to_quote.isEmpty()) {
	text_to_quote = m_msgview->body_as_text();
      }
    }
  }
  mail_msg msg = m_pCurrentItem->setup_reply(text_to_quote, whom_to, format);

  new_mail_widget* w = new new_mail_widget(&msg, 0);
  if (format == mail_msg::format_html)
    w->format_html_text();
  else
    w->format_plain_text();

  w->show_tags();
  w->insert_signature();
  w->start_edit(); 

  /* A signal will be emitted when the reply is stored, and we connect
     that to a visual update on all our listviews of the original mail
     new status (now replied)  */
  connect(w, SIGNAL(refresh_request(mail_id_t)),
	  this, SLOT(global_refresh_status(mail_id_t)));

  if (w->errmsg().isEmpty()) {
    w->show();
  }
  else {
    delete w;
  }

}

void
msg_list_window::forward()
{
  std::vector<mail_msg*> v_sel;
  m_qlist->get_selected (v_sel);

  if (v_sel.size()==0)
    return;
  if (v_sel.size()>1) {
    QMessageBox::information(this, APP_NAME, tr("Only one message can be forwarded at a time."));
    return;
  }
  mail_msg msg = v_sel[0]->setup_forward();
  new_mail_widget* w = new new_mail_widget(&msg, 0);
  w->insert_signature();
  w->start_edit();
  /* A signal will be emitted when the reply is stored, and we connect
     that to a visual update on all our listviews of the original mail
     new status (now forwarded)  */
  connect(w, SIGNAL(refresh_request(mail_id_t)),
	  this, SLOT(global_refresh_status(mail_id_t)));

  if (w->errmsg().isEmpty()) {
    w->show();
  }
  else {
    delete w;
  }
}

void
msg_list_window::bounce()
{
  if (m_pCurrentItem)
    m_pCurrentItem->bounce();
}

void
msg_list_window::show_tags(bool show)
{
  display_vars.m_show_tags=show;
  if (show)
    m_tags_box->show();
  else
    m_tags_box->hide();
}

/*
  if b=false, disable all widgets and record previous state
  if b=true, restore previous state
  only one state is recorded (no stack)
*/
void
msg_list_window::enable_interaction(bool b)
{
  QWidget* w[] = {
    m_qAttch, menuBar(), m_toolbar, m_new_mail_btn,
    m_tags_box, m_query_lv, m_qlist, m_ql_search, m_msgview
  };
  QAction* actions[] = {
    m_action_search  // search toolbar
  };

  if (!b) {
    m_widgets_enable_state.clear();
    m_actions_enable_state.clear();
  }
  // Widgets
  for (uint i=0; i<sizeof(w)/sizeof(w[0]); i++) {
    if (!b) {
      m_widgets_enable_state.push_back(w[i]->isEnabled());
      w[i]->setEnabled(false);
    }
    else {
      w[i]->setEnabled(m_widgets_enable_state.takeFirst());
    }
  }
  // Actions
  for (uint i=0; i<sizeof(actions)/sizeof(actions[0]); i++) {
    if (!b) {
      m_actions_enable_state.push_back(actions[i]->isEnabled());
      actions[i]->setEnabled(false);
    }
    else {
      actions[i]->setEnabled(m_actions_enable_state.takeFirst());
    }
  }
}

void
msg_list_window::attch_selected(QTreeWidgetItem* p, int column _UNUSED_)
{
  attch_lvitem* pItem=dynamic_cast<attch_lvitem*>(p);
  attachment* pa = pItem->get_attachment();
  if (!pa) {			// if it's not an attachment, it's a note
    edit_note();
    return;
  }
  if (pa->mime_type()=="message/rfc822" || pa->mime_type()=="text/rfc822-headers") {
    const char* contents = pa->get_contents();
    if (contents) {
      mime_msg_viewer* v = new mime_msg_viewer(contents, display_vars);
      v->show();
    }
  }
  else {
    if (!pa->application().isEmpty()) {
      // launch helper application (MIME viewer)
      install_progressbar(m_qAttch);
      QString tmpname = pa->get_temp_location();
      statusBar()->showMessage(tr("Downloading attached file: %1").arg(tmpname));
      pItem->download(tmpname, &m_abort);
      if (!m_abort) {
	DBG_PRINTF(5,"launch_external_viewer");
	pa->launch_external_viewer(tmpname);
      }
      uninstall_progressbar(m_qAttch);
    }
    else {
      // save attachment file
      QString fname = pa->filename();
      if (m_last_attch_dir.isEmpty()) {
	m_last_attch_dir = get_config().get_string("attachments_directory");
      }
      if (!m_last_attch_dir.isEmpty()) {
	fname = m_last_attch_dir + "/" + fname;
      }
//      DBG_PRINTF(5, "fname=%s", fname.latin1());
      fname = QFileDialog::getSaveFileName(this, tr("Save File"), fname);
      if (!fname.isEmpty() && confirm_write(fname)) {
	install_progressbar(m_qAttch);
	statusBar()->showMessage(tr("Downloading attached file: %1").arg(fname));
	bool m_abort=false;
	QString dir=pItem->save_to_disk(fname, &m_abort);
	if (!dir.isEmpty())
	  m_last_attch_dir = dir;
	uninstall_progressbar(m_qAttch);
      }
    }
  }
}

bool
msg_list_window::progress_aborted()
{
  return m_abort;
}

void
msg_list_window::install_progressbar(QWidget* widget)
{
  m_new_mail_btn->hide();
  m_progress_bar = new QProgressBar(this);
  statusBar()->addPermanentWidget(m_progress_bar);
  connect(widget, SIGNAL(progress(int)), this, SLOT(show_progress(int)));
  enable_interaction(false);
  show_abort_button();
}

void
msg_list_window::show_abort_button()
{
  m_abort=false;
  m_abort_button = new QPushButton(tr("Abort"), this);
  m_abort_button->setIcon(UI_ICON(ICON16_CANCEL));
  statusBar()->addPermanentWidget(m_abort_button);
  connect(m_abort_button, SIGNAL(clicked()), this, SLOT(abort_operation()));
}

void
msg_list_window::hide_abort_button()
{
  if (m_abort_button) {
    delete m_abort_button;
    m_abort_button=NULL;
  }
  m_abort=false;
}

void
msg_list_window::uninstall_progressbar(QWidget* widget)
{
  hide_abort_button();
  enable_interaction(true);
  disconnect(widget, SIGNAL(progress(int)), this, SLOT(show_progress(int)));
  if (m_progress_bar) {
    delete m_progress_bar;
    m_progress_bar=NULL;
  }
  statusBar()->clearMessage();
  m_new_mail_btn->show();
}

void
msg_list_window::abort_operation()
{
  m_abort=true;
  if (m_waiting_for_results) {	// query in progress
    m_waiting_for_results = false;
    m_thread.cancel();
    m_thread.release();
    hide_abort_button();
    enable_interaction(true);
    m_new_mail_btn->show();
    unsetCursor();
    statusBar()->showMessage(tr("Query cancelled."));
  }
}

void
msg_list_window::new_mail()
{
  mail_msg msg;;
  new_mail_widget* w = new new_mail_widget(&msg, 0);
  if (w->errmsg().isEmpty()) {
    QString fm_rep = get_config().get_string("composer/format_for_new_mail");
    if (fm_rep == "text/html") {
      w->format_html_text();
    }
    w->show();
    w->insert_signature();
    w->start_edit();
  }
  else {
    delete w;
  }
}

/*
  The selection of messages
*/
bool
msg_list_window::want_new_window() const
{
  QString mode=get_config().get_string("preferred_open_mode");
  return (mode=="new_window")?true:false;
}

void
msg_list_window::new_list()
{
  bool new_window=want_new_window();
  msg_select_dialog* w=new msg_select_dialog(new_window);
  if (!new_window) {
    connect(w,SIGNAL(fetch_done(msgs_filter*)), this,
	    SLOT(fill_fetch_new_page(msgs_filter*)));
  }
  w->show();
}

void
msg_list_window::sel_filter(const msgs_filter& f)
{
  setCursor(Qt::WaitCursor);
//  m_new_mail_btn->hide();
  show_abort_button();
  enable_interaction(false);
  statusBar()->showMessage(tr("Querying database..."));

  m_loading_filter = new msgs_filter(f);
  int r = m_loading_filter->asynchronous_fetch(&m_thread);
  if (r==1) {
    m_waiting_for_results = true;
  }
  else if (r==0) {
    QMessageBox::information(this, APP_NAME, tr("Fetch error"));
  }
  else if (r==2) {
    QMessageBox::information(this, APP_NAME, tr("No results"));
  }
}

void
msg_list_window::sel_save_query()
{
  extern void save_filter_query(msgs_filter*, int,const QString);
  save_filter_query(m_filter, 0, QString::null);
  m_query_lv->reload_user_queries();
}

#if 0
void msg_list_window::sel_header_analysis()
{
  std::list<unsigned int> l;
  std::vector<mail_msg*> v_sel;
  m_qlist->get_selected(v_sel);

  if (v_sel.size() <= 1) {
    // If there is zero or one item selected, the whole page is taken
    // as the data source...
    msgs_filter::mlist_t::iterator it;
    for (it=m_filter->m_list_msgs.begin(); it!=m_filter->m_list_msgs.end(); ++it) {
      l.push_back((*it)->get_id());
    }
  }
  else {
    // ... else only the selected items are taken
    std::vector<mail_msg*>::iterator it;
    for (it=v_sel.begin(); it!=v_sel.end(); ++it) {
      l.push_back((*it)->get_id());
    }
  }
  extern void analyze_headers(const std::list<unsigned int>&);
  analyze_headers(l);
}
#endif

void
msg_list_window::sel_sent()
{
  msgs_filter f;
  f.m_status_set = mail_msg::statusOutgoing;
  sel_filter(f);
  store_quick_sel(query_lvitem::virtfold_sent);
}

void
msg_list_window::sel_trashcan()
{
  msgs_filter f;
  f.m_in_trash=true;
  f.m_status_set = mail_msg::statusTrashed;
  sel_filter(f);
  store_quick_sel(query_lvitem::virtfold_trashcan);
}

/* keep a reference to the entry in the quick selection listview
   for later use */
void
msg_list_window::store_quick_sel(query_lvitem::item_type type, uint tag_id)
{
  int id = m_query_lv->highlight_entry(type, tag_id);
  msgs_page* current = m_pages->current_page();
  if (current) {
    DBG_PRINTF(5, "store_quick_sel id=%d", id);
    current->m_query_lvitem_id = id;
  }
}

void
msg_list_window::new_messages()
{
  msgs_filter f;
  f.m_status=0;
  f.set_auto_refresh();
  sel_filter(f);
  store_quick_sel(query_lvitem::new_all);
}

void msg_list_window::sel_tag(const QString tagname)
{
  msgs_filter f;
  f.m_tag_name = tagname;
  sel_filter(f);
}

void msg_list_window::sel_tag(uint tag_id)
{
  DBG_PRINTF(5, "sel_tag(%d)", tag_id);
  msgs_filter f;
  f.m_tag_id = tag_id;
  sel_filter(f);
  //  store_quick_sel(query_lvitem::tagged, tag_id);
}

void msg_list_window::sel_tag_status(unsigned int tag_id, int status_set,
				     int status_unset)
{
  DBG_PRINTF(5, "sel_tag_status(%d)", tag_id);
  msgs_filter f;
  //  f.m_tag_name = tagname;
  f.m_tag_id=tag_id;
  f.m_status_set = status_set;
  f.m_status_unset = status_unset;
  sel_filter(f);
  if (status_set==0 && status_unset==(mail_msg::statusArchived|mail_msg::statusTrashed)) {
    //    store_quick_sel(query_lvitem::current_tagged, tag_id);
  }
}

void
msg_list_window::non_processed_messages()
{
  msgs_filter f;
  f.m_status_unset = mail_msg::statusArchived|mail_msg::statusTrashed;
  f.set_auto_refresh();
  sel_filter(f);
}

void
msg_list_window::sel_refine()
{
  msg_select_dialog* w = new msg_select_dialog(false);
  w->filter_to_dialog(m_filter);
  w->show();
  connect(w,SIGNAL(fetch_done(msgs_filter*)), this, SLOT(fill_fetch(msgs_filter*)));
}

void
msg_list_window::fetch_more()
{
  setCursor(Qt::WaitCursor);
//  m_new_mail_btn->hide();
  show_abort_button();
  enable_interaction(false);
  statusBar()->showMessage(tr("Querying database..."));

  m_filter->m_fetch_results=NULL;
  int r = m_filter->asynchronous_fetch(&m_thread, true);
  DBG_PRINTF(8, "after async_fetch m_filter->results as %d elements", m_filter->m_list_msgs.size());
  if (r==1) {
    m_waiting_for_results = true;
  }
  else if (r==0) {
    QMessageBox::information(this, APP_NAME, tr("Fetch error"));
  }
  else if (r==2) {
    QMessageBox::information(this, APP_NAME, tr("No results"));
  }
}

void
msg_list_window::fill_fetch(msgs_filter* f)
{
  DBG_PRINTF(5,"fill_fetch()");

  // clear old contents
  m_qlist->clear();
  m_filter->m_list_msgs.clear();
  m_msgview->clear();
  m_qAttch->hide();

  // show new contents
  *m_filter=*f;
  m_filter->make_list(m_qlist);
  set_title();
}

void
msg_list_window::fill_fetch_new_page(msgs_filter* f)
{
  DBG_PRINTF(5,"fill_fetch_new_page()");
  clear_quick_query_selection();
  add_msgs_page(f, false);
  // Remove any highlighting in the quick query selection to avoid confusion
  // between a user query and and a selection branch
}


void
msg_list_window::goto_next_message()
{
  std::vector<mail_msg*> v_sel;
  m_qlist->get_selected(v_sel);
  if (v_sel.size()>0) {
    m_qlist->select_below(v_sel.back());
  }
  else {
    mail_msg* first = m_qlist->first_msg();
    if (first) {
      m_qlist->select_msg(first);
    }
  }
}

void
msg_list_window::goto_previous_message()
{
  std::vector<mail_msg*> v_sel;
  m_qlist->get_selected(v_sel);
  if (v_sel.size()>0)
    m_qlist->select_above(v_sel.front());
  else {
    mail_msg* first = m_qlist->first_msg();
    if (first) {
      m_qlist->select_msg(first);
    }
  }    
}

void
msg_list_window::sel_bottom()
{
  m_qlist->scrollToBottom();
}

void
msg_list_window::sel_refresh_list()
{
  m_filter->fetch(m_qlist);
  set_title();
  if (m_filter->auto_refresh()) {
//    m_new_mail_btn->enable(false);
    statusBar()->clearMessage();
  }
}

/* Refresh the current list of messages, but only if it's bound to a
   filter in auto-refresh mode */
void
msg_list_window::sel_auto_refresh_list()
{
  if (m_filter->auto_refresh()) {
    m_filter->fetch(m_qlist);
    set_title();
  }
}

void
msg_list_window::sel_refresh()
{
  sel_refresh_list();
  m_query_lv->refresh();
}

void msg_list_window::sel_import()
{
  // TODO
}

void
msg_list_window::about()
{
  about_box* b = new about_box(NULL);
  b->show();
}

void
msg_list_window::focus_on_msglist()
{
  m_qlist->setFocus();
}

void
msg_list_window::open_help()
{
  helper::show_help("help");
}

void
msg_list_window::dynamic_help()
{
  // auto-track ON = menu item checked
  helper::auto_track(m_menu_actions[me_Help_Dynamic]->isChecked());
}

void
msg_list_window::close_window()
{
  close();
}

void
msg_list_window::new_window()
{
  msgs_filter f;
  f.m_sql_stmt="0";		// FIXME (empty filter)
  msg_list_window* w=new msg_list_window(&f);
  w->show();
}

void
msg_list_window::preferences()
{
  prefs_dialog* w = new prefs_dialog;
  w->show();
}

void
msg_list_window::find_text()
{
  if (!m_wSearch) {
    m_wSearch=new search_box(NULL);
    connect(m_wSearch, SIGNAL(mail_find(const QString&, int, int)),
	    SLOT(search_generic(const QString&, int, int)));
    connect (m_wSearch, SIGNAL(search_closed()),
	     SLOT(search_finished()));
  }
  m_wSearch->show();
}

// stop highlighting the searched text found in the body
void
msg_list_window::search_finished()
{
  m_highlighted_text.clear();
  if (!m_fetch_on_demand) {
    m_msgview->clear();
    display_body();
  }
}

void
msg_list_window::save_body()
{
  // get the mails whose body we want to save
  std::vector<mail_msg*> v_sel;
  m_qlist->get_selected(v_sel);
  if (v_sel.size()==0)
    return;			// shouldn't happen

  QString filename=QFileDialog::getSaveFileName(this, tr("Filename"),
						QString(), QString("*.txt"));
  if (filename.isEmpty())
    return;
  QByteArray filename_qba = filename.toLocal8Bit();
  std::ofstream of(filename_qba.constData(), std::ios::out);

  // stream the bodies to the file
  std::vector<mail_msg*>::iterator iter;
  for (iter=v_sel.begin(); iter!=v_sel.end(); ++iter) {
    (*iter)->streamout_body(of);
  }
  of.close();
}

void
msg_list_window::edit_body()
{
  if (!m_pCurrentItem)
    return;
  body_edit* be = new body_edit(NULL);
  be->set_contents(m_pCurrentItem->get_id(), m_pCurrentItem->get_body_text(false));
  be->resize(600,500);
  connect(be, SIGNAL(text_updated(uint,const QString*)), this,
	  SLOT(body_edited(uint,const QString*)));
  be->show();
}

void
msg_list_window::body_edited(uint mail_id, const QString* new_text)
{
  mail_msg* p = m_qlist->find(mail_id);
  if (p) {
    p->set_body_text(*new_text);
    if (p==m_pCurrentItem) {
      display_body();		// refresh
    }
  }
  else {
   DBG_PRINTF(2, "body_edited: mail_msg not found");
  }
}

#if 0
void
msg_list_window::save_to_mbox()
{
  std::vector<mail_lvitem*> v_sel;
  m_qlist->get_selected(v_sel);
  if (v_sel.size()==0)
    return;

  QString filename=QFileDialog::getSaveFileName(this, tr("Save mailbox"));
  if (filename.isEmpty())
    return;
  std::ofstream of(filename, std::ios::out|std::ios::app);

  for (int i=0; i<v_sel.size(); i++) {
    v_sel[i]->streamout_mbox(of);
  }
  of.close();
}
#endif

void
msg_list_window::view_attachment()
{
  std::vector<mail_msg*> v_sel;
  m_qlist->get_selected(v_sel);
  if (v_sel.size()!=1)		// act upon one and only one message
    return;

#if 0
  attch_lvitem* item = static_cast<attch_lvitem*>(m_qAttch->firstChild());
  while (item) {
    if (item->is_note())
      continue;
    if (item->isSelected())
      break;
    item = static_cast<attch_lvitem*>(item->itemBelow());
  }
  if (!item)
    return;
#else
  attch_lvitem* item=dynamic_cast<attch_lvitem*>(m_qAttch->currentItem());
  if (!item || item->is_note()) return;
#endif
  attachment* a = item->get_attachment();

  QTextEdit* qe = new QTextEdit(NULL);
  qe->setReadOnly(true);
  if (a->get_contents())
    qe->setPlainText(a->get_contents());
  qe->resize(400,200);
  qe->show();
}

bool
msg_list_window::confirm_write(const QString fname)
{
  if (QFile::exists(fname)) {
    int res = QMessageBox::question(this, QObject::tr("Please confirm"), QObject::tr("A file '%1' already exists. Overwrite?").arg(fname), QObject::tr("&Yes"), QObject::tr("&No"), QString::null, 0, 1);
    if (res==1)
      return false;
  }
  return true;
}

void
msg_list_window::save_attachment()
{
  // list of attachments with a filename
  std::list<attch_lvitem*> list_name;
  // list of attachments without a filename
  std::list<attch_lvitem*> list_noname;

  QList<QTreeWidgetItem*> list = m_qAttch->selectedItems();
  QList<QTreeWidgetItem*>::const_iterator itw = list.begin();
  for (; itw!=list.end(); ++itw) {
    attch_lvitem* item = static_cast<attch_lvitem*>(*itw);
    if (item->is_note())
      continue;
    if (!item->get_attachment()->filename().isEmpty())
      list_name.push_back(item);
    else
      list_noname.push_back(item);
  }
  if (list_name.empty() && list_noname.empty()) {
    QMessageBox::warning(this, APP_NAME, tr("Please select one or several attachments"));
    return;
  }

  QString fname;
  QString dir=m_last_attch_dir;
  if (dir.isEmpty()) {
    dir = get_config().get_string("attachments_directory");
  }
  std::list<attch_lvitem*>::iterator it;
  if (list_name.size()>1) {
    // several named attachments: ask only for the directory
    dir = QFileDialog::getExistingDirectory(this, tr("Save to directory..."), dir);
    if (!dir.isEmpty()) { // if accepted
      for (it=list_name.begin(); it!=list_name.end(); ++it) {
	attachment* pa = (*it)->get_attachment();
	fname = dir + "/" + pa->filename();
	if (!confirm_write(fname))
	  continue;
	install_progressbar(m_qAttch);
	//DBG_PRINTF(6, "downloading %s", fname.latin1());
	statusBar()->showMessage(tr("Downloading attachment into: %1").arg(fname));
	dir=(*it)->save_to_disk(fname, &m_abort);
	uninstall_progressbar(m_qAttch);
	if (m_abort)
	  break;
      }
    }
    else
      return;			// cancel
  }
  if (list_name.size()==1) {
    it=list_name.begin();
    attachment* pa = (*it)->get_attachment();
    fname = pa->filename();
    if (!dir.isEmpty()) {
      fname = dir + "/" + fname;
    }
    //DBG_PRINTF(5, "fname=%s", fname.latin1());
    fname = QFileDialog::getSaveFileName(this, tr("File"), fname);
    if (!fname.isEmpty() && confirm_write(fname)) {
      install_progressbar(m_qAttch);
      statusBar()->showMessage(tr("Downloading attached file: %1").arg(fname));
      dir=(*it)->save_to_disk(fname, &m_abort);
      uninstall_progressbar(m_qAttch);
    }
  }
  if (!list_noname.empty()) {
    for (it=list_noname.begin(); it!=list_noname.end(); ++it) {
      fname = QFileDialog::getSaveFileName(this, tr("File"), dir);
      if (!fname.isEmpty()) {
	install_progressbar(m_qAttch);
	statusBar()->showMessage(tr("Downloading attachment into: %1").arg(fname));
	dir=(*it)->save_to_disk(fname, &m_abort);
	uninstall_progressbar(m_qAttch);
      }
      else
	break;
    }
  }
  if (!dir.isEmpty())
    m_last_attch_dir=dir;
}

// slot
void
msg_list_window::show_progress(int progress)
{
  DBG_PRINTF(5, "show_progress(%d)", progress);
  // when negative, progress is the total number of steps expected
  if (progress<0) {
    m_progress_bar->setMaximum(-progress);
  }
  else {
    m_progress_bar->setValue(progress);
  }
  gl_pApplication->processEvents();
  m_progress_bar->show();
  m_progress_bar->repaint();
  statusBar()->repaint();
  QApplication::flush();
}

void
msg_list_window::sender_properties()
{
  std::vector<mail_msg*> v_sel;
  m_qlist->get_selected(v_sel);
  if (v_sel.size()>1) {
    QMessageBox::warning(NULL, "Error", tr("Please select one message only"));
  }
  else if (v_sel.size()==0) {
    QMessageBox::warning(NULL, "Error", tr("Please select a message"));
  }
  else {
    if (!v_sel[0]->From().isEmpty()) {
      void open_address_book(QString email);
      open_address_book(v_sel[0]->From());
    }
    else {
      QMessageBox::warning(NULL, "Error", tr("The message has no sender!"));
    }
  }
}

void
msg_list_window::msg_print()
{
  std::vector<mail_msg*> v_sel;
  m_qlist->get_selected (v_sel);
  if (v_sel.size()!=1) {
    QMessageBox::warning(NULL, "Error", tr("Please select a message to print"));
    return;
  }
  m_msgview->print();
}

// Edit the current message's private note
void
msg_list_window::edit_note()
{
  if (!m_pCurrentItem)
    return;
  if (!m_pCurrentItem->fetchNote())
    return;
  note_widget* w=new note_widget(this);
  QString initial_note = m_pCurrentItem->getNote();
  w->set_note_text (initial_note);
  int ret=w->exec();
  if (ret && w->get_note_text() != initial_note) {
    m_pCurrentItem->set_note(w->get_note_text());
    m_pCurrentItem->store_note();
    display_msg_note();
    m_qlist->update_msg(m_pCurrentItem); // visual update
  }
  w->close();
}

/*
  Update the note entry in the attachments & note's listview
  There are 4 cases:
  1. no note in db/no note in listview => nothing to do
  2. no note in db/a note in listview => remove it
  3. a note in db/no note in listview => add it
  4. a note in db/a note in listview => update the text
*/
void
msg_list_window::display_msg_note()
{
  if (!m_pCurrentItem || !m_qAttch)
    return;
  m_pCurrentItem->fetchNote();
  QString n = m_pCurrentItem->getNote();
  uint index=0;
  attch_lvitem* lvpItem = dynamic_cast<attch_lvitem*>(m_qAttch->topLevelItem(0));
  while (lvpItem) {
    if (!lvpItem->get_attachment()) {
      break;
    }
    index++;
    lvpItem = dynamic_cast<attch_lvitem*>(m_qAttch->topLevelItem(index));
  }

  if (n.isEmpty()) {
    if (lvpItem) {		// case 2
      delete lvpItem;
      lvpItem=NULL;
    }
  }
  else {
    if (!lvpItem) {		// case 3
      lvpItem = new attch_lvitem(m_qAttch, NULL);
    }
    // case 3 & 4
    lvpItem->set_note(n);
    lvpItem->fill_columns();
  }
  // don't show the attachments & note's listview when it's empty
  if (m_qAttch->topLevelItem(0)) {
    m_qAttch->show();
#if QT_VERSION<0x040400
    /* Qt-4.3 has a bug that zeroes the attachments panel's size when
       it's hidden and the user moves the splitter betweens msgs list
       and body. As a workaround, we force here a non-zero size */
    QSplitter* sp = (QSplitter*)m_qAttch->parent();
    if (sp->sizes().at(2)==0) {
      QList<int> sz = sp->sizes();
      sz.replace(2, 10); // arbitrary height of 10, the actual size on screen will be larger
      sp->setSizes(sz);
    }
#endif
  }
  else
    m_qAttch->hide();
}

void
msg_list_window::closeEvent(QCloseEvent *e)
{
  if (m_wSearch)
    m_wSearch->close();
  e->accept();
}

void
msg_list_window::search_generic(const QString& text, int where, int options)
{
  bool found=FALSE;
  bool wrapped=FALSE;

  static mail_msg* msg_last_hit = NULL;
  static QString string_last_hit;
  mail_msg* cur_msg = NULL;

  std::vector<mail_msg*> v_sel;
  m_qlist->get_selected(v_sel);

  /* if one (and only one) message is selected, start the search from
     this one */
  if (v_sel.size() == 1) {
    cur_msg = v_sel[0];
    /* If the search is to start from the same message where we found a hit
       last time, and the text searched for is the same,
       then we start from the next message instead.
       This is an implicit "Find next". */
    if (cur_msg==msg_last_hit && text==string_last_hit) {
      cur_msg = m_qlist->nearest_msg(cur_msg, 1); // below
      if (!cur_msg)
	cur_msg=m_qlist->first_msg(); // wrap around
    }
  }
  else {
    /* else start from the beginning of the list */
    cur_msg = m_qlist->first_msg();
  }

  if (cur_msg) {
    DBG_PRINTF(3, "search starts at msg #%d", cur_msg->get_id());
  }
  else {
    DBG_PRINTF(3, "no first message in the list");
    return;			// looks like there's no message at all
  }


  Qt::CaseSensitivity cs=((options&FT::caseInsensitive)==0)?Qt::CaseSensitive:Qt::CaseInsensitive;

  {
    const QCursor cursor(Qt::WaitCursor);
    QApplication::setOverrideCursor(cursor);

    mail_msg* start_msg=cur_msg;

    while (!found && cur_msg) {
      if (where & FT::searchInSubjects) {
	found=(cur_msg->Subject().indexOf(text, 0, cs)>=0);
      }
      if (!found && (where & FT::searchInHeaders)) {
	found=(cur_msg->get_headers().indexOf(text, 0, cs)>=0);
      }
      if (!found && (where & FT::searchInBodies)) {
	found=(cur_msg->get_body_text().indexOf(text, 0, cs)>=0);
      }
      if (!found) {
	cur_msg = m_qlist->nearest_msg(cur_msg, 1);	// below
	if (cur_msg==start_msg)	// all done
	  cur_msg=NULL;
	else if (cur_msg==NULL && !wrapped) {
	  wrapped=TRUE;
	  cur_msg=m_qlist->first_msg(); // wrap around
	}
      }
    }
    QApplication::restoreOverrideCursor();
  }

  if (found && cur_msg) {
    string_last_hit=text;

    m_highlighted_text.clear();
    searched_text s;
    s.m_text=text;
    s.m_is_cs = ((options&FT::caseInsensitive)==0);
    s.m_is_word=false;
    m_highlighted_text.push_back(s);
    m_highlightedCaseSensitive = s.m_is_cs;
    
    m_qlist->select_msg(cur_msg);
    msg_last_hit=cur_msg;
  }
  else {
    string_last_hit = "";
    m_highlighted_text.clear();
    msg_last_hit=NULL;
    QString msg = "'" + text + "' not found.";
    QMessageBox::information (this, APP_NAME, msg);
  }
}

// Slot. To be called when the selection of messages changes
void
msg_list_window::mails_selected()
{
  if (m_ignore_selection_change)
    return;
  std::vector<mail_msg*> v;
  m_qlist->get_selected(v);
  DBG_PRINTF(5, "%u mails are selected", v.size());
  if (v.size()==1) {
    mail_selected(v[0]);
  }
  else {
    m_msgview->clear();
    m_qAttch->hide();
  }
  enable_commands();
}



/* Contextual popup menu on body view */
void
msg_list_window::body_menu()
{
  QMenu qmenu(this);
  QAction* actions[] = { 
    m_action_move_backward,
    m_action_move_forward,
    m_action_reply_sender,
    m_action_reply_all,
    m_action_reply_list
  };
  for (unsigned int i=0; i<sizeof(actions)/sizeof(actions[0]); i++) {
    qmenu.addAction(actions[i]);
  }
  qmenu.exec(QCursor::pos());
}

/* Display body and attachments */
void
msg_list_window::display_msg_contents()
{
  if (!m_pCurrentItem) {
    m_msgview->clear();
    return;
  }

  display_body();

  // display attachments
  m_qAttch->clear();

  attachments_list& attchs=m_pCurrentItem->attachments();
  if (m_pCurrentItem->has_attachments()) {
    attchs.fetch();
  }

  if (attchs.size()) {
    attachments_list::iterator iter;
    attch_lvitem* last_item=NULL; // used to insert the attachments in the same order as they're in the database
    for (iter=attchs.begin(); iter!=attchs.end(); ++iter) {
      attch_lvitem* lvpItem = new attch_lvitem(m_qAttch, last_item, &(*iter));
      last_item = lvpItem;
      lvpItem->fill_columns();
    }
  }
  display_msg_note();
}

void
msg_list_window::display_selection_tags()
{
  // TODO: display tags intersection when several messages are selected
  if (m_pCurrentItem) {
    m_tags_box->set_tags(m_pCurrentItem->get_tags());
  }
}

// Called when and if only one message gets selected
void
msg_list_window::mail_selected(mail_msg* msg)
{
  m_pCurrentItem=msg;
  if (!msg) {
    DBG_PRINTF(6, "mail_selected: null msg");
    return;
  }
  DBG_PRINTF(5,"mail_selected: %d", msg->GetId());
  m_qlist->refresh(msg->get_id()); // will update from database
  // display body
  m_msgview->set_mail_item(msg);
  if (!m_fetch_on_demand) {
    display_msg_contents();
  }
  else {
    m_msgview->set_show_on_demand(true);
    m_qAttch->clear();
    m_qAttch->hide();
  }

  // show tags
  if (!m_fetch_on_demand)
    display_selection_tags();

  // set status
  // we don't change the status of a message that is an
  // attachment to a database message: we would have no way of saving
  // this information.
  // also we don't update the status in fetch on demand mode
  if (!(msg->status() & mail_msg::statusAttached) && !m_fetch_on_demand) {
    uint known_status=msg->status();
    // update in memory
    msg->set_status(msg->status() | mail_msg::statusRead);
    // update in database
    msg->update_status();
    // show for all views
    if (known_status != msg->status()) {
      m_qlist->update_msg(msg);
      propagate_status(msg);
    }
  }
}

void
msg_list_window::msg_archive()
{
  DBG_PRINTF(5, "msg_archive");
  std::vector<mail_msg*> v;
  m_qlist->get_selected(v);
  if (v.empty()) {
    DBG_PRINTF(5,"no mail selected");
    return;
  }
  else if (v.size()==1) {
    DBG_PRINTF(5, "emit mail_chg_status processed 0x%p", v[0]);
    emit mail_chg_status(mail_msg::statusArchived+mail_msg::statusRead ,v[0]);
  }
  else {
    DBG_PRINTF(5,"%d mails selected", v.size());
    emit mail_multi_chg_status(mail_msg::statusArchived+mail_msg::statusRead, &v);
  }
}

void
msg_list_window::clear_quick_query_selection()
{
  m_query_lv->clear_selection();
}

void
msg_list_window::quick_query_selection(QTreeWidgetItem* qt_item, int column)
{
  Q_UNUSED(column);
  DBG_PRINTF(5, "quick_query_selection");

  query_lvitem* item = (query_lvitem*)qt_item;
  if (!item) {
    DBG_PRINTF(1, "no item selected!");
    return;
  }
  switch(item->m_type) {
  case query_lvitem::new_all:
    new_messages();
    break;
  case query_lvitem::new_not_tagged:
    {
      msgs_filter f;
      f.m_status=0;
      f.m_tag_name=("(No tag set)");
      sel_filter(f);
    }
    break;
  case query_lvitem::nonproc_all:
    non_processed_messages();
    break;
  case query_lvitem::nonproc_not_tagged:
    {
      msgs_filter f;
      f.m_status_unset = mail_msg::statusArchived|mail_msg::statusTrashed;
      f.m_tag_name=("(No tag set)");
      sel_filter(f);
    }
    break;
  case query_lvitem::current_prio:
    {
      msgs_filter f;
      f.m_min_prio = 1;		// TODO: user definable?
      f.m_status_unset = mail_msg::statusArchived|mail_msg::statusTrashed;
      sel_filter(f);
    }
    break;
  case query_lvitem::archived_tagged:
    {
      query_tag_lvitem* ti = dynamic_cast<query_tag_lvitem*>(item);
      if (ti && ti->m_tag_id)
	sel_tag_status(ti->m_tag_id, mail_msg::statusArchived, 0);
    }
    break;
  case query_lvitem::current_tagged:
    {
      query_tag_lvitem* ti = dynamic_cast<query_tag_lvitem*>(item);
      if (ti && ti->m_tag_id)
	sel_tag_status(ti->m_tag_id, 0, mail_msg::statusArchived|mail_msg::statusTrashed);
    }
    break;
  case query_lvitem::virtfold_sent:
    sel_sent();
    break;
  case query_lvitem::virtfold_trashcan:
    sel_trashcan();
    break;
  case query_lvitem::user_defined:
    {
      msgs_filter f;
      f.m_sql_stmt = item->m_sql;
      sel_filter(f);
    }
    break;
  case query_lvitem::tree_node:
    break;			// nothing to do
  default:
    QMessageBox::warning(this, APP_NAME, tr("Not implemented yet!"));
    break;
  }
}

void
msg_list_window::check_new_mail()
{
  try {
    statusBar()->showMessage(tr("Checking for new mail..."));
    sql_query q;
    m_filter->build_query(q);
#ifdef WITH_PGSQL
    PGconn* c=GETDB();
    QString s=q.get();
    QByteArray qb = s.toLatin1();
    const char* query=qb.constData();
    DBG_PRINTF(5,"%s", query);
    PGresult* res = PQexec(c, query);
    m_auto_refresh_results.clear();
    m_filter->load_result_list(res, &m_auto_refresh_results);
    if (res) PQclear(res);
#endif // WITH_PGSQL
    if (!m_auto_refresh_results.empty()) {
      DBG_PRINTF(5, "non empty refresh result list");
      // check if any of these results is new
      std::list<mail_result>::iterator iter = m_auto_refresh_results.begin();
      while (iter != m_auto_refresh_results.end()) {
	if (m_qlist->find(iter->m_id)!=NULL) {
	  // remove the messages that are already in m_filter
	  iter = m_auto_refresh_results.erase(iter);
	}
	else
	  ++iter;
      }
    }
    if (!m_auto_refresh_results.empty()) {
//      m_new_mail_btn->set_number(m_auto_refresh_results.size());
//      m_new_mail_btn->enable(true);
      statusBar()->showMessage(tr("New mail is available."));
    }
    else {
//      m_new_mail_btn->enable(false);
      statusBar()->showMessage(tr("No new mail."), 3000);
    }
  }
  catch (int) {
    statusBar()->showMessage(tr("Error while checking for new mail."));
    return;
  }
}

void
msg_list_window::show_status_message(const QString& msg)
{
  if (msg.isEmpty())
    statusBar()->clearMessage();
  else
    statusBar()->showMessage(msg);
}

void
msg_list_window::blip_status_message(const QString& msg)
{
  if (msg.isEmpty())
    statusBar()->clearMessage();
  else
    statusBar()->showMessage(msg, 3000);
}

void
msg_list_window::timer_func()
{
  m_timer_ticks++;

  // Check if we got results from a fetch
  if (m_waiting_for_results && m_thread.isFinished()) {
    DBG_PRINTF(5, "End of asynchronous fetch detected in timer_func()");
    m_waiting_for_results = false;

    enable_interaction(true);

    if (m_thread.m_fetch_more) { // FIXME: use a better abstraction
      // this is a "fetch more" operation. It uses the current filter (m_filter)
      m_filter->postprocess_fetch(m_thread);
      DBG_PRINTF(8, "fetch_more -> make_list");
      m_filter->make_list(m_qlist);
      DBG_PRINTF(8, "after async_fetch m_filter->results as %d elements", m_filter->m_list_msgs.size());
      set_title();
    }
    else if (m_loading_filter && m_loading_filter->m_fetch_results) {
      // this is a fetch for a new list of results. It uses a temporary filter
      m_loading_filter->postprocess_fetch(m_thread);
      if (want_new_window()) {
	msg_list_window* w = new msg_list_window(m_loading_filter, 0);
	w->show();
      }
      else {
	add_msgs_page(m_loading_filter, false); // will instantiate m_filter
      }
    }

    m_thread.release();

    unsetCursor();
    hide_abort_button();
//    m_new_mail_btn->show();

#if 0
    if (m_loading_filter->exec_time() < 0) {
      statusBar()->showMessage(tr("Query failed."));
    }
    else
#endif
    {
      double exec_time = m_thread.m_exec_time/1000.0; // in seconds
      statusBar()->showMessage(tr("Query executed in %1 s.").arg(exec_time, 0, 'f', 2),3000);
    }
    if (m_loading_filter) {
      delete m_loading_filter;
      m_loading_filter = NULL;
    }
  }

  else {
    int delay=get_config().get_number("fetch/auto_refresh_messages_list"); // minutes
    if (delay!=0 && m_timer_ticks%(delay*60*5)==0) {
      db_cnx db;
      if (!db.ping()) {
	DBG_PRINTF(3, "No reply to database ping");
	if (!db.datab()->reconnect()) {
	  DBG_PRINTF(3, "Failed to reconnect to database");
	  return;
	}
	else {
	  DBG_PRINTF(3, "Database reconnect successful");
	}
      }

      m_query_lv->refresh();

      if (get_config().get_bool("fetch/auto_incorporate_new_results", false)) {
	if (m_filter->auto_refresh())
	  sel_refresh_list();
      }
      else {
	check_new_mail();
      }
    }
  }
}

void
msg_list_window::timer_idle()
{
  int max_ahead = get_config().get_number("fetch_ahead_max_msgs");
  if (max_ahead==0 || !db_cnx::idle())
    return;			// do nothing if we're busy with the db already
  std::vector<mail_msg*> v;
  m_qlist->get_selected(v);
  if (v.size()==1) {
    mail_msg* item = v.front();
    while ((max_ahead--)>0 && (item = m_qlist->nearest_msg(item, 1))!=NULL) {
      if (!item->body_in_cache()) {
	DBG_PRINTF(8, "body fetched in idle timer");
	item->fetch_body_text(true); // partial fetch
	return;
      }
    }
  }
}

void
newmail_button::update_font(QFont f)
{
  int ps=f.pointSize();
  f.setPointSize((ps*80)/100); // reduce the font size by 1/5
  setFont(f);
}

newmail_button::newmail_button(QString txt, QWidget* parent) :
  QPushButton(txt, parent)
{
  update_font(font());
  QIcon ico(FT_MAKE_ICON(FT_ICON16_INBOX));
  setIcon(ico);
}

newmail_button::~newmail_button()
{
}

void
newmail_button::trayicon_click()
{
  emit show_new_mail();
}

void
newmail_button::enable(bool enable)
{
  if (enable) {
    QIcon ico(FT_MAKE_ICON(FT_ICON16_INBOX));
    //    setPixmap(FT_MAKE_ICON(FT_ICON16_INBOX));
    setWindowIcon(ico);
    if (m_number>0)
      setText(QString("%1").arg(m_number));
    else
      setText("");
    setEnabled(true);
#ifdef HAVE_TRAYICON
    static TrayIcon* tray;
    if (!tray) {
      tray = new TrayIcon();
      tray->setIcon(FT_MAKE_ICON(FT_ICON16_NEW_MAIL));
      connect(tray, SIGNAL(clicked( const QPoint&, int)), this,
	SLOT(trayicon_click()));
      tray->show();
    }
    if (m_number>0) {
      tray->setToolTip(tr("%1 new message(s)").arg(m_number));
    }
#endif
  }
  else {
    //    setPixmap(QPixmap());	// empty pixmap
    setEnabled(false);
    setText(tr("New mail !"));
#ifdef HAVE_TRAYICON
    if (tray) {
      delete tray;
      tray=NULL;
    }
#endif
  }
}

#if QT_VERSION<0x040000
void
newmail_button::drawButtonLabel(QPainter *p)
{
#if 0
  // doesn't work yet, FIXME someday
  if (isEnabled()) {
    QPen pen(QColor(255,0,0));
    p->setPen(pen);
    p->setBackgroundColor(QColor(255,100,100));
    QPushButton::drawButtonLabel(p);
  }
  else
    QPushButton::drawButtonLabel(p);
#else
  QPushButton::drawButtonLabel(p);
#endif
}
#endif

//static
void
msg_list_window::apply_conf_all_windows()
{
  std::map<msg_list_window*,int> window_processed;
  std::list<msgs_page*>::iterator page_it;
  for (page_it=msgs_page_list::m_all_pages_list.begin();
       page_it!=msgs_page_list::m_all_pages_list.end();
       ++page_it)
  {
    msg_list_window* w = (*page_it)->m_msgs_window;
    if (window_processed.find(w) == window_processed.end()) {
      window_processed[w]=1;
      w->apply_conf(get_config());
    }
  }
}

void
msg_list_window::apply_conf(app_config& conf)
{
  DBG_PRINTF(3, "conf.show_tags=%d", conf.get_number("show_tags"));
  DBG_PRINTF(3, "display_vars.show_tags=%d", display_vars.m_show_tags);
  if (display_vars.m_show_tags != conf.get_bool("show_tags")) {
    toggle_show_tags(display_vars.m_show_tags);
    m_menu_actions[me_Display_Tags]->setChecked(display_vars.m_show_tags);
  }

  if (conf.get_bool("display_threads") != display_vars.m_threaded) {
    toggle_threaded(display_vars.m_threaded);
    m_menu_actions[me_Display_Threaded]->setChecked(display_vars.m_threaded);
  }

  int headers_level = conf.get_number("show_headers_level");
  if (display_vars.m_show_headers_level != headers_level) {
    QAction* action=NULL;
    switch(headers_level) {
    case 0:
      action=m_menu_actions[me_Display_Headers_None];
      break;
    case 1:
    default:
      action=m_menu_actions[me_Display_Headers_Most];
      break;
    case 2:
      action=m_menu_actions[me_Display_Headers_All];
      break;
    case 3:
      action=m_menu_actions[me_Display_Headers_Raw];
      break;
    case 4:
      action=m_menu_actions[me_Display_Headers_RawDec];
      break;
    }
    if (action) {
      show_headers(action);
      action->setChecked(true);
    }
  }

  bool clickable_urls = conf.get_bool("body_clickable_urls");
  if (display_vars.m_clickable_urls != clickable_urls) {
    display_vars.m_clickable_urls = clickable_urls;
    display_body();
  }

}

void
msg_list_window::display_body()
{
  if (m_pCurrentItem) {
    m_msgview->display_body(display_vars, 0);
    m_msgview->highlight_terms(m_highlighted_text);
  }
}

void
msg_list_window::msg_zoom_in()
{
  m_msgview->change_zoom(+1);
}

void
msg_list_window::msg_zoom_out()
{
  m_msgview->change_zoom(-1);
}
void
msg_list_window::msg_zoom_zero()
{
  m_msgview->change_zoom(0);
}

const display_prefs&
msg_list_window::get_display_prefs()
{
  return display_vars;
}

void
msg_list_window::incorporate_message(mail_result& r)
{
  m_filter->add_result(r, m_qlist);
  QApplication::processEvents();
}
