#!/usr/bin/python

# Copyright 2017 Daniel Silverstone <dsilvers@digital-scurf.org>
#
# This file is part of NetSurf, http://www.netsurf-browser.org/
#
# NetSurf is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# NetSurf is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""
Monkey Farmer

The monkey farmer is a wrapper around `nsmonkey` which can be used to simplify
access to the monkey behaviours and ultimately to write useful tests in an
expressive but not overcomplicated DSLish way.  Tests are, ultimately, still
Python code.

"""

import asyncore
import os
import socket
import subprocess
import time

#monkey_cmd = ['./nsmonkey', '--accept_language=fr']
monkey_cmd = ['./nsmonkey']

class MonkeyFarmer(asyncore.dispatcher):
    def __init__(self, online, quiet=False):
        (mine, monkeys) = socket.socketpair()
        
        asyncore.dispatcher.__init__(self, sock=mine)

        self.monkey = subprocess.Popen(
            monkey_cmd,
            stdin=monkeys,
            stdout=monkeys,
            close_fds=[mine])

        monkeys.close()

        self.buffer = ""
        self.incoming = ""
        self.lines = []
        self.scheduled = []
        self.deadmonkey = False
        self.online = online
        self.quiet = quiet

    def handle_connect(self):
        pass
        
    def handle_read(self):
        got = self.recv(8192)
        if got == "" or got is None:
            self.deadmonkey = True
            return
        self.incoming += got
        if "\n" in self.incoming:
            lines = self.incoming.split("\n")
            self.incoming = lines.pop()
            self.lines = lines

    def writable(self):
        return (len(self.buffer) > 0)

    def handle_write(self):
        sent = self.send(self.buffer)
        self.buffer = self.buffer[sent:]

    def tell_monkey(self, *args):
        cmd = (" ".join(args))
        if not self.quiet:
            print ">>> %s" % cmd
        self.buffer += "%s\n" % cmd

    def monkey_says(self, line):
        if not self.quiet:
            print "<<< %s" % line
        self.online(line)

    def schedule_event(self, event, secs=None, when=None):
        assert(secs is not None or when is not None)
        if when is None:
            when = time.time() + secs
        self.scheduled.append((when, event))
        self.scheduled.sort(lambda a,b: cmp(a[0],b[0]))

    def unschedule_event(self, event):
        self.scheduled = [x for x in self.scheduled if x[1] != event]
        
    def loop(self, once=False):
        if len(self.lines) > 0:
            self.monkey_says(self.lines.pop(0))
            if once:
                return
        while not self.deadmonkey:
            now = time.time()
            while len(self.scheduled) > 0 and now >= self.scheduled[0][0]:
                func = self.scheduled[0][1]
                self.scheduled.pop(0)
                func(self)
                now = time.time()
            if len(self.scheduled) > 0:
                next = self.scheduled[0][0]
                asyncore.loop(timeout=next-now, count=1)
            else:
                asyncore.loop(count=1)
            if len(self.lines) > 0:
                self.monkey_says(self.lines.pop(0))
            if once:
                break

class Browser:
    def __init__(self, quiet=False):
        self.farmer = MonkeyFarmer(online=self.on_monkey_line, quiet=quiet)
        self.windows = {}
        self.current_draw_target = None

    def pass_options(self, *opts):
        if len(opts) > 0:
            self.farmer.tell_monkey("OPTIONS " + (" ".join(opts)))
        
    def on_monkey_line(self, line):
        parts = line.split(" ")
        handler = getattr(self, "handle_" + parts[0], None)
        if handler is not None:
            handler(*parts[1:])

    def quit(self):
        self.farmer.tell_monkey("QUIT")

    def quit_and_wait(self):
        self.quit()
        self.farmer.loop()
        
    def handle_GENERIC(self, what, *args):
        pass

    def handle_WINDOW(self, action, _win, winid, *args):
        if action == "NEW":
            new_win = BrowserWindow(self, winid, *args)
            self.windows[winid] = new_win
        else:
            win = self.windows.get(winid, None)
            if win is None:
                print "    Unknown window id %s" % winid
            else:
                win.handle(action, *args)

    def handle_PLOT(self, *args):
        if self.current_draw_target is not None:
            self.current_draw_target.handle_plot(*args)
                
    def new_window(self, url=None):
        if url is None:
            self.farmer.tell_monkey("WINDOW NEW")
        else:
            self.farmer.tell_monkey("WINDOW NEW %s" % url)
        wins_known = set(self.windows.keys())
        while len(set(self.windows.keys()).difference(wins_known)) == 0:
            self.farmer.loop(once=True)
        poss_wins = set(self.windows.keys()).difference(wins_known)
        return self.windows[poss_wins.pop()]
        
                
class BrowserWindow:
    def __init__(self, browser, winid, _for, coreid, _existing, otherid, _newtab, newtab, _clone, clone):
        self.alive = True
        self.browser = browser
        self.winid = winid
        self.coreid = coreid
        self.existing = browser.windows.get(otherid, None)
        self.newtab = newtab == "TRUE"
        self.clone = clone == "TRUE"
        self.width = 0
        self.height = 0
        self.title = ""
        self.throbbing = False
        self.scrollx = 0
        self.scrolly = 0
        self.content_width = 0
        self.content_height = 0
        self.status = ""
        self.pointer = ""
        self.scale = 1.0
        self.url = ""
        self.plotted = []
        self.plotting = False

    def kill(self):
        self.browser.farmer.tell_monkey("WINDOW DESTROY %s" % self.winid)

    def go(self, url, referer = None):
        if referer is None:
            self.browser.farmer.tell_monkey("WINDOW GO %s %s" % (
                self.winid, url))
        else:
            self.browser.farmer.tell_monkey("WINDOW GO %s %s %s" % (
                self.winid, url, referer))

    def reload(self):
        self.browser.farmer.tell_monkey("WINDOW RELOAD %s" % self.winid)
            
    def redraw(self, coords=None):
        if coords is None:
            self.browser.farmer.tell_monkey("WINDOW REDRAW %s" % self.winid)
        else:
            self.browser.farmer.tell_monkey("WINDOW REDRAW %s %s" % (
                self.winid, (" ".join(coords))))

    def handle(self, action, *args):
        handler = getattr(self, "handle_window_" + action, None)
        if handler is not None:
            handler(*args)

    def handle_window_SIZE(self, _width, width, _height, height):
        self.width = int(width)
        self.height = int(height)
    
    def handle_window_DESTROY(self):
        self.alive = False

    def handle_window_TITLE(self, _str, *title):
        self.title = " ".join(title)
        
    def handle_window_REDRAW(self):
        pass

    def handle_window_GET_DIMENSIONS(self, _width, width, _height, height):
        self.width = width
        self.height = height

    def handle_window_NEW_CONTENT(self):
        pass

    def handle_window_NEW_ICON(self):
        pass

    def handle_window_START_THROBBER(self):
        self.throbbing = True

    def handle_window_STOP_THROBBER(self):
        self.throbbing = False

    def handle_window_SET_SCROLL(self, _x, x, _y, y):
        self.scrollx = int(x)
        self.scrolly = int(y)

    def handle_window_UPDATE_BOX(self, _x, x, _y, y, _width, width, _height, height):
        x = int(x)
        y = int(y)
        width = int(width)
        height = int(height)
        pass

    def handle_window_UPDATE_EXTENT(self, _width, width, _height, height):
        self.content_width = int(width)
        self.content_height = int(height)

    def handle_window_SET_STATUS(self, _str, *status):
        self.status = (" ".join(status))

    def handle_window_SET_POINTER(self, _ptr, ptr):
        self.pointer = ptr

    def handle_window_SET_SCALE(self, _scale, scale):
        self.scale = float(scale)

    def handle_window_SET_URL(self, _url, url):
        self.url = url

    def handle_window_GET_SCROLL(self, _x, x, _y, y):
        self.scrollx = int(x)
        self.scrolly = int(y)

    def handle_window_SCROLL_START(self):
        self.scrollx = 0
        self.scrolly = 0

    def handle_window_REDRAW(self, act):
        if act == "START":
            self.browser.current_draw_target = self
            self.plotted = []
            self.plotting = True
        else:
            self.browser.current_draw_target = None
            self.plotting = False

    def load_page(self, url=None, referer=None):
        if url is not None:
            self.go(url, referer)
        self.wait_loaded()

    def reload(self):
        self.browser.farmer.tell_monkey("WINDOW RELOAD %s" % self.winid)
        self.wait_loaded()

    def wait_loaded(self):
        while not self.throbbing:
            self.browser.farmer.loop(once=True)
        while self.throbbing:
            self.browser.farmer.loop(once=True)

    def handle_plot(self, *args):
        self.plotted.append(args)

    def redraw(self, coords=None):
        if coords is None:
            self.browser.farmer.tell_monkey("WINDOW REDRAW %s" % self.winid)
        else:
            self.browser.farmer.tell_monkey("WINDOW REDRAW %s %s" % (
                self.winid, (" ".join(coords))))
        while not self.plotting:
            self.browser.farmer.loop(once=True)
        while self.plotting:
            self.browser.farmer.loop(once=True)
        return self.plotted
            
            
# Simple test is as follows...
            
browser = Browser(quiet=True)

win = browser.new_window()

fname = "test/js/inline-doc-write-simple.html"
full_fname = os.path.join(os.getcwd(), fname)

browser.pass_options("--enable_javascript=0")
win.load_page("file://" + full_fname)

print("Loaded, URL is %s" % win.url)

cmds = win.redraw()
print("Received %d plot commands" % len(cmds))
for cmd in cmds:
    if cmd[0] == "TEXT":
        print "%s %s -> %s" % (cmd[2], cmd[4], (" ".join(cmd[6:])))


browser.pass_options("--enable_javascript=1")
win.load_page("file://" + full_fname)

print("Loaded, URL is %s" % win.url)

cmds = win.redraw()
print("Received %d plot commands" % len(cmds))
for cmd in cmds:
    if cmd[0] == "TEXT":
        print "%s %s -> %s" % (cmd[2], cmd[4], (" ".join(cmd[6:])))

browser.quit_and_wait()
