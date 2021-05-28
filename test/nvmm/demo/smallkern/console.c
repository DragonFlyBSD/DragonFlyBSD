/*
 * Copyright (c) 2018 The NetBSD Foundation, Inc. All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Maxime Villard.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "smallkern.h"

void outsb(int port, char *buf, size_t size);

void print_ext(int color __unused, char *buf)
{
	size_t i;

	for (i = 0; buf[i] != '\0'; i++) {
		outsb(123, &buf[i], 1);
	}
}

void print(char *buf)
{
	print_ext(WHITE_ON_BLACK, buf);
}

void print_state(bool ok, char *buf)
{
	print("[");
	if (ok)
		print_ext(GREEN_ON_BLACK, "+");
	else
		print_ext(RED_ON_BLACK, "!");
	print("] ");
	print(buf);
	print("\n");
}

void print_banner()
{
	char *banner = 
		"  _________               __   __   __                        \n"
		" /   _____/ _____ _____  |  | |  | |  | __ ___________  ____  \n"
		" \\_____  \\ /     \\\\__  \\ |  | |  | |  |/ // __ \\_  __ \\/    \\ \n"
		" /        \\  Y Y  \\/ __ \\|  |_|  |_|    <\\  ___/|  | \\/   |  \\\n"
		"/_______  /__|_|  (____  /____/____/__|_ \\\\___  >__|  |___|  /\n"
		"        \\/      \\/     \\/               \\/    \\/           \\/ \n"
	;
	print(banner);
}
