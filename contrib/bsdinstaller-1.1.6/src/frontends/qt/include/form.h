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
 * $Id: form.h,v 1.16 2005/02/06 19:59:00 cpressey Exp $
 * $DragonFly: src/contrib/bsdinstaller-1.1.6/src/frontends/qt/include/form.h,v 1.1.1.1 2008/03/12 22:15:53 dave Exp $
 */

#ifndef FORM_H
#define FORM_H

#include <dfui/dfui.h>

#include <qcheckbox.h>
#include <qcombobox.h>
#include <qdialog.h>
#include <qgrid.h>
#include <qlabel.h>
#include <qlayout.h>
#include <qlineedit.h>
#include <qobjectlist.h>
#include <qpushbutton.h>
#include <qprogressbar.h>
#include <qscrollview.h>
#include <qtable.h>
#include <qtextedit.h>
#include <qtooltip.h>
#include <qvbox.h>

class Form : public QScrollView
{
	Q_OBJECT

public:
	Form(QWidget *parent = 0, const char *name = 0,
	     struct dfui_connection *c = NULL);
	bool present(struct dfui_form *payload);
	bool present(struct dfui_progress *payload);
	void setProgress(int amount);
	void updateProgressText(const char *text);

	bool processing;
	bool cancelled;

private:
	struct dfui_connection	*connection;
	struct dfui_form *data;
	QProgressBar *pbar;
	bool extensible;
	bool multiple;

	QObject *buildWidget(QWidget *pwidget, struct dfui_celldata *cd,
			     struct dfui_field *field);
	QPushButton *buildActWidget(QWidget *pwidget,
				    struct dfui_action *action);

	bool buildProgress(struct dfui_progress *payload);
	bool buildSingle();
	void buildRow(int row, QWidget *pwidget, QTable *mLayout,
		      struct dfui_dataset *ds);
	bool buildMultiple();
	void renumberControls(QTable *table);
	void submitSingle(const QString *action);
	void submitMultiple(const QString *action);

private slots:
	void actProgress();
	void actButton();
	void actAddRow();
	void actInsertRow();
	void actDeleteRow();
};

#endif
