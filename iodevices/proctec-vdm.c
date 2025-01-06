/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Common I/O devices used by various simulated machines
 *
 * Copyright (C) 2017-2019 by Udo Munk
 * Copyright (C) 2025 by Thomas Eberhardt
 *
 * Emulation of a Processor Technology VDM-1 S100 board
 *
 * History:
 * 28-FEB-2017 first version, all software tested with working
 * 21-JUN-2017 don't use dma_read(), switches Tarbell ROM off
 * 20-APR-2018 avoid thread deadlock on Windows/Cygwin
 * 15-JUL-2018 use logging
 * 04-NOV-2019 eliminate usage of mem_base()
 * 03-JAN-2025 use SDL2 instead of X11
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef WANT_SDL
#include <stdbool.h>
#include <SDL.h>
#else
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <pthread.h>
#endif

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simmem.h"
#include "simport.h"
#include "simsdl.h"

#include "proctec-vdm-charset.h"
#include "proctec-vdm.h"

#ifndef WANT_SDL
#include "log.h"
static const char *TAG = "VDM";
#endif

#define XOFF 10				/* use some offset inside the window */
#define YOFF 15				/* for the drawing area */

/* SDL2/X11 stuff */
       int slf = 1;			/* scanlines factor, default no lines */
static int xsize, ysize;		/* window size */
static int sx, sy;
#ifdef WANT_SDL
static SDL_Window *window;
static SDL_Renderer *renderer;
static int proctec_win_id = -1;
#else
static Display *display;
static Window window;
static int screen;
static GC gc;
static XWindowAttributes wa;
static Pixmap pixmap;
static Colormap colormap;
static XColor black, bg, fg;
static char black_color[] = "#000000";	/* black */
static XEvent event;
static KeySym key;
static char text[10];
#endif
       uint8_t bg_color[3] = {48, 48, 48};	/* default background color */
       uint8_t fg_color[3] = {255, 255, 255};	/* default foreground color */

/* VDM stuff */
static int state;			/* state on/off for refresh thread */
static int mode;			/* video mode from I/O port */
int proctec_kbd_status = 1;		/* keyboard status */
int proctec_kbd_data = -1;		/* keyboard data */
static int first;			/* first displayed screen position */
static int beg;				/* beginning display line address */

#ifndef WANT_SDL
/* UNIX stuff */
static pthread_t thread;
#endif

/* create the SDL window for VDM display */
static void open_display(void)
{
	xsize = 576 + (XOFF * 2);
	ysize = (208 * slf) + (YOFF * 2);

#ifdef WANT_SDL
	window = SDL_CreateWindow("Processor Technology VDM-1",
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  xsize, ysize, 0);
	renderer = SDL_CreateRenderer(window, -1, (SDL_RENDERER_ACCELERATED |
						   SDL_RENDERER_PRESENTVSYNC));

	SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);
#else /* !WANT_SDL */
	Window rootwindow;
	XSizeHints *size_hints = XAllocSizeHints();
	Atom wm_delete_window;
	char buf[8];

	display = XOpenDisplay(NULL);
	XLockDisplay(display);
	screen = DefaultScreen(display);
	rootwindow = RootWindow(display, screen);
	XGetWindowAttributes(display, rootwindow, &wa);
	window = XCreateSimpleWindow(display, rootwindow, 0, 0,
				     xsize, ysize, 1, 0, 0);
	XStoreName(display, window, "Processor Technology VDM-1");
	size_hints->flags = PSize | PMinSize | PMaxSize;
	size_hints->min_width = xsize;
	size_hints->min_height = ysize;
	size_hints->base_width = xsize;
	size_hints->base_height = ysize;
	size_hints->max_width = xsize;
	size_hints->max_height = ysize;
	XSetWMNormalHints(display, window, size_hints);
	XFree(size_hints);
	wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, window, &wm_delete_window, 1);
	XSelectInput(display, window, KeyPressMask);
	colormap = DefaultColormap(display, 0);
	gc = XCreateGC(display, window, 0, NULL);
	pixmap = XCreatePixmap(display, rootwindow, xsize, ysize, wa.depth);

	XParseColor(display, colormap, black_color, &black);
	XAllocColor(display, colormap, &black);
	sprintf(buf, "#%02X%02X%02X", bg_color[0], bg_color[1], bg_color[2]);
	XParseColor(display, colormap, buf, &bg);
	XAllocColor(display, colormap, &bg);
	sprintf(buf, "#%02X%02X%02X", fg_color[0], fg_color[1], fg_color[2]);
	XParseColor(display, colormap, buf, &fg);
	XAllocColor(display, colormap, &fg);

	XMapWindow(display, window);
	XSetForeground(display, gc, black.pixel);
	XFillRectangle(display, pixmap, gc, 0, 0, xsize, ysize);
	XSync(display, True);
	XUnlockDisplay(display);
