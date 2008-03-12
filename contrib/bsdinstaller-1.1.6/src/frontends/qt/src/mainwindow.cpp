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
 * $Id: mainwindow.cpp,v 1.19 2004/11/06 18:17:51 cpressey Exp $
 * $DragonFly: src/contrib/bsdinstaller-1.1.6/src/frontends/qt/src/mainwindow.cpp,v 1.1.1.1 2008/03/12 22:15:53 dave Exp $
 */

#include <stdio.h>
#include <iostream>

#include "form.h"
#include "mainwindow.h"

MainWindow::MainWindow(QWidget *parent, const char *name)
    : QMainWindow(parent, name)
{
	myTimerId = 0;

	createActions();
	createMenus();

	setCaption(tr("The BSD Installer"));
	setIcon(QPixmap::fromMimeSource("icon.png"));

	connection = dfui_connection_new(DFUI_TRANSPORT_TCP, "9999");
	dfui_fe_connect(connection);

	resize(640, 480);

	form = new Form(this, NULL, connection);
	setCentralWidget(form);
}

void MainWindow::createActions()
{
	exitAct = new QAction(tr("E&xit"), tr("Ctrl+Q"), this);
	exitAct->setStatusTip(tr("Exit the application"));
	connect(exitAct, SIGNAL(activated()), this, SLOT(close()));

	aboutAct = new QAction(tr("&About"), 0, this);
	aboutAct->setStatusTip(tr("Show the application's About box"));
	connect(aboutAct, SIGNAL(activated()), this, SLOT(about()));

	aboutQtAct = new QAction(tr("About &Qt"), 0, this);
	aboutQtAct->setStatusTip(tr("Show the Qt library's About box"));
	connect(aboutQtAct, SIGNAL(activated()), qApp, SLOT(aboutQt()));
}

void MainWindow::createMenus()
{
	fileMenu = new QPopupMenu(this);
	exitAct->addTo(fileMenu);

	helpMenu = new QPopupMenu(this);
	aboutAct->addTo(helpMenu);
	aboutQtAct->addTo(helpMenu);

	menuBar()->insertItem(tr("&File"), fileMenu);
	menuBar()->insertSeparator();
	menuBar()->insertItem(tr("&Help"), helpMenu);
}

bool MainWindow::message(DFUI_MSG msg, const char *message, bool confirm)
{
	int ret = 0;

	if (msg == DFUI_INFORMATION) {
		ret = QMessageBox::information(this, tr("Information"), tr(message),
			confirm ? QMessageBox::Yes : NULL,
			confirm ? QMessageBox::No | QMessageBox::Default : NULL);
	} else if (msg == DFUI_QUESTION) {
		ret = QMessageBox::question(this, tr("Question"), tr(message),
			confirm ? QMessageBox::Yes : NULL,
			confirm ? QMessageBox::No | QMessageBox::Default : NULL);
	} else if (msg == DFUI_WARNING) {
		ret = QMessageBox::warning(this, tr("Warning"), tr(message),
			confirm ? QMessageBox::Yes : NULL,
			confirm ? QMessageBox::No | QMessageBox::Default : NULL);
	} else if (msg == DFUI_CRITICAL) {
		ret = QMessageBox::critical(this, tr("Critical"), tr(message),
			confirm ? QMessageBox::Yes : NULL,
			confirm ? QMessageBox::No | QMessageBox::Default : NULL);
	}

	if (ret == QMessageBox::Yes || ret == QMessageBox::Ok)
		return true;
	else
		return false;
}

void MainWindow::about()
{
	QMessageBox::about(this, tr("About The BSD Installer"),
	    tr("<h2>The BSD Installer 1.0</h2>"
	    "<p>Copyright &copy; 2004 The DragonFly Project.</p>"));
}

void MainWindow::showEvent(QShowEvent *)
{
	myTimerId = startTimer(150); // Process dfuibe events every 150ms
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (message(DFUI_CRITICAL, tr("Are you sure you want to exit?"))) {
		event->accept();
		killTimer(myTimerId);
		dfui_fe_abort(connection);
		dfui_fe_disconnect(connection);
	} else {
		event->ignore();
	}
}

void MainWindow::timerEvent(QTimerEvent *event)
{
	QString str;
	void *payload;
	struct dfui_property *gp;
	char msgtype;

	if ((event->timerId() == myTimerId) && !form->processing) {
		dfui_fe_receive(connection, &msgtype, &payload);

		switch(msgtype) {
		case DFUI_BE_MSG_PRESENT:
			form->present((struct dfui_form *)payload);
			break;

		case DFUI_BE_MSG_PROG_BEGIN:
			form->present((struct dfui_progress *)payload);
			dfui_progress_free((struct dfui_progress *)payload);
			dfui_fe_progress_continue(connection);
			qApp->processEvents();
			break;

		case DFUI_BE_MSG_PROG_UPDATE:
			qApp->processEvents();
			form->setProgress(dfui_progress_get_amount(
			    (struct dfui_progress *)payload));

			if (dfui_progress_get_streaming((struct dfui_progress *)payload)) {
				form->updateProgressText(
				    dfui_progress_get_msg_line(
				    (struct dfui_progress *)payload));
			}

			if (form->cancelled)
				dfui_fe_progress_cancel(connection);
			else
				dfui_fe_progress_continue(connection);

			dfui_progress_free((struct dfui_progress *)payload);
			qApp->processEvents();
			break;

		case DFUI_BE_MSG_PROG_END:
			form->setProgress(-1);
			dfui_fe_progress_continue(connection);
			qApp->processEvents();
			break;

		case DFUI_BE_MSG_SET_GLOBAL:
			gp = (struct dfui_property *)payload;

			/*
			 * Check for a change to the "lang" setting...
			 */
			if (strcmp(dfui_property_get_name(gp), "lang") == 0) {
				/*
				 * TODO: call the appropriate gettext function.
				 */
			}

			dfui_fe_confirm_set_global(connection);
			dfui_property_free(gp);
			break;

		case DFUI_BE_MSG_STOP:
			dfui_fe_confirm_stop(connection);
			dfui_fe_disconnect(connection);
			qApp->exit(0);
			break;
		}
	}
}
