/*
 * Copyright (c) 2015-2017, John R. Marino <draco@marino.st>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
var SbInterval = 10;
var progwidth = 950;
var progheight = 14;
var progtop    = 2;
var run_active;
var kfiles = 0;
var last_kfile = 1;
var history = [[]];

/* Disabling jQuery caching */
$.ajaxSetup({
	cache: false
});

function catwidth (variable, queued) {
	if (variable == 0)
		return 0;
	var width = variable * progwidth / queued;
	return (width < 1) ? 1 : Math.round (width);
}

function maxcatwidth(A, B, C, D, queued) {
	var cat = new Array();
	cat[0] = catwidth (A, queued);
	cat[1] = catwidth (B, queued);
	cat[2] = catwidth (C, queued);
	cat[3] = catwidth (D, queued);
	cat.sort(function(a,b){return a-b});
	return (progwidth - cat[0] - cat[1] - cat[2]);
}

function minidraw(x, context, color, queued, variable, mcw) {
	var width = catwidth (variable, queued);
	if (width == 0)
		return (0);
	if (width > mcw)
		width = mcw;
	context.fillStyle = color;
	context.fillRect(x, progtop + 1, width, progheight + 2);
	return (width);
}

function update_canvas(stats) {
	var queued = stats.queued;
	var built = stats.built;
	var meta = stats.meta;
	var failed = stats.failed;
	var skipped = stats.skipped;
	var ignored = stats.ignored;

	var canvas = document.getElementById('progressbar');
	if (canvas.getContext === undefined) {
		/* Not supported */
		return;
	}

	var context = canvas.getContext('2d');

	context.fillStyle = '#D8D8D8';
	context.fillRect(0, progtop + 1, progwidth, progheight + 2);
	var x = 0;
	var mcw = maxcatwidth (built, meta, failed, ignored, skipped, queued);
	x += minidraw(x, context, "#339966", queued, built, mcw);
	x += minidraw(x, context, "#A577E1", queued, meta, mcw);
	x += minidraw(x, context, "#CC0033", queued, failed, mcw);
	x += minidraw(x, context, "#FFCC33", queued, ignored, mcw);
	x += minidraw(x, context, "#CC6633", queued, skipped, mcw);
}

function filter (txt) {
	$('#report input').val (txt).trigger('search');
}

function process_summary(data) {
	var html;
	var RB = '<tr>';
	var RE = '</tr>';
	var B = '<td>';
	var E = '</td>';

	kfiles = parseInt (data.kfiles);
	run_active = parseInt (data.active);
	$('#profile').html(data.profile);
	$('#kickoff').html(data.kickoff);
	$('#polling').html(run_active ? "Active" : "Complete");
	if (data.stats) {
		$.each(data.stats, function(status, count) {
			html = count;
			$('#stats_' + status).html(html);
		});
		update_canvas (data.stats);
	}

	$('#builders_body tbody').empty();
	for (n = 0; n < data.builders.length; n++) {
		var trow = RB + '<td class="b' + data.builders[n].ID +
			'" onclick="filter(\'[' + data.builders[n].ID +
			']\')" title="Click to filter for work done by builder ' +
			data.builders[n].ID + '">'
			  + data.builders[n].ID + E +
			B + data.builders[n].elapsed + E +
			B + data.builders[n].phase + E +
			B + data.builders[n].origin + E +
			B + data.builders[n].lines + E +
			RE;
		$('#builders_body tbody').append (trow);
	}
}

function digit2(n){
	return n > 9 ? "" + n: "0" + n;
}

function logfile (origin) {
	var parts = origin.split('/');
	return '../' + parts[0] + '___' + parts[1] + '.log';
}

function format_result (result) {
	return '<div class="' + result + ' result">' + result + '<div>';
}

function format_entry (entry, origin) {
	return '<span class="entry" onclick="filter(\'' + origin+ '\')">' +
		entry + '</span>';
}