#endif /* !WANT_SDL */
}

/* close the SDL window for VDM display */
static void close_display(void)
{
#ifdef WANT_SDL
	SDL_DestroyRenderer(renderer);
	renderer = NULL;
	SDL_DestroyWindow(window);
	window = NULL;
#else
	XLockDisplay(display);
	XFreePixmap(display, pixmap);
	XFreeGC(display, gc);
	XUnlockDisplay(display);
	XCloseDisplay(display);
#endif
}

/* shutdown VDM window */
void proctec_vdm_off(void)
{
	state = 0;		/* tell refresh thread to stop */

#ifdef WANT_SDL
	if (proctec_win_id >= 0) {
		simsdl_destroy(proctec_win_id);
		proctec_win_id = -1;
	}
#else /* !WANT_SDL */
	/* works if X11 with posix threads implemented correct, but ... */
	if (thread != 0) {
		sleep_for_ms(50);	/* wait a bit for thread to stop */
		pthread_cancel(thread);
		pthread_join(thread, NULL);
	}

	if (display != NULL)
		close_display();
#endif /* !WANT_SDL */
}

#ifdef WANT_SDL

/*
 * Process a SDL event, we are only interested in keyboard input.
 * Note that I'm using the event queue as typeahead buffer, saves to
 * implement one self.
 */
static void event_handler(SDL_Event *event)
{
	/* if there is a keyboard event get it and convert to ASCII */
	switch (event->type) {
	case SDL_WINDOWEVENT:
		if (event->window.windowID == SDL_GetWindowID(window)) {
			switch (event->window.event) {
			case SDL_WINDOWEVENT_FOCUS_GAINED:
				SDL_StartTextInput();
				break;
			case SDL_WINDOWEVENT_FOCUS_LOST:
				SDL_StopTextInput();
				break;
			default:
				break;
			}
		}
		break;
	case SDL_TEXTINPUT:
		if (event->text.windowID == SDL_GetWindowID(window)) {
			/* if the last character wasn't processed already do nothing */
			/* keep event in queue until the CPU emulation got current one */
			if (proctec_kbd_status == 0)
				return;

			proctec_kbd_data = event->text.text[0];
			proctec_kbd_status = 0;
		}
		break;
	case SDL_KEYDOWN:
		if (event->key.windowID == SDL_GetWindowID(window)) {
			/* if the last character wasn't processed already do nothing */
			/* keep event in queue until the CPU emulation got current one */
			if (proctec_kbd_status == 0)
				return;

			if (!(event->key.keysym.sym & SDLK_SCANCODE_MASK) &&
			    ((event->key.keysym.mod & KMOD_CTRL) ||
			     (event->key.keysym.sym < 32))) {
				proctec_kbd_data = event->key.keysym.sym & 0x1f;
				proctec_kbd_status = 0;
			}
		}
		break;
	default:
		break;
	}
}

static inline void set_fg_color(void)
{
	SDL_SetRenderDrawColor(renderer,
			       fg_color[0], fg_color[1], fg_color[2],
			       SDL_ALPHA_OPAQUE);
}

static inline void set_bg_color(void)
{
	SDL_SetRenderDrawColor(renderer,
			       bg_color[0], bg_color[1], bg_color[2],
			       SDL_ALPHA_OPAQUE);
}

static inline void draw_point(int x, int y)
{
	SDL_RenderDrawPoint(renderer, x, y);
}

