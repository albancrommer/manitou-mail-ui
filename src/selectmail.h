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

#ifndef INC_SELECTMAIL_H
#define INC_SELECTMAIL_H

#include <QDialog>
#include <QKeyEvent>
#include <QThread>
#include <QStringList>
#include <QDateTime>
#include <QTime>

#include <vector>
#include "db.h"
#include "words.h"
#include "sqlquery.h"
#include "mail_listview.h"
#include "edit_address_widget.h"
#include "filter_rules.h"

class QLineEdit;
class QSpinBox;
class QButtonGroup;
class QTimer;
class QDateTimeEdit;
class QCheckBox;
class QToolButton;
class tag_selector;
class QButtonGroup;
class QComboBox;


class fetch_thread: public QThread
{
public:
  fetch_thread();
  virtual void run();
  void release();
  void cancel();
  db_cnx* m_cnx;
  int m_max_results;
  int m_step_count; // increase each time the thread is reused (fetch more)
  std::list<mail_result>* m_results;
  int store_results(sql_stream& stream, int max_nb);
  QString m_query;
  QString m_errstr;
  int m_exec_time;   // query exec time in milliseconds
  int m_tuples_count;

  // boundaries
  mail_id_t m_max_mail_id;
  mail_id_t m_min_mail_id;
  QString m_max_msg_date;
  QString m_min_msg_date;
  QString m_boundary;

  progressive_wordsearch m_psearch;
  bool m_fetch_more;
};

class msgs_filter
{
public:
  msgs_filter();
  void init();
  virtual ~msgs_filter();
  const QString user_query() const {
    return m_user_query;
  }
  void set_user_query(const QString q) {
    m_user_query=q;
  }
  void add_result(mail_result&, mail_listview*);
  int exec_time() const;	// in milliseconds
  typedef std::list<mail_msg*> mlist_t;
  mlist_t m_list_msgs;
  // fetch the selection into a mail list widget
  int fetch(mail_listview*, bool fetch_more=false);
  int asynchronous_fetch (fetch_thread* t, bool fetch_more=false);
  void make_list(mail_listview*);
  void set_auto_refresh(bool s=true) {
    m_auto_refresh=s;
  }
  bool auto_refresh() const {
    return m_auto_refresh;
  }
  bool has_more_results() const {
    return m_has_more_results;
  }
  int parse_search_string(QString s, QStringList& words, QStringList& substrs);

  // to do some pre-processing before the fetch
  void preprocess_fetch(fetch_thread&);

  // to do some post-processing after the fetch
  void postprocess_fetch(fetch_thread&);

  //private:

  enum recipient_type {
    rFrom=0,
    rTo,
    rCc,
    rAny
  };
  // search criteria
  uint m_mailId;
  int m_nAddrType;
  QString m_sAddress;
  QString m_subject;
  QString m_body_substring;
  QString m_addr_to;
  QString m_sql_stmt;
  QString m_tag_name;
  QDate m_date_min;
  QDate m_date_max;
  //  QString m_word;
  QStringList m_words;		// full-text search: words to find
  QStringList m_substrs;	// full-text search: substrings to find
  uint m_thread_id;

  int m_status;			// exact value wanted in mail.status
  int m_status_set;		// bitmask: OR'ed bits that have to be 1 in mail.status
  int m_status_unset;		// bitmask: OR'ed bits that have to be 0 in mail.status

  int m_newer_than;		/* newer than or N days old */
  // detailed user input
  struct {
    int days;
    int weeks;
    int months;
  } m_newer_details;

  int m_min_prio;
  uint m_tag_id;

  bool m_in_trash;
  bool m_include_trash;

  filter_expr m_filter_expression;
  bool m_has_filter_expr;
  expr_list m_filter_expr_list;

  void set_date_order(int o) {
    m_order=o;
  }
  int m_max_results;		// max results per fetch iteration
  bool m_fetched;

  std::list<mail_result>* m_fetch_results;
  int build_query (sql_query&, bool fetch_more=false);
  //  mail_msg* in_list(mail_id_t id);
  static int load_result_list(PGresult* res, std::list<mail_result>* l, int max_nb=-1);

  QTime m_start_time;
  int m_exec_time;
  QString m_errmsg;

