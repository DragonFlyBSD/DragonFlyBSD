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
 * $Id: form.cpp,v 1.24 2005/02/07 06:49:10 cpressey Exp $
 * $DragonFly: src/contrib/bsdinstaller-1.1.6/src/frontends/qt/src/form.cpp,v 1.1.1.1 2008/03/12 22:15:53 dave Exp $
 */

#include "form.h"

Form::Form(QWidget *parent, const char *name, struct dfui_connection *c)
    : QScrollView(parent, name)
{
	extensible = false;
	multiple = false;
	processing = false;
	cancelled = false;
	connection = c;
	pbar = NULL;
}

bool Form::buildProgress(struct dfui_progress *payload)
{
	QWidget *main;
	QTextEdit *long_text;

	main = new QWidget(viewport(), "main");
	addChild(main);

	QVBoxLayout *layout = new QVBoxLayout(main); // Overall
	layout->setMargin(10);
	layout->setSpacing(5);

	QVBoxLayout *tLayout = new QVBoxLayout(layout); // Top
	QVBoxLayout *bLayout = new QVBoxLayout(layout); // Bottom

	// Set our title
	setCaption("Processing...");

	// Add description
	long_text = new QTextEdit(main);
	long_text->setReadOnly(true);
	long_text->setText(
	    tr(dfui_info_get_short_desc(dfui_progress_get_info(payload))));
	tLayout->addWidget(long_text);
	long_text = NULL;

	// Add Progress bar
	pbar = new QProgressBar(main);
	if (dfui_progress_get_streaming(payload))
		pbar->setTotalSteps(0);
	else
		pbar->setTotalSteps(100);

	pbar->setProgress(0);
	tLayout->addWidget(pbar);

	// Add Cancel
	QPushButton *button = new QPushButton(tr("Cancel"), main, "actProgress");
	connect(button, SIGNAL(clicked()), this, SLOT(actProgress()));
	bLayout->addWidget(button);

	// Add Progress Text (for streaming case)
	if (dfui_progress_get_streaming(payload)) {
		long_text = new QTextEdit(main, "progress_text");
		long_text->setReadOnly(true);
		long_text->setText("");
		bLayout->addWidget(long_text);
		long_text = NULL;
	}

	multiple = false;
	cancelled = false;
	main->setMinimumWidth(viewport()->width() -
	    verticalScrollBar()->width());
	main->setMinimumHeight(viewport()->height());
	main->show();

	return true;
}

void Form::actProgress()
{
	const QObject *wid = QObject::sender();
	const QString text = ((QPushButton *)wid)->text();

	if (text == "Cancel") {
		cancelled = true;
	} else if (text == "Ok") {
		processing = false;
		close();
		child("main")->deleteLater();
	}
}

void Form::setProgress(int amount)
{
	if (pbar && amount == -1) {
		pbar->setProgress(pbar->totalSteps());
		if (pbar->totalSteps() == 0) {
			QObject *widget = child("main")->child("actProgress");
			((QPushButton *)widget)->setText("Ok");

			// Trick the event loop into waiting for the user
			// to acknowledge the streaming message output
			processing = true;
		} else {
			close();
			child("main")->deleteLater();
		}
	} else if (pbar) {
		pbar->setProgress(amount);
	} else {
		cancelled = true;
	}
}

void Form::updateProgressText(const char *text)
{
	QObject *progressText = NULL;
	if (child("main"))
		progressText = child("main")->child("progress_text");

	if (progressText)
		((QTextEdit *)progressText)->append(tr(text));
}

QObject *Form::buildWidget(QWidget *pwidget,
			   struct dfui_celldata *cd,
			   struct dfui_field *field)
{
	QObject *widget;
	struct dfui_option *option;
	const char *desc = dfui_info_get_short_desc(dfui_field_get_info(field));
	const char *name = dfui_field_get_id(field);
	const char *value;

	if (cd)
		value = dfui_celldata_get_value(cd);
	else
		value = "";

