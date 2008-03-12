/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Shawn R. Walker <adonijah@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: mainwindow.h,v 1.7 2004/07/30 01:59:47 adonijah Exp $
 * $DragonFly: src/contrib/bsdinstaller-1.1.6/src/frontends/qt/include/mainwindow.h,v 1.1.1.1 2008/03/12 22:15:53 dave Exp $
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <dfui/dfui.h>

#include <qaction.h>
#include <qapplication.h>
#include <qmainwindow.h>
#include <qmenubar.h>
#include <qmessagebox.h>
#include <qpopmenu.h>
#include <qtimer.h>

#include "form.h"

enum DFUI_MSG {
	DFUI_INFORMATION,
	DFUI_QUESTION,
	DFUI_WARNING,
	DFUI_CRITICAL,
};

class QAction;
class QTimer;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	MainWindow(QWidget *parent = 0, const char *name = 0);

protected:
	void showEvent(QShowEvent *);
	void closeEvent(QCloseEvent *event);
	void timerEvent(QTimerEvent *event);

private slots:
	void about();

private:
	struct dfui_connection *connection;
	int myTimerId;
	Form *form;

	QPopupMenu *fileMenu;
	QAction *exitAct;

	QPopupMenu *helpMenu;
	QAction *aboutAct;
	QAction *aboutQtAct;

	void createActions();
	void createMenus();
	bool message(DFUI_MSG msg, const char *message, bool confirm = true);
};

#endif
