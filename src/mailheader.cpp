/* Copyright (C) 2004-2010 Daniel Verite

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

#include <locale.h>
#include <stdlib.h>

#include "main.h"
#include "db.h"
#include "config.h"
#include "mailheader.h"
#include "sqlstream.h"

#include <time.h>
#include <qtextcodec.h>
#include <qregexp.h>

#ifndef __GNUG__
// for _alloca()
#include <malloc.h>
#endif

void
mail_header::make()
{
  m_lines.truncate(0);
  if (!m_from.isEmpty())
    m_lines.append(QString("From: %1\n").arg(m_from));
  if (!m_to.isEmpty())
    m_lines.append(QString("To: %1\n").arg(m_to));
  if (!m_cc.isEmpty())
    m_lines.append(QString("Cc: %1\n").arg(m_cc));
  if (!m_bcc.isEmpty())
    m_lines.append(QString("Bcc: %1\n").arg(m_bcc));
  if (!m_replyTo.isEmpty())
    m_lines.append(QString("Reply-To: %1\n").arg(m_replyTo));
  if (!m_subject.isEmpty())
    m_lines.append(QString("Subject: %1\n").arg(m_subject));
  if (!m_inReplyTo.isEmpty())
    m_lines.append(QString("In-Reply-To: %1\n").arg(m_inReplyTo));

  QString date;
  if (!database::fetchServerDate(date))
    return;
  char parsedate[200];
  strncpy(parsedate, date.toLatin1().constData(),199);
  char ldate[256];

  // Initialize with the localtime to get the timezone and dst
  struct tm t;
  time_t tt;
  time (&tt);
  struct tm* ltm = localtime (&tt);
  memcpy (&t, ltm, sizeof(struct tm));

  // "DD/MM/YYYY HH:NN:SS";
  parsedate[2]='\0'; t.tm_mday=atoi(parsedate);
  parsedate[5]='\0'; t.tm_mon=atoi(parsedate+3)-1;
  parsedate[10]='\0'; t.tm_year=atoi(parsedate+6)-1900;
  parsedate[13]='\0'; t.tm_hour=atoi(parsedate+11);
  parsedate[16]='\0'; t.tm_min=atoi(parsedate+14);
  t.tm_sec=atoi(parsedate+17);
  // use a portable locale for the Date: field
  char* loc=setlocale(LC_TIME, "C");
#ifdef Q_WS_WIN
  /* MS C library insists on replacing %z with non-standard, locale-tainted text
     so we use our own code based on the global _timezone variable */
  strftime(ldate, sizeof(ldate), "%a, %d %b %Y %H:%M:%S", &t);
  QString datetz;
  _tzset();
  int delta_s=(_timezone>=0)?_timezone:-_timezone;
  if (t.tm_isdst) {
    // apparently MSC can tell us if DST is in effect, but not the DST offset;
    // we assume it's one hour.
    delta_s += 3600;
  }
  datetz.sprintf(" %c%02d%02d", _timezone<0?'+':'-', delta_s/3600,
    delta_s%60);
  strcat(ldate, datetz.toLatin1().constData());
#else
  const char* fmt="%a, %d %b %Y %H:%M:%S %z";
  strftime(ldate, sizeof(ldate), fmt, &t);
#endif // !Q_WS_WIN
  // restore the previous locale
  if (loc)
    setlocale(LC_TIME, loc);
  m_lines.append(QString("Date: %1\n").arg(ldate));

  if (!m_messageId.isEmpty()) {
    m_lines.append(QString("Message-Id: <%1>\n").arg(m_messageId));
  }
  m_lines.append(QString("X-Mailer: Manitou v%1\n").arg(VERSION));

  if (!m_xface.isEmpty())
    m_lines.append(QString("X-Face: %1\n").arg(m_xface));
}

// store from,to,cc,bcc addresses into the database
bool
mail_header::store_addresses()
{
  bool res=true;
  if (!m_from.isEmpty())
    res &= store_addresses_list(m_from, (int)mail_address::addrFrom);
  if (!m_to.isEmpty())
    res &= store_addresses_list(m_to, (int)mail_address::addrTo);
  if (!m_cc.isEmpty())
    res &= store_addresses_list(m_cc, (int)mail_address::addrCc);
  if (!m_bcc.isEmpty())
    res &= store_addresses_list(m_bcc, (int)mail_address::addrBcc);
  if (!m_replyTo.isEmpty())
    res &= store_addresses_list(m_replyTo, (int)mail_address::addrReplyTo);
  return res;
}