	if (dfui_field_property_is(field, "control", "checkbox")) {
		widget = new QCheckBox("", pwidget, name);

		if (strcmp(value, "Y") == 0)
			((QCheckBox *)widget)->setDown(true);

	} else if ((option = dfui_field_option_get_first(field)) != NULL) {
		widget = new QComboBox(pwidget, name);

		if (dfui_field_property_is(field, "editable", "false"))
			((QComboBox *)widget)->setEditable(false);

		for (option = dfui_field_option_get_first(field);
		    option != NULL; option = dfui_option_get_next(option)) {
			QString optval = tr(dfui_option_get_value(option));
			((QComboBox *)widget)->insertItem(optval);
			if (strcmp(optval.ascii(), value) == 0)
				((QComboBox *)widget)->setCurrentText(
				    optval);
	
		}
	} else {
		widget = new QLineEdit(value, pwidget, name);

		if (dfui_field_property_is(field, "editable", "false"))
			((QLineEdit *)widget)->setReadOnly(true);

		if (dfui_field_property_is(field, "obscured","true")) {
			((QLineEdit *)widget)->setEchoMode(
			    QLineEdit::Password);
		}
	}

	QToolTip::add((QWidget *)widget, tr(desc));
	return widget;
}

QPushButton *Form::buildActWidget(QWidget *pwidget, struct dfui_action *action)
{
	QPushButton *button;
	const char *name, *value;
	
	name = dfui_action_get_id(action);
	value = dfui_info_get_name(dfui_action_get_info(action));
	
	button = new QPushButton(tr(value), pwidget, name);
	connect(button, SIGNAL(clicked()), this, SLOT(actButton()));
	return button;
}

bool Form::buildSingle()
{
	struct dfui_action *action;
	struct dfui_dataset *ds;
	struct dfui_field *field;
	QWidget *main;
	QTextEdit *long_text;
	int row;

	main = new QWidget(viewport(), "main");
	addChild(main);

	QVBoxLayout *layout = new QVBoxLayout(main); // Overall
	layout->setMargin(10);
	layout->setSpacing(5);

	QVBoxLayout *tLayout = new QVBoxLayout(layout); // Top
	QGridLayout *mLayout = new QGridLayout(layout); // Middle
	QVBoxLayout *bLayout = new QVBoxLayout(layout); // Bottom

	// Set our identifier for response later
	setName(dfui_form_get_id(data));

	// Set our title
	setCaption(tr(dfui_info_get_name(dfui_form_get_info(data))));

	// Add description
	long_text = new QTextEdit(main);
	long_text->setReadOnly(true);
	long_text->setText(tr(dfui_info_get_short_desc(dfui_form_get_info(data))));
	tLayout->addWidget(long_text);
	long_text = NULL;

	// Grab the dataset so we can get values from it during field generation
	ds = dfui_form_dataset_get_first(data);

	row = 0;
	for (field = dfui_form_field_get_first(data); field != NULL;
	    field = dfui_field_get_next(field)) {
		QLabel *wlabel;
		QObject *widget;
		struct dfui_celldata *cd;
		const char *label;

		// Grab the incoming dataset celldata
		cd = dfui_dataset_celldata_find(ds, dfui_field_get_id(field));

		label = dfui_info_get_name(dfui_field_get_info(field));
		wlabel = new QLabel(tr(label), main);
		mLayout->addWidget(wlabel, row, 0);

		widget = buildWidget(main, cd, field);
		mLayout->addWidget((QWidget *)widget, row++, 1);
	}

	for (action = dfui_form_action_get_first(data); action != NULL;
	    action = dfui_action_get_next(action)) {
		QPushButton *button = buildActWidget(main, action);
		bLayout->addWidget(button);
	}

	multiple = false;
	main->setMinimumWidth(viewport()->width() -
	    verticalScrollBar()->width());
	main->setMinimumHeight(viewport()->height());
	main->show();

	return true;
}