  progressive_wordsearch m_psearch;

private:
  bool m_auto_refresh;
  int add_address_selection (sql_query& q, const QString email_addr, int addr_type);
  /* number of criteria that needs to match an address from
     the mail_addresses table. For each one of these we have to use
     a different sql alias for the mail_addresses table */
  unsigned int m_addresses_count;
  static const int max_possible_prio;

  /* escape % and _ for LIKE clauses and add % at the start and end */
  QString quote_like_arg(const QString&);

  PGresult* res;

  /* the part of the query that's made visible to the user: it
     includes a select-list that retrieves only the mail_id column,
     and also  shouldn't have ORDER BY or LIMIT clauses. Typically,
     it's aimed at being included in a subquery, i.e:

     SELECT <columns> FROM mail WHERE mail_id IN (m_user_query) ORDER BY
     ... LIMIT ... */
  QString m_user_query;

  /* used to fetch another set of results that are older/newer (depending on m_order) */
  QString m_date_bound;
  int m_mail_id_bound;
  QString m_boundary;

  /* ordering of msg_date (+1=ASC, -1=DESC) column for the fetch */
  int m_order;

  /* true if m_max_results is set and the last fetch retrieved
     m_max_results+1 results, meaning that there's at least one more
     result to get */
  bool m_has_more_results;

  /* keep the last part_no we joined against for IWI search. A
     subsequent "fetch more" will start from this part */
  int m_iwi_last_part_no;
};

// Array of checkboxes for viewing, editing or selecting
// a combination of all possible status
class select_status_box : public QFrame
{
  Q_OBJECT
public:
  select_status_box (bool either, QWidget* parent=0);
  virtual ~select_status_box() {}
  int mask_yes() const;
  int mask_no() const;
  void set_mask(int maskYes, int maskNo);
  int status() const;
public slots:
  void status_changed(int);
private:
  struct st_status {
    const char* name;
    int value;
  };
  static st_status m_status_tab[];
  int m_mask_set;		/* the bits we want to be 1 */
  int m_mask_unset;		/* the bits we want to be 0 */

  // true: the buttons are "yes, no, either"
  // false: the buttons are "yes", no"
  bool m_either;

  std::vector<QButtonGroup*> m_button_groups;
};

// a modal dialog that embeds a select_status_box with OK and Cancel
// buttons
class status_dialog : public QDialog
{
  Q_OBJECT
public:
  status_dialog(QWidget* parent=0);
  select_status_box* m_statusBox;
};

class msg_select_dialog : public QDialog
{
  Q_OBJECT
public:
  msg_select_dialog(bool open_new=true);
  virtual ~msg_select_dialog() {}

  /* fill in the dialog controls with the values provided by the message
     filter */
  void filter_to_dialog (const msgs_filter* cFilter);

public slots:
  void ok();
  void cancel();
  void help();
private slots:
  void more_status();
  void zoom_on_sql();
  void timer_done();
  void addr_type_changed(int);
private:
  QString str_status_mask();
  edit_address_widget* m_wcontact;
  QSpinBox* m_wdate_spin;
  QButtonGroup* m_wdate_button_group;
  QComboBox* m_wAddrType;
  tag_selector* m_qtag_sel;
  edit_address_widget* m_wto;
  QLineEdit* m_wSubject;
  QLineEdit* m_wString;
  QLineEdit* m_wSqlStmt;
  QDateTimeEdit* m_wmin_date;
  QDateTimeEdit* m_wmax_date;
  QCheckBox* m_chk_datemin;
  QCheckBox* m_chk_datemax;
  QLineEdit* m_wStatus;
  QLineEdit* m_wMaxResults;
  QPushButton* m_wCancelButton;
  QPushButton* m_wOkButton;
  QPushButton* m_wStatusMoreButton;
  QToolButton* m_zoom_button;
  QCheckBox* m_trash_checkbox;
  int m_status_set_mask;
  int m_status_unset_mask;
  void to_filter(msgs_filter*);
  msgs_filter m_filter;
  void enable_inputs (bool enable);

  fetch_thread m_thread;
  QTimer* m_timer;
  bool m_waiting_for_results;
  bool m_new_selection;
signals:
  void fetch_done(msgs_filter*);
};



#endif // INC_SELECTMAIL_H
