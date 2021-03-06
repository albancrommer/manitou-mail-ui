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
#include "helper.h"
#include <QMessageBox>
#include <QStringList>
#include <QFile>
#include "prog_chooser.h"
#include <QLocale>
#include <QProcess>
#include <QLibraryInfo>

qassistant::qassistant(const QString path)
{
  m_help_path=path;
  init();
}

qassistant::~qassistant()
{
  if (m_process) {
    m_process->terminate();
  }
}

void
qassistant::init()
{
  m_process = new QProcess(this);
  QString app = QLibraryInfo::location(QLibraryInfo::BinariesPath)
    + QLatin1String("/assistant");

  DBG_PRINTF(4, "assistant: %s", app.toLocal8Bit().constData());
  //#ifndef Q_OS_WIN
  //  QString fullpath=prog_chooser::find_in_path("assistant");
  //#endif
  QStringList args;
  args << "-enableRemoteControl" << "-collectionFile" << m_help_path+"/manitou.qhc";
  DBG_PRINTF(4, "assistant args=%s", args.join("\n").toLocal8Bit().constData());
  m_process->start(app, args);
  if (!m_process->waitForStarted()) {
    QMessageBox::critical(NULL, tr("Remote Control"),
			  tr("Could not start Qt Assistant from %1.").arg(app));
    return;

  }
  connect(m_process, SIGNAL(finished(int,QProcess::ExitStatus)),
	  this, SLOT(process_finished(int,QProcess::ExitStatus)));
}

void
qassistant::process_finished(int exit_code, QProcess::ExitStatus exit_status)
{
  Q_UNUSED(exit_code);
  Q_UNUSED(exit_status);
  if (m_process) {
    delete m_process;
    m_process=NULL;
  }
}

bool
qassistant::started()
{
  return m_process!=NULL;
}

void
qassistant::showPage(const QString path)
{
  if (!started())
    init();
  QTextStream str(m_process);
  QString p = QString("setSource ") + "qthelp://org.manitou-mail/doc/" + path;
  DBG_PRINTF(4, "setSource %s", p.toLatin1().constData());
  str << QLatin1String(p.toLatin1().constData()) << QLatin1Char('\0') << endl;
}

// translate help topics into HTML help files
//static
struct helper::topic helper::m_topics[] = {
  {"help", "user-interface.html"},
  {"connecting", "ui.invocation.html"},
  {"preferences/display", "ui.preferences.html"},
  {"preferences/identities", "ui.preferences.html#ui.preferences.identities"},
  {"preferences/fetching", "ui.preferences.html#ui.preferences.fetching"},
  {"preferences/mimeviewers", "ui.preferences.html#ui.preferences.mimeviewers"},
  {"preferences/paths", "ui.preferences.html#ui.preferences.paths"},
  {"query selection", "ui.query-form.html"},
  {"filters", "ui.filter-editor.html"},
  {"sql", "sql.html"}
};

//static
bool helper::m_track_mode=false;

//static
qassistant* helper::m_qassistant;

//static
void
helper::show_help(const QString topic)
{
  QString path = get_config().get_string("help_path");
  QString path_tried[3];
  if (path.isEmpty()) {
    extern QString gl_help_path;
    path = gl_help_path;
  }

  if (!path.isEmpty() && path.right(1) == "/")
    path.truncate(path.length()-1); // remove trailing slash

  /* The help path normally refers to the 'help' directory in which
     there are per-language directory like 'en', 'fr',
     but if the user makes it point directly into one of these
     sub-directories, then let's use that instead of guessing the language */

  path_tried[0] = path;
  if (!QFile::exists(path+"/manitou.qhc")) {
    QLocale locale;
#if 0 // temporarily disable the help translation until we get it translated :)
    QString lname = locale.name();
    if (lname == "C") 
      lname="en_us";
#else
    QString lname="en_us";
#endif
    // try help_path + language_country
    path_tried[1] = path+"/"+lname;
    if (!QFile::exists(path_tried[1]+"/manitou.qhc")) {
      // try help_path + language
      int sep_pos = lname.indexOf('_');
      if (sep_pos>=0) {
	path_tried[2]=path+"/"+lname.left(sep_pos);
	if (!QFile::exists(path_tried[2]+"/manitou.qhc")) {
	  path.truncate(0);
	}
	else {
	  path = path_tried[2];
	}
      }
      else
	path.truncate(0);
    }
    else {
      path = path_tried[1];
    }
  }
  // else use path as is

  if (path.isEmpty()) {
    QString msg=QObject::tr("The file 'manitou.qhc' could not be found in any of the following directories:\n");
    for (int ii=0; ii<3 && !path_tried[ii].isEmpty(); ii++) {
     msg.append(path_tried[ii]);
      msg.append("\n");
    }
    msg.append(QObject::tr("Please use the Preferences (Paths tab) to enter the directory where help files are located."));
    QMessageBox::warning(NULL, QObject::tr("Unable to locate help"), msg);
    return;
  }

  if (!m_qassistant) {
    m_qassistant = new qassistant(path);
  }
  QString page;
  for (uint i=0; i<sizeof(m_topics)/sizeof(m_topics[0]); i++) {
    if (topic==m_topics[i].name) {
      page=m_topics[i].page;
      break;
    }
  }
  if (page.isEmpty()) {
//    DBG_PRINTF(2, "WRN: topic '%s' not found\n", (const char*)topic.local8Bit());
    return;
  }
/*
  QString p=path + "/" + page;
  DBG_PRINTF(4, "showPage('%s')", p.toLocal8Bit().constData());
*/
  m_qassistant->showPage(page);
}

bool
helper::auto_track_status()
{
  return m_track_mode;
}

void
helper::auto_track(bool on)
{
  m_track_mode=on;
}

void
helper::track(const QString topic)
{
  if (m_track_mode)
    show_help(topic);
}

void
helper::close()
{
  if (m_qassistant) {
    delete m_qassistant;
    m_qassistant=NULL;
  }
}