bool
mail_header::store_addresses_list(const QString& addr_list, int addr_type)
{
  db_cnx db;
  sql_stream s("INSERT INTO mail_addresses(mail_id,addr_id,addr_type,addr_pos) VALUES(:p1,:p2,:p3,:p4)", db);
  std::list<QString> emails;
  std::list<QString> names;
  mail_address::ExtractAddresses(addr_list.toLatin1().constData(), emails, names);
  bool found;
  std::list<QString>::iterator it1 = emails.begin();
  std::list<QString>::iterator it2 = names.begin();
  for (int addr_pos = 0; it1 != emails.end(); addr_pos++) {
    mail_address a;
    QString s_addr=(*it1).toLower();
    bool res=a.fetchByEmail(*(it1),&found);
    if (!res)
      return false;
    if (!found) {
      // create the address if it doesn't exist yet
      a.set(s_addr);
      a.set_name(*(it2));
      if (!a.store())
	return false;
    }
    if (addr_type == mail_address::addrTo) {
      a.update_last_used("sent");
    }
    s << m_mail_id << a.id() << (int)addr_type << addr_pos;
    it1++; it2++;
  }
  return true;
}

/* Return a string based on the To header field, in a format suitable to go
   into mail.recipients */
QString
mail_header::recipients_list()
{
  QString result;
  std::list<QString> emails;
  std::list<QString> names;
  mail_address::ExtractAddresses(m_to.toLatin1().constData(), emails, names);
  std::list<QString>::iterator it1 = emails.begin();
  std::list<QString>::iterator it2 = names.begin();
  for (; it1 != emails.end() && it2!=names.end(); ) {
    mail_address a;
    a.set((*it1).toLower());
    a.set_name(*(it2));
    if (!result.isEmpty())
      result.append(',');
    result.append(a.email_and_name());
    it1++; it2++;
  }
  return result;
}

bool
mail_header::store()
{
  make();
  store_addresses();
  db_cnx db;
  sql_stream s("INSERT INTO header(mail_id,lines) VALUES (:p1, ':p2')",
	      db);
  s << m_mail_id << m_lines;
  return true;
}

bool
mail_header::fetch()
{
  db_cnx db;
  try {
    sql_stream s("SELECT lines FROM header WHERE mail_id=:p1", db);
    s << m_mail_id;
    if (!s.eos()) {
      s >> m_lines;
    }
    else
      m_lines="";
  }
  catch(db_excpt& p) {
    DBEXCPT(p);
    m_lines="";
    return false;
  }
  return true;
}

static char
to_hex(char c1, char c2)
{
  int i;
  if (c1>='0' && c1<='9')
    i=(c1-'0')<<4;
  else if (c1>='A' && c1<='F')
    i=(c1-'A'+10)<<4;
  else if (c1>='a' && c1<='f')
    i=(c1-'a'+10)<<4;
  else throw 6;

  if (c2>='0' && c2<='9')
    i+=c2-'0';
  else if (c2>='A' && c2<='F')
    i+=c2-'A'+10;
  else if (c2>='a' && c2<='f')
    i+=c2-'a'+10;
  else throw 6;

  return i;
}

static int
append_decoded_qp(const QString& s, char* buf)
{
  int jb=0;
  for (int j=0; j<s.length(); j++) {
    char c=s.at(j).toAscii();
    if (c=='=') {
      if (j<s.length()-2) {
	try {
	  char c1=to_hex(s.at(j+1).toAscii(), s.at(j+2).toAscii());
	  buf[jb++]=c1;
	  j+=2;
	  continue;
	}
	catch(int) {
	  /* in case of QP decoding error, we fall into the normal case
	     as if it wasn't QP */
	}
      }
    }
    else if (c=='_') {
      c=' ';
    }
    buf[jb++]=c;
  }
  return jb;
}

/*
 src=base64 source
 dest=destination
 size=pointer to the number of input bytes to be processed.
 On return, *size contains the number of bytes that still need
 to be processed (should be between 0 and 3)
*/

