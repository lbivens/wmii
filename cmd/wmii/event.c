/* Copyright ©2006-2007 Kris Maglione <fbsdaemon@gmail.com>
 * See LICENSE file for license details.
 */
#include <stdio.h>
#include <X11/keysym.h>
#include <util.h>
#include "dat.h"
#include "fns.h"
#include "printevent.h"

void
dispatch_event(XEvent *e) {
	Debug printevent(e);
	if(handler[e->type])
		handler[e->type](e);
}

#define handle(w, fn, ev) ((w)->handler->fn ? (w)->handler->fn((w), ev) : (void)0)

uint
flushevents(long event_mask, Bool dispatch) {
	XEvent ev;
	uint n = 0;

	while(XCheckMaskEvent(display, event_mask, &ev)) {
		if(dispatch)
			dispatch_event(&ev);
		n++;
	}
	return n;
}

static void
buttonrelease(XEvent *e) {
	XButtonPressedEvent *ev;
	Window *w;

	ev = &e->xbutton;
	if((w = findwin(ev->window)))
		handle(w, bup, ev);
}

static void
buttonpress(XEvent *e) {
	XButtonPressedEvent *ev;
	Window *w;

	ev = &e->xbutton;
	if((w = findwin(ev->window)))
		handle(w, bdown, ev);
	else
		XAllowEvents(display, ReplayPointer, ev->time);
}

static void
configurerequest(XEvent *e) {
	XConfigureRequestEvent *ev;
	XWindowChanges wc;
	Window *w;

	ev = &e->xconfigurerequest;
	if((w = findwin(ev->window)))
		handle(w, configreq, ev);
	else{
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(display, ev->window, ev->value_mask, &wc);
	}
}

static void
destroynotify(XEvent *e) {
	XDestroyWindowEvent *ev;
	Window *w;
	Client *c;

	ev = &e->xdestroywindow;
	if((w = findwin(ev->window))) 
		handle(w, destroy, ev);
	else {
		Debug fprintf(stderr, "DestroyWindow(%x) (no handler)\n", (uint)ev->window);
		if((c = win2client(ev->window)))
			fprintf(stderr, "Badness: Unhandled DestroyNotify: "
				"Client: %p, Window: %x, Name: %s\n", c, (uint)c->w.w, c->name);
	}
}

static void
enternotify(XEvent *e) {
	XCrossingEvent *ev;
	Window *w;

	ev = &e->xcrossing;
	if(ev->mode != NotifyNormal)
		return;

	if((w = findwin(ev->window))) 
		handle(w, enter, ev);
	else if(ev->window == scr.root.w) {
		sel_screen = True;
		draw_frames();
	}
}

static void
leavenotify(XEvent *e) {
	XCrossingEvent *ev;
	Window *w;

	ev = &e->xcrossing;
	w = findwin(ev->window);
	if((ev->window == scr.root.w) && !ev->same_screen) {
		sel_screen = True;
		draw_frames();
	}
}

void
print_focus(Client *c, char *to) {
		Debug {
			fprintf(stderr, "screen->focus: %p[%x] => %p[%x]\n",
				screen->focus, clientwin(screen->focus), c, clientwin(c));
			fprintf(stderr, "\t%s => %s\n", clientname(screen->focus), to);
		}
}

static void
focusin(XEvent *e) {
	XFocusChangeEvent *ev;
	Window *w;
	Client *c;

	ev = &e->xfocus;
	/* Yes, we're focusing in on nothing, here. */
	if(ev->detail == NotifyDetailNone) {
		print_focus(&c_magic, "<magic[none]>");
		screen->focus = &c_magic;
		setfocus(screen->barwin, RevertToParent);
		return;
	}

	if(!((ev->detail == NotifyNonlinear)
	   ||(ev->detail == NotifyNonlinearVirtual)
	   ||(ev->detail == NotifyVirtual)
	   ||(ev->detail == NotifyInferior)
	   ||(ev->detail == NotifyAncestor)))
		return;
	if((ev->mode == NotifyWhileGrabbed) && (screen->hasgrab != &c_root))
		return;

	if(ev->window == screen->barwin->w) {
		print_focus(nil, "<nil>");
		screen->focus = nil;
	}
	else if((w = findwin(ev->window))) 
		handle(w, focusin, ev);
	else if(ev->mode == NotifyGrab) {
		if(ev->window == scr.root.w)
			screen->hasgrab = &c_root;
		/* Some unmanaged window has grabbed focus */
		else if((c = screen->focus)) {
			print_focus(&c_magic, "<magic>");
			screen->focus = &c_magic;
			if(c->sel)
				draw_frame(c->sel);
		}
	}
}