void Form::buildRow(int row, QWidget *pwidget, QTable *mLayout, struct dfui_dataset *ds)
{
	int col = 0;
	for (struct dfui_field *field = dfui_form_field_get_first(data);
	    field != NULL; field = dfui_field_get_next(field)) {
		struct dfui_celldata *cd = NULL;
	
		// Grab the incoming dataset celldata
		if (ds)
			cd = dfui_dataset_celldata_find(ds, dfui_field_get_id(field));
	
		QObject *widget = buildWidget(pwidget, cd, field);
		mLayout->setCellWidget(row, col++, (QWidget *)widget);
	}
	
	if (extensible) {
		QString row_id;
		row_id.setNum(row);
	
		QPushButton *ins = new QPushButton(tr("Insert"), pwidget,
		    row_id);
		QPushButton *del = new QPushButton(tr("Delete"), pwidget,
		    row_id);
	
		mLayout->setCellWidget(row, col, ins);
		connect(ins, SIGNAL(clicked()), this, SLOT(actInsertRow()));
	
		mLayout->setCellWidget(row, col + 1, del);
		connect(del, SIGNAL(clicked()), this, SLOT(actDeleteRow()));
	}
}

bool Form::buildMultiple()
{
	QWidget *main;
	QTextEdit *long_text;
	int col, row;

	main = new QWidget(viewport(), "main");
	addChild(main);

	QVBoxLayout *layout = new QVBoxLayout(main); // Overall
	layout->setMargin(10);
	layout->setSpacing(5);

	QVBoxLayout *tLayout = new QVBoxLayout(layout); // Top
	QTable *mLayout = new QTable(main, "table"); // Middle
	layout->addWidget(mLayout);

	QVBoxLayout *bLayout = new QVBoxLayout(layout); // Bottom

	// Set our identifier for response later
	setName(dfui_form_get_id(data));

	// Set our title
	setCaption(tr(dfui_info_get_name(dfui_form_get_info(data))));

	// Add description
	long_text = new QTextEdit(main);
	long_text->setReadOnly(true);
	long_text->setText(tr(dfui_info_get_short_desc(dfui_form_get_info(data))));
	tLayout->addWidget(long_text);
	long_text = NULL;

	col = 0;
	for (struct dfui_field *field = dfui_form_field_get_first(data); field != NULL;
	    field = dfui_field_get_next(field)) {
		const char *label = dfui_info_get_name(dfui_field_get_info(field));

		mLayout->insertColumns(col);
		mLayout->horizontalHeader()->setLabel(col++, tr(label));
	}

	if (dfui_form_is_extensible(data)) {
		extensible = true;
		for (int i = 0; i < 2; i++) {
			mLayout->insertColumns(col, 1); // Needed for Ins, Del, Add
			mLayout->horizontalHeader()->setLabel(col++, tr(""));
		}
	}

	row = 0;
	for (struct dfui_dataset *ds = dfui_form_dataset_get_first(data); ds != NULL;
	     ds = dfui_dataset_get_next(ds)) {
		mLayout->insertRows(row);
		buildRow(row, main, mLayout, ds);
		row++;
	}

	if (extensible) {
		QPushButton *add = new QPushButton(tr("Add"), main);

		mLayout->insertRows(row);
		mLayout->setRowReadOnly(row, true);
		mLayout->setCellWidget(row, mLayout->numCols() - 2, add);
		connect(add, SIGNAL(clicked()), this, SLOT(actAddRow()));
	}

	for (struct dfui_action *action = dfui_form_action_get_first(data); action != NULL;
	    action = dfui_action_get_next(action)) {
		QPushButton *button = buildActWidget(main, action);
		bLayout->addWidget(button);
	}

	multiple = true;
	main->setMinimumWidth(viewport()->width() -
	    verticalScrollBar()->width());
	main->setMinimumHeight(viewport()->height());
	main->show();

	return true;
}

void Form::actButton()
{
	const QObject *wid = QObject::sender();
	const QString widname = wid->name();

	if (multiple)
		submitMultiple(&widname);
	else
		submitSingle(&widname);

	if (data) {
		dfui_form_free(data); // We're done with it
		data = NULL;
	}
}

void Form::renumberControls(QTable *table)
{
	int maxcols = table->numCols();

	// Cycle through all rows except last since it's only used for Add
	for (int row = 0; row < table->numRows() - 1; row++) {
		QString row_id;
		row_id.setNum(row);

		// Change Insert Row ID
		QWidget *widget = table->cellWidget(row, maxcols - 2);
		if (widget)
			widget->setName(row_id);


		// Change Delete Row ID
		widget = table->cellWidget(row, maxcols - 1);
		if (widget)
			widget->setName(row_id);
	}
}