static int
decode_base64(const char* src, char* dest, int* size)
{
  int i=0;
  char c;
  int pad=0;
  char* initial_dest=dest;
  int s=*size;
  int v;
  int val=0;
  while (s>0) {
    c=*src++;
    s--;
    if (c=='\n' || c=='\r')
      continue;

    if (c>='A' && c<='Z') {
      v=c-'A';
    }
    else if (c>='a' && c<='z')
      v=c-'a'+26;
    else if (c>='0' && c<='9')
      v=c-'0'+52;
    else if (c=='+')
      v=62;
    else if (c=='/')
      v=63;
    else if (c=='=') {
      v=0;
      if (++pad>2) {
	return -1;		/* no more than 2 '=' are allowed */
      }
    }
    else
      return -1;		/* non-base64 input character */
    val=(val<<6)|v;
    if (++i==4) {
      i=0;
      *dest++=(val&0xFF0000)>>16;
      if (pad<2) *dest++=(val&0x00FF00)>>8;
      if (pad<1) *dest++=(val&0x0000FF);
      val=0;
    }
  }
  *size=i;
  return dest-initial_dest;
}

static int
append_decoded_b64(const QString& s, char* buf, int sz)
{
  return decode_base64(s.toLatin1().constData(), buf, &sz);
}

//static
void
mail_header::decode_line(QString& s)
{
  int pos=s.indexOf("=?");
  if (pos<0)
    return;
  QString dline;
  QRegExp rx("=\\?([^\\?]+)\\?(q|Q|b|B)\\?([^\\?]*)\\?=");
  int r=rx.indexIn(s, pos);
  int end_rx=0;
  pos=0;
#ifndef __GNUG__
  int maxbufsz=0;
#endif
  while (r>=0) {
    if (r-pos>0) {
      // append the contents before the encoded word
      // except if it's spaces only
      QString before=s.mid(pos,r-pos);
      //	printf("\tend_rx=%d before=%s\n", end_rx, before.latin1());
      if (end_rx>0) {
	// when between two encoded words, ignore the contents
	// if it contains only spaces (rfc822 'linear-white-space')
	for (int i=0; i<before.length(); i++) {
	  QChar c=before.at(i);
	  if (c!=' ' && c!='\t' && c!='\n') {
	    dline.append(before);
	    break;
	  }
	}
      }
      else {
	//	printf("\tappend %s\n", before.latin1());
	dline.append(before);
      }
    }
    //printf("\tmatch at r=%d\n", r);
    int rxsz=rx.matchedLength();
    QStringList l=rx.capturedTexts();
    QStringList::Iterator it = l.begin();
    ++it;
    //printf("\tcodecname=%s\n", (*it).latin1());
    QTextCodec* codec=QTextCodec::codecForName((*it).toLatin1());
    ++it;
    char enctype=(*it).at(0).toUpper().toAscii();
    ++it;
    int bufsz=(*it).length();
#ifdef __GNUG__
    char buf[bufsz+1];
#else
    char* buf;
    if (bufsz>maxbufsz) {
      buf = (char*)_alloca(bufsz+1);
      maxbufsz = bufsz;
    }
#endif
    //printf("\t*it=%s\n", (*it).latin1());
    int jb;
    if (enctype=='Q') {
      jb=append_decoded_qp((*it), buf);
    }
    else if (enctype=='B') {
      jb=append_decoded_b64((*it), buf, bufsz);
    }
    else {
      // avoid a compiler warning (this code should never be reached)
      jb=0;
    }
    if (codec) {
      dline.append(codec->toUnicode(buf, jb));
    }
    else {
      //printf("\tNO CODEC!\n");
      buf[bufsz]='\0';
      dline.append(buf);
    }
    end_rx=r+rxsz;
    pos=r+rxsz;
    r=rx.indexIn(s, pos);
  }
  dline.append(s.mid(end_rx));
  s=dline;
}

/*
  Convert an encoded header into a decoded form:
  - internal QString encoding (unicode)
  - lines unfolded
*/
// static
void
mail_header::decode_rfc822(const QString src, QString& dest)
{
  QString curline;
  int len=src.length();
  for (int i=0; i<len; i++) {
    char c=src.at(i).toAscii();
    switch(c) {
    case '\n':
      if (i+1<len) {
	char c1=src.at(i+1).toAscii();
	if (c1!=' ' && c1!='\t') {
	  curline.append(c);
	}
	else {
	  curline.append(' ');
	  i++;
	  while (i<len && (c1==' ' || c1=='\t')) {
	    c1=src.at(i).toAscii();
	    i++;
	  }
	  if (i<len) {
	    i--;
	    curline.append(c1);
	  }
	}
      }
      break;
    default:
      curline.append(c);
      break;
    }
  }
  dest=curline;
  decode_line(dest);
}