static void
focusout(XEvent *e) {
	XEvent me;
	XFocusChangeEvent *ev;
	Window *w;

	ev = &e->xfocus;
	if(!((ev->detail == NotifyNonlinear)
	   ||(ev->detail == NotifyNonlinearVirtual)
	   ||(ev->detail == NotifyVirtual)
	   ||(ev->detail == NotifyInferior)
	   ||(ev->detail == NotifyAncestor)))
		return;
	if(ev->mode == NotifyUngrab)
		screen->hasgrab = nil;

	if((ev->mode == NotifyGrab)
	&& XCheckMaskEvent(display, KeyPressMask, &me))
			dispatch_event(&me);
	else if((w = findwin(ev->window))) 
		handle(w, focusout, ev);
}

static void
expose(XEvent *e) {
	XExposeEvent *ev;
	Window *w;

	ev = &e->xexpose;
	if(ev->count == 0) {
		if((w = findwin(ev->window))) 
			handle(w, expose, ev);
	}
}

static void
keypress(XEvent *e) {
	XKeyEvent *ev;
	Window *w;

	ev = &e->xkey;
	w = findwin(ev->window);
	ev->state &= valid_mask;
	if(ev->window == scr.root.w)
		kpress(scr.root.w, ev->state, (KeyCode) ev->keycode);
}

static void
mappingnotify(XEvent *e) {
	XMappingEvent *ev;

	ev = &e->xmapping;
	XRefreshKeyboardMapping(ev);
	if(ev->request == MappingKeyboard)
		update_keys();
}

static void
maprequest(XEvent *e) {
	XMapRequestEvent *ev;
	Window *w;
	XWindowAttributes wa;

	ev = &e->xmaprequest;
	w = findwin(ev->window);

	if(!XGetWindowAttributes(display, ev->window, &wa))
		return;

	if(wa.override_redirect) {
		XSelectInput(display, ev->window,
				(StructureNotifyMask | PropertyChangeMask));
		return;
	}
	if(!win2client(ev->window))
		create_client(ev->window, &wa);
}

static void
motionnotify(XEvent *e) {
	XMotionEvent *ev;
	Window *w;

	ev = &e->xmotion;
	if((w = findwin(ev->window)))
		handle(w, motion, ev);
}

static void
propertynotify(XEvent *e) {
	XPropertyEvent *ev;
	Window *w;

	ev = &e->xproperty;
	if((w = findwin(ev->window))) 
		handle(w, property, ev);
}

static void
mapnotify(XEvent *e) {
	XMapEvent *ev;
	Window *w;

	ev = &e->xmap;
	if((w = findwin(ev->window))) 
		handle(w, map, ev);
}

static void
unmapnotify(XEvent *e) {
	XUnmapEvent *ev;
	Window *w;

	ev = &e->xunmap;
	if((w = findwin(ev->window)) && (ev->event == w->parent->w)) {
		if(ev->send_event || w->unmapped-- == 0)
			handle(w, unmap, ev);
	}
}

void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ButtonRelease] = buttonrelease,
	[ConfigureRequest] = configurerequest,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[FocusOut] = focusout,
	[KeyPress] = keypress,
	[LeaveNotify] = leavenotify,
	[MapNotify] = mapnotify,
	[MapRequest] = maprequest,
	[MappingNotify] = mappingnotify,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	[UnmapNotify] = unmapnotify,
};

void
check_x_event(IxpConn *c) {
	XEvent ev;
	while(XPending(display)) {
		XNextEvent(display, &ev);
		dispatch_event(&ev);
		/* Hack to alleviate an apparant Xlib bug */
		XPending(display);
	}
}