void Form::actAddRow()
{
	QTable *table = ((QTable *)(child("main")->child("table")));
	table->insertRows(table->numRows() - 1, 1);
	buildRow(table->numRows() - 2, (QWidget *)child("main"), table, NULL);
}

void Form::actInsertRow()
{
	const QObject *wid = QObject::sender();
	const QString row = wid->name();
	QTable *table = ((QTable *)(child("main")->child("table")));
	table->insertRows(row.toInt());
	buildRow(row.toInt(), (QWidget *)child("main"), table, NULL);
	renumberControls(table);
}

void Form::actDeleteRow()
{
	const QObject *wid = QObject::sender();
	const QString row = wid->name();
	QTable *table = ((QTable *)(child("main")->child("table")));
	table->removeRow(row.toInt());
	renumberControls(table);
}

void Form::submitSingle(const QString *action)
{
	struct dfui_dataset *ds;
	struct dfui_response *response;

	ds = dfui_dataset_new();
	response = dfui_response_new(name(), action->ascii());

	QObjectList *widgets = topLevelWidget()->queryList();
	QObjectListIterator it(*widgets);
	QObject *widget;

	while ((widget = it.current()) != 0) {
		QString value;

		if (widget->isA("QCheckBox")) {
			if (((QCheckBox *)widget)->isOn())
				value.setAscii("Y", 1);
			else
				value.setAscii("N", 1);
		} else if (widget->isA("QLineEdit")) {
			QLineEdit *textbox = (QLineEdit *)widget;
			value.setAscii(textbox->text());
		} else if (widget->isA("QComboBox")) {
			QComboBox *combobox = (QComboBox *)widget;
			value.setAscii(combobox->currentText());
		} else {
			++it;
			continue;
		}

		dfui_dataset_celldata_add(ds,
		    widget->name(), value.ascii());

		++it;
	}
	delete widgets;

	dfui_response_dataset_add(response, ds);

	dfui_fe_submit(connection, response);
	dfui_response_free(response);

	extensible = false;
	multiple = false;
	processing = false;
	close();

	child("main")->deleteLater(); // Clear the form
}

void Form::submitMultiple(const QString *action)
{
	struct dfui_dataset *ds;
	struct dfui_response *response;
	QTable *table;
	int row, col;
	int maxcols;

	response = dfui_response_new(name(), action->ascii());

	table = (QTable *)(child("main")->child("table"));
	maxcols = table->numCols();
	if (extensible)
		maxcols -= 2; // Ignore Insert / Delete Columns

	for (row = 0; row < table->numRows(); row++) {
		bool submit = false;

		ds = dfui_dataset_new();
		for (col = 0; col < maxcols; col++) {
			QString value;
			QWidget *widget = table->cellWidget(row, col);

			if (widget) {
				if (widget->isA("QCheckBox")) {
					if (((QCheckBox *)widget)->isOn())
						value.setAscii("Y", 1);
					else
						value.setAscii("N", 1);
				} else if (widget->isA("QLineEdit")) {
					QLineEdit *textbox = (QLineEdit *)widget;
					value.setAscii(textbox->text());
				} else if (widget->isA("QComboBox")) {
					QComboBox *combobox = (QComboBox *)widget;
					value.setAscii(combobox->currentText());
				} else {
					continue;
				}
			} else {
				continue;
			}

			dfui_dataset_celldata_add(ds,
			    widget->name(), value.ascii());
			submit = true;
		}

		if (submit)
			dfui_response_dataset_add(response, ds);
		else
			dfui_dataset_free(ds);
	}

	dfui_fe_submit(connection, response);
	dfui_response_free(response);

	extensible = false;
	multiple = false;
	processing = false;
	close();

	child("main")->deleteLater(); // Clear the form
}

bool Form::present(struct dfui_form *payload)
{
	bool result;

	data = payload;
	if (dfui_form_is_multiple(data))
		result = buildMultiple();
	else
		result = buildSingle();

	if (result) {
		processing = true;
		show();
	}

	return result;
}

bool Form::present(struct dfui_progress *payload)
{
	bool result;

	result = buildProgress((struct dfui_progress *)payload);
	if (result)
		show();

	return result;
}