#else /* !WANT_SDL */

/*
 * Check the X11 event queue, we are only interested in keyboard input.
 * Note that I'm using the event queue as typeahead buffer, saves to
 * implement one self.
 */
static inline void event_handler(void)
{
	/* if the last character wasn't processed already do nothing */
	/* keep event in queue until the CPU emulation got current one */
	if (proctec_kbd_status == 0)
		return;

	/* if there is a keyboard event get it and convert with keymap */
	if (XEventsQueued(display, QueuedAlready) > 0) {
		XNextEvent(display, &event);
		if ((event.type == KeyPress) &&
		    XLookupString(&event.xkey, text, 1, &key, 0) == 1) {
			proctec_kbd_data = text[0];
			proctec_kbd_status = 0;
		}
	}
}

static inline void set_fg_color(void)
{
	XSetForeground(display, gc, fg.pixel);
}

static inline void set_bg_color(void)
{
	XSetForeground(display, gc, bg.pixel);
}

static inline void draw_point(int x, int y)
{
	XDrawPoint(display, pixmap, gc, x, y);
}

#endif /* !WANT_SDL */

/* display characters, bit 7 = inverse video */
static void dc(BYTE c)
{
	register int x, y;
	int inv = (c & 128) ? 1 : 0;

	for (x = 0; x < 9; x++) {
		for (y = 0; y < 13; y++) {
			if (charset[c & 0x7f][y][x] == 1) {
				if (!inv)
					set_fg_color();
				else
					set_bg_color();
			} else {
				if (!inv)
					set_bg_color();
				else
					set_fg_color();
			}
			draw_point(sx + x, sy + (y * slf));
		}
	}
}

/* refresh the display buffer */
static void refresh(void)
{
	register int x, y;
	static int addr;
	static BYTE c;

	sy = YOFF;
	addr = 0xcc00 + beg * 64;

	for (y = 0; y < 16; y++) {
		sx = XOFF;
#ifndef WANT_SDL
		event_handler();
#endif
		for (x = 0; x < 64; x++) {
			if (y >= first) {
				c = getmem(addr + x);
				dc(c);
			} else
				dc(' ');
			sx += 9;
		}
		sy += 13 * slf;
		addr += 64;
		if (addr >= 0xd000)
			addr = 0xcc00;
	}
}

#ifdef WANT_SDL

/* function for updating the display */
static void update_display(bool tick)
{
	UNUSED(tick);

	if (state) {
		/* update display window */
		refresh();
		SDL_RenderPresent(renderer);
	}
}

static win_funcs_t proctec_funcs = {
	open_display,
	close_display,
	event_handler,
	update_display
};

#else /* !WANT_SDL */

/* thread for updating the display */
static void *update_display(void *arg)
{
	uint64_t t1, t2;
	int tdiff;

	UNUSED(arg);

	t1 = get_clock_us();

	while (state) {

		/* lock display, don't cancel thread while locked */
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		XLockDisplay(display);

		/* update display window */
		refresh();
		XCopyArea(display, pixmap, window, gc, 0, 0,
			  xsize, ysize, 0, 0);
		XSync(display, False);

		/* unlock display, thread can be canceled again */
		XUnlockDisplay(display);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		/* sleep rest to 33ms so that we get 30 fps */
		t2 = get_clock_us();
		tdiff = t2 - t1;
		if ((tdiff > 0) && (tdiff < 33000))
			sleep_for_ms(33 - (tdiff / 1000));

		t1 = get_clock_us();
	}

	pthread_exit(NULL);
}

#endif /* !WANT_SDL */

/* I/O port for the VDM */
void proctec_vdm_out(BYTE data)
{
	mode = data;
	first = (data & 0xf0) >> 4;
	beg = data & 0x0f;

	state = 1;

#ifdef WANT_SDL
	if (proctec_win_id < 0)
		proctec_win_id = simsdl_create(&proctec_funcs);
#else
	if (display == 0) {
		open_display();

		if (pthread_create(&thread, NULL, update_display, (void *) NULL)) {
			LOGE(TAG, "can't create thread");
			exit(EXIT_FAILURE);
		}
	}
#endif
}