/*
  Compute the length of a header from a rfc822 message
*/
//static
uint
mail_header::header_length(const char* rawmsg)
{
  const char* p=rawmsg;
  char c;
  char last_c='\0';
  int idx=0;
  while ((c=*p++)) {
    if (c=='\n') {
      if (last_c=='\n')
	return p-rawmsg;	// \n\n => end of header
      if (last_c=='\r' && idx>3 && *(p-2)=='\n' && *(p-3)=='\r') {
	// \r\n\r\n => end of header
	return p-rawmsg;
      }
    }
    last_c=c;
    idx++;
  }
  return 0;
}

bool
mail_header::fetch_raw()
{
  db_cnx db;
  try {
    m_raw.truncate(0);
    db.begin_transaction();
    sql_stream s("SELECT mail_text FROM raw_mail WHERE mail_id=:p1", db);
    s << m_mail_id;
    Oid lobjId;
    if (!s.eos()) {
      s >> lobjId;
    }
    else
      return false;

    PGconn* c=db.connection();
    int lobj_fd = lo_open(c, lobjId, INV_READ);
    if (lobj_fd < 0) {
      return false;
    }
    unsigned char data[8192+1];
    unsigned int nread;
    bool end_header=false;
    bool has_8_bit=false;
    data[sizeof(data)-1]='\0';
    do {
      nread = lo_read(c, lobj_fd, (char*)data, sizeof(data)-1);
      // check if the contents are 8 bit clean
      char lastc=0;
      unsigned int j;
      for (j=0; j<nread && !end_header; j++) {
	if (data[j]>=0x80)
	  has_8_bit=true;
	if (lastc=='\n' && data[j]=='\n') {
	  end_header=true;
	  data[j]='\0';
	}
	lastc=data[j];
      }
      QTextCodec* codec;
      // if not 8 bit clean, apply an arbitrary codec
      if (has_8_bit && (codec=QTextCodec::codecForName("iso-8859-15"))) {
	m_raw.append(codec->toUnicode((char*)data, j));
#if QT_VERSION<0x040000
	/* it's not clear whether it's desirable or not to delete the
	   codec under Qt3, but it's not possible under Qt4 anyway,
	   the destructor being protected */
	delete codec;
#endif
      }
      else {
	m_raw.append((char*)data);
      }
    } while (!end_header && nread==sizeof(data));
    lo_close(c, lobj_fd);
    db.commit_transaction();
  }
  catch(db_excpt& p) {
    db.rollback_transaction();
    DBEXCPT(p);
    return false;
  }
  return true;
}

void
mail_header::format(QString& sOutput, const QString& h1)
{
  QString h;
  decode_rfc822(h1, h);
  uint nPos=0;
  uint nEnd=h.length();
  QString hfontsz_o;		// <big> or empty
  QString hfontsz_c;		// </big> or empty

  // show all headers
  while (nPos<nEnd) {
    int nPosEh=0;		// end of header name
    // position of end of current line
    int nPosLf=h.indexOf('\n',nPos);
    if (nPosLf==-1)
      nPosLf=nEnd;
    if (h.mid(nPos,1)!=" " && h.mid(nPos,1)!="\t" && h.mid(nPos,5)!="From "
	&& (nPosEh=h.indexOf(':',nPos))>0)
    {
	QString sContents=h.mid(nPosEh+1, nPosLf-nPosEh-1);
	sContents.replace(QRegExp("<"), "&lt;");
	sContents.replace(QRegExp(">"), "&gt;");
	sContents.replace (QRegExp ("\t"), " ");
	sOutput += hfontsz_o + QString("<font color=\"blue\">") +
	  h.mid(nPos,nPosEh-nPos+1) +
	  "</font>" + sContents + hfontsz_c+"<br>";
    }
    else {
      sOutput += hfontsz_o + h.mid(nPos,nPosLf-nPos+1) + hfontsz_c + "<br>";
    }
    nPos=nPosLf+1;
  }
}