function information (result, origin, info) {
	var parts;
	if (result == "meta") {
		return 'meta-node complete.';
	} else if (result == "built") {
		return '<a href="' + logfile (origin) + '">logfile</a>';
	} else if (result == "failed") {
		parts = info.split(':');
		return 'Failed ' + parts[0] + ' phase (<a href="' + logfile (origin) +
			'">logfile</a>)';
	} else if (result == "skipped") {
		return 'Issue with ' + info;
	} else if (result == "ignored") {
		parts = info.split(':|:');
		return parts[0];
	} else {
		return "??";
	}
}

function skip_info (result, info) {
	var parts;
	if (result == "failed") {
		parts = info.split(':');
		return parts[1];
	} else if (result == "ignored") {
		parts = info.split(':|:');
		return parts[1];
	} else {
		return "";
	}
}

function portsmon (origin) {
	var parts = origin.split('/');
	var FPClink = '<a title="portsmon for "' + origin + '" href="http://portsmon.freebsd.org/portoverview.py?category=' + parts[0] + '&portname=' + parts[1] + '">' + origin + '</a>';
	var NPSlink = '<a title="pkgsrc.se overview" href="http://pkgsrc.se/' + origin + '">' + origin + '</a>';
	return FPClink;
}

function process_history_file(data, k) {
	history [k] = [];
	for (n = 0; n < data.length; n++) {
		var trow = [];
		trow.push(format_entry (data[n].entry, data[n].origin));
		trow.push(data[n].elapsed);
		trow.push('[' + data[n].ID + ']');
		trow.push(format_result (data[n].result));
		trow.push(portsmon (data[n].origin));
		trow.push(information (data[n].result, data[n].origin, data[n].info));
		trow.push(skip_info (data[n].result, data[n].info));
		trow.push(data[n].duration);
		history [k].push (trow);
	}
}

function cycle () {
	if (run_active) {
		setTimeout(update_summary_and_builders, SbInterval * 1000);
	} else {
		$('#builders_zone_2').fadeOut(2500);
		$('#main').css('border-top', '1px solid #404066');
	}
}

function update_history_success(kfile) {
	if (kfile == kfiles) {
		var full_history = [];
		for (var k = 1; k <= kfiles; k++) {
			full_history = full_history.concat (history[k]);
		}
		$('#report_table').dataTable().fnClearTable();
		$('#report_table').dataTable().fnAddData(full_history);
		cycle();
	} else {
		last_kfile = kfile + 1;
		update_history();
	}
}

function update_history() {
	if (kfiles == 0) {
		cycle();
		return;
	}
	clearInterval(update_history);
	$.ajax({
		url: digit2(last_kfile) + '_history.json',
		dataType: 'json',
		success: function(data) {
			process_history_file(data, last_kfile);
			update_history_success (last_kfile);
		},
		error: function(data) {
			/* May not be there yet, try again shortly */
			setTimeout(update_history, SbInterval * 500);
		}
	})
}

function update_summary_and_builders() {
	$.ajax({
		url: 'summary.json',
		dataType: 'json',
		success: function(data) {
			process_summary(data);
			clearInterval(update_summary_and_builders);
			update_history();
		},
		error: function(data) {
			/* May not be there yet, try again shortly */
			setTimeout(update_summary_and_builders, SbInterval * 500);
		}
	});
}

$(document).ready(function() {

	$('#report_table').dataTable({
		"aaSorting": [[ 0, 'desc' ]],
		"bProcessing": true, // Show processing icon
		"bDeferRender": true, // Defer creating TR/TD until needed
		"aoColumnDefs": [
			{"bSortable": false, "aTargets": [1,2,3,5]},
			{"bSearchable": false, "aTargets": [0,1,6,7]},
		],
		"bStateSave": true, // Enable cookie for keeping state
		"aLengthMenu":[10,20,50, 100, 200],
		"iDisplayLength": 20,
		});

	update_summary_and_builders();
})

$(document).bind("keydown", function(e) {
  /* Disable F5 refreshing since this is AJAX driven. */
  if (e.which == 116) {
    e.preventDefault();
  }
});
